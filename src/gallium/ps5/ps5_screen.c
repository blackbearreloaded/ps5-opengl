#include "ps5_screen.h"

#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#ifdef PS5_RUNTIME_QUIET
static int
ps5_runtime_printf(const char *format, ...)
{
   (void)format;
   return 0;
}
#define printf ps5_runtime_printf
#endif

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "nir/tgsi_to_nir.h"
#include "amd/common/amdgfxregs.h"
#include "amd/common/ac_descriptors.h"
#include "amd/common/ac_formats.h"
#include "amd/common/ac_shader_util.h"
#include "amd/common/gfx10_format_table.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "psbc_compile.h"
#include "ps5_agc_package.h"
#include "util/ralloc.h"
#include "util/format/u_format.h"
#include "util/format/u_formats.h"
#include "util/os_time.h"
#include "util/u_sample_positions.h"
#include "util/simple_mtx.h"
#include "util/u_draw.h"
#include "util/u_blitter.h"
#include "util/u_framebuffer.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_prim.h"
#include "util/u_prim_restart.h"
#include "util/u_surface.h"
#include "util/u_upload_mgr.h"

/* This driver passes Mesa-owned NIR directly into the standalone backend. */
_Static_assert(sizeof(nir_instr_type) == 1, "NIR enums must be packed");
_Static_assert(sizeof(nir_intrinsic_op) == 4, "unexpected NIR intrinsic enum size");
_Static_assert(offsetof(nir_intrinsic_instr, intrinsic) == 56,
               "PSBC/Mesa NIR layout mismatch");
_Static_assert(PIPE_FUNC_NEVER == 0 && PIPE_FUNC_LESS == 1 &&
               PIPE_FUNC_EQUAL == 2 && PIPE_FUNC_LEQUAL == 3 &&
               PIPE_FUNC_GREATER == 4 && PIPE_FUNC_NOTEQUAL == 5 &&
               PIPE_FUNC_GEQUAL == 6 && PIPE_FUNC_ALWAYS == 7,
               "Gallium and GFX10 compare encodings must match");
_Static_assert(PIPE_STENCIL_OP_KEEP == 0 && PIPE_STENCIL_OP_ZERO == 1 &&
               PIPE_STENCIL_OP_REPLACE == 2 && PIPE_STENCIL_OP_INCR == 3 &&
               PIPE_STENCIL_OP_DECR == 4 &&
               PIPE_STENCIL_OP_INCR_WRAP == 5 &&
               PIPE_STENCIL_OP_DECR_WRAP == 6 &&
               PIPE_STENCIL_OP_INVERT == 7,
               "unexpected Gallium stencil operation order");
_Static_assert(PIPE_LOGICOP_CLEAR == 0 && PIPE_LOGICOP_COPY == 12 &&
               PIPE_LOGICOP_SET == 15,
               "unexpected Gallium logic-operation order");

#define PS5_DIRECT_ALIGNMENT 0x4000u
#define PS5_RENDER_TARGET_BYTES 0xa00000u
#ifndef PS5_RENDER_POOL_BYTES
#define PS5_RENDER_POOL_BYTES (2u * PS5_RENDER_TARGET_BYTES)
#endif
#define PS5_RENDER_ARENA_OFFSET (2u * PS5_RENDER_TARGET_BYTES)
#define PS5_RENDER_ARENA_SLOT_BYTES PS5_DIRECT_ALIGNMENT
#define PS5_RENDER_ARENA_SLOT_COUNT \
   ((PS5_RENDER_POOL_BYTES - PS5_RENDER_ARENA_OFFSET) / \
    PS5_RENDER_ARENA_SLOT_BYTES)
#define PS5_RENDER_ARENA_BITMAP_WORDS \
   ((PS5_RENDER_ARENA_SLOT_COUNT + 63u) / 64u)

struct ps5_screen {
   struct pipe_screen base;
   struct pipe_resource *render_pool;
   uint64_t render_arena_bitmap[PS5_RENDER_ARENA_BITMAP_WORDS];
   simple_mtx_t resource_mutex;
   simple_mtx_t submit_mutex;
};

void
ps5_screen_submit_lock(struct pipe_screen *base)
{
   simple_mtx_lock(&((struct ps5_screen *)base)->submit_mutex);
}

void
ps5_screen_submit_unlock(struct pipe_screen *base)
{
   simple_mtx_unlock(&((struct ps5_screen *)base)->submit_mutex);
}

#define PS5_MAX_TEXTURE_UNITS 16u
#define PS5_MERGED_TEXTURE_UNITS (2u * PS5_MAX_TEXTURE_UNITS)
#define PS5_MAX_CONSTANT_BUFFERS 13u
#define PS5_DESCRIPTOR_STAGE_COUNT 2u
#define PS5_TEXTURE_STAGE_COUNT 3u
#define PS5_CONSTANT_STAGE_COUNT 3u
#define PS5_GEOMETRY_CONSTANT_SLOT 2u
#define PS5_GEOMETRY_TEXTURE_SLOT 2u
#define PS5_MAX_RENDER_TARGETS 8u
#define PS5_TEXTURE_DESCRIPTOR_STRIDE 48u
#define PS5_TEXTURE_DESCRIPTOR_BYTES \
   (PS5_MERGED_TEXTURE_UNITS * PS5_TEXTURE_DESCRIPTOR_STRIDE)
#define PS5_STREAMOUT_DESCRIPTOR_OFFSET (PIPE_MAX_ATTRIBS * 16u)
#define PS5_STREAMOUT_CONTROL_DESCRIPTOR 4u
#define PS5_STREAMOUT_CONTROL_OFFSET \
   (PS5_STREAMOUT_DESCRIPTOR_OFFSET + 8u * 16u)
#define PS5_STREAMOUT_CONTROL_BYTES 64u
#define PS5_OCCLUSION_MAX_RBS 16u
#define PS5_OCCLUSION_QUERY_BYTES (PS5_OCCLUSION_MAX_RBS * 16u)
#define PS5_OCCLUSION_VALID_BIT (UINT64_C(1) << 63)
#define PS5_BORDER_COLOR_COUNT 4096u
#define PS5_BORDER_COLOR_BYTES \
   (PS5_BORDER_COLOR_COUNT * sizeof(union pipe_color_union))

struct ps5_sampler_state {
   struct pipe_sampler_state base;
   uint16_t border_color_ptr;
   uint8_t border_color_type;
};

struct ps5_constant_state {
   struct pipe_resource *buffer;
   unsigned offset;
   unsigned size;
   bool copied;
   bool valid;
};

struct ps5_context {
   struct pipe_context base;
   struct blitter_context *blitter;
   int last_draw_status;
   unsigned draw_calls;
   struct ps5_shader *vs;
   struct ps5_shader *gs;
   struct ps5_shader *fs;
   struct ps5_shader *geometry_vs;
   struct ps5_shader *geometry_gs;
   struct ps5_vertex_layout *geometry_layout;
   uint32_t geometry_primitive_type;
   bool geometry_provoking_vtx_last;
   PsbcShaderOutput geometry_output;
   uint8_t *geometry_package;
   size_t geometry_package_size;
   PsbcShaderOutput geometry_streamout_output;
   uint8_t *geometry_streamout_package;
   size_t geometry_streamout_package_size;
   struct pipe_framebuffer_state framebuffer;
   bool framebuffer_valid;
   struct ps5_vertex_elements *vertex_elements;
   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];
   unsigned vertex_buffer_count;
   struct pipe_resource *vertex_descriptor_table;
   struct pipe_resource *descriptor_storage[PS5_DESCRIPTOR_STAGE_COUNT];
   struct pipe_resource *border_color_storage;
   unsigned border_color_count;
   struct pipe_stream_output_target *stream_output_targets[PIPE_MAX_SO_BUFFERS];
   unsigned stream_output_target_count;
   enum mesa_prim stream_output_primitive;
   struct ps5_constant_state
      constants[PS5_CONSTANT_STAGE_COUNT][PS5_MAX_CONSTANT_BUFFERS];
   void *samplers[PS5_TEXTURE_STAGE_COUNT][PS5_MAX_TEXTURE_UNITS];
   struct pipe_sampler_view
      *sampler_views[PS5_TEXTURE_STAGE_COUNT][PS5_MAX_TEXTURE_UNITS];
   struct pipe_blend_state *blend;
   bool logicop_used;
   struct pipe_rasterizer_state *rasterizer;
   struct pipe_blend_color blend_color;
   struct pipe_viewport_state viewport;
   struct pipe_scissor_state scissor;
   bool viewport_valid;
   bool scissor_valid;
   struct pipe_depth_stencil_alpha_state *depth_stencil_alpha;
   struct pipe_stencil_ref stencil_ref;
   struct ps5_query *active_occlusion_query;
   struct ps5_query *active_primitives_generated_query;
   struct ps5_query *active_primitives_emitted_query;
   struct ps5_query *render_condition_query;
   unsigned sample_mask;
   bool queries_enabled;
   bool render_condition_inverted;
};

struct ps5_shader {
   PsbcStage stage;
   nir_shader *nir;
   struct pipe_stream_output_info stream_output;
   struct ps5_shader_variant *variants;
   struct ps5_shader_variant *active;
};

struct ps5_vertex_layout {
   uint32_t count;
   PsbcVertexAttribute attributes[PSBC_MAX_VERTEX_ATTRIBUTES];
};

struct ps5_fragment_exports {
   uint32_t formats;
   uint32_t int8_mask;
   uint32_t int10_mask;
   uint32_t color_mask;
};

struct ps5_shader_variant {
   struct ps5_vertex_layout layout;
   struct ps5_fragment_exports exports;
   uint32_t primitive_type;
   bool provoking_vtx_last;
   bool alpha_to_one;
   bool poly_line_smooth;
   PsbcShaderOutput output;
   PsbcShaderOutput streamout_output;
   uint8_t *package;
   size_t package_size;
   uint8_t *streamout_package;
   size_t streamout_package_size;
   struct ps5_shader_variant *next;
};

struct ps5_stream_output_target {
   struct pipe_stream_output_target base;
   unsigned offset;
};

struct ps5_streamout_control {
   uint32_t buffer_offsets[4];
   uint32_t generated_primitives[4];
   uint32_t emitted_primitives[4];
   uint32_t reserved[4];
};

_Static_assert(sizeof(struct ps5_streamout_control) ==
               PS5_STREAMOUT_CONTROL_BYTES,
               "PS5 streamout control ABI");

struct ps5_resource {
   struct pipe_resource base;
   struct pipe_resource *render_pool_owner;
   uint8_t *data;
   uint8_t *stencil_data;
   size_t size;
   size_t allocation_size;
   size_t stencil_allocation_size;
   unsigned stride;
   size_t level_offset[PIPE_MAX_TEXTURE_LEVELS];
   unsigned level_stride[PIPE_MAX_TEXTURE_LEVELS];
   size_t layer_stride;
   size_t render_staging_offset;
   size_t render_staging_size;
   size_t depth_staging_offset;
   size_t depth_staging_size;
   int64_t direct_start;
   int64_t stencil_direct_start;
   unsigned render_arena_first_slot;
   unsigned render_arena_slot_count;
};

struct ps5_transfer {
   struct pipe_transfer base;
   void *staging;
};

struct ps5_vertex_elements {
   unsigned count;
   struct pipe_vertex_element elements[PIPE_MAX_ATTRIBS];
};

struct ps5_fence {
   unsigned references;
};

struct ps5_query {
   unsigned type;
   uint64_t start;
   uint64_t end;
   uint64_t value;
   struct pipe_resource *buffer;
   bool active;
   bool ready;
};

static struct ps5_query **
ps5_active_primitive_query(struct ps5_context *context, unsigned type)
{
   if (type == PIPE_QUERY_PRIMITIVES_GENERATED)
      return &context->active_primitives_generated_query;
   if (type == PIPE_QUERY_PRIMITIVES_EMITTED)
      return &context->active_primitives_emitted_query;
   return NULL;
}

static bool
ps5_vertex_format(enum pipe_format format, PsbcVertexFormat *out);
static unsigned
ps5_vertex_format_size(enum pipe_format format);
static bool
ps5_vertex_layout_from_state(const struct ps5_shader *shader,
                             const struct ps5_vertex_elements *elements,
                             struct ps5_vertex_layout *layout);
static bool
ps5_select_shader_variant(struct ps5_shader *shader, uint32_t address32_hi,
                          const struct ps5_vertex_layout *layout,
                          uint32_t primitive_type,
                          bool provoking_vtx_last, bool alpha_to_one,
                          bool poly_line_smooth,
                          const struct ps5_fragment_exports *exports);
static bool
ps5_select_geometry_pipeline(struct ps5_context *context,
                             uint32_t address32_hi,
                             const struct ps5_vertex_layout *layout,
                             uint32_t primitive_type);
static bool
ps5_render_condition_passes(const struct ps5_context *context);

#define PS5_DIRECT_MEMORY_TYPE 12
#define PS5_MAP_PROTECTION 0x33
#define PS5_RENDER_ALIGNMENT 0x200000u
#define PS5_COLOR_TARGET_ALIGNMENT 0x10000u
#define PS5_DEPTH_TARGET_BYTES 0xa00000u
#define PS5_D32_SURFACE_BYTES 0x870000u
#define PS5_STENCIL_TARGET_BYTES 0x280000u
#define PS5_STENCIL_ALIGNMENT 0x10000u
#define PS5_MAX_CONSTANT_BUFFER_SIZE 0x4000u
/* Mesa reserves eight vec4 slots in VS/GS CB0 for lowered clip planes.
 * Advertise enough backing storage for the 1024 user components required by
 * OpenGL 3.3 after that reservation. */
#define PS5_MAX_DEFAULT_CONSTANT_BUFFER_SIZE 0x1080u
#define PS5_CONSTANT_DATA_OFFSET 0x800u
#define PS5_DESCRIPTOR_STORAGE_BYTES \
   (PS5_CONSTANT_DATA_OFFSET + 2u * PS5_MAX_CONSTANT_BUFFER_SIZE)
#define PS5_MAX_TEXTURE_2D_SIZE PS5_MAX_RENDER_SIZE
#define PS5_MAX_TEXTURE_CUBE_LEVELS 12u
#define PS5_MAX_TEXTURE_CUBE_SIZE (1u << (PS5_MAX_TEXTURE_CUBE_LEVELS - 1u))
#define PS5_MAX_TEXTURE_ARRAY_LAYERS 256u
#define PS5_MAX_TEXTURE_3D_LEVELS 9u
#define PS5_MAX_TEXTURE_3D_SIZE (1u << (PS5_MAX_TEXTURE_3D_LEVELS - 1u))
#define PS5_MAX_TEXEL_BUFFER_ELEMENTS (1u << 20)
#define PS5_MIN_POINT_LINE_SIZE 1.0f
#define PS5_MAX_POINT_LINE_SIZE 64.0f
#ifndef PS5_ENABLE_PACKED_DEPTH_STENCIL
#define PS5_ENABLE_PACKED_DEPTH_STENCIL 0
#endif
#ifndef PS5_ENABLE_SRGB_CANDIDATE
#define PS5_ENABLE_SRGB_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE
#define PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_TEXTURE_RG_CANDIDATE
#define PS5_ENABLE_TEXTURE_RG_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_TEXTURE_SWIZZLE_CANDIDATE
#define PS5_ENABLE_TEXTURE_SWIZZLE_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_TEXTURE_SNORM_CANDIDATE
#define PS5_ENABLE_TEXTURE_SNORM_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE
#define PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_SHARED_EXPONENT_CANDIDATE
#define PS5_ENABLE_SHARED_EXPONENT_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_PACKED_FLOAT_CANDIDATE
#define PS5_ENABLE_PACKED_FLOAT_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE
#define PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE
#define PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_RGB10_A2UI_CANDIDATE
#define PS5_ENABLE_RGB10_A2UI_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE
#define PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_SHADER_TEXTURE_LOD_CANDIDATE
#define PS5_ENABLE_SHADER_TEXTURE_LOD_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE
#define PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_TEXTURE_CUBE_CANDIDATE
#define PS5_ENABLE_TEXTURE_CUBE_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_TEXTURE_ARRAY_CANDIDATE
#define PS5_ENABLE_TEXTURE_ARRAY_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE
#define PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_TEXTURE_3D_CANDIDATE
#define PS5_ENABLE_TEXTURE_3D_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_TEXTURE_1D_CANDIDATE
#define PS5_ENABLE_TEXTURE_1D_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_UBO_CANDIDATE
#define PS5_ENABLE_UBO_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_TIMER_QUERY_CANDIDATE
#define PS5_ENABLE_TIMER_QUERY_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE
#define PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_DEPTH_CLAMP_CANDIDATE
#define PS5_ENABLE_DEPTH_CLAMP_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_GLSL_330_CANDIDATE
#define PS5_ENABLE_GLSL_330_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_PACKED_VERTEX_CANDIDATE
#define PS5_ENABLE_PACKED_VERTEX_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_INTEGER_VERTEX_CANDIDATE
#define PS5_ENABLE_INTEGER_VERTEX_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_PADDED_FBO_CANDIDATE
#define PS5_ENABLE_PADDED_FBO_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE
#define PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE
#define PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_SOFTWARE_BLIT_CANDIDATE
#define PS5_ENABLE_SOFTWARE_BLIT_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE
#define PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE
#define PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE 0
#endif
#define PS5_MAX_COLOR_WIDTH (PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE \
   ? PS5_MAX_RENDER_SIZE : PS5_RENDER_WIDTH)
#define PS5_MAX_COLOR_HEIGHT (PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE \
   ? PS5_MAX_RENDER_SIZE : PS5_RENDER_HEIGHT)
#define PS5_MAX_DEPTH_WIDTH (PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE \
   ? PS5_MAX_RENDER_SIZE : PS5_RENDER_WIDTH)
#define PS5_MAX_DEPTH_HEIGHT (PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE \
   ? PS5_MAX_RENDER_SIZE : PS5_RENDER_HEIGHT)
#ifndef PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE
#define PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE 1
#endif
#ifndef PS5_ENABLE_FAKE_SW_MSAA_CANDIDATE
#define PS5_ENABLE_FAKE_SW_MSAA_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_MSAA4_CANDIDATE
#define PS5_ENABLE_MSAA4_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_MSAA_ARRAY_CANDIDATE
#define PS5_ENABLE_MSAA_ARRAY_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE
#define PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_GEOMETRY_CANDIDATE
#define PS5_ENABLE_GEOMETRY_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_MRT_CANDIDATE
#define PS5_ENABLE_MRT_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_INDEPENDENT_BLEND_CANDIDATE
#define PS5_ENABLE_INDEPENDENT_BLEND_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE
#define PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE
#define PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE
#define PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_POINT_COORD_CANDIDATE
#define PS5_ENABLE_POINT_COORD_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_BORDER_COLOR_CANDIDATE
#define PS5_ENABLE_BORDER_COLOR_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE
#define PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE
#define PS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_CORE_RENDER_FORMATS_CANDIDATE
#define PS5_ENABLE_CORE_RENDER_FORMATS_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_SMOOTH_RASTER_CANDIDATE
#define PS5_ENABLE_SMOOTH_RASTER_CANDIDATE 0
#endif
#ifndef PS5_PUBLIC_TEXTURE_RG_RENDER_TEST
#define PS5_PUBLIC_TEXTURE_RG_RENDER_TEST 0
#endif
#ifndef PS5_PUBLIC_TEXTURE_RG_TILE_TEST
#define PS5_PUBLIC_TEXTURE_RG_TILE_TEST 0
#endif

_Static_assert(PS5_DEPTH_TARGET_BYTES >= PS5_D32_SURFACE_BYTES,
               "D32 allocation is smaller than the swizzled surface");
_Static_assert(PS5_RENDER_POOL_BYTES >= 2u * PS5_RENDER_TARGET_BYTES,
               "render pool cannot hold two VideoOut buffers");
_Static_assert((PS5_RENDER_POOL_BYTES - PS5_RENDER_ARENA_OFFSET) %
                  PS5_RENDER_ARENA_SLOT_BYTES == 0,
               "render arena is not slot aligned");
_Static_assert((PS5_RENDER_ARENA_SLOT_COUNT + 63u) / 64u ==
                  PS5_RENDER_ARENA_BITMAP_WORDS,
               "render arena bitmap does not cover its slots");
_Static_assert(PS5_MAX_CONSTANT_BUFFERS * 16u <= PS5_CONSTANT_DATA_OFFSET,
               "constant descriptors overlap copied data");
_Static_assert(PS5_TEXTURE_DESCRIPTOR_BYTES +
                  2u * PS5_MAX_CONSTANT_BUFFERS * 16u <=
               PS5_CONSTANT_DATA_OFFSET,
               "merged VS/GS descriptors overlap copied data");
_Static_assert(PS5_MERGED_TEXTURE_UNITS <= PSBC_GALLIUM_UBO_BINDING_BASE,
               "merged VS/GS samplers overlap UBO bindings");
_Static_assert(PSBC_GALLIUM_UBO_BINDING_BASE +
                  2u * PS5_MAX_CONSTANT_BUFFERS <=
               PSBC_MAX_DESCRIPTOR_BINDINGS,
               "PSBC descriptor ABI is too small for merged VS/GS UBOs");

struct ps5_native_depth_stencil_state {
   uint32_t depth_control;
   uint32_t stencil_control;
   uint32_t stencil_refmask;
   uint32_t stencil_refmask_bf;
};

struct ps5_native_graphics_state {
   uint32_t blend_control[PS5_MAX_RENDER_TARGETS];
   uint32_t target_mask;
   uint32_t color_control;
   uint32_t color_control_valid;
   uint32_t dual_source_blend;
   uint32_t alpha_to_coverage;
   uint32_t alpha_to_one;
   uint32_t blend_color[4];
   uint32_t viewport[8];
   uint32_t scissor[2];
   uint32_t clip_control;
   uint32_t clip_control_valid;
   uint32_t rasterizer_control;
   uint32_t rasterizer_valid;
   uint32_t point_line[3];
   uint32_t point_line_valid;
   uint32_t interp_control;
   uint32_t interp_control_valid;
   uint32_t polygon_offset[6];
   uint32_t polygon_offset_valid;
};

static uint32_t
ps5_float_bits(float value)
{
   uint32_t bits;

   memcpy(&bits, &value, sizeof(bits));
   return bits;
}

static size_t
ps5_tiled_surface_size(unsigned width, unsigned height)
{
   return (size_t)((width + 127u) >> 7) *
          ((height + 127u) >> 7) * UINT32_C(0x10000);
}

static size_t
ps5_tiled_stencil_surface_size(unsigned width, unsigned height)
{
   return (size_t)((width + 255u) >> 8) *
          ((height + 255u) >> 8) * UINT32_C(0x10000);
}

static size_t
ps5_tiled_stencil_surface_size_samples(unsigned width, unsigned height,
                                       unsigned samples)
{
   return samples == 4
             ? (size_t)((width + 127u) >> 7) *
                  ((height + 127u) >> 7) * UINT32_C(0x10000)
             : ps5_tiled_stencil_surface_size(width, height);
}

static size_t
ps5_tiled_rgba8_msaa4_surface_size(unsigned width, unsigned height)
{
   return (size_t)((width + 63u) >> 6) *
          ((height + 63u) >> 6) * UINT32_C(0x10000);
}

static bool
ps5_tiled_color_msaa4_tile(enum pipe_format format, unsigned *width,
                           unsigned *height)
{
   switch (util_format_get_blocksize(format)) {
   case 1: *width = 128; *height = 128; return true;
   case 2: *width = 128; *height = 64; return true;
   case 4: *width = 64; *height = 64; return true;
   case 8: *width = 64; *height = 32; return true;
   case 16: *width = 32; *height = 32; return true;
   default: return false;
   }
}

static size_t
ps5_tiled_color_msaa4_surface_size(enum pipe_format format, unsigned width,
                                   unsigned height)
{
   unsigned tile_width;
   unsigned tile_height;

   if (!ps5_tiled_color_msaa4_tile(format, &tile_width, &tile_height))
      return 0;
   return (size_t)((width + tile_width - 1u) / tile_width) *
          ((height + tile_height - 1u) / tile_height) *
          UINT32_C(0x10000);
}

static size_t
ps5_tiled_depth_surface_size(unsigned width, unsigned height,
                             unsigned samples)
{
   return samples == 4
             ? ps5_tiled_rgba8_msaa4_surface_size(width, height)
             : ps5_tiled_surface_size(width, height);
}

static bool
ps5_integer_texture_format(enum pipe_format format)
{
   if (format == PIPE_FORMAT_R32G32B32A32_UINT ||
       format == PIPE_FORMAT_R32G32B32A32_SINT)
      return true;
   return PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE &&
          (format == PIPE_FORMAT_R8G8B8A8_UINT ||
           format == PIPE_FORMAT_R8G8B8A8_SINT ||
           format == PIPE_FORMAT_R16G16B16A16_UINT ||
           format == PIPE_FORMAT_R16G16B16A16_SINT);
}

static bool
ps5_core_render_target_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R8_UNORM:
   case PIPE_FORMAT_R8_SNORM:
   case PIPE_FORMAT_R8G8_UNORM:
   case PIPE_FORMAT_R8G8_SNORM:
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R8G8B8A8_SNORM:
   case PIPE_FORMAT_R8G8B8A8_SRGB:
   case PIPE_FORMAT_R16_UNORM:
   case PIPE_FORMAT_R16_SNORM:
   case PIPE_FORMAT_R16_FLOAT:
   case PIPE_FORMAT_R16G16_UNORM:
   case PIPE_FORMAT_R16G16_SNORM:
   case PIPE_FORMAT_R16G16_FLOAT:
   case PIPE_FORMAT_R16G16B16A16_UNORM:
   case PIPE_FORMAT_R16G16B16A16_SNORM:
   case PIPE_FORMAT_R16G16B16A16_FLOAT:
   case PIPE_FORMAT_R32_FLOAT:
   case PIPE_FORMAT_R32G32_FLOAT:
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
   case PIPE_FORMAT_R8_UINT:
   case PIPE_FORMAT_R8_SINT:
   case PIPE_FORMAT_R8G8_UINT:
   case PIPE_FORMAT_R8G8_SINT:
   case PIPE_FORMAT_R8G8B8A8_UINT:
   case PIPE_FORMAT_R8G8B8A8_SINT:
   case PIPE_FORMAT_R16_UINT:
   case PIPE_FORMAT_R16_SINT:
   case PIPE_FORMAT_R16G16_UINT:
   case PIPE_FORMAT_R16G16_SINT:
   case PIPE_FORMAT_R16G16B16A16_UINT:
   case PIPE_FORMAT_R16G16B16A16_SINT:
   case PIPE_FORMAT_R32_UINT:
   case PIPE_FORMAT_R32_SINT:
   case PIPE_FORMAT_R32G32_UINT:
   case PIPE_FORMAT_R32G32_SINT:
   case PIPE_FORMAT_R32G32B32A32_UINT:
   case PIPE_FORMAT_R32G32B32A32_SINT:
   case PIPE_FORMAT_R10G10B10A2_UNORM:
   case PIPE_FORMAT_R10G10B10A2_UINT:
   case PIPE_FORMAT_R11G11B10_FLOAT:
      return true;
   default:
      return false;
   }
}

static bool
ps5_render_target_format(enum pipe_format format)
{
   if (PS5_ENABLE_CORE_RENDER_FORMATS_CANDIDATE)
      return ps5_core_render_target_format(format);
   return format == PIPE_FORMAT_R8G8B8A8_UNORM ||
          (PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE &&
           format == PIPE_FORMAT_R8G8B8A8_SRGB) ||
          (PS5_ENABLE_TEXTURE_RG_CANDIDATE &&
           (format == PIPE_FORMAT_R8_UNORM ||
            format == PIPE_FORMAT_R8G8_UNORM)) ||
          (PS5_ENABLE_PACKED_FLOAT_CANDIDATE &&
           format == PIPE_FORMAT_R11G11B10_FLOAT) ||
          (PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE &&
           ps5_integer_texture_format(format)) ||
          (PS5_ENABLE_RGB10_A2UI_CANDIDATE &&
           format == PIPE_FORMAT_R10G10B10A2_UINT);
}

static bool
ps5_msaa4_color_format(enum pipe_format format)
{
   unsigned tile_width;
   unsigned tile_height;

   return ps5_render_target_format(format) &&
          ps5_tiled_color_msaa4_tile(format, &tile_width, &tile_height);
}

static bool
ps5_msaa4_depth_support(enum pipe_format format,
                        enum pipe_texture_target target,
                        unsigned sample_count,
                        unsigned storage_sample_count,
                        unsigned bindings)
{
   const unsigned allowed = PIPE_BIND_DEPTH_STENCIL |
      (PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE ? PIPE_BIND_SAMPLER_VIEW : 0);

   return PS5_ENABLE_MSAA4_CANDIDATE && target == PIPE_TEXTURE_2D &&
          sample_count == 4 && storage_sample_count == 4 && bindings &&
          (format == PIPE_FORMAT_Z32_FLOAT ||
           (PS5_ENABLE_PACKED_DEPTH_STENCIL &&
            format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)) &&
          (bindings & allowed) && !(bindings & ~allowed);
}

static size_t
ps5_tiled_color_surface_size(enum pipe_format format, unsigned width,
                             unsigned height)
{
   unsigned tile_width = 128;
   unsigned tile_height = 128;
   unsigned bytes_per_pixel = util_format_get_blocksize(format);

   if (bytes_per_pixel == 1) {
      tile_width = 256;
      tile_height = 256;
   } else if (bytes_per_pixel == 2) {
      tile_width = 256;
      tile_height = 128;
   } else if (bytes_per_pixel == 8) {
      tile_width = 128;
      tile_height = 64;
   } else if (bytes_per_pixel == 16) {
      tile_width = 64;
      tile_height = 64;
   }
   return (size_t)((width + tile_width - 1u) / tile_width) *
          ((height + tile_height - 1u) / tile_height) *
          UINT32_C(0x10000);
}

static uint32_t
ps5_color_target_info(enum pipe_format format)
{
   if (PS5_ENABLE_CORE_RENDER_FORMATS_CANDIDATE &&
       ps5_core_render_target_format(format)) {
      unsigned cb_format = ac_get_cb_format(GFX10, format);
      unsigned number_type = ac_get_cb_number_type(format);
      unsigned swap = ac_translate_colorswap(GFX10, format, false);
      uint32_t info;

      if (!cb_format || swap == ~0u)
         return 0;
      info = (cb_format << 2) | (number_type << 8) | (swap << 11);
      if (number_type <= 1u || number_type == 6u)
         info |= UINT32_C(1) << 15;
      else
         info |= (UINT32_C(1) << 17) | (UINT32_C(1) << 18);
      if (number_type == 4u || number_type == 5u)
         info |= UINT32_C(1) << 16;
      return info;
   }
   switch (format) {
   case PIPE_FORMAT_R8_UNORM: return UINT32_C(0x00008004);
   case PIPE_FORMAT_R8G8_UNORM: return UINT32_C(0x0000800c);
   case PIPE_FORMAT_R11G11B10_FLOAT: return UINT32_C(0x00060718);
   case PIPE_FORMAT_R8G8B8A8_UINT: return UINT32_C(0x00070428);
   case PIPE_FORMAT_R8G8B8A8_SINT: return UINT32_C(0x00070528);
   case PIPE_FORMAT_R16G16B16A16_UINT: return UINT32_C(0x00070430);
   case PIPE_FORMAT_R16G16B16A16_SINT: return UINT32_C(0x00070530);
   case PIPE_FORMAT_R32G32B32A32_UINT: return UINT32_C(0x00070438);
   case PIPE_FORMAT_R32G32B32A32_SINT: return UINT32_C(0x00070538);
   case PIPE_FORMAT_R10G10B10A2_UINT: return UINT32_C(0x00070424);
   case PIPE_FORMAT_R8G8B8A8_SRGB: return UINT32_C(0x00008628);
   default: return UINT32_C(0x00008028);
   }
}

static struct ps5_fragment_exports
ps5_fragment_exports_for_framebuffer(const struct pipe_framebuffer_state *fb)
{
   struct ps5_fragment_exports exports = {.formats = UINT32_C(0x99999999)};

   for (unsigned i = 0; i < fb->nr_cbufs; ++i) {
      enum pipe_format format = fb->cbufs[i].format;
      struct ac_spi_color_formats formats;
      uint32_t info;

      if (!fb->cbufs[i].texture)
         continue;
      exports.color_mask |= 1u << i;
      info = ps5_color_target_info(format);
      /* Always retain alpha and blending support. This avoids variants for
       * blend enable/alpha-to-coverage while preserving float32 precision. */
      ac_choose_spi_color_formats(G_028C70_FORMAT_GFX6(info),
                                   G_028C70_COMP_SWAP(info),
                                   G_028C70_NUMBER_TYPE(info), false, false,
                                   &formats);
      exports.formats = (exports.formats & ~(UINT32_C(0xf) << (4 * i))) |
                        (formats.blend_alpha << (4 * i));
      if (util_format_is_pure_integer(format)) {
         unsigned bits = util_format_description(format)->channel[0].size;
         exports.int8_mask |= (bits == 8 ? 1u : 0u) << i;
         exports.int10_mask |= (bits == 10 ? 1u : 0u) << i;
      }
   }
   return exports;
}

static bool
ps5_core_sampled_texture_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R16_UNORM:
   case PIPE_FORMAT_R16G16_UNORM:
   case PIPE_FORMAT_R16G16B16A16_UNORM:
   case PIPE_FORMAT_R16_SNORM:
   case PIPE_FORMAT_R16G16_SNORM:
   case PIPE_FORMAT_R16G16B16A16_SNORM:
   case PIPE_FORMAT_R16_FLOAT:
   case PIPE_FORMAT_R16G16_FLOAT:
   case PIPE_FORMAT_R32_FLOAT:
   case PIPE_FORMAT_R32G32_FLOAT:
   case PIPE_FORMAT_R8_UINT:
   case PIPE_FORMAT_R8G8_UINT:
   case PIPE_FORMAT_R16_UINT:
   case PIPE_FORMAT_R16G16_UINT:
   case PIPE_FORMAT_R32_UINT:
   case PIPE_FORMAT_R32G32_UINT:
   case PIPE_FORMAT_R8_SINT:
   case PIPE_FORMAT_R8G8_SINT:
   case PIPE_FORMAT_R16_SINT:
   case PIPE_FORMAT_R16G16_SINT:
   case PIPE_FORMAT_R32_SINT:
   case PIPE_FORMAT_R32G32_SINT:
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      return true;
   default:
      return false;
   }
}

static bool
ps5_sampled_texture_format(enum pipe_format format)
{
   return format == PIPE_FORMAT_R8G8B8A8_UNORM ||
          (PS5_ENABLE_SRGB_CANDIDATE &&
           format == PIPE_FORMAT_R8G8B8A8_SRGB) ||
          (PS5_ENABLE_TEXTURE_RG_CANDIDATE &&
           (format == PIPE_FORMAT_R8_UNORM ||
            format == PIPE_FORMAT_R8G8_UNORM)) ||
          (PS5_ENABLE_TEXTURE_SNORM_CANDIDATE &&
           (format == PIPE_FORMAT_R8_SNORM ||
            format == PIPE_FORMAT_R8G8_SNORM ||
            format == PIPE_FORMAT_R8G8B8A8_SNORM)) ||
          (PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE &&
           (format == PIPE_FORMAT_R16G16B16A16_FLOAT ||
            format == PIPE_FORMAT_R32G32B32A32_FLOAT)) ||
          (PS5_ENABLE_SHARED_EXPONENT_CANDIDATE &&
           format == PIPE_FORMAT_R9G9B9E5_FLOAT) ||
          (PS5_ENABLE_PACKED_FLOAT_CANDIDATE &&
           format == PIPE_FORMAT_R11G11B10_FLOAT) ||
          (PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE &&
           ps5_integer_texture_format(format)) ||
          (PS5_ENABLE_RGB10_A2UI_CANDIDATE &&
           format == PIPE_FORMAT_R10G10B10A2_UINT) ||
          (PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE &&
           (format == PIPE_FORMAT_Z32_FLOAT ||
            (PS5_ENABLE_PACKED_DEPTH_STENCIL &&
             format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT))) ||
          (PS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE &&
           ps5_core_sampled_texture_format(format));
}

static bool
ps5_packed_vertex_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_R10G10B10A2_UNORM:
   case PIPE_FORMAT_B10G10R10A2_UNORM:
   case PIPE_FORMAT_R10G10B10A2_SNORM:
   case PIPE_FORMAT_B10G10R10A2_SNORM:
   case PIPE_FORMAT_R10G10B10A2_USCALED:
   case PIPE_FORMAT_B10G10R10A2_USCALED:
   case PIPE_FORMAT_R10G10B10A2_SSCALED:
   case PIPE_FORMAT_B10G10R10A2_SSCALED:
      return true;
   default:
      return false;
   }
}

static bool
ps5_integer_vertex_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R32_SINT:
   case PIPE_FORMAT_R32G32_SINT:
   case PIPE_FORMAT_R32G32B32_SINT:
   case PIPE_FORMAT_R32G32B32A32_SINT:
   case PIPE_FORMAT_R32_UINT:
   case PIPE_FORMAT_R32G32_UINT:
   case PIPE_FORMAT_R32G32B32_UINT:
   case PIPE_FORMAT_R32G32B32A32_UINT:
      return true;
   default:
      return false;
   }
}

static bool
ps5_texel_buffer_format(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R8_UNORM:
   case PIPE_FORMAT_R8_UINT:
   case PIPE_FORMAT_R8_SINT:
   case PIPE_FORMAT_R16_UNORM:
   case PIPE_FORMAT_R16_FLOAT:
   case PIPE_FORMAT_R16_UINT:
   case PIPE_FORMAT_R16_SINT:
   case PIPE_FORMAT_R32_FLOAT:
   case PIPE_FORMAT_R32_UINT:
   case PIPE_FORMAT_R32_SINT:
   case PIPE_FORMAT_R8G8_UNORM:
   case PIPE_FORMAT_R8G8_UINT:
   case PIPE_FORMAT_R8G8_SINT:
   case PIPE_FORMAT_R16G16_UNORM:
   case PIPE_FORMAT_R16G16_FLOAT:
   case PIPE_FORMAT_R16G16_UINT:
   case PIPE_FORMAT_R16G16_SINT:
   case PIPE_FORMAT_R32G32_FLOAT:
   case PIPE_FORMAT_R32G32_UINT:
   case PIPE_FORMAT_R32G32_SINT:
   case PIPE_FORMAT_R32G32B32_FLOAT:
   case PIPE_FORMAT_R32G32B32_UINT:
   case PIPE_FORMAT_R32G32B32_SINT:
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R8G8B8A8_UINT:
   case PIPE_FORMAT_R8G8B8A8_SINT:
   case PIPE_FORMAT_R16G16B16A16_UNORM:
   case PIPE_FORMAT_R16G16B16A16_FLOAT:
   case PIPE_FORMAT_R16G16B16A16_UINT:
   case PIPE_FORMAT_R16G16B16A16_SINT:
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
   case PIPE_FORMAT_R32G32B32A32_UINT:
   case PIPE_FORMAT_R32G32B32A32_SINT:
      return true;
   default:
      return false;
   }
}

static bool
ps5_sampled_texture_target(enum pipe_texture_target target)
{
   return (PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE && target == PIPE_BUFFER) ||
          (PS5_ENABLE_TEXTURE_1D_CANDIDATE &&
           (target == PIPE_TEXTURE_1D ||
            target == PIPE_TEXTURE_1D_ARRAY)) ||
          target == PIPE_TEXTURE_2D ||
          (PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE &&
           target == PIPE_TEXTURE_RECT) ||
          (PS5_ENABLE_TEXTURE_CUBE_CANDIDATE &&
           target == PIPE_TEXTURE_CUBE) ||
          (PS5_ENABLE_TEXTURE_ARRAY_CANDIDATE &&
           target == PIPE_TEXTURE_2D_ARRAY) ||
          (PS5_ENABLE_TEXTURE_3D_CANDIDATE &&
           target == PIPE_TEXTURE_3D);
}

static bool
ps5_linear_sampled_layout(const struct pipe_resource *resource)
{
   return resource && ps5_sampled_texture_format(resource->format) &&
          resource->nr_samples <= 1 &&
          (!(resource->bind & PIPE_BIND_DEPTH_STENCIL) ||
           resource->target == PIPE_TEXTURE_RECT ||
           ((resource->format == PIPE_FORMAT_Z32_FLOAT ||
             resource->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) &&
            resource->last_level > 0)) &&
          (resource->target != PIPE_TEXTURE_2D ||
           !(resource->bind & PIPE_BIND_RENDER_TARGET) ||
           (resource->bind & PIPE_BIND_SAMPLER_VIEW) ||
           resource->last_level > 0);
}

static bool
ps5_color_render_target(enum pipe_texture_target target)
{
   return target == PIPE_TEXTURE_2D ||
          (PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE &&
           (target == PIPE_TEXTURE_2D_ARRAY ||
            target == PIPE_TEXTURE_CUBE || target == PIPE_TEXTURE_3D));
}

static bool
ps5_depth_render_target(enum pipe_texture_target target)
{
   return (PS5_ENABLE_TEXTURE_1D_CANDIDATE &&
           (target == PIPE_TEXTURE_1D ||
            target == PIPE_TEXTURE_1D_ARRAY)) ||
          target == PIPE_TEXTURE_2D ||
          (PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE &&
           (target == PIPE_TEXTURE_2D_ARRAY ||
            target == PIPE_TEXTURE_CUBE ||
            target == PIPE_TEXTURE_3D));
}

static unsigned
ps5_texture_level_layers(const struct pipe_resource *resource,
                         unsigned level)
{
   return resource->target == PIPE_TEXTURE_3D
             ? MAX2(resource->depth0 >> level, 1u)
             : resource->array_size;
}

static unsigned
ps5_linear_mip_storage_extent(unsigned extent, unsigned level)
{
   return MAX2((extent + BITFIELD_BIT(level) - 1u) >> level, 1u);
}

static bool
ps5_packed_depth_sample_layout(const struct pipe_resource *resource,
                               size_t *layer_size, unsigned *base_stride)
{
   size_t total = 0;

   if (!resource ||
       resource->format != PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
       !layer_size || !base_stride)
      return false;
   for (unsigned level = resource->last_level + 1; level-- > 0;) {
      const unsigned width =
         ps5_linear_mip_storage_extent(resource->width0, level);
      const unsigned height =
         ps5_linear_mip_storage_extent(resource->height0, level);
      const size_t stride = ((size_t)width * sizeof(float) + 255u) &
                            ~(size_t)255u;

      if (stride > SIZE_MAX / height ||
          total > SIZE_MAX - stride * height)
         return false;
      if (!level)
         *base_stride = (unsigned)stride;
      total += stride * height;
   }
   *layer_size = total;
   return true;
}

static bool
ps5_render_staging_required(const struct pipe_resource *resource)
{
   return PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE && resource &&
          (resource->bind & PIPE_BIND_RENDER_TARGET) &&
          ps5_color_render_target(resource->target) &&
          ps5_linear_sampled_layout(resource);
}

static bool
ps5_depth_staging_required(const struct pipe_resource *resource)
{
   return PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE && resource &&
          (resource->format == PIPE_FORMAT_Z32_FLOAT ||
           resource->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) &&
          resource->last_level > 0 &&
          (resource->bind & PIPE_BIND_DEPTH_STENCIL) &&
          ps5_depth_render_target(resource->target);
}

static bool
ps5_mutable_sampled_resource_bind(unsigned bind)
{
   /* Mesa derives mutable texture bindings from 2D format support, so cube,
    * array, and 3D resources can carry an advisory render-target bit even
    * though this backend only exposes those targets for sampling.
    */
   return (bind & PIPE_BIND_SAMPLER_VIEW) &&
          !(bind & ~(PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_RENDER_TARGET));
}

static unsigned
ps5_texture_format_size(enum pipe_format format)
{
   if (PS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE &&
       ps5_core_sampled_texture_format(format))
      return util_format_get_blocksize(format);
   switch (format) {
   case PIPE_FORMAT_R8_UNORM: return PS5_ENABLE_TEXTURE_RG_CANDIDATE ? 1 : 0;
   case PIPE_FORMAT_R8G8_UNORM: return PS5_ENABLE_TEXTURE_RG_CANDIDATE ? 2 : 0;
   case PIPE_FORMAT_R8_SNORM:
      return PS5_ENABLE_TEXTURE_SNORM_CANDIDATE ? 1 : 0;
   case PIPE_FORMAT_R8G8_SNORM:
      return PS5_ENABLE_TEXTURE_SNORM_CANDIDATE ? 2 : 0;
   case PIPE_FORMAT_R8G8B8A8_SNORM:
      return PS5_ENABLE_TEXTURE_SNORM_CANDIDATE ? 4 : 0;
   case PIPE_FORMAT_R16G16B16A16_FLOAT:
      return PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE ? 8 : 0;
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
      return PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE ? 16 : 0;
   case PIPE_FORMAT_R9G9B9E5_FLOAT:
      return PS5_ENABLE_SHARED_EXPONENT_CANDIDATE ? 4 : 0;
   case PIPE_FORMAT_R11G11B10_FLOAT:
      return PS5_ENABLE_PACKED_FLOAT_CANDIDATE ? 4 : 0;
   case PIPE_FORMAT_R8G8B8A8_UINT:
   case PIPE_FORMAT_R8G8B8A8_SINT:
      return PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE &&
             PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE ? 4 : 0;
   case PIPE_FORMAT_R16G16B16A16_UINT:
   case PIPE_FORMAT_R16G16B16A16_SINT:
      return PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE &&
             PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE ? 8 : 0;
   case PIPE_FORMAT_R32G32B32A32_UINT:
   case PIPE_FORMAT_R32G32B32A32_SINT:
      return PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE ? 16 : 0;
   case PIPE_FORMAT_R10G10B10A2_UINT:
      return PS5_ENABLE_RGB10_A2UI_CANDIDATE ? 4 : 0;
   case PIPE_FORMAT_R8G8B8A8_SRGB: return PS5_ENABLE_SRGB_CANDIDATE ? 4 : 0;
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_Z32_FLOAT: return 4;
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      return PS5_ENABLE_PACKED_DEPTH_STENCIL ? 8 : 0;
   default: return 0;
   }
}

static bool
ps5_texture_descriptor_format(enum pipe_format format, uint32_t *word1)
{
   if (PS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE &&
       ps5_core_sampled_texture_format(format)) {
      const struct gfx10_format *native = &gfx10_format_table[format];

      if (!native->img_format || native->buffers_only)
         return false;
      *word1 = native->img_format << 20;
      return true;
   }
   switch (format) {
   case PIPE_FORMAT_R8_UNORM: *word1 = UINT32_C(0x00100000); return true;
   case PIPE_FORMAT_R8_SNORM: *word1 = UINT32_C(0x00200000); return true;
   case PIPE_FORMAT_R8G8_UNORM: *word1 = UINT32_C(0x00e00000); return true;
   case PIPE_FORMAT_R8G8_SNORM: *word1 = UINT32_C(0x00f00000); return true;
   case PIPE_FORMAT_R8G8B8A8_UNORM: *word1 = UINT32_C(0x03800000); return true;
   case PIPE_FORMAT_R8G8B8A8_SNORM: *word1 = UINT32_C(0x03900000); return true;
   case PIPE_FORMAT_R16G16B16A16_FLOAT: *word1 = UINT32_C(0x04700000); return true;
   case PIPE_FORMAT_R32G32B32A32_FLOAT: *word1 = UINT32_C(0x04d00000); return true;
   case PIPE_FORMAT_R9G9B9E5_FLOAT: *word1 = UINT32_C(0x08400000); return true;
   case PIPE_FORMAT_R11G11B10_FLOAT: *word1 = UINT32_C(0x02400000); return true;
   case PIPE_FORMAT_R8G8B8A8_UINT: *word1 = UINT32_C(0x03c00000); return true;
   case PIPE_FORMAT_R8G8B8A8_SINT: *word1 = UINT32_C(0x03d00000); return true;
   case PIPE_FORMAT_R16G16B16A16_UINT: *word1 = UINT32_C(0x04500000); return true;
   case PIPE_FORMAT_R16G16B16A16_SINT: *word1 = UINT32_C(0x04600000); return true;
   case PIPE_FORMAT_R32G32B32A32_UINT: *word1 = UINT32_C(0x04b00000); return true;
   case PIPE_FORMAT_R32G32B32A32_SINT: *word1 = UINT32_C(0x04c00000); return true;
   case PIPE_FORMAT_R10G10B10A2_UINT: *word1 = UINT32_C(0x03600000); return true;
   case PIPE_FORMAT_R8G8B8A8_SRGB: *word1 = UINT32_C(0x08200000); return true;
   case PIPE_FORMAT_Z32_FLOAT:
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      if (PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE) {
         *word1 = UINT32_C(0x01600000);
         return true;
      }
      return false;
   default: return false;
   }
}

static bool
ps5_texture_descriptor_swizzle(unsigned swizzle, uint32_t *selector)
{
   static const uint8_t selectors[] = {4, 5, 6, 7, 0, 1};

   if (swizzle >= ARRAY_SIZE(selectors))
      return false;
   *selector = selectors[swizzle];
   return true;
}

static bool
ps5_texture_descriptor_wrap(unsigned wrap, uint32_t *clamp)
{
   switch (wrap) {
   case PIPE_TEX_WRAP_REPEAT: *clamp = 0; return true;
   case PIPE_TEX_WRAP_MIRROR_REPEAT: *clamp = 1; return true;
   case PIPE_TEX_WRAP_CLAMP_TO_EDGE: *clamp = 2; return true;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
      *clamp = 3;
      return PS5_ENABLE_BORDER_COLOR_CANDIDATE;
   case PIPE_TEX_WRAP_CLAMP:
      *clamp = 4;
      return PS5_ENABLE_BORDER_COLOR_CANDIDATE;
   case PIPE_TEX_WRAP_MIRROR_CLAMP:
      *clamp = 5;
      return PS5_ENABLE_BORDER_COLOR_CANDIDATE;
   case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
      *clamp = 6;
      return PS5_ENABLE_BORDER_COLOR_CANDIDATE;
   case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
      *clamp = 7;
      return PS5_ENABLE_BORDER_COLOR_CANDIDATE;
   default: return false;
   }
}

static bool
ps5_texture_descriptor_filter(unsigned filter, uint32_t *native)
{
   switch (filter) {
   case PIPE_TEX_FILTER_NEAREST: *native = 0; return true;
   case PIPE_TEX_FILTER_LINEAR: *native = 1; return true;
   default: return false;
   }
}

static bool
ps5_texture_descriptor_mip_filter(unsigned filter, uint32_t *native)
{
   switch (filter) {
   case PIPE_TEX_MIPFILTER_NONE: *native = 0; return true;
   case PIPE_TEX_MIPFILTER_NEAREST: *native = 1; return true;
   case PIPE_TEX_MIPFILTER_LINEAR: *native = 2; return true;
   default: return false;
   }
}

static uint32_t
ps5_texture_descriptor_unsigned_lod(float value)
{
   if (value <= 0.0f)
      return 0;
   if (value >= 15.0f)
      return 15u << 8;
   return (uint32_t)(value * 256.0f);
}

static uint32_t
ps5_texture_descriptor_lod_bias(float value)
{
   int32_t fixed;

   if (value <= -32.0f)
      fixed = -32 * 256;
   else if (value >= 31.0f)
      fixed = 31 * 256;
   else
      fixed = (int32_t)(value * 256.0f);
   return (uint32_t)fixed & UINT32_C(0x3fff);
}

static bool
ps5_float_is_finite(float value)
{
   return (ps5_float_bits(value) & UINT32_C(0x7f800000)) !=
          UINT32_C(0x7f800000);
}

static uint32_t
ps5_pack_float_12p4(float value)
{
   if (value <= 0.0f)
      return 0;
   if (value >= 4096.0f)
      return UINT32_C(0xffff);
   return (uint32_t)(value * 16.0f);
}

static bool
ps5_dual_source_blend_factor(unsigned factor)
{
   return factor == PIPE_BLENDFACTOR_SRC1_COLOR ||
          factor == PIPE_BLENDFACTOR_INV_SRC1_COLOR ||
          factor == PIPE_BLENDFACTOR_SRC1_ALPHA ||
          factor == PIPE_BLENDFACTOR_INV_SRC1_ALPHA;
}

static bool
ps5_translate_blend_factor(unsigned factor, uint32_t *native)
{
   if (!native)
      return false;

   switch (factor) {
   case PIPE_BLENDFACTOR_ZERO: *native = 0; return true;
   case PIPE_BLENDFACTOR_ONE: *native = 1; return true;
   case PIPE_BLENDFACTOR_SRC_COLOR: *native = 2; return true;
   case PIPE_BLENDFACTOR_INV_SRC_COLOR: *native = 3; return true;
   case PIPE_BLENDFACTOR_SRC_ALPHA: *native = 4; return true;
   case PIPE_BLENDFACTOR_INV_SRC_ALPHA: *native = 5; return true;
   case PIPE_BLENDFACTOR_DST_ALPHA: *native = 6; return true;
   case PIPE_BLENDFACTOR_INV_DST_ALPHA: *native = 7; return true;
   case PIPE_BLENDFACTOR_DST_COLOR: *native = 8; return true;
   case PIPE_BLENDFACTOR_INV_DST_COLOR: *native = 9; return true;
   case PIPE_BLENDFACTOR_SRC_ALPHA_SATURATE: *native = 10; return true;
   case PIPE_BLENDFACTOR_CONST_COLOR: *native = 13; return true;
   case PIPE_BLENDFACTOR_INV_CONST_COLOR: *native = 14; return true;
   case PIPE_BLENDFACTOR_SRC1_COLOR:
      if (!PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE)
         return false;
      *native = 15;
      return true;
   case PIPE_BLENDFACTOR_INV_SRC1_COLOR:
      if (!PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE)
         return false;
      *native = 16;
      return true;
   case PIPE_BLENDFACTOR_SRC1_ALPHA:
      if (!PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE)
         return false;
      *native = 17;
      return true;
   case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:
      if (!PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE)
         return false;
      *native = 18;
      return true;
   case PIPE_BLENDFACTOR_CONST_ALPHA: *native = 19; return true;
   case PIPE_BLENDFACTOR_INV_CONST_ALPHA: *native = 20; return true;
   default: return false;
   }
}

static bool
ps5_translate_blend_function(unsigned function, uint32_t *native)
{
   static const uint8_t functions[] = {
      0, /* ADD */
      1, /* SUBTRACT */
      4, /* REVERSE_SUBTRACT */
      2, /* MIN */
      3, /* MAX */
   };

   if (!native || function > PIPE_BLEND_MAX)
      return false;
   *native = functions[function];
   return true;
}

static bool
ps5_encode_rt_blend(const struct pipe_rt_blend_state *rt, uint32_t *control)
{
   uint32_t rgb_source;
   uint32_t rgb_destination;
   uint32_t rgb_function;
   uint32_t alpha_source;
   uint32_t alpha_destination;
   uint32_t alpha_function;

   if (!rt || !control)
      return false;
   *control = 0;
   if (!rt->blend_enable || !rt->colormask)
      return true;
   if (!ps5_translate_blend_factor(rt->rgb_src_factor, &rgb_source) ||
       !ps5_translate_blend_factor(rt->rgb_dst_factor, &rgb_destination) ||
       !ps5_translate_blend_function(rt->rgb_func, &rgb_function) ||
       !ps5_translate_blend_factor(rt->alpha_src_factor, &alpha_source) ||
       !ps5_translate_blend_factor(rt->alpha_dst_factor,
                                   &alpha_destination) ||
       !ps5_translate_blend_function(rt->alpha_func, &alpha_function))
      return false;

   *control = UINT32_C(1) << 30;
   *control |= rgb_source | (rgb_function << 5) | (rgb_destination << 8);
   if (alpha_source != rgb_source || alpha_destination != rgb_destination ||
       alpha_function != rgb_function) {
      *control |= UINT32_C(1) << 29;
      *control |= (alpha_source << 16) | (alpha_function << 21) |
                  (alpha_destination << 24);
   }
   return true;
}

static bool
ps5_encode_blend_state(const struct pipe_blend_state *state,
                       unsigned target_count,
                       uint32_t control[PS5_MAX_RENDER_TARGETS],
                       uint32_t *target_mask, uint32_t *color_control,
                       uint32_t *dual_source_blend)
{
   if (target_count > PS5_MAX_RENDER_TARGETS || !control ||
       !target_mask || !color_control || !dual_source_blend)
      return false;
   memset(control, 0, PS5_MAX_RENDER_TARGETS * sizeof(control[0]));
   *target_mask = 0;
   *dual_source_blend = 0;
   if (!target_count) {
      *color_control = 0;
      return true;
   }
   if (!state) {
      for (unsigned i = 0; i < target_count; ++i)
         *target_mask |= UINT32_C(0xf) << (4u * i);
      *color_control = UINT32_C(0x00cc0011);
      return true;
   }
   if (state->advanced_blend_func || state->max_rt >= target_count)
      return false;

   for (unsigned i = 0; i < target_count; ++i) {
      const struct pipe_rt_blend_state *rt =
         &state->rt[state->independent_blend_enable ? i : 0];
      const bool dual_source = rt->blend_enable && rt->colormask &&
         (ps5_dual_source_blend_factor(rt->rgb_src_factor) ||
          ps5_dual_source_blend_factor(rt->rgb_dst_factor) ||
          ps5_dual_source_blend_factor(rt->alpha_src_factor) ||
          ps5_dual_source_blend_factor(rt->alpha_dst_factor));

      if (dual_source &&
          (!PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE || i ||
           target_count != 1 || rt->rgb_func == PIPE_BLEND_MIN ||
           rt->rgb_func == PIPE_BLEND_MAX ||
           rt->alpha_func == PIPE_BLEND_MIN ||
           rt->alpha_func == PIPE_BLEND_MAX))
         return false;
      *dual_source_blend |= dual_source;

      *target_mask |= (uint32_t)rt->colormask << (4u * i);
      if (!ps5_encode_rt_blend(rt, &control[i]))
         return false;
   }
   *color_control = (*target_mask ? UINT32_C(0x10) : 0) |
      (((state->logicop_enable &&
         state->logicop_func != PIPE_LOGICOP_COPY) ?
           (state->logicop_func | (state->logicop_func << 4)) :
           UINT32_C(0xcc)) << 16);
   /* ponytail: keep RB+ off until matching SX conversion state is emitted.
    * This also preserves the required disable for ROP3 and dual-source blend. */
   *color_control |= 1;
   return true;
}

static bool
ps5_polygon_offset_for_fill(const struct pipe_rasterizer_state *state,
                            unsigned fill)
{
   switch (fill) {
   case PIPE_POLYGON_MODE_FILL:
      return state->offset_tri;
   case PIPE_POLYGON_MODE_LINE:
      return state->offset_line;
   case PIPE_POLYGON_MODE_POINT:
      return state->offset_point;
   default:
      return false;
   }
}

static bool
ps5_encode_rasterizer_state(const struct pipe_rasterizer_state *state,
                            uint32_t *control, uint32_t *valid)
{
   static const uint8_t fill[] = {2, 1, 0};
   bool polygon_mode;

   if (!control || !valid)
      return false;
   *control = 0;
   *valid = 0;
   if (!state)
      return true;
   if (state->cull_face > PIPE_FACE_FRONT_AND_BACK ||
       state->fill_front > PIPE_POLYGON_MODE_POINT ||
       state->fill_back > PIPE_POLYGON_MODE_POINT ||
       state->offset_units_unscaled ||
       ((state->poly_smooth || state->line_smooth) &&
        !PS5_ENABLE_SMOOTH_RASTER_CANDIDATE) ||
       state->poly_stipple_enable ||
       state->point_smooth ||
       (state->multisample && !PS5_ENABLE_MSAA4_CANDIDATE) ||
       state->line_stipple_enable || state->conservative_raster_mode ||
       state->clip_halfz)
      return false;
   if ((state->fill_front == PIPE_POLYGON_MODE_LINE ||
        state->fill_back == PIPE_POLYGON_MODE_LINE) &&
       !PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE && state->line_width != 1.0f)
      return false;
   if ((state->fill_front == PIPE_POLYGON_MODE_POINT ||
        state->fill_back == PIPE_POLYGON_MODE_POINT) &&
       !PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE &&
       (state->point_size != 1.0f || state->point_size_per_vertex))
      return false;

   polygon_mode =
      (state->fill_front != PIPE_POLYGON_MODE_FILL &&
       !(state->cull_face & PIPE_FACE_FRONT)) ||
      (state->fill_back != PIPE_POLYGON_MODE_FILL &&
       !(state->cull_face & PIPE_FACE_BACK));
   *control = (state->cull_face & PIPE_FACE_FRONT ? 1u : 0u) |
              (state->cull_face & PIPE_FACE_BACK ? 2u : 0u) |
              (!state->front_ccw ? 4u : 0u) |
              (polygon_mode ? 1u << 3 : 0u) |
              ((uint32_t)fill[state->fill_front] << 5) |
              ((uint32_t)fill[state->fill_back] << 8) |
              (ps5_polygon_offset_for_fill(state, state->fill_front) ?
                  1u << 11 : 0u) |
              (ps5_polygon_offset_for_fill(state, state->fill_back) ?
                  1u << 12 : 0u) |
              (state->offset_point || state->offset_line ?
                  1u << 13 : 0u) |
              (!state->flatshade_first ? 1u << 19 : 0u) |
              (polygon_mode ? 1u << 24 : 0u);
   *valid = 1;
   return true;
}

static bool
ps5_encode_graphics_state(const struct ps5_context *context,
                          struct ps5_native_graphics_state *native)
{
   const struct pipe_viewport_state *viewport;
   const struct pipe_scissor_state *scissor;
   float z_extent;
   float zmin;
   float zmax;
   unsigned i;
   unsigned minx;
   unsigned miny;
   unsigned maxx;
   unsigned maxy;
   float polygon_scale;
   float point_min;
   float point_max;
   uint32_t fixed_size;

   if (!context || !native ||
       !ps5_encode_blend_state(context->blend,
                                context->framebuffer.nr_cbufs,
                                native->blend_control,
                                &native->target_mask,
                                &native->color_control,
                                &native->dual_source_blend) ||
       !ps5_encode_rasterizer_state(context->rasterizer,
                                    &native->rasterizer_control,
                                    &native->rasterizer_valid))
      return false;
   native->clip_control_valid = context->rasterizer != NULL;
   native->clip_control = context->rasterizer
      ? ((!context->rasterizer->depth_clip_near ? 1u << 26 : 0u) |
         (!context->rasterizer->depth_clip_far ? 1u << 27 : 0u) |
         (context->rasterizer->rasterizer_discard ? 1u << 22 : 0u) |
         (1u << 24))
      : 0;
   native->point_line[0] = 0;
   native->point_line[1] = 0;
   native->point_line[2] = 0;
   native->point_line_valid = 0;
   if (PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE && context->rasterizer) {
      /* Mesa's clear/meta triangle rasterizers leave these unused sizes zero.
       * Program safe defaults without rejecting the unrelated triangle draw. */
      const float point_size = context->rasterizer->point_size == 0.0f
                                  ? 1.0f : context->rasterizer->point_size;
      const float line_width = context->rasterizer->line_width == 0.0f
                                  ? 1.0f : context->rasterizer->line_width;
      if (!ps5_float_is_finite(point_size) ||
          !ps5_float_is_finite(line_width) ||
          point_size < PS5_MIN_POINT_LINE_SIZE ||
          point_size > PS5_MAX_POINT_LINE_SIZE ||
          line_width < PS5_MIN_POINT_LINE_SIZE ||
          line_width > PS5_MAX_POINT_LINE_SIZE)
         return false;
      fixed_size = (uint32_t)(point_size * 8.0f);
      native->point_line[0] = fixed_size | (fixed_size << 16);
      if (context->rasterizer->point_size_per_vertex) {
         point_min = PS5_MIN_POINT_LINE_SIZE;
         point_max = PS5_MAX_POINT_LINE_SIZE;
      } else {
         point_min = point_size;
         point_max = point_size;
      }
      native->point_line[1] = ps5_pack_float_12p4(point_min * 0.5f) |
         (ps5_pack_float_12p4(point_max * 0.5f) << 16);
      native->point_line[2] =
         ps5_pack_float_12p4(line_width * 0.5f);
      native->point_line_valid = 1;
   }
   native->interp_control = 0;
   native->interp_control_valid = 0;
   if (PS5_ENABLE_POINT_COORD_CANDIDATE && context->rasterizer) {
      native->interp_control =
         1u | /* FLAT_SHADE_ENA */
         (context->rasterizer->point_quad_rasterization ? 1u << 1 : 0u) |
         (2u << 2) |  /* point X selects S */
         (3u << 5) |  /* point Y selects T */
         (1u << 11) | /* point W selects 1 */
         (context->rasterizer->sprite_coord_mode !=
               PIPE_SPRITE_COORD_UPPER_LEFT ? 1u << 14 : 0u);
      native->interp_control_valid = 1;
   }
   native->color_control_valid = 1;
   native->alpha_to_coverage =
      context->blend && context->blend->alpha_to_coverage;
   native->alpha_to_one = context->blend && context->blend->alpha_to_one;
   for (i = 0; i < context->framebuffer.nr_cbufs; ++i) {
      if (!context->framebuffer.cbufs[i].texture)
         native->target_mask &= ~(UINT32_C(0xf) << (4u * i));
   }
   native->color_control = (native->color_control & ~UINT32_C(0x10)) |
                           (native->target_mask ? UINT32_C(0x10) : 0);
   for (i = 0; i < 4; ++i)
      native->blend_color[i] = ps5_float_bits(context->blend_color.color[i]);
   native->polygon_offset_valid = 0;
   for (i = 0; i < 6; ++i)
      native->polygon_offset[i] = 0;
   if (context->rasterizer &&
       (context->rasterizer->offset_point ||
        context->rasterizer->offset_line ||
        context->rasterizer->offset_tri)) {
      if (!ps5_float_is_finite(context->rasterizer->offset_scale) ||
          !ps5_float_is_finite(context->rasterizer->offset_units) ||
          !ps5_float_is_finite(context->rasterizer->offset_clamp))
         return false;
      polygon_scale = context->rasterizer->offset_scale * 16.0f;
      if (!ps5_float_is_finite(polygon_scale))
         return false;

      /* GFX10 D32F: -23 depth bits plus floating-point format. */
      native->polygon_offset[0] = UINT32_C(0x000001e9);
      native->polygon_offset[1] =
         ps5_float_bits(context->rasterizer->offset_clamp);
      native->polygon_offset[2] = ps5_float_bits(polygon_scale);
      native->polygon_offset[3] =
         ps5_float_bits(context->rasterizer->offset_units);
      native->polygon_offset[4] = native->polygon_offset[2];
      native->polygon_offset[5] = native->polygon_offset[3];
      native->polygon_offset_valid =
         context->framebuffer.zsbuf.texture != NULL;
   }

   if (context->viewport_valid) {
      viewport = &context->viewport;
      for (i = 0; i < 3; ++i) {
         if (!ps5_float_is_finite(viewport->scale[i]) ||
             !ps5_float_is_finite(viewport->translate[i]))
            return false;
         native->viewport[2 * i] = ps5_float_bits(viewport->scale[i]);
         native->viewport[2 * i + 1] =
            ps5_float_bits(viewport->translate[i]);
      }
      z_extent = viewport->scale[2] < 0.0f ? -viewport->scale[2] :
                                                   viewport->scale[2];
      zmin = viewport->translate[2] - z_extent;
      zmax = viewport->translate[2] + z_extent;
      native->viewport[6] = ps5_float_bits(zmin);
      native->viewport[7] = ps5_float_bits(zmax);
   } else {
      native->viewport[0] = UINT32_C(0x44700000); /* 960.0 */
      native->viewport[1] = UINT32_C(0x44700000);
      native->viewport[2] = UINT32_C(0xc4070000); /* -540.0 */
      native->viewport[3] = UINT32_C(0x44070000); /* 540.0 */
      native->viewport[4] = UINT32_C(0x3f800000);
      native->viewport[5] = 0;
      native->viewport[6] = 0;
      native->viewport[7] = UINT32_C(0x3f800000);
   }

   scissor = context->rasterizer && context->rasterizer->scissor &&
             context->scissor_valid ? &context->scissor : NULL;
   minx = scissor ? MIN2(scissor->minx, context->framebuffer.width) : 0;
   miny = scissor ? MIN2(scissor->miny, context->framebuffer.height) : 0;
   maxx = scissor ? MIN2(scissor->maxx, context->framebuffer.width)
                 : context->framebuffer.width;
   maxy = scissor ? MIN2(scissor->maxy, context->framebuffer.height)
                 : context->framebuffer.height;
   if (maxx < minx)
      maxx = minx;
   if (maxy < miny)
      maxy = miny;
   native->scissor[0] = UINT32_C(0x80000000) | minx | (miny << 16);
   native->scissor[1] = maxx | (maxy << 16);
   return true;
}

static bool
ps5_stencil_uses_unit_op_value(const struct pipe_stencil_state *state)
{
   /* GFX10 ADD/SUB stencil operations consume STENCILOPVAL. */
   const unsigned add_sub_ops =
      BITFIELD_BIT(PIPE_STENCIL_OP_INCR) |
      BITFIELD_BIT(PIPE_STENCIL_OP_DECR) |
      BITFIELD_BIT(PIPE_STENCIL_OP_INCR_WRAP) |
      BITFIELD_BIT(PIPE_STENCIL_OP_DECR_WRAP);

   return (add_sub_ops & BITFIELD_BIT(state->fail_op)) ||
          (add_sub_ops & BITFIELD_BIT(state->zpass_op)) ||
          (add_sub_ops & BITFIELD_BIT(state->zfail_op));
}

static bool
ps5_encode_depth_stencil_state(
   const struct pipe_depth_stencil_alpha_state *dsa,
   const struct pipe_stencil_ref *ref,
   struct ps5_native_depth_stencil_state *native)
{
   static const uint8_t stencil_op[] = {
      0, /* KEEP */
      1, /* ZERO */
      3, /* REPLACE_TEST */
      5, /* ADD_CLAMP */
      6, /* SUB_CLAMP */
      8, /* ADD_WRAP */
      9, /* SUB_WRAP */
      7, /* INVERT */
   };
   const struct pipe_stencil_state *front;
   const struct pipe_stencil_state *back;

   if (!dsa || !ref || !native || dsa->alpha_enabled ||
       dsa->depth_bounds_test || dsa->depth_func > PIPE_FUNC_ALWAYS)
      return false;
   front = &dsa->stencil[0];
   back = dsa->stencil[1].enabled ? &dsa->stencil[1] : front;
   if ((!front->enabled && dsa->stencil[1].enabled) ||
       front->func > PIPE_FUNC_ALWAYS || back->func > PIPE_FUNC_ALWAYS ||
       front->fail_op > PIPE_STENCIL_OP_INVERT ||
       front->zpass_op > PIPE_STENCIL_OP_INVERT ||
       front->zfail_op > PIPE_STENCIL_OP_INVERT ||
       back->fail_op > PIPE_STENCIL_OP_INVERT ||
       back->zpass_op > PIPE_STENCIL_OP_INVERT ||
       back->zfail_op > PIPE_STENCIL_OP_INVERT)
      return false;

   memset(native, 0, sizeof(*native));
   if (dsa->depth_enabled)
      native->depth_control = (dsa->depth_func << 4) | 2u |
                              (dsa->depth_writemask ? 4u : 0u);
   if (!front->enabled)
      return true;

   native->depth_control |= 1u | (front->func << 8);
   native->stencil_control =
      stencil_op[front->fail_op] |
      (stencil_op[front->zpass_op] << 4) |
      (stencil_op[front->zfail_op] << 8);
   native->stencil_refmask = ref->ref_value[0] |
      (front->valuemask << 8) | (front->writemask << 16) |
      (ps5_stencil_uses_unit_op_value(front) ? UINT32_C(1) << 24 : 0);
   if (dsa->stencil[1].enabled) {
      native->depth_control |= 1u << 7;
      native->depth_control |= back->func << 20;
      native->stencil_control |=
         (stencil_op[back->fail_op] << 12) |
         (stencil_op[back->zpass_op] << 16) |
         (stencil_op[back->zfail_op] << 20);
      native->stencil_refmask_bf = ref->ref_value[1] |
         (back->valuemask << 8) | (back->writemask << 16) |
         (ps5_stencil_uses_unit_op_value(back) ? UINT32_C(1) << 24 : 0);
   } else {
      native->stencil_refmask_bf = native->stencil_refmask;
   }
   return true;
}

static uint32_t
ps5_hash32(const void *data, size_t size)
{
   const uint8_t *bytes = data;
   uint32_t hash = UINT32_C(2166136261);
   size_t i;

   for (i = 0; i < size; ++i)
      hash = (hash ^ bytes[i]) * UINT32_C(16777619);
   return hash;
}

static void
ps5_flush_gpu_data(const void *address, size_t bytes)
{
   const uint8_t *at = address;
   const uint8_t *end = at + bytes;

   for (; at < end; at += 64)
      __asm__ volatile("clflush (%0)" : : "r"(at) : "memory");
   __asm__ volatile("mfence" ::: "memory");
}

static bool
ps5_stage_packed_depth_samples(struct ps5_resource *resource,
                               unsigned *base_stride)
{
   const unsigned layers = ps5_texture_level_layers(&resource->base, 0);
   uint8_t *staging = resource->data + resource->depth_staging_offset;
   size_t sample_layer_size;
   size_t sample_size;
   size_t sample_level_offset = 0;

   if (!ps5_packed_depth_sample_layout(&resource->base,
                                       &sample_layer_size, base_stride) ||
       sample_layer_size > SIZE_MAX / layers)
      return false;
   sample_size = sample_layer_size * layers;
   if (!resource->depth_staging_size ||
       sample_size > resource->depth_staging_size ||
       resource->depth_staging_offset >= resource->allocation_size ||
       resource->depth_staging_size >
          resource->allocation_size - resource->depth_staging_offset)
      return false;

   for (unsigned level = resource->base.last_level + 1; level-- > 0;) {
      const unsigned width = MAX2(resource->base.width0 >> level, 1u);
      const unsigned height = MAX2(resource->base.height0 >> level, 1u);
      const unsigned storage_width =
         ps5_linear_mip_storage_extent(resource->base.width0, level);
      const unsigned storage_height =
         ps5_linear_mip_storage_extent(resource->base.height0, level);
      const size_t sample_stride =
         ((size_t)storage_width * sizeof(float) + 255u) & ~(size_t)255u;

      for (unsigned layer = 0; layer < layers; ++layer) {
         const size_t source_base = (size_t)layer * resource->layer_stride +
                                    resource->level_offset[level];
         const size_t sample_base = (size_t)layer * sample_layer_size +
                                    sample_level_offset;

         for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
               const size_t source = source_base +
                  (size_t)y * resource->level_stride[level] +
                  (size_t)x * 8u;
               const size_t destination = sample_base +
                  (size_t)y * sample_stride + (size_t)x * sizeof(float);

               if (source > resource->size ||
                   resource->size - source < sizeof(float) ||
                   destination > sample_size ||
                   sample_size - destination < sizeof(float))
                  return false;
               memcpy(staging + destination, resource->data + source,
                      sizeof(float));
            }
         }
      }
      sample_level_offset += sample_stride * storage_height;
   }
   ps5_flush_gpu_data(staging, sample_size);
   return true;
}

#if PS5_PUBLIC_TEXTURE_RG_TILE_TEST
static bool
ps5_record_public_rg_tile(struct ps5_context *context)
{
   static const struct {
      enum pipe_format format;
      unsigned width;
      unsigned height;
      const char *name;
      const char *path;
   } cases[] = {
      { PIPE_FORMAT_R8_UNORM, 256, 256, "r8-x",
        "/data/VdecHello/opengl33-public-r8-x.raw" },
      { PIPE_FORMAT_R8_UNORM, 256, 256, "r8-y",
        "/data/VdecHello/opengl33-public-r8-y.raw" },
      { PIPE_FORMAT_R8G8_UNORM, 256, 128, "rg8-xy",
        "/data/VdecHello/opengl33-public-rg8-xy.raw" },
   };
   const unsigned index = context->draw_calls - 1;
   struct ps5_resource *target;
   FILE *file;
   size_t written = 0;
   size_t nonzero = 0;
   int close_status = -1;

   if (index >= ARRAY_SIZE(cases) ||
       !context->framebuffer.cbufs[0].texture)
      return false;
   target = (struct ps5_resource *)context->framebuffer.cbufs[0].texture;
   if (target->base.format != cases[index].format ||
       target->base.width0 != cases[index].width ||
       target->base.height0 != cases[index].height ||
       target->allocation_size < UINT32_C(0x10000))
      return false;

   ps5_flush_gpu_data(target->data, UINT32_C(0x10000));
   for (size_t i = 0; i < UINT32_C(0x10000); ++i)
      nonzero += target->data[i] != 0;
   file = fopen(cases[index].path, "wb");
   if (file) {
      written = fwrite(target->data, 1, UINT32_C(0x10000), file);
      close_status = fclose(file);
   }
   printf("[ps5-gallium] rg-tile-evidence case=%s format=%u size=%ux%u "
          "bytes=%zu hash=%08x nonzero=%zu dump=%d\n",
          cases[index].name, target->base.format, target->base.width0,
          target->base.height0, written,
          ps5_hash32(target->data, UINT32_C(0x10000)), nonzero,
          written == UINT32_C(0x10000) && close_status == 0 ? 0 : -1);
   return written == UINT32_C(0x10000) && close_status == 0;
}
#endif

#ifdef PS5_PUBLIC_STENCIL_TEST
static bool
ps5_record_public_stencil(struct ps5_resource *depth)
{
   size_t depth_nonzero = 0;
   size_t stencil_matches = 0;
   size_t stencil_unexpected = 0;
   int depth_dump_status = -1;
   int stencil_dump_status = -1;
   FILE *file;

   if (!depth ||
       depth->base.format != PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
       !depth->data || !depth->stencil_data) {
      printf("[ps5-gallium] public-stencil-evidence invalid-resource\n");
      return false;
   }

   for (size_t i = 0;
        i < depth->allocation_size / sizeof(uint32_t); ++i)
      depth_nonzero += ((const uint32_t *)depth->data)[i] != 0;
   for (size_t i = 0; i < depth->stencil_allocation_size; ++i) {
      if (depth->stencil_data[i] == UINT8_C(0x5a))
         stencil_matches++;
      else if (depth->stencil_data[i])
         stencil_unexpected++;
   }

   file = fopen("/data/VdecHello/opengl33-public-stencil-depth.raw", "wb");
   if (file) {
      const size_t written = fwrite(
         depth->data, 1, depth->allocation_size, file);
      const int close_status = fclose(file);
      depth_dump_status =
         written == depth->allocation_size && close_status == 0 ? 0 : -1;
   }
   file = fopen("/data/VdecHello/opengl33-public-stencil.raw", "wb");
   if (file) {
      const size_t written = fwrite(
         depth->stencil_data, 1, depth->stencil_allocation_size, file);
      const int close_status = fclose(file);
      stencil_dump_status = written == depth->stencil_allocation_size &&
                            close_status == 0 ? 0 : -1;
   }

   printf("[ps5-gallium] public-stencil-evidence depth=%p/%zu nonzero=%zu dump=%d stencil=%p/%zu matches=%zu unexpected=%zu ref=5a dump=%d\n",
          depth->data, depth->allocation_size, depth_nonzero,
          depth_dump_status, depth->stencil_data,
          depth->stencil_allocation_size, stencil_matches,
          stencil_unexpected, stencil_dump_status);
   return depth_nonzero == 0 && stencil_matches == 16320 &&
          stencil_unexpected == 0 && depth_dump_status == 0 &&
          stencil_dump_status == 0;
}
#endif

static bool
ps5_uses_merged_geometry_metadata(const struct ps5_context *context,
                                  const struct ps5_shader *shader,
                                  const PsbcShaderMetadata *metadata)
{
   return context->gs && shader == context->vs &&
          metadata == &context->geometry_output.metadata;
}

static bool
ps5_texture_used(const struct ps5_context *context,
                 const struct ps5_shader *shader,
                 const PsbcShaderMetadata *metadata, unsigned unit)
{
   if (unit < PS5_MAX_TEXTURE_UNITS)
      return BITSET_TEST(shader->nir->info.textures_used, unit);
   return unit < PS5_MERGED_TEXTURE_UNITS &&
      ps5_uses_merged_geometry_metadata(context, shader, metadata) &&
      BITSET_TEST(context->gs->nir->info.textures_used,
                  unit - PS5_MAX_TEXTURE_UNITS);
}

static unsigned
ps5_shader_texture_count(const struct ps5_shader *shader)
{
   unsigned count = 0;

   for (unsigned unit = 0; unit < PS5_MAX_TEXTURE_UNITS; ++unit)
      count += BITSET_TEST(shader->nir->info.textures_used, unit);
   return count;
}

static unsigned
ps5_texture_count(const struct ps5_context *context,
                  const struct ps5_shader *shader,
                  const PsbcShaderMetadata *metadata)
{
   unsigned count = 0;

   for (unsigned unit = 0; unit < PS5_MERGED_TEXTURE_UNITS; ++unit)
      count += ps5_texture_used(context, shader, metadata, unit);
   return count;
}

static unsigned
ps5_constant_state_binding(const struct ps5_shader *shader,
                           unsigned ubo_index)
{
   return ubo_index + !shader->nir->info.first_ubo_is_default_ubo;
}

static size_t
ps5_copied_constant_offset(unsigned state_slot)
{
   return PS5_CONSTANT_DATA_OFFSET +
          (state_slot == PS5_GEOMETRY_CONSTANT_SLOT
              ? PS5_MAX_CONSTANT_BUFFER_SIZE : 0u);
}

static bool
ps5_prepare_constant(struct ps5_context *context,
                     const struct ps5_shader *shader, unsigned slot,
                     uint32_t *user_data, unsigned user_data_count,
                     const PsbcShaderMetadata *metadata_override)
{
   const PsbcShaderMetadata *metadata;
   struct ps5_resource *storage;
   uintptr_t descriptor_address;
   unsigned index;
   unsigned ubo_count = 0;
   unsigned expected_ubo_count;
   unsigned expected_texture_count;
   bool merged_geometry;

   if (!shader || !shader->active || slot >= 2)
      return false;
   metadata = metadata_override ? metadata_override :
                                   &shader->active->output.metadata;
   merged_geometry = ps5_uses_merged_geometry_metadata(context, shader,
                                                       metadata);
   expected_ubo_count = shader->nir->info.num_ubos +
      (merged_geometry ? context->gs->nir->info.num_ubos : 0u);
   expected_texture_count = ps5_texture_count(context, shader, metadata);
   if (!expected_ubo_count)
      return true;

   storage = (struct ps5_resource *)context->descriptor_storage[slot];
   if ((!PS5_ENABLE_UBO_CANDIDATE &&
        (shader->nir->info.num_ubos != 1 ||
         !shader->nir->info.first_ubo_is_default_ubo)) ||
       !storage ||
       storage->base.target != PIPE_BUFFER ||
       storage->size < (PS5_ENABLE_UBO_CANDIDATE
                           ? PS5_DESCRIPTOR_STORAGE_BYTES
                           : PS5_DIRECT_ALIGNMENT) ||
       metadata->descriptor_binding_count !=
          expected_ubo_count + expected_texture_count ||
       !metadata->descriptor_set0_valid ||
       metadata->descriptor_set0_user_data_dword >= user_data_count)
      return false;

   descriptor_address = (uintptr_t)storage->data;
   if ((uint32_t)(descriptor_address >> 32) != metadata->address32_hi ||
       metadata->descriptor_binding_count > PSBC_MAX_DESCRIPTOR_BINDINGS)
      return false;

   memset(storage->data, 0, PS5_CONSTANT_DATA_OFFSET);
   for (index = 0; index < metadata->descriptor_binding_count; ++index) {
      const PsbcDescriptorBinding *binding =
         &metadata->descriptor_bindings[index];
      const struct ps5_constant_state *state;
      struct ps5_resource *buffer;
      uintptr_t data_address;
      uint32_t *descriptor;
      unsigned state_binding;
      unsigned state_slot = slot;
      unsigned ubo_index;

      if (binding->type != PSBC_DESCRIPTOR_UNIFORM_BUFFER)
         continue;
      if (binding->set ||
          binding->binding < PSBC_GALLIUM_UBO_BINDING_BASE ||
          binding->binding >= PSBC_GALLIUM_UBO_BINDING_BASE +
                                 2u * PS5_MAX_CONSTANT_BUFFERS ||
          binding->array_size != 1 || binding->stride != 16 ||
          binding->offset != PS5_TEXTURE_DESCRIPTOR_BYTES +
                                (binding->binding -
                                 PSBC_GALLIUM_UBO_BINDING_BASE) * 16u)
         return false;
      state_binding = binding->binding - PSBC_GALLIUM_UBO_BINDING_BASE;
      if (state_binding >= expected_ubo_count)
         return false;
      ubo_index = state_binding;
      if (merged_geometry && state_binding >= shader->nir->info.num_ubos) {
         state_slot = PS5_GEOMETRY_CONSTANT_SLOT;
         ubo_index -= shader->nir->info.num_ubos;
         if (ubo_index >= context->gs->nir->info.num_ubos)
            return false;
         state_binding = ps5_constant_state_binding(context->gs, ubo_index);
      } else {
         if (ubo_index >= shader->nir->info.num_ubos)
            return false;
         state_binding = ps5_constant_state_binding(shader, ubo_index);
      }
      if (state_binding >= PS5_MAX_CONSTANT_BUFFERS)
         return false;
      state = &context->constants[state_slot][state_binding];
      descriptor = (uint32_t *)(storage->data + binding->offset);
      ubo_count++;
      if (!state->valid)
         continue;
      if (!state->size || state->size > PS5_MAX_CONSTANT_BUFFER_SIZE)
         return false;
      if (state->copied) {
         const size_t copied_offset =
            ps5_copied_constant_offset(state_slot);

         if (copied_offset > storage->size ||
             state->size > storage->size - copied_offset)
            return false;
         data_address = (uintptr_t)storage->data + copied_offset;
      } else {
         buffer = (struct ps5_resource *)state->buffer;
         if (!buffer || buffer->base.target != PIPE_BUFFER ||
             state->offset > buffer->size ||
             state->size > buffer->size - state->offset)
            return false;
         data_address = (uintptr_t)buffer->data + state->offset;
         ps5_flush_gpu_data((void *)data_address, state->size);
      }
      descriptor[0] = (uint32_t)data_address;
      descriptor[1] = (uint32_t)(data_address >> 32);
      descriptor[2] = state->size;
      descriptor[3] = UINT32_C(0x0004dfac) |
         S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW);
   }
   if (ubo_count != expected_ubo_count)
      return false;
   user_data[metadata->descriptor_set0_user_data_dword] =
      (uint32_t)descriptor_address;
   ps5_flush_gpu_data(storage->data, storage->size);
   return true;
}

static bool
ps5_prepare_texture(struct ps5_context *context,
                    const struct ps5_shader *shader, unsigned slot,
                    uint32_t *user_data, unsigned user_data_count,
                    const PsbcShaderMetadata *metadata_override)
{
   const PsbcShaderMetadata *metadata;
   struct ps5_resource *table;
   uintptr_t table_address;
   size_t flush_size = 0;
   unsigned texture_count = 0;
   unsigned expected_texture_count;
   unsigned expected_ubo_count;
   bool merged_geometry;

   if (!shader || !shader->active || slot >= PS5_DESCRIPTOR_STAGE_COUNT)
      return false;
   metadata = metadata_override ? metadata_override :
                                   &shader->active->output.metadata;
   merged_geometry = ps5_uses_merged_geometry_metadata(context, shader,
                                                       metadata);
   expected_texture_count = ps5_texture_count(context, shader, metadata);
   if (!expected_texture_count)
      return true;
   expected_ubo_count = shader->nir->info.num_ubos +
      (merged_geometry ? context->gs->nir->info.num_ubos : 0u);
   table = (struct ps5_resource *)context->descriptor_storage[slot];
   if ((shader->stage == PSBC_STAGE_VERTEX && slot != 0) ||
       (shader->stage == PSBC_STAGE_FRAGMENT && slot != 1) ||
       (shader->stage != PSBC_STAGE_VERTEX &&
        shader->stage != PSBC_STAGE_FRAGMENT) ||
       expected_texture_count > (merged_geometry ? PS5_MERGED_TEXTURE_UNITS
                                                 : PS5_MAX_TEXTURE_UNITS) ||
       !metadata->descriptor_set0_valid ||
       metadata->descriptor_set0_user_data_dword >= user_data_count ||
       metadata->descriptor_binding_count !=
          expected_ubo_count + expected_texture_count ||
       !table)
      return false;

   table_address = (uintptr_t)table->data;
   if ((uint32_t)(table_address >> 32) != metadata->address32_hi)
      return false;

   for (unsigned index = 0; index < metadata->descriptor_binding_count;
        ++index) {
      const PsbcDescriptorBinding *binding =
         &metadata->descriptor_bindings[index];
      const unsigned unit = binding->binding % PS5_MAX_TEXTURE_UNITS;
      unsigned state_slot = slot;
      const struct ps5_sampler_state *sampler_state;
      const struct pipe_sampler_state *sampler;
      const struct pipe_sampler_view *view;
      struct ps5_resource *texture;
      uintptr_t texture_address;
      uint32_t *descriptor;
      uint32_t format_word;
      uint32_t swizzle[4];
      uint32_t wrap[3];
      uint32_t filter[2];
      uint32_t mip_filter;
      uint32_t min_lod;
      uint32_t max_lod;
      uint32_t lod_bias;
      unsigned format_size;
      unsigned descriptor_format_size;
      unsigned descriptor_stride;
      bool tiled_render_target;
      bool tiled_depth_target;
      bool depth_texture;
      bool staged_packed_depth;
      bool multisampled;

      if (binding->type != PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER)
         continue;
      if (binding->set ||
          binding->binding >= (merged_geometry ? PS5_MERGED_TEXTURE_UNITS
                                               : PS5_MAX_TEXTURE_UNITS) ||
          binding->array_size != 1 || binding->stride != 48 ||
          binding->offset != binding->binding * PS5_TEXTURE_DESCRIPTOR_STRIDE ||
          binding->offset + binding->stride > table->size ||
          !ps5_texture_used(context, shader, metadata, binding->binding))
         return false;
      texture_count++;
      if (merged_geometry && binding->binding >= PS5_MAX_TEXTURE_UNITS)
         state_slot = PS5_GEOMETRY_TEXTURE_SLOT;
      view = context->sampler_views[state_slot][unit];
      if (!view || !view->texture)
         return false;
      texture = (struct ps5_resource *)view->texture;
      descriptor = (uint32_t *)(table->data + binding->offset);
      if (texture->base.target == PIPE_BUFFER) {
         const unsigned texel_size = util_format_get_blocksize(view->format);
         const size_t offset = view->u.buf.offset;
         const size_t size = view->u.buf.size;
         struct ac_buffer_state state;

         if (!PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE ||
             view->target != PIPE_BUFFER ||
             !ps5_texel_buffer_format(view->format) || !texel_size ||
             offset > texture->size || size > texture->size - offset ||
             (offset % texel_size) || (size % texel_size) ||
             size / texel_size > PS5_MAX_TEXEL_BUFFER_ELEMENTS)
            return false;
         texture_address = (uintptr_t)texture->data + offset;
         if ((uint32_t)(texture_address >> 32) != metadata->address32_hi)
            return false;
         memset(&state, 0, sizeof(state));
         state.va = texture_address;
         state.size = size / texel_size;
         state.format = view->format;
         state.swizzle[0] = view->swizzle_r;
         state.swizzle[1] = view->swizzle_g;
         state.swizzle[2] = view->swizzle_b;
         state.swizzle[3] = view->swizzle_a;
         state.stride = texel_size;
         memset(descriptor, 0, binding->stride);
         ac_build_buffer_descriptor(GFX10_3, &state, descriptor);
         ps5_flush_gpu_data((uint8_t *)texture->data + offset, size);
         flush_size = MAX2(flush_size, binding->offset + binding->stride);
         continue;
      }
      sampler_state = context->samplers[state_slot][unit];
      sampler = sampler_state ? &sampler_state->base : NULL;
      if (!sampler)
         return false;
      format_size = ps5_texture_format_size(texture->base.format);
      depth_texture = texture->base.format == PIPE_FORMAT_Z32_FLOAT ||
                      texture->base.format ==
                         PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
      multisampled = PS5_ENABLE_MSAA4_CANDIDATE &&
                     texture->base.nr_samples == 4 &&
                     texture->base.nr_storage_samples == 4;
      tiled_render_target = (multisampled && !depth_texture) ||
                            (PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE &&
                             (texture->base.target == PIPE_TEXTURE_2D ||
                              (PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE &&
                               texture->base.target ==
                                  PIPE_TEXTURE_2D_ARRAY)) &&
                             (texture->base.bind & PIPE_BIND_RENDER_TARGET) &&
                             !ps5_linear_sampled_layout(&texture->base));
      tiled_depth_target = PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE &&
                           depth_texture &&
                           ps5_depth_render_target(texture->base.target) &&
                           ((texture->base.bind & PIPE_BIND_DEPTH_STENCIL) ||
                            multisampled) &&
                           !texture->depth_staging_size;
      descriptor_format_size = format_size;
      descriptor_stride = texture->level_stride[0];
      staged_packed_depth =
         texture->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT &&
         !tiled_depth_target && texture->depth_staging_size;
      if (!ps5_sampled_texture_target(texture->base.target) ||
          !ps5_sampled_texture_format(texture->base.format) ||
          !texture->base.width0 || !texture->base.height0 ||
          texture->base.width0 > PS5_MAX_TEXTURE_2D_SIZE ||
          texture->base.height0 > PS5_MAX_TEXTURE_2D_SIZE ||
          (texture->base.target == PIPE_TEXTURE_1D &&
           (texture->base.height0 != 1 || texture->base.array_size != 1)) ||
          (texture->base.target == PIPE_TEXTURE_1D_ARRAY &&
           (texture->base.height0 != 1 || !texture->base.array_size ||
            texture->base.array_size > PS5_MAX_TEXTURE_ARRAY_LAYERS)) ||
          (texture->base.target == PIPE_TEXTURE_CUBE &&
           (texture->base.array_size != 6 ||
            texture->base.width0 > PS5_MAX_TEXTURE_CUBE_SIZE ||
            texture->base.width0 != texture->base.height0)) ||
          (texture->base.target == PIPE_TEXTURE_2D_ARRAY &&
           (!texture->base.array_size ||
            texture->base.array_size > PS5_MAX_TEXTURE_ARRAY_LAYERS)) ||
          (texture->base.target == PIPE_TEXTURE_3D &&
           (texture->base.array_size != 1 || !texture->base.depth0 ||
            texture->base.depth0 > PS5_MAX_TEXTURE_3D_SIZE ||
            texture->base.width0 > PS5_MAX_TEXTURE_3D_SIZE ||
            texture->base.height0 > PS5_MAX_TEXTURE_3D_SIZE)) ||
          (texture->base.last_level && !PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE) ||
          (texture->base.target == PIPE_TEXTURE_RECT &&
           texture->base.last_level) ||
          texture->base.last_level > 15 ||
          ((tiled_render_target || tiled_depth_target)
              ? (tiled_render_target
              ? ((multisampled
                    ? !ps5_msaa4_color_format(texture->base.format)
                    : (texture->base.format != PIPE_FORMAT_R8G8B8A8_UNORM &&
                       texture->base.format != PIPE_FORMAT_R8G8B8A8_SRGB &&
                       texture->base.format != PIPE_FORMAT_R8_UNORM &&
                       texture->base.format != PIPE_FORMAT_R8G8_UNORM &&
                       texture->base.format !=
                          PIPE_FORMAT_R11G11B10_FLOAT &&
                       !(PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE &&
                         ps5_integer_texture_format(
                            texture->base.format)) &&
                       texture->base.format !=
                          PIPE_FORMAT_R10G10B10A2_UINT)) ||
                       (PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE
                           ? (texture->base.width0 > PS5_MAX_COLOR_WIDTH ||
                              texture->base.height0 > PS5_MAX_COLOR_HEIGHT)
                           : (texture->base.width0 != PS5_RENDER_WIDTH ||
                              texture->base.height0 != PS5_RENDER_HEIGHT)))
                    : ((texture->base.format != PIPE_FORMAT_Z32_FLOAT &&
                        texture->base.format !=
                           PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) ||
                       (PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE
                           ? (texture->base.width0 > PS5_MAX_DEPTH_WIDTH ||
                              texture->base.height0 > PS5_MAX_DEPTH_HEIGHT)
                           : (texture->base.width0 != PS5_RENDER_WIDTH ||
                              texture->base.height0 != PS5_RENDER_HEIGHT)))) ||
                 texture->base.last_level ||
                 texture->level_stride[0] !=
                    texture->base.width0 * format_size
              : texture->level_stride[0] !=
                   ((format_size * texture->base.width0 + 255u) & ~255u)) ||
          view->target != texture->base.target ||
          view->format != texture->base.format ||
          view->u.tex.first_level > view->u.tex.last_level ||
          view->u.tex.last_level > texture->base.last_level ||
          ((texture->base.target == PIPE_TEXTURE_1D ||
            texture->base.target == PIPE_TEXTURE_2D ||
            texture->base.target == PIPE_TEXTURE_RECT) &&
           (view->u.tex.first_layer || view->u.tex.last_layer)) ||
          (texture->base.target == PIPE_TEXTURE_CUBE &&
           (view->u.tex.first_layer ||
            view->u.tex.last_layer != texture->base.array_size - 1)) ||
          ((texture->base.target == PIPE_TEXTURE_1D_ARRAY ||
            texture->base.target == PIPE_TEXTURE_2D_ARRAY) &&
           (view->u.tex.first_layer > view->u.tex.last_layer ||
            view->u.tex.last_layer >= texture->base.array_size)) ||
          (texture->base.target == PIPE_TEXTURE_3D &&
           (view->u.tex.first_layer || view->u.tex.last_layer)) ||
          (sampler->min_mip_filter != PIPE_TEX_MIPFILTER_NONE &&
           !PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE) ||
          (sampler->compare_mode &&
           (!depth_texture || sampler->compare_func > PIPE_FUNC_ALWAYS)) ||
          sampler->unnormalized_coords ||
          sampler->max_anisotropy > 1 ||
          !ps5_float_is_finite(sampler->min_lod) ||
          !ps5_float_is_finite(sampler->max_lod) ||
          !ps5_float_is_finite(sampler->lod_bias) ||
          !ps5_texture_descriptor_wrap(sampler->wrap_s, &wrap[0]) ||
          !ps5_texture_descriptor_wrap(sampler->wrap_t, &wrap[1]) ||
          !ps5_texture_descriptor_wrap(sampler->wrap_r, &wrap[2]) ||
          !ps5_texture_descriptor_filter(sampler->min_img_filter,
                                         &filter[0]) ||
          !ps5_texture_descriptor_filter(sampler->mag_img_filter,
                                         &filter[1]) ||
          !ps5_texture_descriptor_mip_filter(sampler->min_mip_filter,
                                             &mip_filter) ||
          !ps5_texture_descriptor_format(view->format, &format_word) ||
          !ps5_texture_descriptor_swizzle(view->swizzle_r, &swizzle[0]) ||
          !ps5_texture_descriptor_swizzle(view->swizzle_g, &swizzle[1]) ||
          !ps5_texture_descriptor_swizzle(view->swizzle_b, &swizzle[2]) ||
          !ps5_texture_descriptor_swizzle(view->swizzle_a, &swizzle[3]))
         return false;

      if (staged_packed_depth) {
         descriptor_format_size = sizeof(float);
         if (!ps5_stage_packed_depth_samples(texture, &descriptor_stride))
            return false;
      } else if (texture->base.format ==
                    PIPE_FORMAT_Z32_FLOAT_S8X24_UINT &&
                 !tiled_depth_target) {
         format_word = gfx10_format_table[view->format].img_format << 20;
      }

      min_lod = ps5_texture_descriptor_unsigned_lod(sampler->min_lod);
      max_lod = ps5_texture_descriptor_unsigned_lod(sampler->max_lod);
      lod_bias = ps5_texture_descriptor_lod_bias(sampler->lod_bias);

      texture_address = (uintptr_t)texture->data +
         (staged_packed_depth ? texture->depth_staging_offset : 0);
      if ((uint32_t)(texture_address >> 32) != metadata->address32_hi ||
          (texture_address & 0xffu))
         return false;
      memset(descriptor, 0, binding->stride);
      descriptor[0] = (uint32_t)(texture_address >> 8);
      descriptor[1] = format_word |
                      (((texture->base.width0 - 1u) & 3u) << 30) |
                      (uint32_t)(texture_address >> 40);
      descriptor[2] = ((texture->base.width0 - 1u) >> 2) |
                      ((texture->base.height0 - 1u) << 14) |
                      (UINT32_C(1) << 31); /* GFX10 RESOURCE_LEVEL. */
      descriptor[3] = (tiled_depth_target && multisampled
                           ? UINT32_C(0xe1820000)
                       : tiled_depth_target
                           ? (texture->base.target == PIPE_TEXTURE_1D
                                 ? UINT32_C(0x81800000)
                              : texture->base.target ==
                                   PIPE_TEXTURE_1D_ARRAY
                                 ? UINT32_C(0xc1800000)
                              : texture->base.target == PIPE_TEXTURE_CUBE
                                 ? UINT32_C(0xb1800000)
                              : texture->base.target ==
                                   PIPE_TEXTURE_2D_ARRAY
                                 ? UINT32_C(0xd1800000)
                              : texture->base.target == PIPE_TEXTURE_3D
                                 ? UINT32_C(0xa1800000)
                                 : UINT32_C(0x91800000))
                       : multisampled
                           ? (texture->base.target == PIPE_TEXTURE_2D_ARRAY
                                 ? UINT32_C(0xf1b20000)
                                 : UINT32_C(0xe1b20000))
                       : tiled_render_target
                           ? (texture->base.target == PIPE_TEXTURE_2D_ARRAY
                                 ? UINT32_C(0xd1b00000)
                                 : UINT32_C(0x91b00000))
                        : texture->base.target == PIPE_TEXTURE_1D
                           ? UINT32_C(0x80000000)
                       : texture->base.target == PIPE_TEXTURE_1D_ARRAY
                          ? UINT32_C(0xc0000000)
                       : texture->base.target == PIPE_TEXTURE_CUBE
                           ? UINT32_C(0xb0000000)
                       : texture->base.target == PIPE_TEXTURE_2D_ARRAY
                          ? UINT32_C(0xd0000000)
                       : texture->base.target == PIPE_TEXTURE_3D
                          ? UINT32_C(0xa0000000)
                          : UINT32_C(0x90000000)) |
                      swizzle[0] |
                      (swizzle[1] << 3) | (swizzle[2] << 6) |
                      (swizzle[3] << 9) |
                      (view->u.tex.first_level << 12) |
                      (view->u.tex.last_level << 16);
      /* CUBE encodes cube count, while array images encode face/layer bounds. */
      descriptor[4] = texture->base.target == PIPE_TEXTURE_CUBE
                         ? 0
                      : texture->base.target == PIPE_TEXTURE_3D
                         ? texture->base.depth0 - 1
                      : texture->base.target == PIPE_TEXTURE_1D_ARRAY
                         ? view->u.tex.last_layer |
                              (view->u.tex.first_layer << 16)
                         : view->u.tex.last_layer |
                              (view->u.tex.first_layer << 16);
      if (texture->base.target == PIPE_TEXTURE_2D &&
          !texture->base.last_level &&
          !tiled_render_target && !tiled_depth_target && !multisampled &&
          !util_format_is_compressed(texture->base.format)) {
         unsigned pitch = descriptor_stride / descriptor_format_size;

         if (pitch > texture->base.width0 && pitch <= UINT32_C(0x4000))
            descriptor[4] = pitch - 1u; /* GFX10.3 custom linear pitch. */
      }
      descriptor[5] = UINT32_C(0x00400000) |
                      ((multisampled ? 2u : texture->base.last_level) << 4);
      descriptor[8] = wrap[0] | (wrap[1] << 3) | (wrap[2] << 6) |
                      (sampler->compare_mode ?
                         sampler->compare_func << 12 : 0) |
                      ((PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE &&
                        !sampler->seamless_cube_map) << 28);
      descriptor[9] = min_lod | (max_lod << 12);
      descriptor[10] = lod_bias | (filter[1] << 20) | (filter[0] << 22) |
                       (mip_filter << 26);
      descriptor[11] = sampler_state->border_color_ptr |
                       ((uint32_t)sampler_state->border_color_type << 30);
      if (sampler->compare_mode) {
         uint32_t first_depth;

         memcpy(&first_depth,
                texture->data + texture->level_offset[0],
                sizeof(first_depth));
         printf("[ps5-gallium] shadow-sample format=%u size=%ux%u last=%u depth=%08x descriptor=%08x/%08x/%08x/%08x/%08x/%08x sampler=%08x/%08x/%08x\n",
                texture->base.format, texture->base.width0,
                texture->base.height0, texture->base.last_level,
                first_depth, descriptor[0], descriptor[1], descriptor[2],
                descriptor[3], descriptor[4], descriptor[5], descriptor[8],
                descriptor[9], descriptor[10]);
      }
      flush_size = MAX2(flush_size, binding->offset + binding->stride);
      if (tiled_render_target || tiled_depth_target) {
         size_t tiled_size = multisampled
            ? tiled_depth_target
               ? ps5_tiled_rgba8_msaa4_surface_size(texture->base.width0,
                                                    texture->base.height0)
               : ps5_tiled_color_msaa4_surface_size(
                    texture->base.format, texture->base.width0,
                    texture->base.height0)
            : tiled_render_target
               ? ps5_tiled_color_surface_size(texture->base.format,
                                              texture->base.width0,
                                              texture->base.height0)
            : ps5_tiled_surface_size(texture->base.width0,
                                     texture->base.height0);

         ps5_flush_gpu_data(
            texture->data,
            tiled_depth_target ||
            texture->base.target == PIPE_TEXTURE_2D_ARRAY
               ? texture->allocation_size : tiled_size);
      } else {
         ps5_flush_gpu_data(texture->data, texture->size);
      }
   }
   if (texture_count != expected_texture_count)
      return false;
   user_data[metadata->descriptor_set0_user_data_dword] =
      (uint32_t)table_address;
   ps5_flush_gpu_data(table->data, flush_size);
   return true;
}

int64_t sceKernelGetDirectMemorySize(void);
int32_t sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end,
                                     size_t length, size_t alignment,
                                     int memory_type, int64_t *physical_start);
int32_t sceKernelMapDirectMemory(void **address, size_t length, int protection,
                                int flags, int64_t physical_start,
                                size_t alignment);
int32_t sceKernelReleaseDirectMemory(int64_t physical_start, size_t length);
int ps5_agc_gate2_run(void) __attribute__((weak));
int ps5_agc_gate2_set_packages(const void *vs, size_t vs_size,
                               const void *ps, size_t ps_size)
   __attribute__((weak));
int ps5_agc_gate2_set_ngg_control(uint32_t valid, uint32_t ge_pc_alloc)
   __attribute__((weak));
int ps5_agc_gate2_set_framebuffer(void *framebuffer, size_t size)
   __attribute__((weak));
int ps5_agc_gate2_set_framebuffers(void *const *framebuffers,
                                   const size_t *sizes, unsigned count)
   __attribute__((weak));
int ps5_agc_gate2_set_scanout(void *framebuffer, size_t size)
   __attribute__((weak));
int ps5_agc_gate2_set_vertex_user_data(const uint32_t *values, unsigned count)
   __attribute__((weak));
int ps5_agc_gate2_set_index_buffer(const void *indices, unsigned index_count)
   __attribute__((weak));
int ps5_agc_gate2_set_index_buffer_typed(const void *indices,
                                         unsigned index_count,
                                         unsigned index_size)
   __attribute__((weak));
int ps5_agc_gate2_set_draw_state(uint32_t primitive_type,
                                 unsigned draw_count)
   __attribute__((weak));
int ps5_agc_gate2_set_instance_count(unsigned count)
   __attribute__((weak));
int ps5_agc_gate2_set_pixel_user_data(const uint32_t *values, unsigned count)
   __attribute__((weak));
int ps5_agc_gate2_set_depth_buffer(void *depth, size_t size,
                                   uint32_t depth_control)
   __attribute__((weak));
int ps5_agc_gate2_set_depth_stencil_buffer(
   void *depth, size_t depth_size, void *stencil, size_t stencil_size,
   uint32_t depth_control, uint32_t stencil_control,
   uint32_t stencil_refmask, uint32_t stencil_refmask_bf)
   __attribute__((weak));
int ps5_agc_gate2_set_graphics_state(
   uint32_t blend_control, uint32_t target_mask, uint32_t color_control,
   uint32_t color_control_valid,
   const uint32_t blend_color[4], const uint32_t viewport[8],
   const uint32_t scissor[2], uint32_t rasterizer_control,
   uint32_t rasterizer_valid, const uint32_t polygon_offset[6],
   uint32_t polygon_offset_valid) __attribute__((weak));
int ps5_agc_gate2_set_graphics_state_mrt(
   const uint32_t *blend_control, unsigned count, uint32_t target_mask,
   uint32_t color_control, uint32_t color_control_valid,
   const uint32_t blend_color[4], const uint32_t viewport[8],
   const uint32_t scissor[2], uint32_t rasterizer_control,
   uint32_t rasterizer_valid, const uint32_t polygon_offset[6],
   uint32_t polygon_offset_valid) __attribute__((weak));
int ps5_agc_gate2_set_point_line_state(const uint32_t values[3],
                                       uint32_t valid)
   __attribute__((weak));
int ps5_agc_gate2_set_interp_control(uint32_t control, uint32_t valid)
   __attribute__((weak));
int ps5_agc_gate2_set_point_coord_input(uint32_t enabled)
   __attribute__((weak));
int ps5_agc_gate2_set_dual_source_blend(uint32_t enabled)
   __attribute__((weak));
int ps5_agc_gate2_set_multisample_state(unsigned samples,
                                        uint32_t sample_mask,
                                        uint32_t enabled,
                                        uint32_t alpha_to_coverage,
                                        uint32_t poly_line_smooth,
                                        uint32_t sample_shading)
   __attribute__((weak));
int ps5_agc_gate2_set_streamout(const void *vertex_shader,
                                 size_t vertex_shader_size,
                                uint32_t enabled_mask,
                                const uint32_t size_dwords[4],
                                const uint32_t stride_dwords[4],
                                const uint32_t offset_dwords[4])
   __attribute__((weak));
int ps5_agc_gate2_set_occlusion_query(void *query, size_t size, bool precise)
   __attribute__((weak));
int ps5_agc_gate2_set_clip_control(uint32_t control, uint32_t valid)
   __attribute__((weak));
int ps5_agc_gate2_set_vs_out_control(uint32_t control, uint32_t valid)
   __attribute__((weak));
int ps5_agc_gate2_set_color_to_texture_barrier(uint32_t enabled)
   __attribute__((weak));
int ps5_agc_gate2_set_depth_to_texture_barrier(uint32_t enabled)
   __attribute__((weak));
int ps5_agc_gate2_set_depth_target_extents(uint32_t width, uint32_t height)
   __attribute__((weak));
int ps5_agc_gate2_set_depth_target_view(uint32_t view)
   __attribute__((weak));
int ps5_agc_gate2_set_color_target_info(const uint32_t *values,
                                        unsigned count)
   __attribute__((weak));
int ps5_agc_gate2_set_color_target_extents(const uint32_t *widths,
                                           const uint32_t *heights,
                                           unsigned count)
   __attribute__((weak));
int ps5_agc_gate2_set_color_target_views(const uint32_t *views,
                                         unsigned count)
   __attribute__((weak));
int ps5_agc_gate2_set_border_color_table(const void *table, size_t size)
   __attribute__((weak));

static const char *
ps5_get_name(struct pipe_screen *screen)
{
   (void)screen;
   return "PS5 AGC";
}

static const char *
ps5_get_vendor(struct pipe_screen *screen)
{
   (void)screen;
   return "PS5 homebrew";
}

static const char *
ps5_get_device_vendor(struct pipe_screen *screen)
{
   (void)screen;
   return "AMD";
}

static bool
ps5_resource_layout(const struct pipe_resource *templ, size_t *size,
                    unsigned *stride)
{
   size_t total = 0;
   unsigned width;
   unsigned height;
   unsigned level;
   bool linear_sampled;
   bool cube;
   bool texture_1d;
   bool array_1d;
   bool array_2d;
   bool texture_3d;
   bool rectangle;
   bool multisample;
   bool depth_target;
   unsigned storage_layers;

   if (!templ || !templ->width0 ||
       templ->last_level >= PIPE_MAX_TEXTURE_LEVELS)
      return false;

   multisample = templ->nr_samples > 1 || templ->nr_storage_samples > 1;
   if (multisample &&
       (!PS5_ENABLE_MSAA4_CANDIDATE ||
        (templ->target != PIPE_TEXTURE_2D &&
         !(PS5_ENABLE_MSAA_ARRAY_CANDIDATE &&
           templ->target == PIPE_TEXTURE_2D_ARRAY)) ||
        templ->nr_samples != 4 || templ->nr_storage_samples != 4 ||
        templ->last_level || templ->depth0 != 1 ||
        (templ->target == PIPE_TEXTURE_2D && templ->array_size != 1) ||
        !((ps5_msaa4_color_format(templ->format) &&
           (templ->bind &
              (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) &&
          !(templ->bind & PIPE_BIND_DISPLAY_TARGET)) ||
          ps5_msaa4_depth_support(
             templ->format, templ->target, templ->nr_samples,
             templ->nr_storage_samples, templ->bind))))
      return false;

   if (templ->target == PIPE_BUFFER) {
      if (templ->height0 != 1 || templ->depth0 != 1 || templ->array_size != 1)
         return false;
      *stride = templ->width0;
      *size = templ->width0;
      return true;
   }

   texture_1d = PS5_ENABLE_TEXTURE_1D_CANDIDATE &&
                templ->target == PIPE_TEXTURE_1D;
   array_1d = PS5_ENABLE_TEXTURE_1D_CANDIDATE &&
              templ->target == PIPE_TEXTURE_1D_ARRAY;
   cube = PS5_ENABLE_TEXTURE_CUBE_CANDIDATE &&
          templ->target == PIPE_TEXTURE_CUBE;
   array_2d = PS5_ENABLE_TEXTURE_ARRAY_CANDIDATE &&
              templ->target == PIPE_TEXTURE_2D_ARRAY;
   texture_3d = PS5_ENABLE_TEXTURE_3D_CANDIDATE &&
                templ->target == PIPE_TEXTURE_3D;
   rectangle = PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE &&
               templ->target == PIPE_TEXTURE_RECT;
   depth_target =
      (ps5_depth_render_target(templ->target) &&
       (templ->format == PIPE_FORMAT_Z32_FLOAT ||
        templ->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) &&
       (templ->bind & PIPE_BIND_DEPTH_STENCIL) &&
       !(templ->bind & ~(PIPE_BIND_DEPTH_STENCIL |
                         PIPE_BIND_SAMPLER_VIEW))) ||
      ps5_msaa4_depth_support(
         templ->format, templ->target, templ->nr_samples,
         templ->nr_storage_samples, templ->bind);
   if ((templ->target != PIPE_TEXTURE_2D && !texture_1d && !array_1d &&
        !rectangle && !cube && !array_2d && !texture_3d) ||
       !templ->height0 ||
       templ->width0 > PS5_MAX_TEXTURE_2D_SIZE ||
       templ->height0 > PS5_MAX_TEXTURE_2D_SIZE ||
       (!texture_3d && templ->depth0 != 1) ||
       !ps5_texture_format_size(templ->format))
      return false;
   if ((texture_1d && (templ->height0 != 1 ||
                       templ->array_size != 1 ||
                       (!depth_target &&
                        (!ps5_sampled_texture_format(templ->format) ||
                         !ps5_mutable_sampled_resource_bind(templ->bind))))) ||
       (array_1d && (templ->height0 != 1 || !templ->array_size ||
                     templ->array_size > PS5_MAX_TEXTURE_ARRAY_LAYERS ||
                     (!depth_target &&
                      (!ps5_sampled_texture_format(templ->format) ||
                       !ps5_mutable_sampled_resource_bind(templ->bind))))) ||
       (!texture_1d && !array_1d && !cube && !array_2d && !texture_3d &&
        templ->array_size != 1) ||
       (cube && (templ->array_size != 6 ||
                 templ->width0 > PS5_MAX_TEXTURE_CUBE_SIZE ||
                 templ->width0 != templ->height0 ||
                 (!depth_target &&
                  (!ps5_sampled_texture_format(templ->format) ||
                   !ps5_mutable_sampled_resource_bind(templ->bind))))) ||
       (array_2d && (!templ->array_size ||
                     templ->array_size > PS5_MAX_TEXTURE_ARRAY_LAYERS ||
                     !ps5_sampled_texture_format(templ->format) ||
                     (!depth_target &&
                      !ps5_mutable_sampled_resource_bind(templ->bind)))) ||
       (texture_3d && (templ->array_size != 1 || !templ->depth0 ||
                       templ->depth0 > PS5_MAX_TEXTURE_3D_SIZE ||
                       templ->width0 > PS5_MAX_TEXTURE_3D_SIZE ||
                       templ->height0 > PS5_MAX_TEXTURE_3D_SIZE ||
                       (!depth_target &&
                        (!ps5_sampled_texture_format(templ->format) ||
                         !ps5_mutable_sampled_resource_bind(templ->bind))))))
      return false;

   if (templ->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT &&
       !PS5_ENABLE_PACKED_DEPTH_STENCIL)
      return false;

   if ((templ->format == PIPE_FORMAT_Z32_FLOAT ||
        templ->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) &&
       (templ->bind & PIPE_BIND_DEPTH_STENCIL) && templ->last_level &&
       !ps5_depth_staging_required(templ))
      return false;
   if (rectangle && templ->last_level)
      return false;

   linear_sampled = ps5_linear_sampled_layout(templ);
   if (linear_sampled &&
       (templ->last_level > 15 ||
        (templ->last_level && !PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE)))
      return false;

   if (linear_sampled) {
      for (level = templ->last_level + 1; level-- > 0;) {
         size_t row;
         size_t level_size;
         unsigned rows;

         width = ps5_linear_mip_storage_extent(templ->width0, level);
         height = ps5_linear_mip_storage_extent(templ->height0, level);
         row = util_format_get_stride(templ->format, width);
         row = (row + 255u) & ~(size_t)255u;
         rows = util_format_get_nblocksy(templ->format, height);
         if (row > UINT32_MAX || row > SIZE_MAX / rows)
            return false;
         level_size = row * rows;
         if (total > SIZE_MAX - level_size)
            return false;
         if (!level)
            *stride = (unsigned)row;
         total += level_size;
      }
      storage_layers = texture_3d ? templ->depth0 : templ->array_size;
      if (total > SIZE_MAX / storage_layers)
         return false;
      *size = total * storage_layers;
      return true;
   }

   width = templ->width0;
   height = templ->height0;
   for (level = 0; level <= templ->last_level; ++level) {
      size_t row = util_format_get_stride(templ->format, width);
      size_t level_size;
      unsigned rows = util_format_get_nblocksy(templ->format, height);

      if (row > UINT32_MAX || row > SIZE_MAX / rows)
         return false;
      level_size = row * rows;
      if (total > SIZE_MAX - level_size)
         return false;
      if (!level)
         *stride = (unsigned)row;
      total += level_size;
      width = width > 1 ? width >> 1 : 1;
      height = height > 1 ? height >> 1 : 1;
   }

   if (array_2d || depth_target) {
      storage_layers = ps5_texture_level_layers(templ, 0);
      if (total > SIZE_MAX / storage_layers)
         return false;
      total *= storage_layers;
   }

   if (multisample) {
      if (total > SIZE_MAX / 4u)
         return false;
      total *= 4u;
   }
   *size = total;
   return true;
}

static bool
ps5_can_create_resource(struct pipe_screen *screen,
                        const struct pipe_resource *templ)
{
   struct pipe_resource proxy;
   size_t size;
   unsigned stride;

   (void)screen;
   if (ps5_resource_layout(templ, &size, &stride))
      return true;

   /* Mesa's mutable-texture proxy supplies bind=0. Validate its sampled
    * dimensions with the binding real OpenGL texture allocation will carry,
    * rounding a requested minimum sample count to native 4x as allocation does.
    * It can also guess a complete mip chain even when the application has
    * clamped the real texture to level 0, so retry the valid base image.
    */
   if (!templ || templ->bind)
      return false;
   proxy = *templ;
   if (proxy.nr_samples > 1 && proxy.nr_samples <= 4 &&
       proxy.nr_samples == proxy.nr_storage_samples) {
      proxy.nr_samples = 4;
      proxy.nr_storage_samples = 4;
   }
   proxy.bind = PIPE_BIND_SAMPLER_VIEW;
   if (ps5_resource_layout(&proxy, &size, &stride))
      return true;
   if (!proxy.last_level)
      return false;
   proxy.last_level = 0;
   return ps5_resource_layout(&proxy, &size, &stride);
}

static bool
ps5_render_arena_range_is_free(const struct ps5_screen *screen,
                               unsigned first, unsigned count)
{
   for (unsigned slot = first; slot < first + count; ++slot) {
      if (screen->render_arena_bitmap[slot / 64u] &
          (UINT64_C(1) << (slot % 64u)))
         return false;
   }
   return true;
}

static void
ps5_render_arena_mark(struct ps5_screen *screen, unsigned first,
                      unsigned count, bool used)
{
   for (unsigned slot = first; slot < first + count; ++slot) {
      uint64_t mask = UINT64_C(1) << (slot % 64u);

      if (used)
         screen->render_arena_bitmap[slot / 64u] |= mask;
      else
         screen->render_arena_bitmap[slot / 64u] &= ~mask;
   }
}

static bool
ps5_render_arena_allocate(struct ps5_screen *screen,
                          struct ps5_resource *resource, size_t size,
                          size_t alignment)
{
   struct ps5_resource *pool =
      (struct ps5_resource *)screen->render_pool;
   unsigned slots;
   unsigned slot_alignment;

   if (!pool || !pool->data || !size ||
       size > PS5_RENDER_POOL_BYTES - PS5_RENDER_ARENA_OFFSET ||
       size % PS5_RENDER_ARENA_SLOT_BYTES ||
       alignment < PS5_RENDER_ARENA_SLOT_BYTES ||
       alignment % PS5_RENDER_ARENA_SLOT_BYTES)
      return false;
   slots = (unsigned)(size / PS5_RENDER_ARENA_SLOT_BYTES);
   slot_alignment = (unsigned)(alignment / PS5_RENDER_ARENA_SLOT_BYTES);
   for (unsigned first = 0;
        first + slots <= PS5_RENDER_ARENA_SLOT_COUNT; ++first) {
      if (first % slot_alignment ||
          !ps5_render_arena_range_is_free(screen, first, slots))
         continue;
      ps5_render_arena_mark(screen, first, slots, true);
      resource->render_arena_first_slot = first;
      resource->render_arena_slot_count = slots;
      resource->data = pool->data + PS5_RENDER_ARENA_OFFSET +
                       first * PS5_RENDER_ARENA_SLOT_BYTES;
      resource->allocation_size = size;
      pipe_resource_reference(&resource->render_pool_owner,
                              screen->render_pool);
      memset(resource->data, 0, size);
      printf("[ps5-gallium] shared-resource offset=%zu bytes=%zu first=%u count=%u alignment=%zu\n",
             PS5_RENDER_ARENA_OFFSET +
                first * (size_t)PS5_RENDER_ARENA_SLOT_BYTES,
             size, first, slots, alignment);
      return true;
   }
   return false;
}

static struct pipe_resource *
ps5_resource_create_unlocked(struct pipe_screen *screen,
                             const struct pipe_resource *templ)
{
   struct ps5_screen *ps5 = (struct ps5_screen *)screen;
   struct ps5_resource *resource;
   int64_t direct_limit;
   int allocation_status;
   int map_status;
   size_t size;
   size_t allocation_size;
   size_t allocation_alignment = PS5_DIRECT_ALIGNMENT;
   size_t render_staging_offset = 0;
   size_t render_staging_size = 0;
   size_t depth_staging_offset = 0;
   size_t depth_staging_size = 0;
   bool render_staging;
   bool depth_staging;
   unsigned stride;

   if (!ps5_resource_layout(templ, &size, &stride)) {
      printf("[ps5-gallium] reject-resource target=%u format=%u size=%ux%ux%u array=%u last=%u samples=%u/%u bind=%08x flags=%08x\n",
             templ ? templ->target : 0, templ ? templ->format : 0,
             templ ? templ->width0 : 0, templ ? templ->height0 : 0,
             templ ? templ->depth0 : 0, templ ? templ->array_size : 0,
             templ ? templ->last_level : 0, templ ? templ->nr_samples : 0,
             templ ? templ->nr_storage_samples : 0,
             templ ? templ->bind : 0, templ ? templ->flags : 0);
      return NULL;
   }

   resource = calloc(1, sizeof(*resource));
   if (!resource) {
#ifdef PS5_PUBLIC_STENCIL_TEST
      printf("[ps5-gallium] public-stencil-resource-failure stage=metadata format=%u\n",
             templ->format);
#endif
      return NULL;
   }
   resource->direct_start = -1;
   resource->stencil_direct_start = -1;
   render_staging = ps5_render_staging_required(templ);
   depth_staging = ps5_depth_staging_required(templ);

   if ((templ->bind & PIPE_BIND_DISPLAY_TARGET) &&
       ((PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE && ps5->render_pool) ||
        templ->width0 != PS5_RENDER_WIDTH ||
        templ->height0 != PS5_RENDER_HEIGHT)) {
      free(resource);
      return NULL;
   }

   if (size > SIZE_MAX - (PS5_DIRECT_ALIGNMENT - 1)) {
      free(resource);
      return NULL;
   }
   if ((templ->target == PIPE_TEXTURE_2D ||
        (PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE &&
         templ->target == PIPE_TEXTURE_2D_ARRAY)) &&
       ((templ->bind & PIPE_BIND_RENDER_TARGET) ||
        (templ->nr_samples == 4 &&
         ps5_msaa4_color_format(templ->format))) &&
       !ps5_linear_sampled_layout(templ)) {
      if ((!(PS5_ENABLE_PADDED_FBO_CANDIDATE ||
             PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE) &&
           (templ->width0 != PS5_RENDER_WIDTH ||
            templ->height0 != PS5_RENDER_HEIGHT)) ||
          ((PS5_ENABLE_PADDED_FBO_CANDIDATE ||
            PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE) &&
           (templ->width0 > PS5_MAX_COLOR_WIDTH ||
            templ->height0 > PS5_MAX_COLOR_HEIGHT))) {
         free(resource);
         return NULL;
      }
      allocation_size = templ->bind & PIPE_BIND_DISPLAY_TARGET
                           ? PS5_RENDER_POOL_BYTES
                           : PS5_RENDER_TARGET_BYTES;
      if (PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE &&
          !(templ->bind & PIPE_BIND_DISPLAY_TARGET)) {
         size_t tiled_size = templ->nr_samples == 4
            ? ps5_tiled_color_msaa4_surface_size(
                 templ->format, templ->width0, templ->height0)
            : ps5_tiled_color_surface_size(templ->format, templ->width0,
                                           templ->height0);

         if (templ->target == PIPE_TEXTURE_2D_ARRAY) {
            if (tiled_size > SIZE_MAX / templ->array_size) {
               free(resource);
               return NULL;
            }
            tiled_size *= templ->array_size;
         }

         allocation_size =
            (tiled_size + PS5_RENDER_ALIGNMENT - 1u) &
            ~(size_t)(PS5_RENDER_ALIGNMENT - 1u);
      }
#ifdef PS5_PUBLIC_STENCIL_TEST
      if (!(templ->bind & PIPE_BIND_DISPLAY_TARGET))
         printf("[ps5-gallium] public-stencil-offscreen-color bytes=%zu buffers=1\n",
                allocation_size);
#endif
      allocation_alignment = PS5_RENDER_ALIGNMENT;
   } else if (render_staging) {
      const unsigned layers = ps5_texture_level_layers(templ, 0);
      size_t combined;

      render_staging_size = ps5_tiled_color_surface_size(
         templ->format, templ->width0, templ->height0);
      if (!render_staging_size || render_staging_size > SIZE_MAX / layers) {
         free(resource);
         return NULL;
      }
      render_staging_size *= layers;
      render_staging_offset =
         (size + PS5_COLOR_TARGET_ALIGNMENT - 1u) &
         ~(size_t)(PS5_COLOR_TARGET_ALIGNMENT - 1u);
      if (render_staging_offset < size ||
          render_staging_size > SIZE_MAX - render_staging_offset) {
         free(resource);
         return NULL;
      }
      combined = render_staging_offset + render_staging_size;
      if (combined > SIZE_MAX - (PS5_RENDER_ALIGNMENT - 1u)) {
         free(resource);
         return NULL;
      }
      allocation_size =
         (combined + PS5_RENDER_ALIGNMENT - 1u) &
         ~(size_t)(PS5_RENDER_ALIGNMENT - 1u);
      allocation_alignment = PS5_RENDER_ALIGNMENT;
   } else if (depth_staging) {
      const unsigned layers = ps5_texture_level_layers(templ, 0);
      size_t combined;

      depth_staging_size = ps5_tiled_depth_surface_size(
         templ->width0, templ->height0, templ->nr_samples);
      if (depth_staging_size > SIZE_MAX / layers) {
         free(resource);
         return NULL;
      }
      depth_staging_size *= layers;
      if (templ->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
         size_t sample_layer_size;
         unsigned sample_stride;

         if (!ps5_packed_depth_sample_layout(
                templ, &sample_layer_size, &sample_stride) ||
             sample_layer_size > SIZE_MAX / layers) {
            free(resource);
            return NULL;
         }
         depth_staging_size = MAX2(depth_staging_size,
                                   sample_layer_size * layers);
      }
      depth_staging_offset =
         (size + PS5_COLOR_TARGET_ALIGNMENT - 1u) &
         ~(size_t)(PS5_COLOR_TARGET_ALIGNMENT - 1u);
      if (depth_staging_offset < size ||
          depth_staging_size > SIZE_MAX - depth_staging_offset) {
         free(resource);
         return NULL;
      }
      combined = depth_staging_offset + depth_staging_size;
      if (combined > SIZE_MAX - (PS5_RENDER_ALIGNMENT - 1u)) {
         free(resource);
         return NULL;
      }
      allocation_size =
         (combined + PS5_RENDER_ALIGNMENT - 1u) &
         ~(size_t)(PS5_RENDER_ALIGNMENT - 1u);
      allocation_alignment = PS5_RENDER_ALIGNMENT;
   } else if ((ps5_depth_render_target(templ->target) &&
               (templ->bind & PIPE_BIND_DEPTH_STENCIL)) ||
              ps5_msaa4_depth_support(
                 templ->format, templ->target, templ->nr_samples,
                 templ->nr_storage_samples, templ->bind)) {
      const unsigned layers = ps5_texture_level_layers(templ, 0);
      size_t tiled_size = PS5_DEPTH_TARGET_BYTES;

      if ((!PS5_ENABLE_PADDED_FBO_CANDIDATE &&
           (templ->width0 != PS5_RENDER_WIDTH ||
            templ->height0 != PS5_RENDER_HEIGHT)) ||
          (PS5_ENABLE_PADDED_FBO_CANDIDATE &&
           (templ->width0 > PS5_MAX_DEPTH_WIDTH ||
            templ->height0 > PS5_MAX_DEPTH_HEIGHT))) {
         free(resource);
         return NULL;
      }
      if (PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE)
         tiled_size = ps5_tiled_depth_surface_size(
            templ->width0, templ->height0, templ->nr_samples);
      if (tiled_size > SIZE_MAX / layers ||
          tiled_size * layers > SIZE_MAX - (PS5_RENDER_ALIGNMENT - 1u)) {
         free(resource);
         return NULL;
      }
      allocation_size =
         (tiled_size * layers + PS5_RENDER_ALIGNMENT - 1u) &
         ~(size_t)(PS5_RENDER_ALIGNMENT - 1u);
      allocation_alignment = PS5_RENDER_ALIGNMENT;
   } else {
      allocation_size = (size + PS5_DIRECT_ALIGNMENT - 1) &
                        ~(size_t)(PS5_DIRECT_ALIGNMENT - 1);
   }
   if (PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE && ps5->render_pool &&
       !(templ->bind & PIPE_BIND_DISPLAY_TARGET) &&
       templ->format != PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
      if (!ps5_render_arena_allocate(ps5, resource, allocation_size,
                                     allocation_alignment)) {
         printf("[ps5-gallium] shared-resource exhausted bytes=%zu alignment=%zu\n",
                allocation_size, allocation_alignment);
         /* The arena is an allocation fast path, not a resource-size limit.
          * Fall through to the same checked direct allocation as staging/MSAA. */
      } else {
         goto primary_ready;
      }
   }
   direct_limit = sceKernelGetDirectMemorySize();
#ifdef PS5_PUBLIC_STENCIL_TEST
   if (templ->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
      const unsigned layers = ps5_texture_level_layers(templ, 0);
      const size_t layer_size = PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE
         ? ps5_tiled_stencil_surface_size_samples(
              templ->width0, templ->height0, templ->nr_samples)
         : PS5_STENCIL_TARGET_BYTES;

      if (layer_size > SIZE_MAX / layers) {
         free(resource);
         return NULL;
      }
      resource->stencil_allocation_size = layer_size * layers;
      printf("[ps5-gallium] public-stencil-allocation-order stencil-first\n");
      allocation_status = sceKernelAllocateDirectMemory(
         0, direct_limit, resource->stencil_allocation_size,
         PS5_STENCIL_ALIGNMENT, PS5_DIRECT_MEMORY_TYPE,
         &resource->stencil_direct_start);
      if (allocation_status != 0) {
         printf("[ps5-gallium] public-stencil-resource-failure stage=stencil-allocate rc=%08x format=%u bytes=%zu alignment=%u limit=%016llx\n",
                (unsigned)allocation_status, templ->format,
                resource->stencil_allocation_size, PS5_STENCIL_ALIGNMENT,
                (unsigned long long)direct_limit);
         free(resource);
         return NULL;
      }
      map_status = sceKernelMapDirectMemory(
         (void **)&resource->stencil_data,
         resource->stencil_allocation_size, PS5_MAP_PROTECTION, 0,
         resource->stencil_direct_start, PS5_STENCIL_ALIGNMENT);
      if (map_status != 0 || !resource->stencil_data) {
         printf("[ps5-gallium] public-stencil-resource-failure stage=stencil-map rc=%08x format=%u direct=%016llx bytes=%zu alignment=%u address=%p\n",
                (unsigned)map_status, templ->format,
                (unsigned long long)resource->stencil_direct_start,
                resource->stencil_allocation_size, PS5_STENCIL_ALIGNMENT,
                resource->stencil_data);
         if (resource->stencil_data)
            munmap(resource->stencil_data,
                   resource->stencil_allocation_size);
         sceKernelReleaseDirectMemory(resource->stencil_direct_start,
                                      resource->stencil_allocation_size);
         free(resource);
         return NULL;
      }
      memset(resource->stencil_data, 0,
             resource->stencil_allocation_size);
   }
#endif
   allocation_status = sceKernelAllocateDirectMemory(
      0, direct_limit, allocation_size, allocation_alignment,
      PS5_DIRECT_MEMORY_TYPE, &resource->direct_start);
   if (allocation_status != 0) {
#ifdef PS5_PUBLIC_STENCIL_TEST
      printf("[ps5-gallium] public-stencil-resource-failure stage=depth-allocate rc=%08x format=%u bytes=%zu alignment=%zu limit=%016llx\n",
             (unsigned)allocation_status, templ->format, allocation_size,
             allocation_alignment, (unsigned long long)direct_limit);
#endif
#ifdef PS5_PUBLIC_STENCIL_TEST
      if (resource->stencil_data)
         munmap(resource->stencil_data,
                resource->stencil_allocation_size);
      if (resource->stencil_direct_start >= 0)
         sceKernelReleaseDirectMemory(resource->stencil_direct_start,
                                      resource->stencil_allocation_size);
#endif
      free(resource);
      return NULL;
   }

   map_status = sceKernelMapDirectMemory(
      (void **)&resource->data, allocation_size, PS5_MAP_PROTECTION, 0,
      resource->direct_start, allocation_alignment);
   if (map_status != 0 || !resource->data) {
#ifdef PS5_PUBLIC_STENCIL_TEST
      printf("[ps5-gallium] public-stencil-resource-failure stage=depth-map rc=%08x format=%u direct=%016llx bytes=%zu alignment=%zu address=%p\n",
             (unsigned)map_status, templ->format,
             (unsigned long long)resource->direct_start, allocation_size,
             allocation_alignment, resource->data);
#endif
      sceKernelReleaseDirectMemory(resource->direct_start, allocation_size);
#ifdef PS5_PUBLIC_STENCIL_TEST
      if (resource->stencil_data)
         munmap(resource->stencil_data,
                resource->stencil_allocation_size);
      if (resource->stencil_direct_start >= 0)
         sceKernelReleaseDirectMemory(resource->stencil_direct_start,
                                      resource->stencil_allocation_size);
#endif
      free(resource);
      return NULL;
   }
   memset(resource->data, 0, allocation_size);
primary_ready:
   if (templ->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
#ifndef PS5_PUBLIC_STENCIL_TEST
      const unsigned layers = ps5_texture_level_layers(templ, 0);
      const size_t layer_size = PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE
         ? ps5_tiled_stencil_surface_size_samples(
              templ->width0, templ->height0, templ->nr_samples)
         : PS5_STENCIL_TARGET_BYTES;

      if (layer_size > SIZE_MAX / layers)
         goto fail_primary;
      resource->stencil_allocation_size = layer_size * layers;
      allocation_status = sceKernelAllocateDirectMemory(
         0, direct_limit, resource->stencil_allocation_size,
         PS5_STENCIL_ALIGNMENT, PS5_DIRECT_MEMORY_TYPE,
         &resource->stencil_direct_start);
      if (allocation_status != 0) {
#ifdef PS5_PUBLIC_STENCIL_TEST
         printf("[ps5-gallium] public-stencil-resource-failure stage=stencil-allocate rc=%08x format=%u bytes=%zu alignment=%u limit=%016llx\n",
                (unsigned)allocation_status, templ->format,
                resource->stencil_allocation_size, PS5_STENCIL_ALIGNMENT,
                (unsigned long long)direct_limit);
#endif
         goto fail_primary;
      }
      map_status = sceKernelMapDirectMemory(
         (void **)&resource->stencil_data,
         resource->stencil_allocation_size, PS5_MAP_PROTECTION, 0,
         resource->stencil_direct_start, PS5_STENCIL_ALIGNMENT);
      if (map_status != 0 || !resource->stencil_data) {
#ifdef PS5_PUBLIC_STENCIL_TEST
         printf("[ps5-gallium] public-stencil-resource-failure stage=stencil-map rc=%08x format=%u direct=%016llx bytes=%zu alignment=%u address=%p\n",
                (unsigned)map_status, templ->format,
                (unsigned long long)resource->stencil_direct_start,
                resource->stencil_allocation_size, PS5_STENCIL_ALIGNMENT,
                resource->stencil_data);
#endif
         goto fail_stencil;
      }
      memset(resource->stencil_data, 0, resource->stencil_allocation_size);
#endif
#ifdef PS5_PUBLIC_STENCIL_TEST
      printf("[ps5-gallium] public-stencil-resource depth=%p/%zu direct=%016llx stencil=%p/%zu direct=%016llx\n",
             resource->data, allocation_size,
             (unsigned long long)resource->direct_start,
             resource->stencil_data, resource->stencil_allocation_size,
             (unsigned long long)resource->stencil_direct_start);
#endif
   }
   resource->base = *templ;
   resource->base.reference.count = 1;
   resource->base.screen = screen;
   resource->size = size;
   resource->allocation_size = allocation_size;
   resource->stride = stride;
   resource->render_staging_offset = render_staging_offset;
   resource->render_staging_size = render_staging_size;
   resource->depth_staging_offset = depth_staging_offset;
   resource->depth_staging_size = depth_staging_size;
   if (PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE &&
       (templ->bind & PIPE_BIND_DISPLAY_TARGET)) {
      ps5->render_pool = &resource->base;
      printf("[ps5-gallium] shared-render-pool bytes=%zu arena-offset=%u arena-bytes=%zu\n",
             allocation_size, PS5_RENDER_ARENA_OFFSET,
             (size_t)(PS5_RENDER_POOL_BYTES - PS5_RENDER_ARENA_OFFSET));
   }
   if (templ->target == PIPE_BUFFER) {
      resource->level_stride[0] = stride;
      resource->layer_stride = size;
   } else {
      const unsigned bytes_per_pixel = ps5_texture_format_size(templ->format);
      unsigned level;
      size_t offset = 0;
      bool linear_sampled = ps5_linear_sampled_layout(templ);

      if (linear_sampled) {
         for (level = templ->last_level + 1; level-- > 0;) {
            unsigned width =
               ps5_linear_mip_storage_extent(templ->width0, level);
            unsigned height =
               ps5_linear_mip_storage_extent(templ->height0, level);

            resource->level_offset[level] = offset;
            resource->level_stride[level] =
               (util_format_get_stride(templ->format, width) + 255u) &
               ~255u;
            offset += util_format_get_2d_size(
               templ->format, resource->level_stride[level], height);
         }
      } else {
         unsigned width = templ->width0;
         unsigned height = templ->height0;

         for (level = 0; level <= templ->last_level; ++level) {
            resource->level_offset[level] = offset;
            resource->level_stride[level] =
               util_format_get_stride(templ->format, width);
            offset += util_format_get_2d_size(
               templ->format, resource->level_stride[level], height);
            width = width > 1 ? width >> 1 : 1;
            height = height > 1 ? height >> 1 : 1;
         }
      }
      resource->layer_stride = offset;
      if (PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE &&
          templ->target == PIPE_TEXTURE_2D_ARRAY &&
          (templ->bind & PIPE_BIND_RENDER_TARGET) &&
          !linear_sampled)
         resource->layer_stride = templ->nr_samples == 4
            ? ps5_tiled_color_msaa4_surface_size(
                 templ->format, templ->width0, templ->height0)
            : ps5_tiled_color_surface_size(templ->format, templ->width0,
                                           templ->height0);
      if (ps5_depth_render_target(templ->target) &&
          ((templ->bind & PIPE_BIND_DEPTH_STENCIL) ||
           ps5_msaa4_depth_support(
              templ->format, templ->target, templ->nr_samples,
              templ->nr_storage_samples, templ->bind)) &&
          !depth_staging)
         resource->layer_stride = ps5_tiled_depth_surface_size(
            templ->width0, templ->height0, templ->nr_samples);
      if (PS5_ENABLE_PADDED_FBO_CANDIDATE &&
          !PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE &&
          templ->target == PIPE_TEXTURE_2D &&
          (templ->bind & PIPE_BIND_RENDER_TARGET)) {
         resource->stride = PS5_RENDER_WIDTH * bytes_per_pixel;
         resource->level_stride[0] = resource->stride;
         resource->layer_stride =
            (size_t)resource->stride * PS5_RENDER_HEIGHT;
      }
   }
   return &resource->base;

#ifndef PS5_PUBLIC_STENCIL_TEST
fail_stencil:
   if (resource->stencil_data)
      munmap(resource->stencil_data, resource->stencil_allocation_size);
   sceKernelReleaseDirectMemory(resource->stencil_direct_start,
                                resource->stencil_allocation_size);
fail_primary:
   munmap(resource->data, allocation_size);
   sceKernelReleaseDirectMemory(resource->direct_start, allocation_size);
   free(resource);
   return NULL;
#endif
}

static struct pipe_resource *
ps5_resource_create(struct pipe_screen *screen,
                    const struct pipe_resource *templ)
{
   struct ps5_screen *ps5 = (struct ps5_screen *)screen;
   struct pipe_resource *resource;

   simple_mtx_lock(&ps5->resource_mutex);
   resource = ps5_resource_create_unlocked(screen, templ);
   simple_mtx_unlock(&ps5->resource_mutex);
   return resource;
}

int
ps5_resource_info(struct pipe_resource *base, void **address,
                  size_t *logical_size, size_t *allocation_size)
{
   struct ps5_resource *resource = (struct ps5_resource *)base;

   if (!resource)
      return -1;
   if (address)
      *address = resource->data;
   if (logical_size)
      *logical_size = resource->size;
   if (allocation_size)
      *allocation_size = resource->allocation_size;
   return 0;
}

struct pipe_resource *
ps5_display_target_alias(struct pipe_resource *base, unsigned buffer_index)
{
   struct ps5_resource *owner = (struct ps5_resource *)base;
   struct ps5_resource *alias;
   size_t offset;

   if (!owner || !(owner->base.bind & PIPE_BIND_DISPLAY_TARGET) ||
       buffer_index != 1 ||
       owner->allocation_size < 2u * PS5_RENDER_TARGET_BYTES)
      return NULL;
   alias = calloc(1, sizeof(*alias));
   if (!alias)
      return NULL;
   offset = buffer_index * (size_t)PS5_RENDER_TARGET_BYTES;
   alias->base = owner->base;
   alias->base.reference.count = 1;
   alias->data = owner->data + offset;
   alias->size = owner->size;
   alias->allocation_size = PS5_RENDER_TARGET_BYTES;
   alias->stride = owner->stride;
   memcpy(alias->level_offset, owner->level_offset,
          sizeof(alias->level_offset));
   memcpy(alias->level_stride, owner->level_stride,
          sizeof(alias->level_stride));
   alias->layer_stride = owner->layer_stride;
   alias->direct_start = -1;
   alias->stencil_direct_start = -1;
   pipe_resource_reference(&alias->render_pool_owner, base);
   printf("[ps5-gallium] display-alias index=%u offset=%zu bytes=%zu\n",
          buffer_index, offset, alias->allocation_size);
   return &alias->base;
}

int
ps5_resource_stencil_info(struct pipe_resource *base, void **address,
                          size_t *allocation_size)
{
   struct ps5_resource *resource = (struct ps5_resource *)base;

   if (!resource ||
       resource->base.format != PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
       !resource->stencil_data)
      return -1;
   if (address)
      *address = resource->stencil_data;
   if (allocation_size)
      *allocation_size = resource->stencil_allocation_size;
   return 0;
}

static void
ps5_resource_destroy(struct pipe_screen *screen, struct pipe_resource *base)
{
   struct ps5_screen *ps5 = (struct ps5_screen *)screen;
   struct ps5_resource *resource = (struct ps5_resource *)base;
   struct pipe_resource *render_pool_owner = resource->render_pool_owner;
#ifdef PS5_PUBLIC_STENCIL_TEST
   const bool record_prior_depth =
      resource->base.format == PIPE_FORMAT_Z32_FLOAT;
   int prior_depth_unmap_status = 0;
   int32_t prior_depth_release_status = 0;
#endif

   simple_mtx_lock(&ps5->resource_mutex);
   if (render_pool_owner) {
      ps5_render_arena_mark(ps5, resource->render_arena_first_slot,
                            resource->render_arena_slot_count, false);
      resource->data = NULL;
      resource->direct_start = -1;
   }
   if (ps5->render_pool == base) {
      ps5->render_pool = NULL;
      memset(ps5->render_arena_bitmap, 0,
             sizeof(ps5->render_arena_bitmap));
   }
   simple_mtx_unlock(&ps5->resource_mutex);
   if (resource->stencil_data)
      munmap(resource->stencil_data, resource->stencil_allocation_size);
   if (resource->stencil_direct_start >= 0)
      sceKernelReleaseDirectMemory(resource->stencil_direct_start,
                                   resource->stencil_allocation_size);
   if (resource->data) {
#ifdef PS5_PUBLIC_STENCIL_TEST
      prior_depth_unmap_status =
         munmap(resource->data, resource->allocation_size);
#else
      munmap(resource->data, resource->allocation_size);
#endif
   }
   if (resource->direct_start >= 0) {
#ifdef PS5_PUBLIC_STENCIL_TEST
      prior_depth_release_status =
         sceKernelReleaseDirectMemory(resource->direct_start,
                                      resource->allocation_size);
#else
      sceKernelReleaseDirectMemory(resource->direct_start,
                                   resource->allocation_size);
#endif
   }
#ifdef PS5_PUBLIC_STENCIL_TEST
   if (record_prior_depth)
      printf("[ps5-gallium] public-stencil-prior-depth-destroy format=%u address=%p bytes=%zu direct=%016llx unmap=%08x release=%08x\n",
             resource->base.format, resource->data, resource->allocation_size,
             (unsigned long long)resource->direct_start,
             (unsigned)prior_depth_unmap_status,
             (unsigned)prior_depth_release_status);
#endif
   free(resource);
   if (render_pool_owner)
      pipe_resource_reference(&render_pool_owner, NULL);
}

static bool
ps5_is_format_supported(struct pipe_screen *screen, enum pipe_format format,
                        enum pipe_texture_target target, unsigned sample_count,
                        unsigned storage_sample_count, unsigned bindings)
{
   const unsigned supported_bindings =
      PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET |
      PIPE_BIND_SAMPLER_VIEW;

   (void)screen;

   if (sample_count > 1 || storage_sample_count > 1)
      return PS5_ENABLE_MSAA4_CANDIDATE &&
             (target == PIPE_TEXTURE_2D ||
              (PS5_ENABLE_MSAA_ARRAY_CANDIDATE &&
               target == PIPE_TEXTURE_2D_ARRAY)) &&
             sample_count == 4 &&
             storage_sample_count == 4 &&
             ((ps5_msaa4_color_format(format) && bindings &&
               (bindings &
                  (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW)) &&
              !(bindings & ~(PIPE_BIND_RENDER_TARGET |
                              PIPE_BIND_SAMPLER_VIEW))) ||
              ps5_msaa4_depth_support(format, target, sample_count,
                                      storage_sample_count, bindings));

   if (target == PIPE_BUFFER)
      return sample_count <= 1 && storage_sample_count <= 1 &&
             (((format == PIPE_FORMAT_R32_FLOAT ||
                format == PIPE_FORMAT_R32G32_FLOAT ||
                format == PIPE_FORMAT_R32G32B32_FLOAT ||
                format == PIPE_FORMAT_R32G32B32A32_FLOAT ||
                (PS5_ENABLE_INTEGER_VERTEX_CANDIDATE &&
                 ps5_integer_vertex_format(format)) ||
                (PS5_ENABLE_PACKED_VERTEX_CANDIDATE &&
                 ps5_packed_vertex_format(format))) &&
               !(bindings & ~PIPE_BIND_VERTEX_BUFFER)) ||
              (PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE &&
               ps5_texel_buffer_format(format) &&
               bindings == PIPE_BIND_SAMPLER_VIEW));

   if (format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT &&
       !PS5_ENABLE_PACKED_DEPTH_STENCIL)
      return false;

   if (PS5_ENABLE_CORE_RENDER_FORMATS_CANDIDATE &&
       !(bindings & PIPE_BIND_DISPLAY_TARGET) &&
       ps5_core_render_target_format(format)) {
      const unsigned allowed = PIPE_BIND_RENDER_TARGET |
                               PIPE_BIND_SAMPLER_VIEW;
      const bool render = bindings & PIPE_BIND_RENDER_TARGET;

      return (render ? ps5_color_render_target(target) :
                       ps5_sampled_texture_target(target)) &&
             sample_count <= 1 && storage_sample_count <= 1 && bindings &&
             !(bindings & ~allowed) &&
             (!(render && (bindings & PIPE_BIND_SAMPLER_VIEW)) ||
              PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE);
   }

   if (PS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE &&
       ps5_core_sampled_texture_format(format))
      return ps5_sampled_texture_target(target) && sample_count <= 1 &&
             storage_sample_count <= 1 &&
             (bindings & PIPE_BIND_SAMPLER_VIEW) &&
             !(bindings & ~PIPE_BIND_SAMPLER_VIEW);

   if (format == PIPE_FORMAT_Z32_FLOAT ||
       format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) {
      const unsigned allowed = PIPE_BIND_DEPTH_STENCIL |
         (PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE ? PIPE_BIND_SAMPLER_VIEW : 0);
      const bool depth_target = bindings & PIPE_BIND_DEPTH_STENCIL;
      const bool sampled = bindings & PIPE_BIND_SAMPLER_VIEW;

      if (format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT &&
          !PS5_ENABLE_PACKED_DEPTH_STENCIL)
         return false;
      return sample_count <= 1 && storage_sample_count <= 1 &&
             bindings && (depth_target || sampled) &&
             (!depth_target || ps5_depth_render_target(target)) &&
             (!sampled || PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE) &&
             (!sampled || depth_target ||
              (target != PIPE_BUFFER &&
               ps5_sampled_texture_target(target))) &&
             !(bindings & ~allowed);
   }

   if (format == PIPE_FORMAT_R8G8B8A8_SRGB) {
      const unsigned allowed = PIPE_BIND_RENDER_TARGET |
                               PIPE_BIND_SAMPLER_VIEW;
      const bool render = bindings & PIPE_BIND_RENDER_TARGET;

      return PS5_ENABLE_SRGB_CANDIDATE &&
             (render ? ps5_color_render_target(target) :
                       ps5_sampled_texture_target(target)) &&
             sample_count <= 1 && storage_sample_count <= 1 && bindings &&
             !(bindings & ~allowed) &&
             (!render || PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE) &&
             (!(render && (bindings & PIPE_BIND_SAMPLER_VIEW)) ||
              PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE);
   }

   if (format == PIPE_FORMAT_R8_UNORM ||
       format == PIPE_FORMAT_R8G8_UNORM) {
      const unsigned allowed = PIPE_BIND_RENDER_TARGET |
                               PIPE_BIND_SAMPLER_VIEW;
      const bool render = bindings & PIPE_BIND_RENDER_TARGET;

      return PS5_ENABLE_TEXTURE_RG_CANDIDATE &&
             (render ? (target == PIPE_TEXTURE_2D &&
                        PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE) :
                       ps5_sampled_texture_target(target)) &&
             sample_count <= 1 && storage_sample_count <= 1 && bindings &&
             !(bindings & ~allowed) &&
             (!(render && (bindings & PIPE_BIND_SAMPLER_VIEW)) ||
              PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE);
   }

   if (format == PIPE_FORMAT_R8_SNORM ||
       format == PIPE_FORMAT_R8G8_SNORM ||
       format == PIPE_FORMAT_R8G8B8A8_SNORM)
      return PS5_ENABLE_TEXTURE_SNORM_CANDIDATE &&
             ps5_sampled_texture_target(target) && sample_count <= 1 &&
             storage_sample_count <= 1 &&
             (bindings & PIPE_BIND_SAMPLER_VIEW) &&
             !(bindings & ~PIPE_BIND_SAMPLER_VIEW);

   if (format == PIPE_FORMAT_R16G16B16A16_FLOAT ||
       format == PIPE_FORMAT_R32G32B32A32_FLOAT)
      return PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE &&
             ps5_sampled_texture_target(target) && sample_count <= 1 &&
             storage_sample_count <= 1 &&
             (bindings & PIPE_BIND_SAMPLER_VIEW) &&
             !(bindings & ~PIPE_BIND_SAMPLER_VIEW);

   if (format == PIPE_FORMAT_R9G9B9E5_FLOAT)
      return PS5_ENABLE_SHARED_EXPONENT_CANDIDATE &&
             ps5_sampled_texture_target(target) && sample_count <= 1 &&
             storage_sample_count <= 1 &&
             (bindings & PIPE_BIND_SAMPLER_VIEW) &&
             !(bindings & ~PIPE_BIND_SAMPLER_VIEW);

   if (format == PIPE_FORMAT_R11G11B10_FLOAT) {
      const unsigned allowed = PIPE_BIND_RENDER_TARGET |
                               PIPE_BIND_SAMPLER_VIEW;
      const bool render = bindings & PIPE_BIND_RENDER_TARGET;

      return PS5_ENABLE_PACKED_FLOAT_CANDIDATE &&
             (render ? (target == PIPE_TEXTURE_2D &&
                        PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE) :
                       ps5_sampled_texture_target(target)) &&
             sample_count <= 1 && storage_sample_count <= 1 && bindings &&
             !(bindings & ~allowed) &&
             (!(render && (bindings & PIPE_BIND_SAMPLER_VIEW)) ||
              PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE);
   }

   if (ps5_integer_texture_format(format)) {
      const unsigned allowed = PIPE_BIND_RENDER_TARGET |
                               PIPE_BIND_SAMPLER_VIEW;
      const bool render = bindings & PIPE_BIND_RENDER_TARGET;

      return PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE &&
             (render ? (target == PIPE_TEXTURE_2D &&
                        PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE) :
                       ps5_sampled_texture_target(target)) &&
             sample_count <= 1 && storage_sample_count <= 1 && bindings &&
             !(bindings & ~allowed) &&
             (!(render && (bindings & PIPE_BIND_SAMPLER_VIEW)) ||
              PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE);
   }

   if (format == PIPE_FORMAT_R10G10B10A2_UINT) {
      const unsigned allowed = PIPE_BIND_RENDER_TARGET |
                               PIPE_BIND_SAMPLER_VIEW;
      const bool render = bindings & PIPE_BIND_RENDER_TARGET;

      return PS5_ENABLE_RGB10_A2UI_CANDIDATE &&
             (render ? (target == PIPE_TEXTURE_2D &&
                        PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE) :
                       ps5_sampled_texture_target(target)) &&
             sample_count <= 1 && storage_sample_count <= 1 && bindings &&
             !(bindings & ~allowed) &&
             (!(render && (bindings & PIPE_BIND_SAMPLER_VIEW)) ||
              PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE);
   }

   if ((target == PIPE_TEXTURE_2D_ARRAY ||
        target == PIPE_TEXTURE_CUBE || target == PIPE_TEXTURE_3D) &&
       PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE)
      return format == PIPE_FORMAT_R8G8B8A8_UNORM && sample_count <= 1 &&
             storage_sample_count <= 1 && bindings &&
             (bindings & PIPE_BIND_SAMPLER_VIEW) &&
             !(bindings & ~(PIPE_BIND_RENDER_TARGET |
                            PIPE_BIND_SAMPLER_VIEW));

   if (target != PIPE_TEXTURE_2D)
      return ps5_sampled_texture_target(target) &&
             format == PIPE_FORMAT_R8G8B8A8_UNORM && sample_count <= 1 &&
             storage_sample_count <= 1 &&
             (bindings & PIPE_BIND_SAMPLER_VIEW) &&
             !(bindings & ~PIPE_BIND_SAMPLER_VIEW);

   return (format == PIPE_FORMAT_R8G8B8A8_UNORM ||
           (format == PIPE_FORMAT_B8G8R8A8_UNORM &&
            !(bindings & PIPE_BIND_SAMPLER_VIEW))) &&
          target == PIPE_TEXTURE_2D && sample_count <= 1 &&
          storage_sample_count <= 1 &&
          (PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE ||
           !((bindings & PIPE_BIND_RENDER_TARGET) &&
             (bindings & PIPE_BIND_SAMPLER_VIEW))) &&
          !(bindings & ~supported_bindings);
}

static bool
ps5_map_bounds(const struct ps5_resource *resource, unsigned level,
               const struct pipe_box *box, size_t *offset)
{
   uint64_t end;
   unsigned block_size;
   unsigned block_width;
   unsigned block_height;
   unsigned layers;

   if (!box || box->x < 0 || box->y < 0 || box->z < 0 || box->width < 0 ||
       box->height < 0 || box->depth < 1)
      return false;
   if (resource->base.nr_samples > 1)
      return false;

   block_size = ps5_texture_format_size(resource->base.format);
   block_width = util_format_get_blockwidth(resource->base.format);
   block_height = util_format_get_blockheight(resource->base.format);

   if (resource->base.target == PIPE_BUFFER) {
      if (level)
         return false;
      end = (uint64_t)(unsigned)box->x + (unsigned)box->width;
      if (box->y || box->z || box->height != 1 || box->depth != 1 ||
          end > resource->size)
         return false;
      *offset = (unsigned)box->x;
      return true;
   }

   if (level > resource->base.last_level)
      return false;
   layers = resource->base.target == PIPE_TEXTURE_3D
               ? MAX2(resource->base.depth0 >> level, 1u)
               : resource->base.array_size;
   if ((uint64_t)(unsigned)box->x + (unsigned)box->width >
          (resource->base.width0 >> level ? resource->base.width0 >> level : 1) ||
       (uint64_t)(unsigned)box->y + (unsigned)box->height >
          (resource->base.height0 >> level ? resource->base.height0 >> level : 1) ||
       (uint64_t)(unsigned)box->z + (unsigned)box->depth >
          layers)
      return false;

   if ((unsigned)box->x % block_width ||
       (unsigned)box->y % block_height)
      return false;

   *offset = (size_t)(unsigned)box->z * resource->layer_stride +
              resource->level_offset[level] +
              (size_t)((unsigned)box->y / block_height) *
                 resource->level_stride[level] +
              (size_t)((unsigned)box->x / block_width) * block_size;
   return true;
}

static size_t
ps5_tiled_affine_offset(unsigned x, unsigned y,
                        const uint16_t *x_masks, unsigned x_bits,
                        const uint16_t *y_masks, unsigned y_bits)
{
   size_t offset = 0;

   for (unsigned bit = 0; bit < x_bits; ++bit) {
      if (x & BITFIELD_BIT(bit))
         offset ^= x_masks[bit];
   }
   for (unsigned bit = 0; bit < y_bits; ++bit) {
      if (y & BITFIELD_BIT(bit))
         offset ^= y_masks[bit];
   }
   return offset;
}

static size_t
ps5_tiled_depth_offset(unsigned x, unsigned y, unsigned width)
{
   static const uint16_t x_masks[7] = {
      0x0004, 0x0010, 0x0040, 0x0100, 0x2200, 0x0800, 0x8400,
   };
   static const uint16_t y_masks[7] = {
      0x0008, 0x0020, 0x0080, 0x1100, 0x0200, 0x0400, 0x4800,
   };
   size_t local = ps5_tiled_affine_offset(
      x & 127u, y & 127u, x_masks, ARRAY_SIZE(x_masks),
      y_masks, ARRAY_SIZE(y_masks));

   return ((((size_t)y >> 7) * ((width + 127u) >> 7) + (x >> 7)) << 16) +
          local;
}

static size_t
ps5_tiled_depth_msaa4_offset(unsigned x, unsigned y, unsigned sample,
                             unsigned width)
{
   static const uint16_t x_masks[7] = {
      0x0010, 0x0040, 0x8000, 0x0100, 0x2200, 0x0800, 0x0400,
   };
   static const uint16_t y_masks[8] = {
      0x0020, 0x0080, 0x4000, 0x1100,
      0x0200, 0x0400, 0x4800, 0x8000,
   };
   static const uint16_t sample_masks[2] = {0x0004, 0x0008};
   size_t local;

   if (sample >= 4)
      return SIZE_MAX;
   local = ps5_tiled_affine_offset(x, y, x_masks, ARRAY_SIZE(x_masks),
                                   y_masks, ARRAY_SIZE(y_masks));
   for (unsigned bit = 0; bit < ARRAY_SIZE(sample_masks); ++bit) {
      if (sample & BITFIELD_BIT(bit))
         local ^= sample_masks[bit];
   }
   return ((((size_t)y >> 6) * ((width + 63u) >> 6) + (x >> 6)) << 16) +
          local;
}

static size_t
ps5_tiled_stencil_offset(unsigned x, unsigned y, unsigned width)
{
   static const uint16_t x_masks[8] = {
      0x0001, 0x0004, 0x0010, 0x0140,
      0x0200, 0x0800, 0x2400, 0x8000,
   };
   static const uint16_t y_masks[8] = {
      0x0002, 0x0008, 0x0020, 0x0100,
      0x0280, 0x0400, 0x1800, 0x4000,
   };
   size_t local = ps5_tiled_affine_offset(
      x & 255u, y & 255u, x_masks, ARRAY_SIZE(x_masks),
      y_masks, ARRAY_SIZE(y_masks));

   return ((((size_t)y >> 8) * ((width + 255u) >> 8) + (x >> 8)) << 16) +
          local;
}

static size_t
ps5_tiled_stencil_msaa4_offset(unsigned x, unsigned y, unsigned sample,
                               unsigned width)
{
   static const uint16_t x_masks[7] = {
      0x0004, 0x0010, 0x0040, 0x0100, 0x2200, 0x0800, 0x8400,
   };
   static const uint16_t y_masks[7] = {
      0x0008, 0x0020, 0x0080, 0x1100, 0x0200, 0x0400, 0x4800,
   };
   static const uint16_t sample_masks[2] = {0x0001, 0x0002};
   size_t local;

   if (sample >= 4)
      return SIZE_MAX;
   local = ps5_tiled_affine_offset(x, y, x_masks, ARRAY_SIZE(x_masks),
                                   y_masks, ARRAY_SIZE(y_masks));
   for (unsigned bit = 0; bit < ARRAY_SIZE(sample_masks); ++bit) {
      if (sample & BITFIELD_BIT(bit))
         local ^= sample_masks[bit];
   }
   return ((((size_t)y >> 7) * ((width + 127u) >> 7) + (x >> 7)) << 16) +
          local;
}

static size_t
ps5_tiled_color_offset(enum pipe_format format, unsigned x, unsigned y,
                       unsigned width)
{
   /* Exhaustive GPU coordinate ramps in the 2026-08-30 tile-layout receipt
    * validated every address in one 64 KiB swizzle-27 tile. */
   static const uint16_t r8_x_masks[8] = {
      0x0001, 0x0002, 0x0004, 0x0140,
      0x0200, 0x0800, 0x2400, 0x8000,
   };
   static const uint16_t r8_y_masks[8] = {
      0x0010, 0x0008, 0x0020, 0x0100,
      0x0280, 0x0400, 0x1800, 0x4000,
   };
   static const uint16_t rg8_x_masks[8] = {
      0x0001, 0x0002, 0x0004, 0x00c0,
      0x0100, 0x0400, 0x1200, 0x4000,
   };
   static const uint16_t rg8_y_masks[7] = {
      0x0008, 0x0010, 0x0020, 0x0080,
      0x0900, 0x0200, 0x2400,
   };
   static const uint16_t rgba8_x_masks[7] = {
      0x0004, 0x0008, 0x0080, 0x0100, 0x2200, 0x0800, 0x8400,
   };
   static const uint16_t rgba8_y_masks[7] = {
      0x0010, 0x0020, 0x0040, 0x1100, 0x0200, 0x0400, 0x4800,
   };
   /* AMD GFX10 AddressLib's non-RBPlus 16-pipe SW_64K_R_X equation
    * uniquely reproduces the hardware-proven R8, RG8 and RGBA8 ramps.
    * The corresponding 128-bpp equation locally addresses each 64x64 tile;
    * its seventh X/Y terms apply the inter-tile XOR. */
   static const uint16_t rgba32_x_masks[7] = {
      0x0010, 0x0040, 0x2000, 0x0100, 0x8200, 0x0800, 0x0400,
   };
   static const uint16_t rgba32_y_masks[7] = {
      0x0020, 0x0080, 0x1000, 0x4100, 0x0200, 0x0400, 0x0800,
   };
   static const uint16_t rgba16_x_masks[7] = {
      0x0008, 0x0020, 0x0040, 0x2100, 0x0200, 0x0800, 0x8400,
   };
   static const uint16_t rgba16_y_masks[7] = {
      0x0010, 0x0080, 0x1000, 0x0100, 0x4200, 0x0400, 0x0800,
   };
   unsigned tile_width;
   unsigned tile_height;
   size_t local;

   switch (util_format_get_blocksize(format)) {
   case 1:
      tile_width = 256;
      tile_height = 256;
      local = ps5_tiled_affine_offset(x & 255u, y & 255u,
                                      r8_x_masks, 8, r8_y_masks, 8);
      break;
   case 2:
      tile_width = 256;
      tile_height = 128;
      local = ps5_tiled_affine_offset(x & 255u, y & 127u,
                                      rg8_x_masks, 8, rg8_y_masks, 7) << 1;
      break;
   case 4:
      tile_width = 128;
      tile_height = 128;
      local = ps5_tiled_affine_offset(x & 127u, y & 127u,
                                      rgba8_x_masks, 7, rgba8_y_masks, 7);
      break;
   case 8:
      tile_width = 128;
      tile_height = 64;
      local = ps5_tiled_affine_offset(x, y, rgba16_x_masks, 7,
                                      rgba16_y_masks, 7);
      break;
   case 16:
      tile_width = 64;
      tile_height = 64;
      local = ps5_tiled_affine_offset(x, y, rgba32_x_masks, 7,
                                      rgba32_y_masks, 7);
      break;
   default:
      return SIZE_MAX;
   }
   return ((((size_t)y / tile_height) *
               ((width + tile_width - 1u) / tile_width) +
            x / tile_width) << 16) + local;
}

static size_t
ps5_tiled_color_msaa4_offset(enum pipe_format format, unsigned x, unsigned y,
                             unsigned sample, unsigned width)
{
   static const uint16_t bpe1_x_masks[7] = {
      0x0001, 0x0002, 0x0004, 0x0140, 0x0200, 0x0800, 0x2400,
   };
   static const uint16_t bpe1_y_masks[9] = {
      0x0010, 0x0008, 0x0020, 0x0100, 0x0280,
      0x0400, 0x1800, 0x4000, 0x8000,
   };
   static const uint16_t bpe1_sample_masks[2] = {0x4000, 0x8000};
   static const uint16_t bpe2_x_masks[7] = {
      0x0002, 0x0004, 0x0008, 0x0180, 0x0200, 0x0800, 0x2400,
   };
   static const uint16_t bpe2_y_masks[8] = {
      0x0010, 0x0020, 0x0040, 0x0100,
      0x1200, 0x0400, 0x4800, 0x8000,
   };
   static const uint16_t bpe2_sample_masks[2] = {0x4000, 0x8000};
   static const uint16_t bpe4_x_masks[7] = {
      0x0004, 0x0008, 0x0080, 0x0100, 0x2200, 0x0800, 0x0400,
   };
   static const uint16_t bpe4_y_masks[8] = {
      0x0010, 0x0020, 0x0040, 0x1100,
      0x0200, 0x0400, 0x4800, 0x8000,
   };
   static const uint16_t bpe4_sample_masks[2] = {0x4000, 0x8000};
   static const uint16_t bpe8_x_masks[7] = {
      0x0008, 0x0020, 0x0040, 0x2100, 0x0200, 0x0800, 0x0400,
   };
   static const uint16_t bpe8_y_masks[7] = {
      0x0010, 0x0080, 0x1000, 0x0100, 0x4200, 0x0400, 0x8800,
   };
   static const uint16_t bpe8_sample_masks[2] = {0x8000, 0x0400};
   static const uint16_t bpe16_x_masks[7] = {
      0x0010, 0x0040, 0x2000, 0x0100, 0x8200, 0x0800, 0x0400,
   };
   static const uint16_t bpe16_y_masks[7] = {
      0x0020, 0x0080, 0x1000, 0x4100, 0x0200, 0x0400, 0x0800,
   };
   static const uint16_t bpe16_sample_masks[2] = {0x0800, 0x0400};
   const uint16_t *x_masks;
   const uint16_t *y_masks;
   const uint16_t *sample_masks;
   unsigned x_bits;
   unsigned y_bits;
   unsigned tile_width;
   unsigned tile_height;
   size_t local;

   if (sample >= 4 ||
       !ps5_tiled_color_msaa4_tile(format, &tile_width, &tile_height))
      return SIZE_MAX;
   switch (util_format_get_blocksize(format)) {
   case 1:
      x_masks = bpe1_x_masks;
      y_masks = bpe1_y_masks;
      sample_masks = bpe1_sample_masks;
      x_bits = ARRAY_SIZE(bpe1_x_masks);
      y_bits = ARRAY_SIZE(bpe1_y_masks);
      break;
   case 2:
      x_masks = bpe2_x_masks;
      y_masks = bpe2_y_masks;
      sample_masks = bpe2_sample_masks;
      x_bits = ARRAY_SIZE(bpe2_x_masks);
      y_bits = ARRAY_SIZE(bpe2_y_masks);
      break;
   case 4:
      x_masks = bpe4_x_masks;
      y_masks = bpe4_y_masks;
      sample_masks = bpe4_sample_masks;
      x_bits = ARRAY_SIZE(bpe4_x_masks);
      y_bits = ARRAY_SIZE(bpe4_y_masks);
      break;
   case 8:
      x_masks = bpe8_x_masks;
      y_masks = bpe8_y_masks;
      sample_masks = bpe8_sample_masks;
      x_bits = ARRAY_SIZE(bpe8_x_masks);
      y_bits = ARRAY_SIZE(bpe8_y_masks);
      break;
   case 16:
      x_masks = bpe16_x_masks;
      y_masks = bpe16_y_masks;
      sample_masks = bpe16_sample_masks;
      x_bits = ARRAY_SIZE(bpe16_x_masks);
      y_bits = ARRAY_SIZE(bpe16_y_masks);
      break;
   default:
      return SIZE_MAX;
   }
   local = ps5_tiled_affine_offset(x, y, x_masks, x_bits,
                                   y_masks, y_bits);
   for (unsigned bit = 0; bit < 2; ++bit) {
      if (sample & BITFIELD_BIT(bit))
         local ^= sample_masks[bit];
   }
   return ((((size_t)y / tile_height) *
               ((width + tile_width - 1u) / tile_width) +
            x / tile_width) << 16) + local;
}

static unsigned
ps5_tiled_rgba8_width(const struct ps5_resource *resource)
{
   return PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE
             ? resource->base.width0 : PS5_RENDER_WIDTH;
}

static unsigned
ps5_surface_width(const struct pipe_surface *surface)
{
   return MAX2(surface->texture->width0 >> surface->level, 1u);
}

static unsigned
ps5_surface_height(const struct pipe_surface *surface)
{
   return MAX2(surface->texture->height0 >> surface->level, 1u);
}

static unsigned
ps5_surface_layer_count(const struct pipe_surface *surface)
{
   return ps5_texture_level_layers(surface->texture, surface->level);
}

static bool
ps5_stage_color_surface(const struct pipe_surface *surface, bool to_staging)
{
   struct ps5_resource *resource;
   unsigned format_size;
   unsigned width;
   unsigned height;
   size_t tiled_layer_size;
   uint8_t *staging;

   if (!surface || !surface->texture)
      return false;
   resource = (struct ps5_resource *)surface->texture;
   if (!resource->render_staging_size ||
       surface->level > resource->base.last_level ||
       surface->first_layer > surface->last_layer ||
       surface->last_layer >= ps5_surface_layer_count(surface) ||
       !(format_size = ps5_texture_format_size(surface->format)))
      return false;
   width = ps5_surface_width(surface);
   height = ps5_surface_height(surface);
   tiled_layer_size = ps5_tiled_color_surface_size(
      surface->format, width, height);
   staging = resource->data + resource->render_staging_offset;
   if (!tiled_layer_size ||
       surface->last_layer - surface->first_layer + 1u >
          resource->render_staging_size / tiled_layer_size ||
       resource->render_staging_offset >= resource->allocation_size ||
       resource->render_staging_size >
          resource->allocation_size - resource->render_staging_offset)
      return false;

   if (!to_staging)
      ps5_flush_gpu_data(staging, resource->render_staging_size);
   for (unsigned layer = surface->first_layer;
        layer <= surface->last_layer; ++layer) {
      size_t linear_base = (size_t)layer * resource->layer_stride +
                           resource->level_offset[surface->level];
      size_t tiled_base =
         (size_t)(layer - surface->first_layer) * tiled_layer_size;

      for (unsigned y = 0; y < height; ++y) {
         for (unsigned x = 0; x < width; ++x) {
            size_t linear = linear_base +
               (size_t)y * resource->level_stride[surface->level] +
               (size_t)x * format_size;
            size_t tiled = tiled_base +
               ps5_tiled_color_offset(surface->format, x, y, width);

            if (linear > resource->size ||
                resource->size - linear < format_size ||
                tiled > resource->render_staging_size ||
                resource->render_staging_size - tiled < format_size)
               return false;
            if (to_staging)
               memcpy(staging + tiled, resource->data + linear, format_size);
            else
               memcpy(resource->data + linear, staging + tiled, format_size);
         }
      }
   }
   if (to_staging)
      ps5_flush_gpu_data(staging, resource->render_staging_size);
   else
      ps5_flush_gpu_data(resource->data, resource->size);
   return true;
}

static bool
ps5_stage_depth_surface(const struct pipe_surface *surface, bool to_staging)
{
   struct ps5_resource *resource;
   bool packed;
   unsigned width;
   unsigned height;
   unsigned format_size;
   size_t tiled_layer_size;
   size_t stencil_layer_size;
   uint8_t *staging;

   if (!surface || !surface->texture)
      return false;
   resource = (struct ps5_resource *)surface->texture;
   packed = resource->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
   if (!resource->depth_staging_size ||
       (resource->base.format != PIPE_FORMAT_Z32_FLOAT && !packed) ||
       surface->level > resource->base.last_level ||
       surface->first_layer > surface->last_layer ||
       surface->last_layer >= ps5_surface_layer_count(surface) ||
       resource->depth_staging_offset >= resource->allocation_size ||
       resource->depth_staging_size >
          resource->allocation_size - resource->depth_staging_offset)
      return false;

   width = ps5_surface_width(surface);
   height = ps5_surface_height(surface);
   format_size = ps5_texture_format_size(resource->base.format);
   tiled_layer_size = ps5_tiled_depth_surface_size(width, height, 1);
   stencil_layer_size = ps5_tiled_stencil_surface_size(width, height);
   staging = resource->data + resource->depth_staging_offset;
   if (!format_size || !tiled_layer_size ||
       tiled_layer_size > resource->depth_staging_size ||
       surface->last_layer >
          resource->depth_staging_size / tiled_layer_size - 1u ||
       (packed &&
        (!resource->stencil_data || !stencil_layer_size ||
         stencil_layer_size > resource->stencil_allocation_size ||
         surface->last_layer >
            resource->stencil_allocation_size / stencil_layer_size - 1u)))
      return false;

   if (!to_staging) {
      ps5_flush_gpu_data(staging, resource->depth_staging_size);
      if (packed)
         ps5_flush_gpu_data(resource->stencil_data,
                            resource->stencil_allocation_size);
   }
   for (unsigned layer = surface->first_layer;
        layer <= surface->last_layer; ++layer) {
      size_t linear_base = (size_t)layer * resource->layer_stride +
                           resource->level_offset[surface->level];
      size_t tiled_base = (size_t)layer * tiled_layer_size;
      size_t stencil_base = (size_t)layer * stencil_layer_size;

      for (unsigned y = 0; y < height; ++y) {
         for (unsigned x = 0; x < width; ++x) {
            size_t linear = linear_base +
                            (size_t)y * resource->level_stride[surface->level] +
                            (size_t)x * format_size;
            size_t tiled = tiled_base +
                           ps5_tiled_depth_offset(x, y, width);
            size_t stencil = stencil_base +
                             ps5_tiled_stencil_offset(x, y, width);

            if (linear > resource->size ||
                resource->size - linear < format_size ||
                tiled > resource->depth_staging_size ||
                resource->depth_staging_size - tiled < sizeof(float) ||
                (packed && stencil >= resource->stencil_allocation_size))
               return false;
            if (to_staging) {
               memcpy(staging + tiled, resource->data + linear,
                      sizeof(float));
               if (packed)
                  resource->stencil_data[stencil] =
                     resource->data[linear + sizeof(float)];
            } else {
               memcpy(resource->data + linear, staging + tiled,
                      sizeof(float));
               if (packed)
                  resource->data[linear + sizeof(float)] =
                     resource->stencil_data[stencil];
            }
         }
      }
   }
   if (to_staging) {
      ps5_flush_gpu_data(staging, resource->depth_staging_size);
      if (packed)
         ps5_flush_gpu_data(resource->stencil_data,
                            resource->stencil_allocation_size);
   } else {
      ps5_flush_gpu_data(resource->data, resource->size);
   }
   return true;
}

static void *
ps5_transfer_map(struct pipe_context *context, struct pipe_resource *base,
                 unsigned level, unsigned usage, const struct pipe_box *box,
                 struct pipe_transfer **out_transfer)
{
   struct ps5_resource *resource = (struct ps5_resource *)base;
   struct ps5_transfer *transfer;
   size_t offset;
   unsigned format_size;

   (void)context;
   if (!out_transfer || !ps5_map_bounds(resource, level, box, &offset))
      return NULL;

   transfer = calloc(1, sizeof(*transfer));
   if (!transfer)
      return NULL;

   transfer->base.resource = base;
   transfer->base.level = level;
   transfer->base.usage = usage;
   transfer->base.box = *box;
   transfer->base.stride = resource->level_stride[level];
   transfer->base.layer_stride = resource->layer_stride;
   transfer->base.offset = (unsigned)offset;
   *out_transfer = &transfer->base;

   if ((usage & (PIPE_MAP_READ | PIPE_MAP_WRITE)) &&
       (resource->base.bind & PIPE_BIND_DEPTH_STENCIL) &&
       !resource->depth_staging_size &&
       (resource->base.format == PIPE_FORMAT_Z32_FLOAT ||
        resource->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)) {
      const bool packed =
         resource->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
      const size_t depth_layer_size = ps5_tiled_depth_surface_size(
         resource->base.width0, resource->base.height0, 1);
      const size_t stencil_layer_size = ps5_tiled_stencil_surface_size(
         resource->base.width0, resource->base.height0);
      size_t staging_stride;
      size_t staging_layer_stride;
      size_t staging_size;

      format_size = ps5_texture_format_size(resource->base.format);
      staging_stride = (size_t)(unsigned)box->width * format_size;
      if (!format_size || level || box->width <= 0 || box->height <= 0 ||
          staging_stride > SIZE_MAX / (unsigned)box->height) {
         free(transfer);
         *out_transfer = NULL;
         return NULL;
      }
      staging_layer_stride = staging_stride * (unsigned)box->height;
      if (staging_layer_stride > SIZE_MAX / (unsigned)box->depth) {
         free(transfer);
         *out_transfer = NULL;
         return NULL;
      }
      staging_size = staging_layer_stride * (unsigned)box->depth;
      transfer->staging = malloc(staging_size);
      if (!transfer->staging ||
          (packed && (!resource->stencil_data ||
                      resource->stencil_allocation_size <
                         stencil_layer_size *
                         ps5_texture_level_layers(&resource->base, 0)))) {
         free(transfer->staging);
         free(transfer);
         *out_transfer = NULL;
         return NULL;
      }
      if (usage & PIPE_MAP_READ) {
         ps5_flush_gpu_data(resource->data, resource->allocation_size);
         if (packed)
            ps5_flush_gpu_data(resource->stencil_data,
                               resource->stencil_allocation_size);
         for (unsigned z = 0; z < (unsigned)box->depth; ++z) {
            const size_t depth_layer =
               ((size_t)(unsigned)box->z + z) * depth_layer_size;
            const size_t stencil_layer =
               ((size_t)(unsigned)box->z + z) * stencil_layer_size;

            for (unsigned y = 0; y < (unsigned)box->height; ++y) {
               for (unsigned x = 0; x < (unsigned)box->width; ++x) {
                  const size_t depth_offset = depth_layer +
                     ps5_tiled_depth_offset((unsigned)box->x + x,
                                            (unsigned)box->y + y,
                                            resource->base.width0);
                  uint8_t *pixel = (uint8_t *)transfer->staging +
                     z * staging_layer_stride + y * staging_stride +
                     x * format_size;

                  if (depth_offset > resource->allocation_size ||
                      resource->allocation_size - depth_offset < 4) {
                     free(transfer->staging);
                     free(transfer);
                     *out_transfer = NULL;
                     return NULL;
                  }
                  memset(pixel, 0, format_size);
                  memcpy(pixel, resource->data + depth_offset, 4);
                  if (packed) {
                     const size_t stencil_offset = stencil_layer +
                        ps5_tiled_stencil_offset((unsigned)box->x + x,
                           (unsigned)box->y + y, resource->base.width0);

                     if (stencil_offset >=
                           resource->stencil_allocation_size) {
                        free(transfer->staging);
                        free(transfer);
                        *out_transfer = NULL;
                        return NULL;
                     }
                     pixel[4] = resource->stencil_data[stencil_offset];
                  }
               }
            }
         }
      }
      transfer->base.stride = staging_stride;
      transfer->base.layer_stride = staging_layer_stride;
      transfer->base.offset = 0;
      return transfer->staging;
   }

   if ((usage & (PIPE_MAP_READ | PIPE_MAP_WRITE)) &&
       (resource->base.bind & PIPE_BIND_RENDER_TARGET) &&
       !ps5_linear_sampled_layout(&resource->base)) {
      size_t staging_stride;
      size_t staging_size;
      unsigned y;

      format_size = ps5_texture_format_size(resource->base.format);
      if (level || box->depth != 1 ||
          !ps5_render_target_format(resource->base.format) ||
          (!(PS5_ENABLE_PADDED_FBO_CANDIDATE ||
             PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE) &&
           (resource->base.width0 != PS5_RENDER_WIDTH ||
            resource->base.height0 != PS5_RENDER_HEIGHT)) ||
          box->width <= 0 || box->height <= 0) {
         free(transfer);
         *out_transfer = NULL;
         return NULL;
      }
      staging_stride = (size_t)(unsigned)box->width * format_size;
      if (staging_stride > SIZE_MAX / (unsigned)box->height) {
         free(transfer);
         *out_transfer = NULL;
         return NULL;
      }
      staging_size = staging_stride * (unsigned)box->height;
      transfer->staging = malloc(staging_size);
      if (!transfer->staging) {
         free(transfer);
         *out_transfer = NULL;
         return NULL;
      }
      if (usage & PIPE_MAP_READ) {
         const size_t layer_base =
            (size_t)(unsigned)box->z * resource->layer_stride;

         ps5_flush_gpu_data(resource->data, resource->allocation_size);
         for (y = 0; y < (unsigned)box->height; ++y) {
            unsigned x;

            for (x = 0; x < (unsigned)box->width; ++x) {
               size_t tiled = layer_base + ps5_tiled_color_offset(
                  resource->base.format,
                  (unsigned)box->x + x, (unsigned)box->y + y,
                  ps5_tiled_rgba8_width(resource));

               if (tiled > resource->allocation_size ||
                   resource->allocation_size - tiled < format_size) {
                  free(transfer->staging);
                  free(transfer);
                  *out_transfer = NULL;
                  return NULL;
               }
               memcpy((uint8_t *)transfer->staging +
                         y * staging_stride + x * format_size,
                      resource->data + tiled, format_size);
            }
         }
      }
      transfer->base.stride = staging_stride;
      transfer->base.layer_stride = staging_size;
      transfer->base.offset = 0;
      return transfer->staging;
   }
   if ((usage & PIPE_MAP_READ) && resource->base.target == PIPE_BUFFER)
      ps5_flush_gpu_data(resource->data + offset, (size_t)box->width);
   return resource->data + offset;
}

static void
ps5_transfer_flush_region(struct pipe_context *context,
                          struct pipe_transfer *transfer,
                          const struct pipe_box *box)
{
   (void)context;
   (void)transfer;
   (void)box;
}

static void
ps5_transfer_unmap(struct pipe_context *context,
                   struct pipe_transfer *transfer)
{
   struct ps5_transfer *ps5 = (struct ps5_transfer *)transfer;
   struct ps5_resource *resource =
      (struct ps5_resource *)transfer->resource;

   (void)context;
   if (ps5->staging && (transfer->usage & PIPE_MAP_WRITE) &&
       (transfer->resource->bind & PIPE_BIND_DEPTH_STENCIL) &&
       !resource->depth_staging_size &&
       (transfer->resource->format == PIPE_FORMAT_Z32_FLOAT ||
        transfer->resource->format ==
           PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)) {
      const bool packed = resource->base.format ==
                          PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
      const size_t depth_layer_size = ps5_tiled_depth_surface_size(
         resource->base.width0, resource->base.height0, 1);
      const size_t stencil_layer_size = ps5_tiled_stencil_surface_size(
         resource->base.width0, resource->base.height0);
      const unsigned format_size =
         ps5_texture_format_size(resource->base.format);

      for (unsigned z = 0; z < (unsigned)transfer->box.depth; ++z) {
         const size_t depth_layer =
            ((size_t)(unsigned)transfer->box.z + z) * depth_layer_size;
         const size_t stencil_layer =
            ((size_t)(unsigned)transfer->box.z + z) * stencil_layer_size;

         for (unsigned y = 0; y < (unsigned)transfer->box.height; ++y) {
            for (unsigned x = 0; x < (unsigned)transfer->box.width; ++x) {
               const size_t depth_offset = depth_layer +
                  ps5_tiled_depth_offset((unsigned)transfer->box.x + x,
                     (unsigned)transfer->box.y + y,
                     resource->base.width0);
               const uint8_t *pixel = (const uint8_t *)ps5->staging +
                  z * transfer->layer_stride + y * transfer->stride +
                  x * format_size;

               if (depth_offset <= resource->allocation_size &&
                   resource->allocation_size - depth_offset >= 4)
                  memcpy(resource->data + depth_offset, pixel, 4);
               if (packed) {
                  const size_t stencil_offset = stencil_layer +
                     ps5_tiled_stencil_offset(
                        (unsigned)transfer->box.x + x,
                        (unsigned)transfer->box.y + y,
                        resource->base.width0);

                  if (stencil_offset < resource->stencil_allocation_size)
                     resource->stencil_data[stencil_offset] = pixel[4];
               }
            }
         }
      }
      ps5_flush_gpu_data(resource->data, resource->allocation_size);
      if (packed)
         ps5_flush_gpu_data(resource->stencil_data,
                            resource->stencil_allocation_size);
   } else if (ps5->staging && (transfer->usage & PIPE_MAP_WRITE)) {
      size_t layer_base =
         (size_t)(unsigned)transfer->box.z * resource->layer_stride;
      unsigned format_size = ps5_texture_format_size(resource->base.format);

      for (unsigned y = 0; y < (unsigned)transfer->box.height; ++y) {
         for (unsigned x = 0; x < (unsigned)transfer->box.width; ++x) {
            size_t tiled = layer_base + ps5_tiled_color_offset(
               resource->base.format,
               (unsigned)transfer->box.x + x,
               (unsigned)transfer->box.y + y,
               ps5_tiled_rgba8_width(resource));

            if (tiled <= resource->allocation_size &&
                resource->allocation_size - tiled >= format_size)
               memcpy(resource->data + tiled,
                      (uint8_t *)ps5->staging +
                         y * transfer->stride + x * format_size,
                      format_size);
         }
      }
      ps5_flush_gpu_data(resource->data, resource->allocation_size);
   }
   free(ps5->staging);
   free(ps5);
}

static void
ps5_blit_scissor_bounds(const struct pipe_blit_info *info,
                        unsigned *min_x, unsigned *min_y,
                        unsigned *max_x, unsigned *max_y)
{
   if (info->scissor_enable) {
      *min_x = info->scissor.minx;
      *min_y = info->scissor.miny;
      *max_x = info->scissor.maxx;
      *max_y = info->scissor.maxy;
   } else {
      *min_x = 0;
      *min_y = 0;
      *max_x = UINT_MAX;
      *max_y = UINT_MAX;
   }
}

static bool
ps5_color_view_format_compatible(enum pipe_format storage,
                                 enum pipe_format view)
{
   return storage == view ||
          util_format_linear(storage) == util_format_linear(view);
}

static void
ps5_resolve_color_msaa4(struct pipe_context *context,
                        const struct pipe_blit_info *info)
{
   struct ps5_context *ps5 = (struct ps5_context *)context;
   struct ps5_resource *source = (struct ps5_resource *)info->src.resource;
   const bool integer = util_format_is_pure_integer(info->src.format);
   const bool sample0_only = info->sample0_only || integer;
   struct pipe_transfer *dst_transfer = NULL;
   size_t source_layer_base;
   uint8_t *dst;
   unsigned min_x, min_y, max_x, max_y;
   unsigned src_width;
   unsigned src_height;
   unsigned src_pixel_size;
   unsigned dst_pixel_size;
   int64_t src_x;
   int64_t src_y;

   if (info->mask != PIPE_MASK_RGBA || info->src.level || info->dst.level ||
       !ps5_color_view_format_compatible(info->src.resource->format,
                                         info->src.format) ||
       !ps5_color_view_format_compatible(info->dst.resource->format,
                                         info->dst.format) ||
       !ps5_msaa4_color_format(info->src.format) ||
       !ps5_render_target_format(info->dst.format) ||
       info->src.resource->nr_samples != 4 ||
       info->src.resource->nr_storage_samples != 4 ||
       info->dst.resource->nr_samples > 1 || info->dst_sample ||
       info->num_window_rectangles ||
       info->alpha_blend || info->filter != PIPE_TEX_FILTER_NEAREST ||
       !info->src.box.width || !info->src.box.height ||
       info->src.box.width == INT_MIN ||
       info->src.box.height == INT_MIN ||
       info->dst.box.x < 0 || info->dst.box.y < 0 ||
       info->dst.box.width <= 0 || info->dst.box.height <= 0 ||
       info->src.box.depth != 1 || info->dst.box.depth != 1 ||
       (unsigned)abs(info->src.box.width) !=
          (unsigned)info->dst.box.width ||
       (unsigned)abs(info->src.box.height) !=
          (unsigned)info->dst.box.height ||
       info->src.box.z < 0 ||
       (unsigned)info->src.box.z >= source->base.array_size ||
       (unsigned)info->dst.box.x + (unsigned)info->dst.box.width >
          info->dst.resource->width0 ||
       (unsigned)info->dst.box.y + (unsigned)info->dst.box.height >
          info->dst.resource->height0) {
      printf("[ps5-gallium] msaa4-resolve rejected\n");
      return;
   }
   src_width = (unsigned)abs(info->src.box.width);
   src_height = (unsigned)abs(info->src.box.height);
   src_x = info->src.box.x;
   src_y = info->src.box.y;
   if (info->src.box.width < 0)
      src_x += info->src.box.width;
   if (info->src.box.height < 0)
      src_y += info->src.box.height;
   if (src_x < 0 || src_y < 0 ||
       (uint64_t)src_x + src_width > source->base.width0 ||
       (uint64_t)src_y + src_height > source->base.height0) {
      printf("[ps5-gallium] msaa4-resolve rejected\n");
      return;
   }
   if (info->render_condition_enable &&
       !ps5_render_condition_passes(ps5))
      return;

   dst = ps5_transfer_map(context, info->dst.resource, info->dst.level,
                          PIPE_MAP_WRITE |
                             (info->scissor_enable ? PIPE_MAP_READ : 0),
                          &info->dst.box, &dst_transfer);
   if (!dst)
      return;
   src_pixel_size = ps5_texture_format_size(info->src.format);
   dst_pixel_size = ps5_texture_format_size(info->dst.format);
   if (!src_pixel_size || !dst_pixel_size) {
      ps5_transfer_unmap(context, dst_transfer);
      return;
   }
   source_layer_base = (size_t)(unsigned)info->src.box.z *
                       source->layer_stride;
   ps5_blit_scissor_bounds(info, &min_x, &min_y, &max_x, &max_y);
   ps5_flush_gpu_data(source->data, source->allocation_size);
   for (unsigned y = 0; y < (unsigned)info->dst.box.height; ++y) {
      for (unsigned x = 0; x < (unsigned)info->dst.box.width; ++x) {
         union pipe_color_union resolved = {{0}};
         unsigned dst_x = (unsigned)info->dst.box.x + x;
         unsigned dst_y = (unsigned)info->dst.box.y + y;

         if (dst_x < min_x || dst_x >= max_x ||
             dst_y < min_y || dst_y >= max_y)
            continue;

         for (unsigned sample = 0; sample < (sample0_only ? 1u : 4u);
              ++sample) {
            union pipe_color_union value;
            unsigned source_x = (unsigned)src_x +
               (info->src.box.width < 0 ? src_width - 1u - x : x);
            unsigned source_y = (unsigned)src_y +
               (info->src.box.height < 0 ? src_height - 1u - y : y);
            size_t offset = source_layer_base +
               ps5_tiled_color_msaa4_offset(
                  info->src.format, source_x, source_y, sample,
                  source->base.width0);

            if (offset > source->allocation_size ||
               source->allocation_size - offset < src_pixel_size) {
               ps5_transfer_unmap(context, dst_transfer);
               return;
            }
            for (unsigned channel = 0; channel < 4; ++channel)
               value.ui[channel] = 0;
            util_format_unpack_rgba(info->src.format, value.ui,
                                    source->data + offset, 1);
            if (sample0_only) {
               resolved = value;
            } else {
               for (unsigned channel = 0; channel < 4; ++channel)
                  resolved.f[channel] += value.f[channel] * 0.25f;
            }
         }
         if (info->swizzle_enable) {
            union pipe_color_union swizzled;

            util_format_apply_color_swizzle(&swizzled, &resolved,
                                            info->swizzle, integer);
            resolved = swizzled;
         }
         util_format_pack_rgba(info->dst.format,
                               dst + (size_t)y * dst_transfer->stride +
                                  x * dst_pixel_size,
                               resolved.ui, 1);
      }
   }
   ps5_transfer_unmap(context, dst_transfer);
   printf("[ps5-gallium] msaa4-resolve color=%dx%d\n",
          info->dst.box.width, info->dst.box.height);
}

static void
ps5_resolve_depth_stencil_msaa4(struct pipe_context *context,
                                const struct pipe_blit_info *info)
{
   struct ps5_context *ps5 = (struct ps5_context *)context;
   struct ps5_resource *source = (struct ps5_resource *)info->src.resource;
   struct ps5_resource *destination =
      (struct ps5_resource *)info->dst.resource;
   const bool packed = source &&
      source->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
   unsigned min_x, min_y, max_x, max_y;
   size_t source_depth_size;
   size_t destination_depth_size;
   size_t source_stencil_size;
   size_t destination_stencil_size;
   unsigned src_width;
   unsigned src_height;
   int64_t src_x;
   int64_t src_y;

   if (!source || !destination || !(info->mask & PIPE_MASK_ZS) ||
       (info->mask & ~PIPE_MASK_ZS) || info->src.level || info->dst.level ||
       info->src.box.z || info->dst.box.z ||
       info->src.box.depth != 1 || info->dst.box.depth != 1 ||
       !info->src.box.width || !info->src.box.height ||
       info->src.box.width == INT_MIN || info->src.box.height == INT_MIN ||
       info->dst.box.x < 0 || info->dst.box.y < 0 ||
       info->dst.box.width <= 0 || info->dst.box.height <= 0 ||
       (unsigned)abs(info->src.box.width) !=
          (unsigned)info->dst.box.width ||
       (unsigned)abs(info->src.box.height) !=
          (unsigned)info->dst.box.height ||
       info->src.format != info->dst.format ||
       info->src.format != source->base.format ||
       info->dst.format != destination->base.format ||
       (source->base.format != PIPE_FORMAT_Z32_FLOAT && !packed) ||
       ((info->mask & PIPE_MASK_S) && !packed) ||
       source->base.target != PIPE_TEXTURE_2D ||
       destination->base.target != PIPE_TEXTURE_2D ||
       source->base.nr_samples != 4 ||
       source->base.nr_storage_samples != 4 ||
       destination->base.nr_samples > 1 || info->dst_sample ||
       info->sample0_only || info->swizzle_enable ||
       info->num_window_rectangles || info->alpha_blend ||
       info->filter != PIPE_TEX_FILTER_NEAREST ||
       (uint64_t)(unsigned)info->dst.box.x +
          (unsigned)info->dst.box.width > destination->base.width0 ||
       (uint64_t)(unsigned)info->dst.box.y +
          (unsigned)info->dst.box.height > destination->base.height0) {
      printf("[ps5-gallium] msaa4-resolve depth-stencil rejected\n");
      return;
   }
   src_width = (unsigned)abs(info->src.box.width);
   src_height = (unsigned)abs(info->src.box.height);
   src_x = info->src.box.x;
   src_y = info->src.box.y;
   if (info->src.box.width < 0)
      src_x += info->src.box.width;
   if (info->src.box.height < 0)
      src_y += info->src.box.height;
   if (src_x < 0 || src_y < 0 ||
       (uint64_t)src_x + src_width > source->base.width0 ||
       (uint64_t)src_y + src_height > source->base.height0) {
      printf("[ps5-gallium] msaa4-resolve depth-stencil rejected\n");
      return;
   }
   if (info->render_condition_enable && !ps5_render_condition_passes(ps5))
      return;

   source_depth_size = ps5_tiled_depth_surface_size(
      source->base.width0, source->base.height0, 4);
   destination_depth_size = ps5_tiled_depth_surface_size(
      destination->base.width0, destination->base.height0, 1);
   source_stencil_size = ps5_tiled_stencil_surface_size_samples(
      source->base.width0, source->base.height0, 4);
   destination_stencil_size = ps5_tiled_stencil_surface_size_samples(
      destination->base.width0, destination->base.height0, 1);
   if (source->allocation_size < source_depth_size ||
       destination->allocation_size < destination_depth_size ||
       ((info->mask & PIPE_MASK_S) &&
        (!source->stencil_data || !destination->stencil_data ||
         source->stencil_allocation_size < source_stencil_size ||
         destination->stencil_allocation_size < destination_stencil_size))) {
      printf("[ps5-gallium] msaa4-resolve depth-stencil rejected\n");
      return;
   }

   ps5_blit_scissor_bounds(info, &min_x, &min_y, &max_x, &max_y);
   if (info->mask & PIPE_MASK_Z)
      ps5_flush_gpu_data(source->data, source_depth_size);
   if (info->mask & PIPE_MASK_S)
      ps5_flush_gpu_data(source->stencil_data, source_stencil_size);
   for (unsigned y = 0; y < (unsigned)info->dst.box.height; ++y) {
      for (unsigned x = 0; x < (unsigned)info->dst.box.width; ++x) {
         unsigned source_x = (unsigned)src_x +
            (info->src.box.width < 0 ? src_width - 1u - x : x);
         unsigned source_y = (unsigned)src_y +
            (info->src.box.height < 0 ? src_height - 1u - y : y);
         unsigned dst_x = (unsigned)info->dst.box.x + x;
         unsigned dst_y = (unsigned)info->dst.box.y + y;

         if (dst_x < min_x || dst_x >= max_x ||
             dst_y < min_y || dst_y >= max_y)
            continue;
         if (info->mask & PIPE_MASK_Z) {
            size_t src_offset = ps5_tiled_depth_msaa4_offset(
               source_x, source_y, 0, source->base.width0);
            size_t dst_offset = ps5_tiled_depth_offset(
               dst_x, dst_y, destination->base.width0);

            if (src_offset > source_depth_size ||
                source_depth_size - src_offset < sizeof(float) ||
                dst_offset > destination_depth_size ||
                destination_depth_size - dst_offset < sizeof(float))
               return;
            memcpy(destination->data + dst_offset,
                   source->data + src_offset, sizeof(float));
         }
         if (info->mask & PIPE_MASK_S) {
            size_t src_offset = ps5_tiled_stencil_msaa4_offset(
               source_x, source_y, 0, source->base.width0);
            size_t dst_offset = ps5_tiled_stencil_offset(
               dst_x, dst_y, destination->base.width0);

            if (src_offset >= source_stencil_size ||
                dst_offset >= destination_stencil_size)
               return;
            destination->stencil_data[dst_offset] =
               source->stencil_data[src_offset];
         }
      }
   }
   if (info->mask & PIPE_MASK_Z)
      ps5_flush_gpu_data(destination->data, destination_depth_size);
   if (info->mask & PIPE_MASK_S)
      ps5_flush_gpu_data(destination->stencil_data,
                         destination_stencil_size);
   printf("[ps5-gallium] msaa4-resolve depth-stencil=%dx%d mask=%x\n",
          info->dst.box.width, info->dst.box.height, info->mask);
}

static void
ps5_replicate_depth_stencil_msaa4(struct pipe_context *context,
                                  const struct pipe_blit_info *info)
{
   struct ps5_context *ps5 = (struct ps5_context *)context;
   struct ps5_resource *source = (struct ps5_resource *)info->src.resource;
   struct ps5_resource *destination =
      (struct ps5_resource *)info->dst.resource;
   const bool packed = source &&
      source->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
   unsigned min_x, min_y, max_x, max_y;
   size_t source_depth_size;
   size_t destination_depth_size;
   size_t source_stencil_size;
   size_t destination_stencil_size;

   if (!source || !destination || !(info->mask & PIPE_MASK_ZS) ||
       (info->mask & ~PIPE_MASK_ZS) || info->src.level || info->dst.level ||
       info->src.box.z || info->dst.box.z ||
       info->src.box.depth != 1 || info->dst.box.depth != 1 ||
       info->src.box.width <= 0 || info->src.box.height <= 0 ||
       info->src.box.width != info->dst.box.width ||
       info->src.box.height != info->dst.box.height ||
       info->src.box.x < 0 || info->src.box.y < 0 ||
       info->dst.box.x < 0 || info->dst.box.y < 0 ||
       info->src.format != info->dst.format ||
       info->src.format != source->base.format ||
       info->dst.format != destination->base.format ||
       (source->base.format != PIPE_FORMAT_Z32_FLOAT && !packed) ||
       ((info->mask & PIPE_MASK_S) && !packed) ||
       source->base.target != PIPE_TEXTURE_2D ||
       destination->base.target != PIPE_TEXTURE_2D ||
       source->base.nr_samples > 1 ||
       destination->base.nr_samples != 4 ||
       destination->base.nr_storage_samples != 4 || info->dst_sample ||
       info->sample0_only || info->swizzle_enable ||
       info->num_window_rectangles || info->alpha_blend ||
       info->filter != PIPE_TEX_FILTER_NEAREST ||
       (uint64_t)(unsigned)info->src.box.x +
          (unsigned)info->src.box.width > source->base.width0 ||
       (uint64_t)(unsigned)info->src.box.y +
          (unsigned)info->src.box.height > source->base.height0 ||
       (uint64_t)(unsigned)info->dst.box.x +
          (unsigned)info->dst.box.width > destination->base.width0 ||
       (uint64_t)(unsigned)info->dst.box.y +
          (unsigned)info->dst.box.height > destination->base.height0) {
      printf("[ps5-gallium] msaa4-replicate depth-stencil rejected\n");
      return;
   }
   if (info->render_condition_enable && !ps5_render_condition_passes(ps5))
      return;

   source_depth_size = ps5_tiled_depth_surface_size(
      source->base.width0, source->base.height0, 1);
   destination_depth_size = ps5_tiled_depth_surface_size(
      destination->base.width0, destination->base.height0, 4);
   source_stencil_size = ps5_tiled_stencil_surface_size_samples(
      source->base.width0, source->base.height0, 1);
   destination_stencil_size = ps5_tiled_stencil_surface_size_samples(
      destination->base.width0, destination->base.height0, 4);
   if (source->allocation_size < source_depth_size ||
       destination->allocation_size < destination_depth_size ||
       ((info->mask & PIPE_MASK_S) &&
        (!source->stencil_data || !destination->stencil_data ||
         source->stencil_allocation_size < source_stencil_size ||
         destination->stencil_allocation_size < destination_stencil_size))) {
      printf("[ps5-gallium] msaa4-replicate depth-stencil rejected\n");
      return;
   }

   ps5_blit_scissor_bounds(info, &min_x, &min_y, &max_x, &max_y);
   if (info->mask & PIPE_MASK_Z)
      ps5_flush_gpu_data(source->data, source_depth_size);
   if (info->mask & PIPE_MASK_S)
      ps5_flush_gpu_data(source->stencil_data, source_stencil_size);
   for (unsigned y = 0; y < (unsigned)info->dst.box.height; ++y) {
      for (unsigned x = 0; x < (unsigned)info->dst.box.width; ++x) {
         unsigned src_x = (unsigned)info->src.box.x + x;
         unsigned src_y = (unsigned)info->src.box.y + y;
         unsigned dst_x = (unsigned)info->dst.box.x + x;
         unsigned dst_y = (unsigned)info->dst.box.y + y;
         size_t src_depth_offset = 0;
         size_t src_stencil_offset = 0;

         if (dst_x < min_x || dst_x >= max_x ||
             dst_y < min_y || dst_y >= max_y)
            continue;
         if (info->mask & PIPE_MASK_Z) {
            src_depth_offset = ps5_tiled_depth_offset(
               src_x, src_y, source->base.width0);
            if (src_depth_offset > source_depth_size ||
                source_depth_size - src_depth_offset < sizeof(float))
               return;
         }
         if (info->mask & PIPE_MASK_S) {
            src_stencil_offset = ps5_tiled_stencil_offset(
               src_x, src_y, source->base.width0);
            if (src_stencil_offset >= source_stencil_size)
               return;
         }
         for (unsigned sample = 0; sample < 4; ++sample) {
            if (info->mask & PIPE_MASK_Z) {
               size_t dst_offset = ps5_tiled_depth_msaa4_offset(
                  dst_x, dst_y, sample, destination->base.width0);

               if (dst_offset > destination_depth_size ||
                   destination_depth_size - dst_offset < sizeof(float))
                  return;
               memcpy(destination->data + dst_offset,
                      source->data + src_depth_offset, sizeof(float));
            }
            if (info->mask & PIPE_MASK_S) {
               size_t dst_offset = ps5_tiled_stencil_msaa4_offset(
                  dst_x, dst_y, sample, destination->base.width0);

               if (dst_offset >= destination_stencil_size)
                  return;
               destination->stencil_data[dst_offset] =
                  source->stencil_data[src_stencil_offset];
            }
         }
      }
   }
   if (info->mask & PIPE_MASK_Z)
      ps5_flush_gpu_data(destination->data, destination_depth_size);
   if (info->mask & PIPE_MASK_S)
      ps5_flush_gpu_data(destination->stencil_data,
                         destination_stencil_size);
   printf("[ps5-gallium] msaa4-replicate depth-stencil=%dx%d mask=%x\n",
          info->dst.box.width, info->dst.box.height, info->mask);
}

static void
ps5_blit(struct pipe_context *context, const struct pipe_blit_info *info)
{
   struct ps5_context *ps5 = (struct ps5_context *)context;
   struct ps5_resource *dst_resource;
   struct pipe_transfer *src_transfer = NULL;
   struct pipe_transfer *dst_transfer = NULL;
   struct pipe_box src_box;
   uint8_t *src;
   uint8_t *dst;
   unsigned src_width;
   unsigned src_height;
   unsigned src_pixel_size;
   unsigned dst_pixel_size;
   int64_t src_x;
   int64_t src_y;
   bool direct_tiled_dst;
   unsigned min_x, min_y, max_x, max_y;

   if (PS5_ENABLE_MSAA4_CANDIDATE && info && info->src.resource &&
       info->src.resource->nr_samples == 4) {
      if (info->mask & PIPE_MASK_ZS)
         ps5_resolve_depth_stencil_msaa4(context, info);
      else
         ps5_resolve_color_msaa4(context, info);
      return;
   }
   if (PS5_ENABLE_MSAA4_CANDIDATE && info && info->dst.resource &&
       info->dst.resource->nr_samples == 4 &&
       (info->mask & PIPE_MASK_ZS)) {
      ps5_replicate_depth_stencil_msaa4(context, info);
      return;
   }

   if (info && (info->mask & PIPE_MASK_ZS)) {
      struct ps5_resource *source =
         (struct ps5_resource *)info->src.resource;
      struct ps5_resource *destination =
         (struct ps5_resource *)info->dst.resource;
      const unsigned source_width = source ? source->base.width0 : 0;
      const unsigned source_height = source ? source->base.height0 : 0;
      const unsigned destination_width =
         destination ? destination->base.width0 : 0;
      const unsigned destination_height =
         destination ? destination->base.height0 : 0;
      const size_t source_depth_size =
         ps5_tiled_surface_size(source_width, source_height);
      const size_t destination_depth_size =
         ps5_tiled_surface_size(destination_width, destination_height);
      const size_t source_stencil_size =
         ps5_tiled_stencil_surface_size(source_width, source_height);
      const size_t destination_stencil_size =
         ps5_tiled_stencil_surface_size(destination_width,
                                        destination_height);
      const bool packed = source &&
         source->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
      unsigned source_box_width = 0;
      unsigned source_box_height = 0;
      int64_t source_x = 0;
      int64_t source_y = 0;

      if (!source || !destination ||
          !(info->mask & PIPE_MASK_ZS) ||
          (info->mask & ~PIPE_MASK_ZS) ||
          info->src.level || info->dst.level ||
          info->src.box.z || info->dst.box.z ||
          info->src.box.depth != 1 || info->dst.box.depth != 1 ||
          !info->src.box.width || !info->src.box.height ||
          info->src.box.width == INT_MIN ||
          info->src.box.height == INT_MIN ||
          info->dst.box.width <= 0 || info->dst.box.height <= 0 ||
          info->src.format != info->dst.format ||
          info->src.format != source->base.format ||
          info->dst.format != destination->base.format ||
          (source->base.format != PIPE_FORMAT_Z32_FLOAT && !packed) ||
          ((info->mask & PIPE_MASK_S) && !packed) ||
          source->base.target != PIPE_TEXTURE_2D ||
          destination->base.target != PIPE_TEXTURE_2D ||
          source->base.nr_samples > 1 ||
          destination->base.nr_samples > 1 || info->dst_sample ||
          info->sample0_only ||
          info->swizzle_enable || info->num_window_rectangles ||
          info->alpha_blend || info->filter != PIPE_TEX_FILTER_NEAREST ||
          source->allocation_size < source_depth_size ||
          destination->allocation_size < destination_depth_size ||
          ((info->mask & PIPE_MASK_S) &&
           (!source->stencil_data || !destination->stencil_data ||
            source->stencil_allocation_size < source_stencil_size ||
            destination->stencil_allocation_size <
               destination_stencil_size))) {
         printf("[ps5-gallium] software-blit depth-stencil rejected\n");
         return;
      }
      source_box_width = info->src.box.width < 0
                            ? (unsigned)-info->src.box.width
                            : (unsigned)info->src.box.width;
      source_box_height = info->src.box.height < 0
                             ? (unsigned)-info->src.box.height
                             : (unsigned)info->src.box.height;
      source_x = info->src.box.x;
      source_y = info->src.box.y;
      if (info->src.box.width < 0)
         source_x += info->src.box.width;
      if (info->src.box.height < 0)
         source_y += info->src.box.height;
      if (source_x < 0 || source_y < 0 || info->dst.box.x < 0 ||
          info->dst.box.y < 0 ||
          (uint64_t)source_x + source_box_width > source_width ||
          (uint64_t)source_y + source_box_height > source_height ||
          (uint64_t)(unsigned)info->dst.box.x +
             (unsigned)info->dst.box.width > destination_width ||
          (uint64_t)(unsigned)info->dst.box.y +
             (unsigned)info->dst.box.height > destination_height) {
         printf("[ps5-gallium] software-blit depth-stencil rejected\n");
         return;
      }
      if (info->render_condition_enable &&
          !ps5_render_condition_passes(ps5))
         return;
      ps5_blit_scissor_bounds(info, &min_x, &min_y, &max_x, &max_y);
      if (info->mask & PIPE_MASK_Z)
         ps5_flush_gpu_data(source->data, source_depth_size);
      if (info->mask & PIPE_MASK_S)
         ps5_flush_gpu_data(source->stencil_data, source_stencil_size);
      for (unsigned y = 0; y < (unsigned)info->dst.box.height; ++y) {
         unsigned sy = (unsigned)(((UINT64_C(2) * y + 1u) *
                                   source_box_height) /
                                  (UINT64_C(2) *
                                   (unsigned)info->dst.box.height));

         if (info->src.box.height < 0)
            sy = source_box_height - 1u - sy;
         for (unsigned x = 0; x < (unsigned)info->dst.box.width; ++x) {
            unsigned sx = (unsigned)(((UINT64_C(2) * x + 1u) *
                                      source_box_width) /
                                     (UINT64_C(2) *
                                      (unsigned)info->dst.box.width));
            unsigned src_px;
            unsigned src_py = (unsigned)source_y + sy;
            unsigned dst_px = (unsigned)info->dst.box.x + x;
            unsigned dst_py = (unsigned)info->dst.box.y + y;

            if (dst_px < min_x || dst_px >= max_x ||
                dst_py < min_y || dst_py >= max_y)
               continue;
            if (info->src.box.width < 0)
               sx = source_box_width - 1u - sx;
            src_px = (unsigned)source_x + sx;
            if (info->mask & PIPE_MASK_Z) {
               size_t src_offset = ps5_tiled_depth_offset(
                  src_px, src_py, source_width);
               size_t dst_offset = ps5_tiled_depth_offset(
                  dst_px, dst_py, destination_width);

               if (src_offset > source_depth_size ||
                   source_depth_size - src_offset < sizeof(float) ||
                   dst_offset > destination_depth_size ||
                   destination_depth_size - dst_offset < sizeof(float)) {
                  printf("[ps5-gallium] software-blit depth-stencil rejected\n");
                  return;
               }
               memmove(destination->data + dst_offset,
                       source->data + src_offset, sizeof(float));
            }
            if (info->mask & PIPE_MASK_S) {
               size_t src_offset = ps5_tiled_stencil_offset(
                  src_px, src_py, source_width);
               size_t dst_offset = ps5_tiled_stencil_offset(
                  dst_px, dst_py, destination_width);

               if (src_offset >= source_stencil_size ||
                   dst_offset >= destination_stencil_size) {
                  printf("[ps5-gallium] software-blit depth-stencil rejected\n");
                  return;
               }
               destination->stencil_data[dst_offset] =
                  source->stencil_data[src_offset];
            }
         }
      }
      if (info->mask & PIPE_MASK_Z)
         ps5_flush_gpu_data(destination->data, destination_depth_size);
      if (info->mask & PIPE_MASK_S)
         ps5_flush_gpu_data(destination->stencil_data,
                            destination_stencil_size);
      printf("[ps5-gallium] software-blit depth-stencil=%ux%u->%dx%d mask=%x\n",
             source_box_width, source_box_height, info->dst.box.width,
             info->dst.box.height, info->mask);
      return;
   }

   if (!info || !info->src.resource || !info->dst.resource ||
       info->mask != PIPE_MASK_RGBA || info->src.level || info->dst.level ||
       info->src.box.depth != 1 || info->dst.box.depth != 1 ||
       info->dst.box.width <= 0 || info->dst.box.height <= 0 ||
       !info->src.box.width || !info->src.box.height ||
       info->src.box.width == INT_MIN || info->src.box.height == INT_MIN ||
       !ps5_color_view_format_compatible(info->src.resource->format,
                                         info->src.format) ||
       !ps5_color_view_format_compatible(info->dst.resource->format,
                                         info->dst.format) ||
       info->src.resource->nr_samples > 1 ||
       info->dst.resource->nr_samples > 1 || info->dst_sample ||
       info->sample0_only || info->swizzle_enable ||
       info->num_window_rectangles || info->alpha_blend ||
       (info->filter != PIPE_TEX_FILTER_NEAREST &&
        info->filter != PIPE_TEX_FILTER_LINEAR) ||
       !ps5_render_target_format(info->src.format) ||
       !ps5_render_target_format(info->dst.format) ||
       util_format_is_pure_uint(info->src.format) !=
          util_format_is_pure_uint(info->dst.format) ||
       util_format_is_pure_sint(info->src.format) !=
          util_format_is_pure_sint(info->dst.format)) {
      printf("[ps5-gallium] software-blit rejected\n");
      return;
   }
   if (info->render_condition_enable &&
       !ps5_render_condition_passes(ps5))
      return;
   ps5_blit_scissor_bounds(info, &min_x, &min_y, &max_x, &max_y);

   src_width = info->src.box.width < 0
                  ? (unsigned)-info->src.box.width
                  : (unsigned)info->src.box.width;
   src_height = info->src.box.height < 0
                   ? (unsigned)-info->src.box.height
                   : (unsigned)info->src.box.height;
   if (info->filter == PIPE_TEX_FILTER_LINEAR &&
       (util_format_is_pure_integer(info->src.format) ||
        util_format_is_pure_integer(info->dst.format))) {
      printf("[ps5-gallium] software-blit rejected linear-format=%u\n",
             info->src.format);
      return;
   }
   src_x = info->src.box.x;
   src_y = info->src.box.y;
   if (info->src.box.width < 0)
      src_x += info->src.box.width;
   if (info->src.box.height < 0)
      src_y += info->src.box.height;
   if (src_x < INT_MIN || src_x > INT_MAX ||
       src_y < INT_MIN || src_y > INT_MAX)
      return;
   src_box = info->src.box;
   src_box.x = (int)src_x;
   src_box.y = (int)src_y;
   src_box.width = (int)src_width;
   src_box.height = (int)src_height;

   src_pixel_size = ps5_texture_format_size(info->src.format);
   dst_pixel_size = ps5_texture_format_size(info->dst.format);
   if (!src_pixel_size || !dst_pixel_size)
      return;
   src = ps5_transfer_map(context, info->src.resource, info->src.level,
                          PIPE_MAP_READ, &src_box, &src_transfer);
   if (!src)
      return;
   dst_resource = (struct ps5_resource *)info->dst.resource;
   direct_tiled_dst =
      (dst_resource->base.bind & PIPE_BIND_RENDER_TARGET) &&
      !ps5_linear_sampled_layout(&dst_resource->base);
   if (direct_tiled_dst) {
      size_t ignored_offset;

      if (!ps5_map_bounds(dst_resource, info->dst.level, &info->dst.box,
                          &ignored_offset)) {
         ps5_transfer_unmap(context, src_transfer);
         return;
      }
      dst = dst_resource->data;
   } else {
      dst = ps5_transfer_map(context, info->dst.resource, info->dst.level,
                             PIPE_MAP_WRITE |
                                (info->scissor_enable ? PIPE_MAP_READ : 0),
                             &info->dst.box, &dst_transfer);
      if (!dst) {
         ps5_transfer_unmap(context, src_transfer);
         return;
      }
   }

   for (unsigned y = 0; y < (unsigned)info->dst.box.height; ++y) {
      unsigned sy = (unsigned)(((UINT64_C(2) * y + 1u) * src_height) /
                               (UINT64_C(2) *
                                (unsigned)info->dst.box.height));
      if (info->src.box.height < 0)
         sy = src_height - 1u - sy;
      for (unsigned x = 0; x < (unsigned)info->dst.box.width; ++x) {
         unsigned sx = (unsigned)(((UINT64_C(2) * x + 1u) * src_width) /
                                  (UINT64_C(2) *
                                   (unsigned)info->dst.box.width));
         uint8_t *dst_pixel;
         unsigned dst_x = (unsigned)info->dst.box.x + x;
         unsigned dst_y = (unsigned)info->dst.box.y + y;

         if (info->src.box.width < 0)
            sx = src_width - 1u - sx;
         if (dst_x < min_x || dst_x >= max_x ||
             dst_y < min_y || dst_y >= max_y)
            continue;
         if (direct_tiled_dst) {
            size_t tiled =
               (size_t)(unsigned)info->dst.box.z *
                  dst_resource->layer_stride +
               ps5_tiled_color_offset(
               dst_resource->base.format,
               (unsigned)info->dst.box.x + x,
               (unsigned)info->dst.box.y + y,
               ps5_tiled_rgba8_width(dst_resource));

            if (tiled > dst_resource->allocation_size ||
                dst_resource->allocation_size - tiled < dst_pixel_size) {
               ps5_transfer_unmap(context, src_transfer);
               return;
            }
            dst_pixel = dst_resource->data + tiled;
         } else {
            dst_pixel = dst + (size_t)y * dst_transfer->stride +
                        (size_t)x * dst_pixel_size;
         }
         if (info->filter == PIPE_TEX_FILTER_LINEAR) {
            int64_t fx = (int64_t)(((UINT64_C(2) * x + 1u) * src_width
                                     << 16) /
                                    (UINT64_C(2) *
                                     (unsigned)info->dst.box.width)) -
                         INT64_C(0x8000);
            int64_t fy = (int64_t)(((UINT64_C(2) * y + 1u) * src_height
                                     << 16) /
                                    (UINT64_C(2) *
                                     (unsigned)info->dst.box.height)) -
                         INT64_C(0x8000);
            int64_t x0;
            int64_t y0;
            uint64_t wx1;
            uint64_t wy1;
            unsigned ix0;
            unsigned ix1;
            unsigned iy0;
            unsigned iy1;

            if (info->src.box.width < 0)
               fx = (int64_t)(src_width - 1u) * INT64_C(0x10000) - fx;
            if (info->src.box.height < 0)
               fy = (int64_t)(src_height - 1u) * INT64_C(0x10000) - fy;
            x0 = fx >= 0 ? fx / INT64_C(0x10000)
                         : -((-fx + INT64_C(0xffff)) / INT64_C(0x10000));
            y0 = fy >= 0 ? fy / INT64_C(0x10000)
                         : -((-fy + INT64_C(0xffff)) / INT64_C(0x10000));
            wx1 = (uint64_t)(fx - x0 * INT64_C(0x10000));
            wy1 = (uint64_t)(fy - y0 * INT64_C(0x10000));
            ix0 = (unsigned)CLAMP(x0, 0, (int64_t)src_width - 1);
            ix1 = (unsigned)CLAMP(x0 + 1, 0, (int64_t)src_width - 1);
            iy0 = (unsigned)CLAMP(y0, 0, (int64_t)src_height - 1);
            iy1 = (unsigned)CLAMP(y0 + 1, 0, (int64_t)src_height - 1);
            union pipe_color_union p00, p10, p01, p11, result;
            float tx = (float)wx1 / 65536.0f;
            float ty = (float)wy1 / 65536.0f;

            util_format_unpack_rgba(
               info->src.format, p00.ui,
               src + (size_t)iy0 * src_transfer->stride +
                  (size_t)ix0 * src_pixel_size, 1);
            util_format_unpack_rgba(
               info->src.format, p10.ui,
               src + (size_t)iy0 * src_transfer->stride +
                  (size_t)ix1 * src_pixel_size, 1);
            util_format_unpack_rgba(
               info->src.format, p01.ui,
               src + (size_t)iy1 * src_transfer->stride +
                  (size_t)ix0 * src_pixel_size, 1);
            util_format_unpack_rgba(
               info->src.format, p11.ui,
               src + (size_t)iy1 * src_transfer->stride +
                  (size_t)ix1 * src_pixel_size, 1);
            for (unsigned channel = 0; channel < 4; ++channel) {
               float top = p00.f[channel] +
                           (p10.f[channel] - p00.f[channel]) * tx;
               float bottom = p01.f[channel] +
                              (p11.f[channel] - p01.f[channel]) * tx;

               result.f[channel] = top + (bottom - top) * ty;
            }
            util_format_pack_rgba(info->dst.format, dst_pixel,
                                  result.ui, 1);
         } else {
            const uint8_t *src_pixel =
               src + (size_t)sy * src_transfer->stride +
                     (size_t)sx * src_pixel_size;

            if (info->src.format == info->dst.format) {
               memcpy(dst_pixel, src_pixel, src_pixel_size);
            } else {
               union pipe_color_union converted;

               util_format_unpack_rgba(info->src.format, converted.ui,
                                       src_pixel, 1);
               util_format_pack_rgba(info->dst.format, dst_pixel,
                                     converted.ui, 1);
            }
         }
      }
   }
   if (direct_tiled_dst)
      ps5_flush_gpu_data(dst_resource->data, dst_resource->allocation_size);
   else
      ps5_transfer_unmap(context, dst_transfer);
   ps5_transfer_unmap(context, src_transfer);
   printf("[ps5-gallium] software-blit color=%ux%u->%dx%d filter=%u\n",
          src_width, src_height, info->dst.box.width, info->dst.box.height,
          info->filter);
}

static void
ps5_texture_subdata(struct pipe_context *context,
                    struct pipe_resource *resource, unsigned level,
                    unsigned usage, const struct pipe_box *box,
                    const void *data, unsigned stride,
                    uintptr_t layer_stride)
{
   struct pipe_transfer *transfer = NULL;
   const uint8_t *source = data;
   uint8_t *destination;
   unsigned format_size;
   unsigned z;

   if (!resource || !box || !data || box->depth < 1)
      return;
   format_size = ps5_texture_format_size(resource->format);
   if (!format_size)
      return;
   destination = context->texture_map(
      context, resource, level, usage | PIPE_MAP_WRITE, box, &transfer);
   if (!destination || !transfer)
      return;

   for (z = 0; z < (unsigned)box->depth; ++z) {
      unsigned y;

      for (y = 0; y < (unsigned)box->height; ++y)
         memcpy(destination + z * transfer->layer_stride +
                   y * transfer->stride,
                source + z * layer_stride + y * stride,
                (size_t)box->width * format_size);
   }
   context->texture_unmap(context, transfer);
}

static bool
ps5_generate_mipmap(struct pipe_context *context,
                    struct pipe_resource *base,
                    enum pipe_format format,
                    unsigned base_level, unsigned last_level,
                    unsigned first_layer, unsigned last_layer)
{
   struct ps5_resource *resource = (struct ps5_resource *)base;
   unsigned format_size;

   (void)context;
   if (!PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE || !resource ||
       format != resource->base.format ||
       base_level >= last_level || last_level > resource->base.last_level ||
       !ps5_linear_sampled_layout(&resource->base) ||
       !ps5_sampled_texture_target(resource->base.target) ||
       !(format_size = ps5_texture_format_size(format)) ||
       util_format_is_compressed(format) ||
       util_format_is_pure_integer(format))
      return false;

   if (resource->base.target == PIPE_TEXTURE_3D) {
      unsigned base_depth = MAX2(resource->base.depth0 >> base_level, 1u);

      if (first_layer || last_layer + 1u < base_depth)
         return false;
   } else if (first_layer > last_layer ||
              last_layer >= resource->base.array_size) {
      return false;
   }

   ps5_flush_gpu_data(resource->data, resource->size);
   for (unsigned level = base_level + 1; level <= last_level; ++level) {
      unsigned src_width = MAX2(resource->base.width0 >> (level - 1), 1u);
      unsigned src_height = MAX2(resource->base.height0 >> (level - 1), 1u);
      unsigned dst_width = MAX2(resource->base.width0 >> level, 1u);
      unsigned dst_height = MAX2(resource->base.height0 >> level, 1u);
      unsigned dst_first = first_layer;
      unsigned dst_last = last_layer;
      unsigned src_depth = 1;
      unsigned dst_depth = 1;

      if (resource->base.target == PIPE_TEXTURE_3D) {
         src_depth = MAX2(resource->base.depth0 >> (level - 1), 1u);
         dst_depth = MAX2(resource->base.depth0 >> level, 1u);
         dst_first = 0;
         dst_last = dst_depth - 1;
      }

      for (unsigned layer = dst_first; layer <= dst_last; ++layer) {
         unsigned src_z0 = resource->base.target == PIPE_TEXTURE_3D
                              ? (unsigned)((uint64_t)layer * src_depth /
                                           dst_depth)
                              : layer;
         unsigned src_z1 = resource->base.target == PIPE_TEXTURE_3D
                              ? (unsigned)((uint64_t)(layer + 1u) * src_depth /
                                           dst_depth)
                              : layer + 1u;

         if (src_z1 <= src_z0)
            src_z1 = src_z0 + 1u;
         for (unsigned y = 0; y < dst_height; ++y) {
            unsigned src_y0 = (unsigned)((uint64_t)y * src_height /
                                         dst_height);
            unsigned src_y1 = (unsigned)((uint64_t)(y + 1u) * src_height /
                                         dst_height);

            if (src_y1 <= src_y0)
               src_y1 = src_y0 + 1u;
            for (unsigned x = 0; x < dst_width; ++x) {
               unsigned src_x0 = (unsigned)((uint64_t)x * src_width /
                                            dst_width);
               unsigned src_x1 = (unsigned)((uint64_t)(x + 1u) * src_width /
                                            dst_width);
               float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
               unsigned samples = 0;

               if (src_x1 <= src_x0)
                  src_x1 = src_x0 + 1u;
               for (unsigned z = src_z0; z < src_z1; ++z) {
                  for (unsigned sy = src_y0; sy < src_y1; ++sy) {
                     for (unsigned sx = src_x0; sx < src_x1; ++sx) {
                        const uint8_t *source =
                           resource->data +
                           (size_t)z * resource->layer_stride +
                           resource->level_offset[level - 1] +
                           (size_t)sy * resource->level_stride[level - 1] +
                           (size_t)sx * format_size;
                        float sample[4];

                        util_format_unpack_rgba(format, sample, source, 1);
                        for (unsigned component = 0; component < 4;
                             ++component)
                           sum[component] += sample[component];
                        samples++;
                     }
                  }
               }
               for (unsigned component = 0; component < 4; ++component)
                  sum[component] /= samples;
               util_format_pack_rgba(
                  format,
                  resource->data +
                     (size_t)layer * resource->layer_stride +
                     resource->level_offset[level] +
                     (size_t)y * resource->level_stride[level] +
                     (size_t)x * format_size,
                  sum, 1);
            }
         }
      }
   }
   ps5_flush_gpu_data(resource->data, resource->size);
   printf("[ps5-gallium] generate-mipmap format=%u levels=%u-%u layers=%u-%u\n",
          format, base_level, last_level, first_layer, last_layer);
   return true;
}

static void
ps5_fence_reference(struct pipe_screen *screen,
                    struct pipe_fence_handle **destination,
                    struct pipe_fence_handle *source)
{
   struct ps5_fence *old = (struct ps5_fence *)*destination;
   struct ps5_fence *next = (struct ps5_fence *)source;

   (void)screen;
   if (old == next)
      return;
   if (next)
      next->references++;
   if (old && --old->references == 0)
      free(old);
   *destination = source;
}

static bool
ps5_fence_finish(struct pipe_screen *screen, struct pipe_context *context,
                 struct pipe_fence_handle *fence, uint64_t timeout)
{
   /* ponytail: draws retire in ps5_agc_gate2_run before returning, so every
    * fence is already signaled. Replace this with queue-backed fences when
    * submission becomes asynchronous or multiple contexts are enabled. */
   (void)screen;
   (void)context;
   (void)fence;
   (void)timeout;
   return true;
}

static uint64_t
ps5_get_timestamp(struct pipe_screen *screen)
{
   (void)screen;
   return os_time_get_nano();
}

static struct pipe_query *
ps5_create_query(struct pipe_context *base, unsigned type, unsigned index)
{
   struct ps5_query *query;
   bool timer_query;
   bool occlusion_query;
   bool primitive_query;

   timer_query = PS5_ENABLE_TIMER_QUERY_CANDIDATE &&
      (type == PIPE_QUERY_TIMESTAMP ||
       type == PIPE_QUERY_TIMESTAMP_DISJOINT ||
       type == PIPE_QUERY_TIME_ELAPSED ||
       type == PIPE_QUERY_GPU_FINISHED);
   occlusion_query = PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE &&
      (type == PIPE_QUERY_OCCLUSION_COUNTER ||
       type == PIPE_QUERY_OCCLUSION_PREDICATE ||
       type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE);
   primitive_query = PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE &&
      (type == PIPE_QUERY_PRIMITIVES_GENERATED ||
       type == PIPE_QUERY_PRIMITIVES_EMITTED);
   if (index || (!timer_query && !occlusion_query && !primitive_query))
      return NULL;
   query = calloc(1, sizeof(*query));
   if (!query)
      return NULL;
   query->type = type;
   if (occlusion_query) {
      struct pipe_resource templ;

      memset(&templ, 0, sizeof(templ));
      templ.target = PIPE_BUFFER;
      templ.format = PIPE_FORMAT_R8_UNORM;
      templ.width0 = PS5_OCCLUSION_QUERY_BYTES;
      templ.height0 = 1;
      templ.depth0 = 1;
      templ.array_size = 1;
      query->buffer = base->screen->resource_create(base->screen, &templ);
      if (!query->buffer) {
         free(query);
         return NULL;
      }
   }
   return (struct pipe_query *)query;
}

static void
ps5_destroy_query(struct pipe_context *base, struct pipe_query *pipe_query)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct ps5_query *query = (struct ps5_query *)pipe_query;

   if (!query)
      return;
   if (context->active_occlusion_query == query)
      context->active_occlusion_query = NULL;
   if (context->active_primitives_generated_query == query)
      context->active_primitives_generated_query = NULL;
   if (context->active_primitives_emitted_query == query)
      context->active_primitives_emitted_query = NULL;
   if (context->render_condition_query == query)
      context->render_condition_query = NULL;
   pipe_resource_reference(&query->buffer, NULL);
   free(query);
}

static bool
ps5_begin_query(struct pipe_context *base, struct pipe_query *pipe_query)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct ps5_query *query = (struct ps5_query *)pipe_query;
   struct ps5_query **primitive_query;

   if (!query || query->active)
      return false;
   primitive_query = ps5_active_primitive_query(context, query->type);
   if (primitive_query) {
      if (!PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE || *primitive_query)
         return false;
      query->value = 0;
      query->active = true;
      query->ready = false;
      *primitive_query = query;
      return true;
   }
   if (query->type == PIPE_QUERY_OCCLUSION_COUNTER ||
       query->type == PIPE_QUERY_OCCLUSION_PREDICATE ||
       query->type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE) {
      if (!PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE || !query->buffer ||
          context->active_occlusion_query)
         return false;
      query->value = 0;
      query->active = true;
      query->ready = false;
      context->active_occlusion_query = query;
      return true;
   }
   if (query->type != PIPE_QUERY_TIME_ELAPSED)
      return false;
   query->start = os_time_get_nano();
   query->end = 0;
   query->active = true;
   query->ready = false;
   return true;
}

static bool
ps5_end_query(struct pipe_context *base, struct pipe_query *pipe_query)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct ps5_query *query = (struct ps5_query *)pipe_query;
   struct ps5_query **primitive_query;

   if (!query)
      return false;
   primitive_query = ps5_active_primitive_query(context, query->type);
   if (primitive_query) {
      if (!query->active || *primitive_query != query)
         return false;
      *primitive_query = NULL;
      query->active = false;
      query->ready = true;
      return true;
   }
   if (query->type == PIPE_QUERY_OCCLUSION_COUNTER ||
       query->type == PIPE_QUERY_OCCLUSION_PREDICATE ||
       query->type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE) {
      if (!query->active || context->active_occlusion_query != query)
         return false;
      context->active_occlusion_query = NULL;
      query->active = false;
      query->ready = true;
      return true;
   }
   if (query->type == PIPE_QUERY_TIME_ELAPSED) {
      if (!query->active)
         return false;
   } else if (query->active) {
      return false;
   }
   query->end = os_time_get_nano();
   query->active = false;
   query->ready = true;
   return true;
}

static bool
ps5_get_query_result(struct pipe_context *base,
                     struct pipe_query *pipe_query, bool wait,
                     union pipe_query_result *result)
{
   const struct ps5_query *query = (const struct ps5_query *)pipe_query;

   (void)base;
   (void)wait;
   if (!query || !query->ready || !result)
      return false;
   if (query->type == PIPE_QUERY_TIMESTAMP_DISJOINT) {
      result->timestamp_disjoint.frequency = UINT64_C(1000000000);
      result->timestamp_disjoint.disjoint = false;
   } else if (query->type == PIPE_QUERY_GPU_FINISHED) {
      result->b = true;
   } else if (query->type == PIPE_QUERY_OCCLUSION_PREDICATE ||
              query->type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE) {
      result->b = query->value != 0;
   } else if (query->type == PIPE_QUERY_OCCLUSION_COUNTER) {
      result->u64 = query->value;
   } else if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED ||
              query->type == PIPE_QUERY_PRIMITIVES_EMITTED) {
      result->u64 = query->value;
   } else {
      result->u64 = query->end - query->start;
   }
   return true;
}

static void
ps5_set_active_query_state(struct pipe_context *base, bool enable)
{
   ((struct ps5_context *)base)->queries_enabled = enable;
}

static void
ps5_render_condition(struct pipe_context *base, struct pipe_query *pipe_query,
                     bool condition, enum pipe_render_cond_flag mode)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct ps5_query *query = (struct ps5_query *)pipe_query;

   if (query && query->type != PIPE_QUERY_OCCLUSION_COUNTER &&
       query->type != PIPE_QUERY_OCCLUSION_PREDICATE &&
       query->type != PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE)
      return;
   (void)mode;
   context->render_condition_query = query;
   context->render_condition_inverted = condition;
}

static bool
ps5_render_condition_passes(const struct ps5_context *context)
{
   const struct ps5_query *query = context->render_condition_query;

   if (!query || !query->ready)
      return true;
   return (query->value != 0) != context->render_condition_inverted;
}

static bool
ps5_prepare_occlusion_query(struct ps5_query *query)
{
   struct ps5_resource *resource;
   uint64_t *samples;

   if (!query || !query->buffer)
      return false;
   resource = (struct ps5_resource *)query->buffer;
   if (!resource->data || resource->size < PS5_OCCLUSION_QUERY_BYTES)
      return false;
   samples = (uint64_t *)resource->data;
   for (unsigned rb = 0; rb < PS5_OCCLUSION_MAX_RBS; ++rb) {
      samples[rb * 2u] = PS5_OCCLUSION_VALID_BIT;
      samples[rb * 2u + 1u] = PS5_OCCLUSION_VALID_BIT;
   }
   ps5_flush_gpu_data(samples, PS5_OCCLUSION_QUERY_BYTES);
   return true;
}

static bool
ps5_collect_occlusion_query(struct ps5_query *query)
{
   struct ps5_resource *resource;
   const uint64_t *samples;
   uint64_t value = 0;

   if (!query || !query->buffer)
      return false;
   resource = (struct ps5_resource *)query->buffer;
   if (!resource->data || resource->size < PS5_OCCLUSION_QUERY_BYTES)
      return false;
   ps5_flush_gpu_data(resource->data, PS5_OCCLUSION_QUERY_BYTES);
   samples = (const uint64_t *)resource->data;
   for (unsigned rb = 0; rb < PS5_OCCLUSION_MAX_RBS; ++rb) {
      uint64_t start = samples[rb * 2u];
      uint64_t end = samples[rb * 2u + 1u];

      if ((start & PS5_OCCLUSION_VALID_BIT) &&
          (end & PS5_OCCLUSION_VALID_BIT))
         value += end - start;
   }
   query->value += value;
   return true;
}

static void
ps5_flush(struct pipe_context *context, struct pipe_fence_handle **out_fence,
          unsigned flags)
{
   struct ps5_fence *fence;

   (void)flags;
   if (!out_fence)
      return;

   fence = calloc(1, sizeof(*fence));
   if (!fence)
      return;
   fence->references = 1;
   context->screen->fence_reference(context->screen, out_fence,
                                    (struct pipe_fence_handle *)fence);
   /* Drop the creator reference; out_fence retains the remaining reference. */
   fence->references--;
}

static bool
ps5_draw_primitive(unsigned mode, unsigned count, uint32_t *primitive_type)
{
   if (!primitive_type)
      return false;

   switch (mode) {
   case MESA_PRIM_POINTS:
      *primitive_type = 1;
      return count >= 1;
   case MESA_PRIM_LINES:
      *primitive_type = 2;
      return count >= 2;
   case MESA_PRIM_LINE_STRIP:
      *primitive_type = 3;
      return count >= 2;
   case MESA_PRIM_TRIANGLES:
      *primitive_type = 4;
      return count >= 3;
   case MESA_PRIM_TRIANGLE_FAN:
      *primitive_type = 5;
      return count >= 3;
   case MESA_PRIM_TRIANGLE_STRIP:
      *primitive_type = 6;
      return count >= 3;
   case MESA_PRIM_LINES_ADJACENCY:
      *primitive_type = 10;
      return count >= 4;
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      *primitive_type = 11;
      return count >= 4;
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      *primitive_type = 12;
      return count >= 6;
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      *primitive_type = 13;
      return count >= 6;
   default:
      return false;
   }
}

static uint32_t
ps5_fragment_primitive_type(const struct ps5_context *context,
                            uint32_t draw_primitive_type)
{
   if (!context->gs)
      return draw_primitive_type;

   switch (context->gs->nir->info.gs.output_primitive) {
   case MESA_PRIM_POINTS:
      return 1;
   case MESA_PRIM_LINES:
   case MESA_PRIM_LINE_STRIP:
      return 3;
   case MESA_PRIM_TRIANGLES:
   case MESA_PRIM_TRIANGLE_STRIP:
      return 6;
   default:
      return 0;
   }
}

static uint32_t
ps5_index_value(const void *indices, unsigned index_size, unsigned index)
{
   return index_size == 2 ? ((const uint16_t *)indices)[index] :
                            ((const uint32_t *)indices)[index];
}

static void
ps5_log_indices(const void *indices, unsigned index_size, unsigned count,
                unsigned limit)
{
   unsigned logged = MIN2(count, limit);

   for (unsigned i = 0; i < logged; ++i)
      printf("%s%u", i ? "," : "",
             ps5_index_value(indices, index_size, i));
   printf("%s\n", logged < count ? ",..." : "");
}

static unsigned
ps5_streamout_vertices_per_primitive(enum mesa_prim primitive)
{
   switch (primitive) {
   case MESA_PRIM_POINTS: return 1;
   case MESA_PRIM_LINES: return 2;
   case MESA_PRIM_TRIANGLES: return 3;
   default: return 0;
   }
}

static uint64_t
ps5_draw_primitive_count(uint32_t primitive_type, unsigned vertex_count)
{
   switch (primitive_type) {
   case 1: return vertex_count;
   case 2: return vertex_count / 2u;
   case 3: return vertex_count > 1 ? vertex_count - 1u : 0;
   case 4: return vertex_count / 3u;
   case 5:
   case 6: return vertex_count > 2 ? vertex_count - 2u : 0;
   case 10: return vertex_count / 4u;
   case 11: return vertex_count > 3 ? vertex_count - 3u : 0;
   case 12: return vertex_count / 6u;
   case 13: return vertex_count > 4 ? (vertex_count - 4u) / 2u : 0;
   default: return 0;
   }
}

static bool
ps5_prepare_streamout(struct ps5_context *context,
                      const PsbcShaderMetadata *metadata,
                      struct ps5_resource *descriptor_resource,
                      uint32_t *user_data, unsigned user_data_count,
                      uint32_t primitive_type, unsigned draw_count,
                      unsigned instance_count, uint32_t *enabled_mask,
                      uint32_t size_dwords[4], uint32_t stride_dwords[4],
                      uint32_t offset_dwords[4], uint64_t *written_vertices)
{
   uint32_t *descriptors = (uint32_t *)(descriptor_resource->data +
                                        PS5_STREAMOUT_DESCRIPTOR_OFFSET);
   struct ps5_streamout_control *control =
      (struct ps5_streamout_control *)(descriptor_resource->data +
                                       PS5_STREAMOUT_CONTROL_OFFSET);
   const bool global_control = context->gs != NULL;
   uintptr_t table_address = (uintptr_t)descriptors;
   unsigned vertices_per_primitive =
      ps5_streamout_vertices_per_primitive(context->stream_output_primitive);
   uint64_t primitives = global_control ? 0 :
      ps5_draw_primitive_count(primitive_type, draw_count);
   uint32_t mask;

   if (!metadata->streamout_valid || !vertices_per_primitive ||
       instance_count != 1 ||
       metadata->streamout_buffer_table_user_data_dword >= user_data_count ||
       (uint32_t)(table_address >> 32) != metadata->address32_hi ||
       (global_control
          ? PS5_STREAMOUT_CONTROL_OFFSET + PS5_STREAMOUT_CONTROL_BYTES
          : PS5_STREAMOUT_DESCRIPTOR_OFFSET + 4u * 16u) >
          descriptor_resource->size ||
       primitives > UINT64_MAX / instance_count)
      return false;
   primitives *= instance_count;
   mask = metadata->streamout_enabled_stream_buffers_mask;
   if (!mask || mask >> PIPE_MAX_SO_BUFFERS)
      return false;
   memset(descriptors, 0,
          (global_control ? PS5_STREAMOUT_CONTROL_DESCRIPTOR + 1u : 4u) *
             16u);
   if (global_control)
      memset(control, 0, sizeof(*control));
   memset(size_dwords, 0, 4u * sizeof(size_dwords[0]));
   memset(stride_dwords, 0, 4u * sizeof(stride_dwords[0]));
   memset(offset_dwords, 0, 4u * sizeof(offset_dwords[0]));

   for (unsigned index = 0; index < PIPE_MAX_SO_BUFFERS; ++index) {
      struct pipe_stream_output_target *pipe_target;
      struct ps5_stream_output_target *target;
      struct ps5_resource *resource;
      uintptr_t address;
      uint64_t end;
      uint64_t begin;
      uint64_t capacity;

      if (!(mask & BITFIELD_BIT(index)))
         continue;
      if (index >= context->stream_output_target_count ||
          !(pipe_target = context->stream_output_targets[index]))
         return false;
      target = (struct ps5_stream_output_target *)pipe_target;
      resource = (struct ps5_resource *)pipe_target->buffer;
      end = (uint64_t)pipe_target->buffer_offset +
            pipe_target->buffer_size;
      begin = (uint64_t)pipe_target->buffer_offset + target->offset;
      if (!metadata->streamout_strides_dwords[index] ||
          resource->base.target != PIPE_BUFFER || end > resource->size ||
          begin > end || end > UINT32_MAX || (uintptr_t)resource->data >> 48)
         return false;

      if ((begin & 3u) || (end & 3u))
         return false;
      address = (uintptr_t)resource->data + begin;
      if (address >> 48)
         return false;
      descriptors[index * 4u] = (uint32_t)address;
      descriptors[index * 4u + 1u] = (uint32_t)(address >> 32);
      /* The NGG shader uses byte offsets and byte-sized buffer descriptors.
       * Point the descriptor at the current target position so PrimitiveID
       * starts every draw at offset zero without a global GDS counter. */
      descriptors[index * 4u + 2u] = (uint32_t)(end - begin);
      descriptors[index * 4u + 3u] = UINT32_C(0x31016fac);
      size_dwords[index] = (uint32_t)((end - begin) >> 2);
      stride_dwords[index] = metadata->streamout_strides_dwords[index];
      offset_dwords[index] = 0;
      if (!global_control) {
         capacity = (end - begin) /
                    (4u * vertices_per_primitive * stride_dwords[index]);
         primitives = MIN2(primitives, capacity);
      }
      ps5_flush_gpu_data(resource->data, resource->size);
   }
   if (global_control) {
      uintptr_t control_address = (uintptr_t)control;
      uint32_t *control_descriptor =
         descriptors + PS5_STREAMOUT_CONTROL_DESCRIPTOR * 4u;

      if (control_address >> 48 ||
          (uint32_t)(control_address >> 32) != metadata->address32_hi)
         return false;
      control_descriptor[0] = (uint32_t)control_address;
      control_descriptor[1] = (uint32_t)(control_address >> 32);
      control_descriptor[2] = sizeof(*control);
      control_descriptor[3] = UINT32_C(0x31016fac);
   }
   user_data[metadata->streamout_buffer_table_user_data_dword] =
      (uint32_t)table_address;
   ps5_flush_gpu_data(
      descriptors,
      global_control
         ? PS5_STREAMOUT_CONTROL_OFFSET + PS5_STREAMOUT_CONTROL_BYTES -
              PS5_STREAMOUT_DESCRIPTOR_OFFSET
         : 4u * 16u);
   *enabled_mask = mask;
   *written_vertices = primitives * vertices_per_primitive;
   return true;
}

static bool
ps5_collect_geometry_streamout(
   struct ps5_context *context, struct ps5_resource *descriptor_resource,
   uint64_t *written_vertices, uint64_t *generated_primitives)
{
   struct ps5_streamout_control *control =
      (struct ps5_streamout_control *)(descriptor_resource->data +
                                       PS5_STREAMOUT_CONTROL_OFFSET);
   unsigned vertices_per_primitive =
      ps5_streamout_vertices_per_primitive(context->stream_output_primitive);
   uint32_t generated;
   uint32_t emitted;

   if (!vertices_per_primitive ||
       PS5_STREAMOUT_CONTROL_OFFSET + sizeof(*control) >
          descriptor_resource->size)
      return false;
   ps5_flush_gpu_data(control, sizeof(*control));
   generated = control->generated_primitives[0];
   emitted = control->emitted_primitives[0];
   if (emitted > generated || control->generated_primitives[1] ||
       control->generated_primitives[2] ||
       control->generated_primitives[3] ||
       control->emitted_primitives[1] ||
       control->emitted_primitives[2] ||
       control->emitted_primitives[3])
      return false;
   *written_vertices = (uint64_t)emitted * vertices_per_primitive;
   *generated_primitives = generated;
   printf("[ps5-gallium] geometry-streamout generated=%u emitted=%u offsets=%u/%u/%u/%u\n",
          generated, emitted, control->buffer_offsets[0],
          control->buffer_offsets[1], control->buffer_offsets[2],
          control->buffer_offsets[3]);
   return true;
}

static void
ps5_draw_vbo_locked(struct pipe_context *base,
                    const struct pipe_draw_info *info,
                    unsigned drawid_offset,
                    const struct pipe_draw_indirect_info *indirect,
                    const struct pipe_draw_start_count_bias *draws,
                    unsigned num_draws)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct pipe_draw_start_count_bias draw;
   const PsbcShaderOutput *vertex_output;
   const PsbcShaderMetadata *vertex_metadata;
   const uint8_t *vertex_package;
   size_t vertex_package_size;
   struct ps5_resource *descriptor_resource;
   struct ps5_resource *pixel_descriptor_resource;
   uintptr_t descriptor_address;
   uint32_t *descriptor;
   uint32_t user_data[32] = {0};
   uint32_t pixel_user_data[32] = {0};
   struct ps5_vertex_layout vertex_layout;
   struct ps5_vertex_layout fragment_layout = {0};
   struct ps5_fragment_exports fragment_exports;
   struct ps5_native_graphics_state graphics;
   unsigned user_data_count;
   unsigned pixel_user_data_count;
   uint32_t primitive_type;
   uint32_t fragment_primitive_type;
   struct ps5_resource *index_resource = NULL;
   size_t index_offset = 0;
   uint32_t base_vertex;
   unsigned vertex_count;
   bool streamout_active = false;
   uint32_t streamout_mask = 0;
   uint32_t streamout_size[4] = {0};
   uint32_t streamout_stride[4] = {0};
   uint32_t streamout_offset[4] = {0};
   uint64_t streamout_written_vertices = 0;
   uint64_t generated_primitives = 0;
   struct ps5_query *occlusion_query = context->queries_enabled
                                         ? context->active_occlusion_query
                                         : NULL;
   bool color_to_texture_barrier = false;
   bool depth_to_texture_barrier = false;
   unsigned sample_count = 1;
   unsigned color_target_count;
   bool provoking_vtx_last;
   bool poly_line_smooth;
   uint32_t vs_out_control = 0;
   uint32_t vs_out_control_valid = 0;

   if (!info || !draws || !num_draws) {
      context->last_draw_status = num_draws ? -2 : 0;
      return;
   }
   if (num_draws > 1) {
      util_draw_multi(base, info, drawid_offset, indirect, draws, num_draws);
      return;
   }
   draw = draws[0];
   if (!indirect && !u_trim_pipe_prim(info->mode, &draw.count)) {
      context->last_draw_status = 0;
      return;
   }
   draws = &draw;
   if (!indirect && context->stream_output_target_count &&
       info->instance_count > 1) {
      uint64_t last_instance = (uint64_t)info->start_instance +
                               info->instance_count - 1u;

      if (last_instance > UINT32_MAX) {
         context->last_draw_status = -2;
         return;
      }
      printf("[ps5-gallium] streamout-instanced-split count=%u start=%u\n",
             info->instance_count, info->start_instance);
      for (unsigned instance = 0; instance < info->instance_count; ++instance) {
         struct pipe_draw_info single = *info;

         single.instance_count = 1;
         single.start_instance = info->start_instance + instance;
         ps5_draw_vbo_locked(base, &single, drawid_offset, NULL, draws, 1);
         if (context->last_draw_status != 0)
            return;
      }
      return;
   }
   context->draw_calls++;
   if (!ps5_render_condition_passes(context)) {
      context->last_draw_status = 0;
      return;
   }
   if (indirect || num_draws != 1 ||
       (info->index_size && info->index_size != 2 && info->index_size != 4) ||
       info->primitive_restart || info->has_user_indices ||
       !info->instance_count ||
       !ps5_draw_primitive(info->mode, draws[0].count, &primitive_type)) {
      context->last_draw_status = -2;
      return;
   }
   generated_primitives =
      ps5_draw_primitive_count(primitive_type, draws[0].count) *
      info->instance_count;
   base_vertex = info->index_size ? (uint32_t)draws[0].index_bias
                                  : draws[0].start;
   if (!info->index_size) {
      uint64_t end = (uint64_t)draws[0].start + draws[0].count;

      if (end > UINT32_MAX) {
         context->last_draw_status = -12;
         return;
      }
      vertex_count = (unsigned)end;
      if (draws[0].start)
         printf("[ps5-gallium] first-vertex first=%u count=%u descriptor-count=%u\n",
                draws[0].start, draws[0].count, vertex_count);
   }
   if (info->index_size) {
      const void *indices;
      uint64_t offset = (uint64_t)draws[0].start * info->index_size;
      uint64_t bytes = (uint64_t)draws[0].count * info->index_size;
      unsigned min_index = UINT32_MAX;
      unsigned max_index = 0;
      int64_t effective_min;
      int64_t effective_end;

      index_resource = (struct ps5_resource *)info->index.resource;
      if (!index_resource || index_resource->base.target != PIPE_BUFFER ||
          offset > index_resource->size ||
          bytes > index_resource->size - offset) {
         context->last_draw_status = -12;
         return;
      }
      index_offset = (size_t)offset;
      indices = (const uint8_t *)index_resource->data + index_offset;
      if (info->was_line_loop) {
         printf("[ps5-gallium] lowered-line-loop mode=%u count=%u start=%u offset=%zu base=%p selected=%p indices=",
                info->mode, draws[0].count, draws[0].start, index_offset,
                index_resource->data, indices);
         ps5_log_indices(indices, info->index_size, draws[0].count, 8);
      }
      if (info->was_primitive_restart) {
         printf("[ps5-gallium] lowered-primitive-restart mode=%u count=%u start=%u offset=%zu base=%p selected=%p indices=",
                info->mode, draws[0].count, draws[0].start, index_offset,
                index_resource->data, indices);
         ps5_log_indices(indices, info->index_size, draws[0].count, 16);
      }
      if (info->was_index_ubyte) {
         printf("[ps5-gallium] lowered-index-ubyte mode=%u index-size=%u count=%u start=%u offset=%zu base=%p selected=%p indices=",
                info->mode, info->index_size, draws[0].count, draws[0].start,
                index_offset, index_resource->data, indices);
         ps5_log_indices(indices, info->index_size, draws[0].count, 16);
      }
      if (info->index_size == 2 && !info->was_index_ubyte) {
         printf("[ps5-gallium] native-index-u16 mode=%u index-size=%u count=%u start=%u offset=%zu base=%p selected=%p indices=",
                info->mode, info->index_size, draws[0].count, draws[0].start,
                index_offset, index_resource->data, indices);
         ps5_log_indices(indices, info->index_size, draws[0].count, 16);
      }
      if (info->index_size == 4) {
         printf("[ps5-gallium] native-index-u32 mode=%u index-size=%u count=%u start=%u offset=%zu base=%p selected=%p indices=",
                info->mode, info->index_size, draws[0].count, draws[0].start,
                index_offset, index_resource->data, indices);
         ps5_log_indices(indices, info->index_size, draws[0].count, 16);
      }
      for (unsigned i = 0; i < draws[0].count; ++i) {
         unsigned index = ps5_index_value(indices, info->index_size, i);
         min_index = MIN2(min_index, index);
         max_index = MAX2(max_index, index);
      }
      if (max_index == UINT32_MAX) {
         context->last_draw_status = -12;
         return;
      }
      effective_min = (int64_t)min_index + draws[0].index_bias;
      effective_end = (int64_t)max_index + draws[0].index_bias + 1;
      if (effective_min < 0 || effective_end > UINT32_MAX) {
         context->last_draw_status = -12;
         return;
      }
      vertex_count = (unsigned)effective_end;
      if (draws[0].index_bias)
         printf("[ps5-gallium] base-vertex bias=%d raw-min=%u raw-max=%u effective-min=%lld effective-max=%lld\n",
                draws[0].index_bias, min_index, max_index,
                (long long)effective_min, (long long)effective_end - 1);
   }
   if (!context->vs || !context->fs || !context->framebuffer_valid) {
      context->last_draw_status = -3;
      return;
   }
   for (unsigned i = 0; i < context->framebuffer.nr_cbufs; ++i) {
      const struct pipe_resource *target =
         context->framebuffer.cbufs[i].texture;

      if (target) {
         sample_count = MAX2(target->nr_samples, 1);
         break;
      }
   }
   if (!context->framebuffer.nr_cbufs &&
       context->framebuffer.zsbuf.texture)
      sample_count = MAX2(context->framebuffer.zsbuf.texture->nr_samples, 1);
   /* AGC submission still needs a valid physical color target when the API
    * framebuffer is depth-only.  Keep writes disabled with target_mask=0. */
   color_target_count = MAX2(context->framebuffer.nr_cbufs, 1);
   if (!ps5_encode_graphics_state(context, &graphics)) {
      context->last_draw_status = -18;
      return;
   }
   descriptor_resource = (struct ps5_resource *)
      context->vertex_descriptor_table;
   if (!descriptor_resource ||
       !ps5_vertex_layout_from_state(context->vs,
                                     context->vertex_elements,
                                     &vertex_layout) ||
       !ps5_select_shader_variant(
          context->vs, (uint32_t)((uintptr_t)descriptor_resource->data >> 32),
          &vertex_layout, primitive_type,
          context->rasterizer && !context->rasterizer->flatshade_first,
          false, false, NULL)) {
      context->last_draw_status = -14;
      return;
   }
   if (!ps5_select_geometry_pipeline(
          context,
          (uint32_t)((uintptr_t)descriptor_resource->data >> 32),
          &vertex_layout, primitive_type)) {
      context->last_draw_status = -19;
      return;
   }
   pixel_descriptor_resource =
      (struct ps5_resource *)context->descriptor_storage[1];
   fragment_primitive_type =
      ps5_fragment_primitive_type(context, primitive_type);
   provoking_vtx_last = context->rasterizer &&
                        !context->rasterizer->flatshade_first;
   poly_line_smooth = sample_count == 1 && context->rasterizer &&
      (((fragment_primitive_type == 2 || fragment_primitive_type == 3) &&
        context->rasterizer->line_smooth) ||
       ((fragment_primitive_type >= 4 && fragment_primitive_type <= 6) &&
        context->rasterizer->poly_smooth));
   fragment_exports = ps5_fragment_exports_for_framebuffer(&context->framebuffer);
   if (!pixel_descriptor_resource || !fragment_primitive_type ||
       !ps5_select_shader_variant(
          context->fs,
          (uint32_t)((uintptr_t)pixel_descriptor_resource->data >> 32),
          &fragment_layout, fragment_primitive_type,
          provoking_vtx_last,
          sample_count > 1 && context->rasterizer &&
             context->rasterizer->multisample && graphics.alpha_to_one,
          poly_line_smooth, &fragment_exports)) {
      context->last_draw_status = -14;
      return;
   }
   streamout_active = PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE &&
                      context->stream_output_target_count != 0;
   if (context->gs) {
      if (streamout_active) {
         if (!context->gs->stream_output.num_outputs ||
             !context->geometry_streamout_package) {
            context->last_draw_status = -21;
            return;
         }
         vertex_output = &context->geometry_streamout_output;
         vertex_package = context->geometry_streamout_package;
         vertex_package_size = context->geometry_streamout_package_size;
      } else {
         vertex_output = &context->geometry_output;
         vertex_package = context->geometry_package;
         vertex_package_size = context->geometry_package_size;
      }
   } else {
      if (streamout_active) {
         if (!context->vs->stream_output.num_outputs ||
             !context->vs->active->streamout_package) {
            context->last_draw_status = -21;
            return;
         }
         vertex_output = &context->vs->active->streamout_output;
         vertex_package = context->vs->active->streamout_package;
         vertex_package_size = context->vs->active->streamout_package_size;
      } else {
         vertex_output = &context->vs->active->output;
         vertex_package = context->vs->active->package;
         vertex_package_size = context->vs->active->package_size;
      }
   }
   user_data_count = vertex_output->metadata.user_sgpr_count;
   pixel_user_data_count =
      context->fs->active->output.metadata.user_sgpr_count;
   if (user_data_count > 32 || pixel_user_data_count > 32) {
      context->last_draw_status = -11;
      return;
   }
   vertex_metadata = &vertex_output->metadata;
   if (vertex_metadata->clip_distance_mask ||
       vertex_metadata->cull_distance_mask) {
      uint32_t base_control = 0;
      unsigned packed_clip = 0;
      unsigned packed_index = 0;
      unsigned packed_cull;
      bool found = false;

      for (uint32_t i = 0; i < vertex_metadata->context_register_count; ++i) {
         if (vertex_metadata->context_registers[i].offset == UINT16_C(0x0207)) {
            base_control = vertex_metadata->context_registers[i].value;
            found = true;
            break;
         }
      }
      if (!found) {
         context->last_draw_status = -18;
         return;
      }
      for (unsigned distance = 0; distance < 8; ++distance) {
         if (!(vertex_metadata->clip_distance_mask & (1u << distance)))
            continue;
         if (context->rasterizer &&
             (context->rasterizer->clip_plane_enable & (1u << distance)))
            packed_clip |= 1u << packed_index;
         ++packed_index;
      }
      packed_cull = ((1u <<
         util_bitcount(vertex_metadata->cull_distance_mask)) - 1u) <<
         packed_index;
      if (info->mode == MESA_PRIM_POINTS)
         packed_cull |= packed_clip;
      vs_out_control = (base_control & UINT32_C(0xffff0000)) |
                       packed_clip | (packed_cull << 8);
      vs_out_control_valid = 1;
   }
   if (!vertex_metadata->base_vertex_valid ||
       vertex_metadata->base_vertex_user_data_dword >= user_data_count) {
      context->last_draw_status = -10;
      return;
   }
   user_data[vertex_metadata->base_vertex_user_data_dword] = base_vertex;
   if (info->start_instance) {
      if (!vertex_metadata->start_instance_valid ||
          vertex_metadata->start_instance_user_data_dword >= user_data_count) {
         context->last_draw_status = -10;
         return;
      }
      user_data[vertex_metadata->start_instance_user_data_dword] =
         info->start_instance;
   } else if (vertex_metadata->start_instance_valid) {
      if (vertex_metadata->start_instance_user_data_dword >= user_data_count) {
         context->last_draw_status = -10;
         return;
      }
      user_data[vertex_metadata->start_instance_user_data_dword] = 0;
   }
   if (vertex_metadata->vertex_buffer_table_valid) {
      uint32_t binding_mask = 0;
      uint32_t binding_records[PIPE_MAX_ATTRIBS] = {0};
      unsigned descriptor_index = 0;
      unsigned element_index;
      unsigned binding;

      if (!context->vertex_elements || !context->vertex_elements->count ||
          !context->vertex_buffer_count) {
         context->last_draw_status = -7;
         return;
      }
      for (element_index = 0;
           element_index < context->vertex_elements->count;
           ++element_index) {
         const struct pipe_vertex_element *element =
            &context->vertex_elements->elements[element_index];
         const struct pipe_vertex_buffer *vertex_buffer;
         struct ps5_resource *vertex_resource;
         PsbcVertexFormat ignored_format;
         unsigned format_size = ps5_vertex_format_size(element->src_format);
         uint64_t records = element->src_stride ? vertex_count : 1u;
         uint64_t required;

         if (element->vertex_buffer_index >= context->vertex_buffer_count ||
             !format_size ||
             !ps5_vertex_format(element->src_format, &ignored_format)) {
            context->last_draw_status = -8;
            return;
         }
         vertex_buffer =
            &context->vertex_buffers[element->vertex_buffer_index];
         if (vertex_buffer->is_user_buffer ||
             !vertex_buffer->buffer.resource) {
            context->last_draw_status = -8;
            return;
         }
         if (element->instance_divisor) {
            records = (uint64_t)info->start_instance +
                      ((uint64_t)info->instance_count - 1u) /
                         element->instance_divisor +
                      1u;
            if (records > UINT32_MAX) {
               context->last_draw_status = -9;
               return;
            }
         }
         vertex_resource =
            (struct ps5_resource *)vertex_buffer->buffer.resource;
         required = (uint64_t)vertex_buffer->buffer_offset +
                    element->src_offset +
                    (records - 1u) * element->src_stride +
                    format_size;
         if (vertex_resource->base.target != PIPE_BUFFER ||
             required > vertex_resource->size) {
            context->last_draw_status = -9;
            return;
         }
         binding_records[element->vertex_buffer_index] =
            MAX2(binding_records[element->vertex_buffer_index],
                 (uint32_t)records);
         binding_mask |= BITFIELD_BIT(element->vertex_buffer_index);
      }
      descriptor_address = (uintptr_t)descriptor_resource->data;
      if ((uint32_t)(descriptor_address >> 32) !=
             vertex_metadata->address32_hi ||
          vertex_metadata->vertex_buffer_table_user_data_dword >=
             user_data_count) {
         context->last_draw_status = -10;
         return;
      }
      if ((size_t)__builtin_popcount(binding_mask) * 16 >
          descriptor_resource->size) {
         context->last_draw_status = -9;
         return;
      }
      descriptor = (uint32_t *)descriptor_resource->data;
      for (binding = 0; binding < context->vertex_buffer_count; ++binding) {
         const struct pipe_vertex_element *element = NULL;
         const struct pipe_vertex_buffer *vertex_buffer;
         struct ps5_resource *vertex_resource;
         uintptr_t vertex_address;

         if (!(binding_mask & BITFIELD_BIT(binding)))
            continue;
         for (element_index = 0;
              element_index < context->vertex_elements->count;
              ++element_index) {
            if (context->vertex_elements->elements[element_index]
                   .vertex_buffer_index == binding) {
               element = &context->vertex_elements->elements[element_index];
               break;
            }
         }
         if (!element) {
            context->last_draw_status = -9;
            return;
         }
         vertex_buffer = &context->vertex_buffers[binding];
         vertex_resource =
            (struct ps5_resource *)vertex_buffer->buffer.resource;
         vertex_address = (uintptr_t)vertex_resource->data +
                          vertex_buffer->buffer_offset;
         descriptor[descriptor_index * 4] = (uint32_t)vertex_address;
         descriptor[descriptor_index * 4 + 1] =
            (uint32_t)(vertex_address >> 32) |
            (element->src_stride << 16);
         descriptor[descriptor_index * 4 + 2] = binding_records[binding];
         descriptor[descriptor_index * 4 + 3] = UINT32_C(0x5204);
         ps5_flush_gpu_data(vertex_resource->data, vertex_resource->size);
         descriptor_index++;
      }
      user_data[vertex_metadata->vertex_buffer_table_user_data_dword] =
         (uint32_t)descriptor_address;
      ps5_flush_gpu_data(descriptor_resource->data,
                         descriptor_index * 16);
   }
   if (!ps5_prepare_constant(context, context->vs, 0, user_data,
                             user_data_count, vertex_metadata)) {
      printf("[ps5-gallium] resource-prepare reject=vertex-constants\n");
      context->last_draw_status = -15;
      return;
   }
   if (!ps5_prepare_texture(context, context->vs, 0, user_data,
                            user_data_count, vertex_metadata)) {
      printf("[ps5-gallium] resource-prepare reject=vertex-textures\n");
      context->last_draw_status = -15;
      return;
   }
   if (!ps5_prepare_constant(context, context->fs, 1, pixel_user_data,
                             pixel_user_data_count, NULL)) {
      printf("[ps5-gallium] resource-prepare reject=fragment-constants\n");
      context->last_draw_status = -15;
      return;
   }
   if (!ps5_prepare_texture(context, context->fs, 1, pixel_user_data,
                            pixel_user_data_count, NULL)) {
      printf("[ps5-gallium] resource-prepare reject=fragment-textures\n");
      context->last_draw_status = -15;
      return;
   }
   if (streamout_active &&
       !ps5_prepare_streamout(
          context, vertex_metadata, descriptor_resource, user_data,
          user_data_count, primitive_type, draws[0].count,
          info->instance_count, &streamout_mask, streamout_size,
          streamout_stride, streamout_offset,
          &streamout_written_vertices)) {
      context->last_draw_status = -21;
      return;
   }
   if (!ps5_agc_gate2_run || !ps5_agc_gate2_set_packages ||
       (!PS5_ENABLE_MRT_CANDIDATE &&
        !PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE &&
        !ps5_agc_gate2_set_framebuffer) ||
       ((PS5_ENABLE_MRT_CANDIDATE ||
         PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE) &&
        (!ps5_agc_gate2_set_framebuffers ||
         (PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE &&
          !ps5_agc_gate2_set_scanout) ||
         (PS5_ENABLE_MRT_CANDIDATE &&
          !ps5_agc_gate2_set_graphics_state_mrt))) ||
       !ps5_agc_gate2_set_vertex_user_data ||
       !ps5_agc_gate2_set_index_buffer ||
       !ps5_agc_gate2_set_draw_state ||
       !ps5_agc_gate2_set_instance_count ||
       !ps5_agc_gate2_set_pixel_user_data ||
       !ps5_agc_gate2_set_depth_buffer ||
       !ps5_agc_gate2_set_clip_control ||
       !ps5_agc_gate2_set_vs_out_control ||
       (PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE &&
        !ps5_agc_gate2_set_color_to_texture_barrier) ||
       (PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE &&
        !ps5_agc_gate2_set_depth_to_texture_barrier) ||
       (PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE &&
        !ps5_agc_gate2_set_depth_target_extents) ||
       (PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE &&
        !ps5_agc_gate2_set_depth_target_view) ||
       ((PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE ||
         PS5_ENABLE_TEXTURE_RG_CANDIDATE ||
         PS5_ENABLE_PACKED_FLOAT_CANDIDATE ||
         PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE ||
         PS5_ENABLE_RGB10_A2UI_CANDIDATE) &&
        !ps5_agc_gate2_set_color_target_info) ||
       (PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE &&
        !ps5_agc_gate2_set_color_target_extents) ||
       (PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE &&
        !ps5_agc_gate2_set_streamout) ||
       (occlusion_query && !ps5_agc_gate2_set_occlusion_query) ||
       (PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE &&
        !ps5_agc_gate2_set_dual_source_blend) ||
       ((PS5_ENABLE_MSAA4_CANDIDATE ||
         PS5_ENABLE_SMOOTH_RASTER_CANDIDATE) &&
        !ps5_agc_gate2_set_multisample_state) ||
       (PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE &&
        !ps5_agc_gate2_set_point_line_state) ||
       (PS5_ENABLE_POINT_COORD_CANDIDATE &&
        (!ps5_agc_gate2_set_interp_control ||
         !ps5_agc_gate2_set_point_coord_input)) ||
       (PS5_ENABLE_BORDER_COLOR_CANDIDATE &&
        !ps5_agc_gate2_set_border_color_table) ||
       (context->gs && !ps5_agc_gate2_set_ngg_control) ||
       (!PS5_ENABLE_MRT_CANDIDATE &&
        !ps5_agc_gate2_set_graphics_state)) {
      context->last_draw_status = -3;
      return;
   }

   if (PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE &&
       ps5_agc_gate2_set_streamout(
          streamout_active ? vertex_package : NULL,
          streamout_active ? vertex_package_size : 0,
          streamout_mask, streamout_size, streamout_stride,
          streamout_offset) != 0) {
      context->last_draw_status = -21;
      return;
   }

   if (((PS5_ENABLE_MSAA4_CANDIDATE ||
         PS5_ENABLE_SMOOTH_RASTER_CANDIDATE) &&
        ps5_agc_gate2_set_multisample_state(
           sample_count, context->sample_mask,
           context->rasterizer && context->rasterizer->multisample,
           graphics.alpha_to_coverage, poly_line_smooth,
           context->fs->nir->info.fs.uses_sample_shading) != 0) ||
       (PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE &&
        ps5_agc_gate2_set_dual_source_blend(
           graphics.dual_source_blend) != 0) ||
       (PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE &&
        ps5_agc_gate2_set_point_line_state(
           graphics.point_line, graphics.point_line_valid) != 0) ||
       (PS5_ENABLE_POINT_COORD_CANDIDATE &&
        ps5_agc_gate2_set_interp_control(
           graphics.interp_control, graphics.interp_control_valid) != 0) ||
       (PS5_ENABLE_POINT_COORD_CANDIDATE &&
        ps5_agc_gate2_set_point_coord_input(
           !!(context->fs->nir->info.inputs_read & VARYING_BIT_PNTC)) != 0) ||
       (PS5_ENABLE_BORDER_COLOR_CANDIDATE &&
        ps5_agc_gate2_set_border_color_table(
           ((struct ps5_resource *)context->border_color_storage)->data,
           ((struct ps5_resource *)context->border_color_storage)
              ->allocation_size) != 0) ||
        ps5_agc_gate2_set_clip_control(graphics.clip_control,
                                       graphics.clip_control_valid) != 0 ||
        ps5_agc_gate2_set_vs_out_control(vs_out_control,
                                         vs_out_control_valid) != 0 ||
       (PS5_ENABLE_MRT_CANDIDATE
          ? ps5_agc_gate2_set_graphics_state_mrt(
               graphics.blend_control, color_target_count,
               graphics.target_mask, graphics.color_control,
               graphics.color_control_valid, graphics.blend_color,
               graphics.viewport, graphics.scissor,
               graphics.rasterizer_control, graphics.rasterizer_valid,
               graphics.polygon_offset, graphics.polygon_offset_valid)
          : ps5_agc_gate2_set_graphics_state(
               graphics.blend_control[0], graphics.target_mask,
               graphics.color_control, graphics.color_control_valid,
               graphics.blend_color, graphics.viewport, graphics.scissor,
               graphics.rasterizer_control, graphics.rasterizer_valid,
               graphics.polygon_offset, graphics.polygon_offset_valid)) != 0) {
      context->last_draw_status = -18;
      return;
   }

   if (ps5_agc_gate2_set_packages(vertex_package,
                                  vertex_package_size,
                                  context->fs->active->package,
                                  context->fs->active->package_size) != 0) {
      context->last_draw_status = -4;
      return;
   }
   if (ps5_agc_gate2_set_ngg_control &&
       ps5_agc_gate2_set_ngg_control(
          context->gs != NULL,
          context->gs ? UINT32_C(0x000007fe) : 0) != 0) {
      context->last_draw_status = -19;
      return;
   }
   {
      void *targets[PS5_MAX_RENDER_TARGETS];
      size_t target_sizes[PS5_MAX_RENDER_TARGETS];
      uint32_t target_info[PS5_MAX_RENDER_TARGETS];
      uint32_t target_widths[PS5_MAX_RENDER_TARGETS];
      uint32_t target_heights[PS5_MAX_RENDER_TARGETS];
      uint32_t target_views[PS5_MAX_RENDER_TARGETS];
      struct ps5_resource *fallback = NULL;
      struct ps5_screen *screen = (struct ps5_screen *)base->screen;

      for (unsigned i = 0; i < context->framebuffer.nr_cbufs; ++i) {
         if (context->framebuffer.cbufs[i].texture) {
            fallback = (struct ps5_resource *)
               context->framebuffer.cbufs[i].texture;
            break;
         }
      }
      if (!fallback && screen->render_pool)
         fallback = (struct ps5_resource *)screen->render_pool;
      if (!fallback) {
         context->last_draw_status = -5;
         return;
      }

      for (unsigned i = 0; i < color_target_count; ++i) {
         const struct pipe_surface *surface = &context->framebuffer.cbufs[i];
         struct ps5_resource *target = context->framebuffer.cbufs[i].texture
            ? (struct ps5_resource *)context->framebuffer.cbufs[i].texture
            : fallback;
         size_t layer_offset = 0;

         if (surface->texture) {
            if (target->render_staging_size) {
               if (!ps5_stage_color_surface(surface, true)) {
                  context->last_draw_status = -5;
                  return;
               }
               layer_offset = target->render_staging_offset;
            } else {
               layer_offset =
                  (size_t)surface->first_layer * target->layer_stride +
                  target->level_offset[surface->level];
            }
         }

         if (layer_offset >= target->allocation_size) {
            context->last_draw_status = -5;
            return;
         }
         targets[i] = target->data + layer_offset;
         target_sizes[i] = surface->texture && target->render_staging_size
                              ? target->render_staging_size
                              : target->allocation_size - layer_offset;
         target_widths[i] = surface->texture ? ps5_surface_width(surface)
                                             : target->base.width0;
         target_heights[i] = surface->texture ? ps5_surface_height(surface)
                                              : target->base.height0;
         target_views[i] = surface->texture &&
                           surface->last_layer > surface->first_layer
                              ? (surface->last_layer -
                                 surface->first_layer) << 13
                              : 0;
         target_info[i] = surface->texture
            ? ps5_color_target_info(surface->format)
            : ps5_color_target_info(PIPE_FORMAT_R8G8B8A8_UNORM);
         color_to_texture_barrier |=
            (target->base.bind & (PIPE_BIND_RENDER_TARGET |
                                  PIPE_BIND_SAMPLER_VIEW)) ==
            (PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW);
         color_to_texture_barrier |= target->base.nr_samples == 4;
      }
      if (PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE) {
         struct ps5_resource *scanout = screen->render_pool
            ? (struct ps5_resource *)screen->render_pool : NULL;

         if (!scanout ||
             ps5_agc_gate2_set_scanout(
                scanout->data, scanout->allocation_size) != 0) {
            context->last_draw_status = -5;
            return;
         }
      }
      if (((PS5_ENABLE_MRT_CANDIDATE ||
            PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE)
             ? ps5_agc_gate2_set_framebuffers(
                  targets, target_sizes, color_target_count)
             : ps5_agc_gate2_set_framebuffer(targets[0], target_sizes[0])) !=
          0) {
         context->last_draw_status = -5;
         return;
      }
      if ((PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE ||
           PS5_ENABLE_TEXTURE_RG_CANDIDATE) &&
          ps5_agc_gate2_set_color_target_info(
             target_info, color_target_count) != 0) {
         context->last_draw_status = -24;
         return;
      }
      if (PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE &&
          ps5_agc_gate2_set_color_target_extents(
             target_widths, target_heights,
             color_target_count) != 0) {
         context->last_draw_status = -25;
         return;
      }
      if (ps5_agc_gate2_set_color_target_views &&
          ps5_agc_gate2_set_color_target_views(
             target_views, color_target_count) != 0) {
         context->last_draw_status = -28;
         return;
      }
   }
   if (context->framebuffer.zsbuf.texture) {
      struct ps5_resource *depth = (struct ps5_resource *)
         context->framebuffer.zsbuf.texture;
      const struct pipe_surface *depth_surface =
         &context->framebuffer.zsbuf;
      const struct pipe_depth_stencil_alpha_state *dsa =
         context->depth_stencil_alpha;
      struct ps5_native_depth_stencil_state native;
      void *depth_data = depth->data;
      size_t depth_allocation = depth->allocation_size;
      unsigned depth_width = ps5_surface_width(depth_surface);
      unsigned depth_height = ps5_surface_height(depth_surface);
      bool packed =
         depth->base.format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
      size_t depth_size = ps5_tiled_depth_surface_size(
         depth_width, depth_height, depth->base.nr_samples);
      size_t stencil_size = ps5_tiled_stencil_surface_size_samples(
         depth_width, depth_height, depth->base.nr_samples);
      uint32_t depth_view = depth_surface->first_layer |
         (depth_surface->last_layer << 13);

      depth_to_texture_barrier =
         (depth->base.bind & (PIPE_BIND_DEPTH_STENCIL |
                              PIPE_BIND_SAMPLER_VIEW)) ==
         (PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_SAMPLER_VIEW);

      if (!ps5_encode_depth_stencil_state(dsa, &context->stencil_ref,
                                          &native) ||
          ((dsa->stencil[0].enabled || dsa->stencil[1].enabled) &&
           !packed)) {
         context->last_draw_status = -17;
         return;
      }
      if (depth->depth_staging_size) {
         if (!ps5_stage_depth_surface(depth_surface, true)) {
            context->last_draw_status = -17;
            return;
         }
         depth_data = depth->data + depth->depth_staging_offset;
         depth_allocation = depth->depth_staging_size;
      }
      printf("[ps5-gallium] depth-state format=%u address=%p enabled=%u write=%u func=%u control=%08x\n",
             depth->base.format, depth_data, dsa->depth_enabled,
             dsa->depth_writemask, dsa->depth_func,
             native.depth_control);
      if (PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE &&
          ps5_agc_gate2_set_depth_target_extents(
             depth_width, depth_height) != 0) {
         context->last_draw_status = -27;
         return;
      }
      if (PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE &&
          ps5_agc_gate2_set_depth_target_view(depth_view) != 0) {
         context->last_draw_status = -29;
         return;
      }
      ps5_flush_gpu_data(depth_data, depth_allocation);
      if (packed) {
         if (!ps5_agc_gate2_set_depth_stencil_buffer ||
             !depth->stencil_data ||
             depth_allocation < depth_size ||
             depth->stencil_allocation_size < stencil_size) {
            context->last_draw_status = -17;
            return;
         }
         ps5_flush_gpu_data(depth->stencil_data,
                            depth->stencil_allocation_size);
         printf("[ps5-gallium] stencil-state format=%u depth=%p/%zu stencil=%p/%zu control=%08x refmask=%08x refmask-bf=%08x\n",
                depth->base.format, depth_data, depth_allocation,
                depth->stencil_data, depth->stencil_allocation_size,
                native.stencil_control, native.stencil_refmask,
                native.stencil_refmask_bf);
         if (ps5_agc_gate2_set_depth_stencil_buffer(
                depth_data, depth_allocation, depth->stencil_data,
                depth->stencil_allocation_size, native.depth_control,
                native.stencil_control, native.stencil_refmask,
                native.stencil_refmask_bf) != 0) {
            context->last_draw_status = -17;
            return;
         }
      } else if (ps5_agc_gate2_set_depth_buffer(
                    depth_data, depth_allocation,
                    native.depth_control) != 0) {
         context->last_draw_status = -17;
         return;
      }
   } else {
      if ((PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE &&
           ps5_agc_gate2_set_depth_target_extents(
              PS5_RENDER_WIDTH, PS5_RENDER_HEIGHT) != 0) ||
          (PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE &&
           ps5_agc_gate2_set_depth_target_view(0) != 0) ||
          ps5_agc_gate2_set_depth_buffer(NULL, 0, 0) != 0) {
         context->last_draw_status = -17;
         return;
      }
   }
   if (PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE &&
       ps5_agc_gate2_set_color_to_texture_barrier(
          color_to_texture_barrier) != 0) {
      context->last_draw_status = -23;
      return;
   }
   if (PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE &&
       ps5_agc_gate2_set_depth_to_texture_barrier(
          depth_to_texture_barrier) != 0) {
      context->last_draw_status = -26;
      return;
   }
   if (ps5_agc_gate2_set_vertex_user_data(user_data,
                                          user_data_count) != 0) {
      context->last_draw_status = -6;
      return;
   }
   if (ps5_agc_gate2_set_pixel_user_data(
          pixel_user_data,
          context->fs->active->output.metadata.descriptor_set0_valid
             ? pixel_user_data_count : 0) != 0) {
      context->last_draw_status = -16;
      return;
   }
   if (ps5_agc_gate2_set_draw_state(primitive_type, draws[0].count) != 0) {
      context->last_draw_status = -19;
      return;
   }
   if (ps5_agc_gate2_set_instance_count(info->instance_count) != 0) {
      context->last_draw_status = -19;
      return;
   }
   if (info->instance_count > 1)
      printf("[ps5-gallium] instanced count=%u vertices=%u\n",
             info->instance_count, draws[0].count);
   if (info->index_size) {
      int rc;

      ps5_flush_gpu_data(index_resource->data, index_resource->size);
      if (ps5_agc_gate2_set_index_buffer_typed)
         rc = ps5_agc_gate2_set_index_buffer_typed(
            (uint8_t *)index_resource->data + index_offset,
            draws[0].count, info->index_size);
      else if (info->index_size == 2)
         rc = ps5_agc_gate2_set_index_buffer(
            (uint8_t *)index_resource->data + index_offset,
            draws[0].count);
      else
         rc = -1;
      if (rc != 0) {
         context->last_draw_status = -13;
         return;
      }
   } else if (ps5_agc_gate2_set_index_buffer(NULL, 0) != 0) {
      context->last_draw_status = -13;
      return;
   }
   if (occlusion_query &&
       (!ps5_prepare_occlusion_query(occlusion_query) ||
        ps5_agc_gate2_set_occlusion_query(
           ((struct ps5_resource *)occlusion_query->buffer)->data,
           PS5_OCCLUSION_QUERY_BYTES,
           occlusion_query->type !=
              PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE) != 0)) {
      context->last_draw_status = -22;
      return;
   }
   /* Temporary Gate 5 bring-up: the runner still owns the target and command
    * submission, but now consumes packages compiled by Gallium state. */
   context->last_draw_status = ps5_agc_gate2_run();
   if (context->last_draw_status == 0) {
      for (unsigned i = 0; i < context->framebuffer.nr_cbufs; ++i) {
         const struct pipe_surface *surface = &context->framebuffer.cbufs[i];
         const struct ps5_resource *target = surface->texture
            ? (const struct ps5_resource *)surface->texture : NULL;

         if (target && target->render_staging_size &&
             !ps5_stage_color_surface(surface, false)) {
            context->last_draw_status = -29;
            break;
         }
         if (target && target->render_staging_size &&
             context->samplers[1][0] &&
             ((const struct ps5_sampler_state *)
                 context->samplers[1][0])->base.compare_mode)
            printf("[ps5-gallium] shadow-output first=%02x%02x%02x%02x\n",
                   target->data[0], target->data[1], target->data[2],
                   target->data[3]);
      }
      if (context->last_draw_status == 0 &&
          context->framebuffer.zsbuf.texture) {
         const struct pipe_surface *surface = &context->framebuffer.zsbuf;
         const struct ps5_resource *depth =
            (const struct ps5_resource *)surface->texture;

         if (depth->depth_staging_size &&
             !ps5_stage_depth_surface(surface, false))
            context->last_draw_status = -29;
      }
   }
#if PS5_PUBLIC_TEXTURE_RG_TILE_TEST
   if (context->last_draw_status == 0 && context->draw_calls <= 3 &&
       !ps5_record_public_rg_tile(context))
      context->last_draw_status = -28;
#endif
   if (occlusion_query) {
      int disable_status =
         ps5_agc_gate2_set_occlusion_query(NULL, 0, false);

      if (context->last_draw_status == 0 &&
          (disable_status != 0 ||
           !ps5_collect_occlusion_query(occlusion_query)))
         context->last_draw_status = -22;
   }
   if (streamout_active && context->gs &&
       context->last_draw_status == 0 &&
       !ps5_collect_geometry_streamout(
          context, descriptor_resource, &streamout_written_vertices,
          &generated_primitives))
      context->last_draw_status = -21;
   if (streamout_active && context->last_draw_status == 0) {
      unsigned vertices_per_primitive =
         ps5_streamout_vertices_per_primitive(
            context->stream_output_primitive);

      for (unsigned index = 0;
           index < context->stream_output_target_count; ++index) {
         struct ps5_stream_output_target *target =
            (struct ps5_stream_output_target *)
               context->stream_output_targets[index];

         if (target && (streamout_mask & BITFIELD_BIT(index)))
            target->offset += (unsigned)(streamout_written_vertices *
                              streamout_stride[index] * 4u);
      }
      if (context->queries_enabled &&
          context->active_primitives_emitted_query &&
          vertices_per_primitive)
         context->active_primitives_emitted_query->value +=
            streamout_written_vertices / vertices_per_primitive;
   }
   if (context->queries_enabled && context->last_draw_status == 0 &&
       (!context->gs || streamout_active) &&
       context->active_primitives_generated_query)
      context->active_primitives_generated_query->value +=
         generated_primitives;
#ifdef PS5_PUBLIC_STENCIL_TEST
   if (context->last_draw_status == 0 && context->draw_calls == 17) {
      struct ps5_resource *depth = context->framebuffer.zsbuf.texture
         ? (struct ps5_resource *)context->framebuffer.zsbuf.texture : NULL;

      if (!ps5_record_public_stencil(depth))
         context->last_draw_status = -20;
   }
#endif
}

static void
ps5_draw_vbo_without_adjacency(
   struct pipe_context *base, const struct pipe_draw_info *info,
   unsigned drawid_offset, const struct pipe_draw_start_count_bias *draw)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct pipe_draw_info new_info = *info;
   struct pipe_draw_start_count_bias new_draw = *draw;
   struct pipe_resource *release_buffer = NULL;
   const void *source = NULL;
   uint32_t *output;
   unsigned primitive_count;
   unsigned vertices_per_primitive;
   unsigned output_count;
   unsigned output_offset;
   unsigned min_index = UINT_MAX;
   unsigned max_index = 0;

   switch (info->mode) {
   case MESA_PRIM_LINES_ADJACENCY:
      new_info.mode = MESA_PRIM_LINES;
      primitive_count = draw->count / 4u;
      vertices_per_primitive = 2;
      break;
   case MESA_PRIM_LINE_STRIP_ADJACENCY:
      new_info.mode = MESA_PRIM_LINES;
      primitive_count = draw->count >= 4 ? draw->count - 3u : 0;
      vertices_per_primitive = 2;
      break;
   case MESA_PRIM_TRIANGLES_ADJACENCY:
      new_info.mode = MESA_PRIM_TRIANGLES;
      primitive_count = draw->count / 6u;
      vertices_per_primitive = 3;
      break;
   case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY:
      new_info.mode = MESA_PRIM_TRIANGLES;
      primitive_count = draw->count >= 6 ? (draw->count - 4u) / 2u : 0;
      vertices_per_primitive = 3;
      break;
   default:
      return;
   }
   if (!primitive_count) {
      context->last_draw_status = 0;
      return;
   }
   if ((uint64_t)primitive_count * vertices_per_primitive > UINT_MAX / 4u) {
      context->last_draw_status = -2;
      return;
   }
   output_count = primitive_count * vertices_per_primitive;
   if (info->index_size) {
      struct ps5_resource *resource = (struct ps5_resource *)info->index.resource;
      uint64_t source_offset = (uint64_t)draw->start * info->index_size;
      uint64_t source_size = (uint64_t)draw->count * info->index_size;

      if (info->has_user_indices ||
          (info->index_size != 2 && info->index_size != 4) ||
          !resource || !resource->data || source_offset > resource->size ||
          source_size > resource->size - source_offset) {
         context->last_draw_status = -2;
         return;
      }
      source = (const uint8_t *)resource->data + source_offset;
   }
   u_upload_alloc(base->stream_uploader, 0, output_count * 4u, 4,
                  &output_offset, &new_info.index.resource,
                  &release_buffer, (void **)&output);
   if (!output) {
      context->last_draw_status = -2;
      return;
   }

   for (unsigned primitive = 0; primitive < primitive_count; ++primitive) {
      unsigned positions[3];

      switch (info->mode) {
      case MESA_PRIM_LINES_ADJACENCY:
         positions[0] = primitive * 4u + 1u;
         positions[1] = primitive * 4u + 2u;
         break;
      case MESA_PRIM_LINE_STRIP_ADJACENCY:
         positions[0] = primitive + 1u;
         positions[1] = primitive + 2u;
         break;
      case MESA_PRIM_TRIANGLES_ADJACENCY:
         positions[0] = primitive * 6u;
         positions[1] = primitive * 6u + 2u;
         positions[2] = primitive * 6u + 4u;
         break;
      case MESA_PRIM_TRIANGLE_STRIP_ADJACENCY: {
         unsigned first = primitive * 2u;

         positions[0] = first + (primitive & 1u ? 2u : 0u);
         positions[1] = first + (primitive & 1u ? 0u : 2u);
         positions[2] = first + 4u;
         break;
      }
      default:
         __builtin_unreachable();
      }
      for (unsigned vertex = 0; vertex < vertices_per_primitive; ++vertex) {
         uint64_t value = info->index_size
                             ? ps5_index_value(source, info->index_size,
                                               positions[vertex])
                             : (uint64_t)draw->start + positions[vertex];

         if (value > UINT_MAX) {
            u_upload_unmap(base->stream_uploader);
            pipe_resource_release(base, release_buffer);
            context->last_draw_status = -2;
            return;
         }
         output[primitive * vertices_per_primitive + vertex] = (uint32_t)value;
         min_index = MIN2(min_index, (unsigned)value);
         max_index = MAX2(max_index, (unsigned)value);
      }
   }
   u_upload_unmap(base->stream_uploader);
   new_info.index_size = 4;
   new_info.has_user_indices = false;
   new_info.primitive_restart = false;
   new_info.index_bounds_valid = true;
   new_info.min_index = min_index;
   new_info.max_index = max_index;
   new_draw.start = output_offset / 4u;
   new_draw.count = output_count;
   new_draw.index_bias = info->index_size ? draw->index_bias : 0;
   base->draw_vbo(base, &new_info, drawid_offset, NULL, &new_draw, 1);
   pipe_resource_release(base, release_buffer);
}

static void
ps5_draw_vbo(struct pipe_context *base, const struct pipe_draw_info *info,
             unsigned drawid_offset,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count_bias *draws,
             unsigned num_draws)
{
   struct ps5_screen *screen = (struct ps5_screen *)base->screen;
   struct ps5_context *context = (struct ps5_context *)base;
   struct pipe_resource *uploaded_indices = NULL;
   struct pipe_draw_info uploaded_info;
   struct pipe_draw_start_count_bias uploaded_draw;
   unsigned uploaded_offset;

   if (num_draws > 1) {
      util_draw_multi(base, info, drawid_offset, indirect, draws, num_draws);
      return;
   }
   if (info && info->primitive_restart) {
      if (util_draw_vbo_without_prim_restart(
             base, info, drawid_offset, indirect, draws) != PIPE_OK)
         ((struct ps5_context *)base)->last_draw_status = -2;
      return;
   }
   if (info && draws && !indirect && info->index_size &&
       info->has_user_indices) {
      if (!util_upload_index_buffer(base, info, &draws[0],
                                    &uploaded_indices, &uploaded_offset, 4)) {
         context->last_draw_status = -2;
         printf("[ps5-gallium] user-index-upload-failed\n");
         return;
      }
      uploaded_info = *info;
      uploaded_info.index.resource = uploaded_indices;
      uploaded_info.has_user_indices = false;
      uploaded_draw = draws[0];
      uploaded_draw.start += uploaded_offset / info->index_size;
      info = &uploaded_info;
      draws = &uploaded_draw;
   }
   if (info && draws && !indirect && !((struct ps5_context *)base)->gs &&
       (info->mode == MESA_PRIM_LINES_ADJACENCY ||
       info->mode == MESA_PRIM_LINE_STRIP_ADJACENCY ||
       info->mode == MESA_PRIM_TRIANGLES_ADJACENCY ||
       info->mode == MESA_PRIM_TRIANGLE_STRIP_ADJACENCY)) {
      ps5_draw_vbo_without_adjacency(base, info, drawid_offset, draws);
      pipe_resource_reference(&uploaded_indices, NULL);
      return;
   }
   /* ponytail: one hardware queue lock; split per queue if parallel submit
    * becomes measurable and the runtime stops using process-global setters. */
   ps5_screen_submit_lock(&screen->base);
   ps5_draw_vbo_locked(base, info, drawid_offset, indirect, draws, num_draws);
   ps5_screen_submit_unlock(&screen->base);
   pipe_resource_reference(&uploaded_indices, NULL);
   if (context->last_draw_status != 0)
      printf("[ps5-gallium] draw-rejected status=%d mode=%u count=%u index=%u streamout=%u geometry=%u\n",
             context->last_draw_status, info ? info->mode : UINT32_MAX,
             draws && num_draws ? draws[0].count : 0,
             info ? info->index_size : 0,
             context->stream_output_target_count, context->gs != NULL);
}

static void
ps5_set_framebuffer_state(struct pipe_context *base,
                          const struct pipe_framebuffer_state *framebuffer)
{
   struct ps5_context *context = (struct ps5_context *)base;
   unsigned max_targets = PS5_ENABLE_MRT_CANDIDATE
                             ? PS5_MAX_RENDER_TARGETS : 1;
   bool colors_valid = framebuffer && framebuffer->nr_cbufs <= max_targets;
   bool has_color = false;
   bool has_depth = framebuffer && framebuffer->zsbuf.texture;
   unsigned color_samples = 0;

   if (colors_valid) {
      for (unsigned i = 0; i < framebuffer->nr_cbufs; ++i) {
         const struct pipe_surface *surface = &framebuffer->cbufs[i];

         if (!surface->texture)
            continue;
         has_color = true;
         if (!color_samples)
            color_samples = MAX2(surface->texture->nr_samples, 1);
         else if (color_samples != MAX2(surface->texture->nr_samples, 1))
            colors_valid = false;
         bool single_layer = surface->first_layer == surface->last_layer;

         colors_valid = colors_valid &&
            surface->level <= surface->texture->last_level &&
            ((surface->texture->target == PIPE_TEXTURE_2D &&
              surface->first_layer == 0 && single_layer) ||
             (PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE &&
              (surface->texture->target == PIPE_TEXTURE_2D_ARRAY ||
               surface->texture->target == PIPE_TEXTURE_CUBE ||
               surface->texture->target == PIPE_TEXTURE_3D) &&
              surface->first_layer <= surface->last_layer &&
              surface->last_layer < ps5_surface_layer_count(surface))) &&
            (!ps5_linear_sampled_layout(surface->texture) ||
             (((struct ps5_resource *)surface->texture)
                 ->render_staging_size)) &&
            ps5_render_target_format(surface->format) &&
            ((PS5_ENABLE_PADDED_FBO_CANDIDATE ||
              PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE)
                ? (ps5_surface_width(surface) >= framebuffer->width &&
                   ps5_surface_height(surface) >= framebuffer->height &&
                   ps5_surface_width(surface) <= PS5_MAX_COLOR_WIDTH &&
                   ps5_surface_height(surface) <= PS5_MAX_COLOR_HEIGHT)
                : (ps5_surface_width(surface) == PS5_RENDER_WIDTH &&
                   ps5_surface_height(surface) == PS5_RENDER_HEIGHT));
         if (!colors_valid)
            break;
      }
   }

   util_copy_framebuffer_state(&context->framebuffer, framebuffer);
   context->framebuffer_valid = framebuffer && colors_valid &&
      (has_color || has_depth) &&
      ((PS5_ENABLE_PADDED_FBO_CANDIDATE ||
        PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE)
          ? (framebuffer->width > 0 && framebuffer->height > 0 &&
             framebuffer->width <= PS5_MAX_COLOR_WIDTH &&
             framebuffer->height <= PS5_MAX_COLOR_HEIGHT)
          : (framebuffer->width == PS5_RENDER_WIDTH &&
             framebuffer->height == PS5_RENDER_HEIGHT)) &&
      (!framebuffer->zsbuf.texture ||
        ((!has_color || MAX2(framebuffer->zsbuf.texture->nr_samples, 1) ==
                         color_samples) &&
        (framebuffer->zsbuf.format == PIPE_FORMAT_Z32_FLOAT ||
         framebuffer->zsbuf.format ==
            PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) &&
        ps5_depth_render_target(framebuffer->zsbuf.texture->target) &&
        (PS5_ENABLE_PADDED_FBO_CANDIDATE
            ? (ps5_surface_width(&framebuffer->zsbuf) >=
                  framebuffer->width &&
               ps5_surface_height(&framebuffer->zsbuf) >=
                  framebuffer->height &&
               ps5_surface_width(&framebuffer->zsbuf) <=
                  PS5_MAX_DEPTH_WIDTH &&
               ps5_surface_height(&framebuffer->zsbuf) <=
                  PS5_MAX_DEPTH_HEIGHT)
            : (ps5_surface_width(&framebuffer->zsbuf) ==
                  PS5_RENDER_WIDTH &&
               ps5_surface_height(&framebuffer->zsbuf) ==
                  PS5_RENDER_HEIGHT)) &&
        framebuffer->zsbuf.level <=
           framebuffer->zsbuf.texture->last_level &&
        framebuffer->zsbuf.first_layer <=
           framebuffer->zsbuf.last_layer &&
        framebuffer->zsbuf.last_layer <
           ps5_surface_layer_count(&framebuffer->zsbuf)));
   if (PS5_PUBLIC_TEXTURE_RG_RENDER_TEST)
      printf("[ps5-gallium] rg-framebuffer size=%ux%u targets=%u format=%u "
             "colors-valid=%u has-color=%u valid=%u\n",
             framebuffer ? framebuffer->width : 0,
             framebuffer ? framebuffer->height : 0,
             framebuffer ? framebuffer->nr_cbufs : 0,
             framebuffer && framebuffer->nr_cbufs
                ? framebuffer->cbufs[0].format : 0,
             colors_valid, has_color, context->framebuffer_valid);
}

static void
ps5_clear_bounds(const struct pipe_scissor_state *scissor,
                 unsigned width, unsigned height,
                 unsigned *min_x, unsigned *min_y,
                 unsigned *max_x, unsigned *max_y)
{
   *min_x = scissor ? MIN2(scissor->minx, width) : 0;
   *min_y = scissor ? MIN2(scissor->miny, height) : 0;
   *max_x = scissor ? MIN2(scissor->maxx, width) : width;
   *max_y = scissor ? MIN2(scissor->maxy, height) : height;
}

static bool
ps5_clear_msaa4_color(struct ps5_context *context, unsigned buffers,
                      uint32_t color_clear_mask,
                      const struct pipe_scissor_state *scissor_state,
                      const union pipe_color_union *color)
{
   unsigned color_buffers = buffers & PIPE_CLEAR_COLOR;

   if (!color_buffers || !color)
      return false;
   for (unsigned target_index = 0;
        target_index < context->framebuffer.nr_cbufs; ++target_index) {
      unsigned bit = PIPE_CLEAR_COLOR0 << target_index;
      const struct pipe_surface *surface =
         &context->framebuffer.cbufs[target_index];

      if (!(color_buffers & bit))
         continue;
      color_buffers &= ~bit;
      if (!surface->texture || surface->texture->nr_samples != 4 ||
          surface->texture->nr_storage_samples != 4 ||
          !ps5_msaa4_color_format(surface->format))
         return false;
   }
   if (color_buffers)
      return false;

   for (unsigned target_index = 0;
        target_index < context->framebuffer.nr_cbufs; ++target_index) {
      unsigned bit = PIPE_CLEAR_COLOR0 << target_index;
      unsigned write_mask =
         (color_clear_mask >> (4u * target_index)) & UINT32_C(0xf);
      const struct pipe_surface *surface =
         &context->framebuffer.cbufs[target_index];
      struct ps5_resource *target;
      unsigned width;
      unsigned height;
      unsigned min_x, min_y, max_x, max_y;
      unsigned pixel_size;
      uint8_t packed[16];

      if (!(buffers & bit))
         continue;
      target = (struct ps5_resource *)surface->texture;
      width = ps5_surface_width(surface);
      height = ps5_surface_height(surface);
      pixel_size = ps5_texture_format_size(surface->format);
      if (!pixel_size || pixel_size > sizeof(packed))
         return false;
      ps5_clear_bounds(scissor_state, width, height,
                       &min_x, &min_y, &max_x, &max_y);
      util_format_pack_rgba(surface->format, packed, color->ui, 1);
      for (unsigned layer = surface->first_layer;
           layer <= surface->last_layer; ++layer) {
         size_t layer_base = (size_t)layer * target->layer_stride;

         for (unsigned y = min_y; y < max_y; ++y) {
            for (unsigned x = min_x; x < max_x; ++x) {
               for (unsigned sample = 0; sample < 4; ++sample) {
                  size_t offset = layer_base + ps5_tiled_color_msaa4_offset(
                     surface->format, x, y, sample, width);

                  if (offset > target->allocation_size ||
                      target->allocation_size - offset < pixel_size)
                     return false;
                  if (write_mask == PIPE_MASK_RGBA) {
                     memcpy(target->data + offset, packed, pixel_size);
                  } else {
                     union pipe_color_union merged = {{0}};

                     util_format_unpack_rgba(surface->format, merged.ui,
                                             target->data + offset, 1);
                     for (unsigned channel = 0; channel < 4; ++channel) {
                        if (write_mask & BITFIELD_BIT(channel))
                           merged.ui[channel] = color->ui[channel];
                     }
                     util_format_pack_rgba(surface->format,
                                           target->data + offset,
                                           merged.ui, 1);
                  }
               }
            }
         }
      }
      ps5_flush_gpu_data(target->data, target->allocation_size);
   }
   printf("[ps5-gallium] clear-msaa4-color targets=%u mask=%08x scissor=%u\n",
          context->framebuffer.nr_cbufs, color_clear_mask,
          scissor_state != NULL);
   return true;
}

static bool
ps5_clear_gpu_color(struct ps5_context *context, unsigned buffers,
                    uint32_t color_clear_mask,
                    const struct pipe_scissor_state *scissor_state,
                    const union pipe_color_union *color)
{
   /* ponytail: accelerate only full, single-layer RGBA8 clears. Extend after
    * affected format/mask/query tests; every other case keeps the CPU path. */
   if (!PS5_ENABLE_MRT_CANDIDATE || !PS5_ENABLE_UBO_CANDIDATE || !context ||
       !context->framebuffer_valid || !color || scissor_state ||
       (buffers & PIPE_CLEAR_COLOR) != PIPE_CLEAR_COLOR0 ||
       (color_clear_mask & PIPE_MASK_RGBA) != PIPE_MASK_RGBA ||
       context->framebuffer.nr_cbufs != 1 || context->render_condition_query ||
       context->stream_output_target_count || context->active_occlusion_query ||
       context->active_primitives_generated_query || context->active_primitives_emitted_query)
      return false;

   const struct pipe_surface *surface = &context->framebuffer.cbufs[0];
   const struct ps5_resource *target = (const struct ps5_resource *)surface->texture;
   if (!target || target->base.target != PIPE_TEXTURE_2D ||
       target->base.nr_samples > 1 || target->base.nr_storage_samples > 1 ||
       target->render_staging_size || surface->level || surface->first_layer ||
       surface->last_layer || surface->format != PIPE_FORMAT_R8G8B8A8_UNORM ||
       target->base.format != surface->format ||
       context->framebuffer.width != ps5_surface_width(surface) ||
       context->framebuffer.height != ps5_surface_height(surface))
      return false;

   /* Slot zero may be an inline uniform copy, not a resource. Preserve its
    * bytes before u_blitter temporarily replaces it with the clear color. */
   const struct ps5_constant_state *state = &context->constants[1][0];
   uint8_t copied_constants[PS5_MAX_CONSTANT_BUFFER_SIZE];
   struct pipe_constant_buffer cb = {0};
   if (state->valid) {
      cb.buffer = state->buffer;
      cb.buffer_offset = state->offset;
      cb.buffer_size = state->size;
      if (state->copied) {
         const struct ps5_resource *storage =
            (const struct ps5_resource *)context->descriptor_storage[1];
         size_t offset = ps5_copied_constant_offset(1);
         if (!storage || offset > storage->size ||
             state->size > sizeof(copied_constants) || state->size > storage->size - offset)
            return false;
         memcpy(copied_constants, storage->data + offset, state->size);
         cb.user_buffer = copied_constants;
      }
   }
   if (!context->blitter)
      context->blitter = util_blitter_create(&context->base);
   struct blitter_context *blitter = context->blitter;
   if (!blitter || blitter->running)
      return false;

   bool viewport_valid = context->viewport_valid;
   bool queries_enabled = context->queries_enabled;
   unsigned draws_before = context->draw_calls;
   util_blitter_save_vertex_buffers(blitter, context->vertex_buffers, context->vertex_buffer_count);
   util_blitter_save_vertex_elements(blitter, context->vertex_elements);
   util_blitter_save_vertex_shader(blitter, context->vs);
   util_blitter_save_geometry_shader(blitter, context->gs);
   util_blitter_save_so_targets(blitter, 0, NULL, context->stream_output_primitive);
   util_blitter_save_rasterizer(blitter, context->rasterizer);
   util_blitter_save_fragment_shader(blitter, context->fs);
   util_blitter_save_depth_stencil_alpha(blitter, context->depth_stencil_alpha);
   util_blitter_save_blend(blitter, context->blend);
   util_blitter_save_stencil_ref(blitter, &context->stencil_ref);
   util_blitter_save_viewport(blitter, &context->viewport);
   util_blitter_save_sample_mask(blitter, context->sample_mask, 1);
   util_blitter_save_fragment_constant_buffer_slot(blitter, &cb);

   /* Match the CPU fallback's RGBA8 quantization exactly. */
   uint8_t packed[4];
   union pipe_color_union quantized;
   util_format_pack_rgba(surface->format, packed, color->ui, 1);
   util_format_unpack_rgba(surface->format, quantized.ui, packed, 1);
   util_blitter_clear(blitter, context->framebuffer.width, context->framebuffer.height,
                      1, PIPE_CLEAR_COLOR0, &quantized, 0, 0, false);
   context->viewport_valid = viewport_valid;
   context->queries_enabled = queries_enabled;
   if (context->draw_calls == draws_before)
      context->last_draw_status = -30; /* Blitter upload failed before drawing. */
   if (context->last_draw_status != 0 || draws_before < 3)
      printf("[ps5-gallium] clear-gpu-color status=%d draws=%u\n",
             context->last_draw_status, context->draw_calls - draws_before);
   /* Never hide an attempted GPU failure by retrying it on the CPU. */
   return true;
}

static void
ps5_clear(struct pipe_context *base, unsigned buffers,
          uint32_t color_clear_mask, uint8_t stencil_clear_mask,
          const struct pipe_scissor_state *scissor_state,
          const union pipe_color_union *color, double depth,
          unsigned stencil)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct ps5_resource *resource;
   uint32_t clear_bits;
   uint32_t *words;
   size_t count;
   size_t index;
   size_t depth_layer_size = 0;
   size_t stencil_layer_size = 0;
   unsigned first_depth_layer = 0;
   unsigned last_depth_layer = 0;

   resource = context && context->framebuffer.zsbuf.texture
                 ? (struct ps5_resource *)context->framebuffer.zsbuf.texture
                 : NULL;
   if (resource) {
      depth_layer_size = ps5_tiled_depth_surface_size(
         resource->base.width0, resource->base.height0,
         resource->base.nr_samples);
      stencil_layer_size = ps5_tiled_stencil_surface_size_samples(
         resource->base.width0, resource->base.height0,
         resource->base.nr_samples);
      first_depth_layer = context->framebuffer.zsbuf.first_layer;
      last_depth_layer = context->framebuffer.zsbuf.last_layer;
   }
   if (context && !ps5_render_condition_passes(context))
      return;
   if (ps5_clear_gpu_color(context, buffers, color_clear_mask, scissor_state, color)) {
      buffers &= ~PIPE_CLEAR_COLOR;
      if (!buffers || context->last_draw_status != 0)
         return;
   }
   if (PS5_ENABLE_MSAA4_CANDIDATE && context &&
       context->framebuffer_valid &&
       ps5_clear_msaa4_color(context, buffers, color_clear_mask,
                             scissor_state, color)) {
      buffers &= ~PIPE_CLEAR_COLOR;
      if (!buffers)
         return;
   }
   if (PS5_ENABLE_MRT_CANDIDATE && context &&
       context->framebuffer_valid && (buffers & PIPE_CLEAR_COLOR) &&
       color) {
      unsigned color_buffers = buffers & PIPE_CLEAR_COLOR;

      for (unsigned target_index = 0;
           target_index < context->framebuffer.nr_cbufs; ++target_index) {
         unsigned bit = PIPE_CLEAR_COLOR0 << target_index;
         struct pipe_surface *surface =
            &context->framebuffer.cbufs[target_index];

         if (!(color_buffers & bit))
            continue;
         color_buffers &= ~bit;
         if (!surface->texture ||
             !ps5_render_target_format(surface->format) ||
             surface->texture->nr_samples > 1)
            goto reject;
      }
      if (color_buffers)
         goto reject;
      for (unsigned target_index = 0;
           target_index < context->framebuffer.nr_cbufs; ++target_index) {
         unsigned bit = PIPE_CLEAR_COLOR0 << target_index;
         unsigned write_mask =
            (color_clear_mask >> (4u * target_index)) & UINT32_C(0xf);
         const struct pipe_surface *surface =
            &context->framebuffer.cbufs[target_index];
         struct ps5_resource *target;
         size_t layer_base;
         unsigned width;
         unsigned height;
         unsigned min_x, min_y, max_x, max_y;
         unsigned format_size;
         uint8_t packed[16];

         if (!(buffers & bit))
            continue;
         target = (struct ps5_resource *)
            context->framebuffer.cbufs[target_index].texture;
         width = ps5_surface_width(surface);
         height = ps5_surface_height(surface);
         format_size = util_format_get_blocksize(surface->format);
         if (!format_size || format_size > sizeof(packed))
            goto reject;
         ps5_clear_bounds(scissor_state, width, height,
                          &min_x, &min_y, &max_x, &max_y);
         util_format_pack_rgba(surface->format, packed, color->ui, 1);
         for (unsigned layer = surface->first_layer;
              layer <= surface->last_layer; ++layer) {
            layer_base = (size_t)layer * target->layer_stride +
                         target->level_offset[surface->level];
            for (unsigned y = min_y; y < max_y; ++y) {
               for (unsigned x = min_x; x < max_x; ++x) {
                  size_t offset = target->render_staging_size
                     ? layer_base +
                          (size_t)y * target->level_stride[surface->level] +
                          (size_t)x * format_size
                     : layer_base + ps5_tiled_color_offset(
                          target->base.format, x, y, width);
                  size_t limit = target->render_staging_size
                                    ? target->size : target->allocation_size;

                  if (offset > limit || limit - offset < format_size)
                     goto reject;
                  if (write_mask == PIPE_MASK_RGBA) {
                     memcpy(target->data + offset, packed, format_size);
                  } else {
                     union pipe_color_union merged;

                     util_format_unpack_rgba(surface->format, merged.ui,
                                             target->data + offset, 1);
                     for (unsigned channel = 0; channel < 4; ++channel) {
                        if (write_mask & BITFIELD_BIT(channel))
                           merged.ui[channel] = color->ui[channel];
                     }
                     util_format_pack_rgba(surface->format,
                                           target->data + offset,
                                           merged.ui, 1);
                  }
               }
            }
         }
         ps5_flush_gpu_data(target->data, target->render_staging_size
                                             ? target->size
                                             : target->allocation_size);
      }
      printf("[ps5-gallium] clear-mrt-color targets=%u mask=%08x scissor=%u\n",
             context->framebuffer.nr_cbufs, color_clear_mask,
             scissor_state != NULL);
      buffers &= ~PIPE_CLEAR_COLOR;
      if (!buffers)
         return;
   }
   if (resource && resource->depth_staging_size &&
       (buffers & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL))) {
      const struct pipe_surface *surface = &context->framebuffer.zsbuf;
      unsigned width = ps5_surface_width(surface);
      unsigned height = ps5_surface_height(surface);
      unsigned min_x, min_y, max_x, max_y;

      if (!context->framebuffer_valid ||
          buffers != PIPE_CLEAR_DEPTH ||
          resource->base.format != PIPE_FORMAT_Z32_FLOAT ||
          first_depth_layer > last_depth_layer ||
          last_depth_layer >= ps5_surface_layer_count(surface) ||
          !(depth >= 0.0 && depth <= 1.0))
         goto reject;

      ps5_clear_bounds(scissor_state, width, height,
                       &min_x, &min_y, &max_x, &max_y);
      clear_bits = ps5_float_bits((float)depth);
      for (unsigned layer = first_depth_layer;
           layer <= last_depth_layer; ++layer) {
         size_t layer_base = (size_t)layer * resource->layer_stride +
                             resource->level_offset[surface->level];

         for (unsigned y = min_y; y < max_y; ++y) {
            for (unsigned x = min_x; x < max_x; ++x) {
               size_t offset = layer_base +
                  (size_t)y * resource->level_stride[surface->level] +
                  (size_t)x * sizeof(clear_bits);

               if (offset > resource->size ||
                   resource->size - offset < sizeof(clear_bits))
                  goto reject;
               memcpy(resource->data + offset, &clear_bits,
                      sizeof(clear_bits));
            }
         }
      }
      ps5_flush_gpu_data(resource->data, resource->size);
      printf("[ps5-gallium] clear-depth-mip level=%u size=%ux%u layers=%u-%u depth=%.9g/%08x scissor=%u\n",
             surface->level, width, height, first_depth_layer,
             last_depth_layer, depth, clear_bits,
             scissor_state != NULL);
      return;
   }
   if (!context || !context->framebuffer_valid ||
       !resource || !(buffers & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL)) ||
       (buffers & ~(PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL)) ||
       (resource->base.format != PIPE_FORMAT_Z32_FLOAT &&
        resource->base.format != PIPE_FORMAT_Z32_FLOAT_S8X24_UINT) ||
       !depth_layer_size || !stencil_layer_size ||
       first_depth_layer > last_depth_layer ||
       last_depth_layer >=
          ps5_surface_layer_count(&context->framebuffer.zsbuf) ||
       ((buffers & PIPE_CLEAR_STENCIL) &&
        (resource->base.format != PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
         !resource->stencil_data ||
         last_depth_layer >=
            resource->stencil_allocation_size / stencil_layer_size)) ||
       ((buffers & PIPE_CLEAR_DEPTH) &&
        (last_depth_layer >= resource->allocation_size / depth_layer_size ||
         depth_layer_size % sizeof(uint32_t) ||
         !(depth >= 0.0 && depth <= 1.0)))) {
reject:
      printf("[ps5-gallium] reject-clear buffers=%08x scissor=%u format=%u depth=%.9g\n",
             buffers, scissor_state != NULL,
             resource ? resource->base.format : 0, depth);
      return;
   }

   clear_bits = ps5_float_bits((float)depth);
   if (buffers & PIPE_CLEAR_DEPTH) {
      for (unsigned layer = first_depth_layer;
           layer <= last_depth_layer; ++layer) {
         uint8_t *layer_data = resource->data + layer * depth_layer_size;

         if (!scissor_state) {
            words = (uint32_t *)layer_data;
            count = depth_layer_size / sizeof(*words);
            for (index = 0; index < count; ++index)
               words[index] = clear_bits;
         } else {
            unsigned min_x, min_y, max_x, max_y;

            ps5_clear_bounds(scissor_state, resource->base.width0,
                             resource->base.height0,
                             &min_x, &min_y, &max_x, &max_y);
            for (unsigned y = min_y; y < max_y; ++y) {
               for (unsigned x = min_x; x < max_x; ++x) {
                  for (unsigned sample = 0;
                       sample < MAX2(resource->base.nr_samples, 1); ++sample) {
                     size_t offset = resource->base.nr_samples == 4
                        ? ps5_tiled_depth_msaa4_offset(
                             x, y, sample, resource->base.width0)
                        : ps5_tiled_depth_offset(
                             x, y, resource->base.width0);

                     if (offset > depth_layer_size ||
                         depth_layer_size - offset < sizeof(clear_bits))
                        goto reject;
                     memcpy(layer_data + offset, &clear_bits,
                            sizeof(clear_bits));
                  }
               }
            }
         }
         ps5_flush_gpu_data(layer_data, depth_layer_size);
      }
   }
   if (buffers & PIPE_CLEAR_STENCIL) {
      uint8_t value = (uint8_t)stencil;

      for (unsigned layer = first_depth_layer;
           layer <= last_depth_layer; ++layer) {
         uint8_t *layer_data = resource->stencil_data +
                               layer * stencil_layer_size;

         if (!scissor_state) {
            for (index = 0; index < stencil_layer_size; ++index)
               layer_data[index] =
                  (layer_data[index] & ~stencil_clear_mask) |
                  (value & stencil_clear_mask);
         } else {
            unsigned min_x, min_y, max_x, max_y;

            ps5_clear_bounds(scissor_state, resource->base.width0,
                             resource->base.height0,
                             &min_x, &min_y, &max_x, &max_y);
            for (unsigned y = min_y; y < max_y; ++y) {
               for (unsigned x = min_x; x < max_x; ++x) {
                  for (unsigned sample = 0;
                       sample < MAX2(resource->base.nr_samples, 1); ++sample) {
                     size_t offset = resource->base.nr_samples == 4
                        ? ps5_tiled_stencil_msaa4_offset(
                             x, y, sample, resource->base.width0)
                        : ps5_tiled_stencil_offset(
                             x, y, resource->base.width0);

                     if (offset >= stencil_layer_size)
                        goto reject;
                     layer_data[offset] =
                        (layer_data[offset] & ~stencil_clear_mask) |
                        (value & stencil_clear_mask);
                  }
               }
            }
         }
         ps5_flush_gpu_data(layer_data, stencil_layer_size);
      }
   }
   printf("[ps5-gallium] clear-depth-stencil buffers=%08x format=%u size=%ux%u layers=%u-%u samples=%u allocation=%zu/%zu depth=%.9g/%08x stencil=%02x/%02x scissor=%u\n",
          buffers, resource->base.format, resource->base.width0,
          resource->base.height0, first_depth_layer, last_depth_layer,
          resource->base.nr_samples,
          resource->allocation_size, resource->stencil_allocation_size,
          depth, clear_bits, stencil & 0xffu, stencil_clear_mask,
          scissor_state != NULL);
}

static bool
ps5_remove_point_size(nir_builder *builder, nir_instr *instruction,
                      void *data)
{
   nir_intrinsic_instr *intrinsic;

   (void)builder;
   (void)data;
   if (instruction->type != nir_instr_type_intrinsic)
      return false;
   intrinsic = nir_instr_as_intrinsic(instruction);
   if (intrinsic->intrinsic != nir_intrinsic_store_output ||
       nir_intrinsic_io_semantics(intrinsic).location != VARYING_SLOT_PSIZ)
      return false;
   nir_instr_remove(instruction);
   return true;
}

static bool
ps5_rebase_meta_vertex_input(nir_builder *builder, nir_instr *instruction,
                             void *data)
{
   const uint64_t inputs = *(const uint64_t *)data;
   nir_intrinsic_instr *intrinsic;
   nir_io_semantics semantics;

   (void)builder;
   if (instruction->type != nir_instr_type_intrinsic)
      return false;
   intrinsic = nir_instr_as_intrinsic(instruction);
   if (intrinsic->intrinsic != nir_intrinsic_load_input)
      return false;
   semantics = nir_intrinsic_io_semantics(intrinsic);
   /* Gallium elements are in used-slot order. Rebase the complete input set
    * when a meta shader uses legacy slots such as VERT_ATTRIB_POS, because
    * RADV vertex lowering expects VERT_ATTRIB_GENERIC0-based locations. */
   semantics.location = VERT_ATTRIB_GENERIC0 +
      util_bitcount64(inputs & BITFIELD64_MASK(semantics.location));
   nir_intrinsic_set_io_semantics(intrinsic, semantics);
   return true;
}

static bool
ps5_lower_fragment_color(nir_builder *builder, nir_instr *instruction,
                         void *data)
{
   const unsigned color_mask = *(const unsigned *)data;
   nir_intrinsic_instr *intrinsic;
   nir_io_semantics semantics;

   if (instruction->type != nir_instr_type_intrinsic)
      return false;
   intrinsic = nir_instr_as_intrinsic(instruction);
   if (intrinsic->intrinsic != nir_intrinsic_store_output)
      return false;
   semantics = nir_intrinsic_io_semantics(intrinsic);
   if (semantics.location != FRAG_RESULT_COLOR)
      return false;
   /* Mesa's builtin clear writes one untyped color to every enabled target.
    * RADV expects explicit per-target outputs, including sparse draw slots. */
   builder->cursor = nir_before_instr(instruction);
   u_foreach_bit(slot, color_mask) {
      nir_intrinsic_instr *store = nir_instr_as_intrinsic(
         nir_instr_clone(builder->shader, instruction));
      semantics.location = FRAG_RESULT_DATA0 + slot;
      nir_intrinsic_set_io_semantics(store, semantics);
      nir_builder_instr_insert(builder, &store->instr);
   }
   nir_instr_remove(instruction);
   return true;
}

static bool
ps5_vertex_format(enum pipe_format format, PsbcVertexFormat *out)
{
   switch (format) {
   case PIPE_FORMAT_R32_FLOAT:
      *out = PSBC_VERTEX_FORMAT_R32_FLOAT;
      return true;
   case PIPE_FORMAT_R32G32_FLOAT:
      *out = PSBC_VERTEX_FORMAT_R32G32_FLOAT;
      return true;
   case PIPE_FORMAT_R32G32B32_FLOAT:
      *out = PSBC_VERTEX_FORMAT_R32G32B32_FLOAT;
      return true;
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
      *out = PSBC_VERTEX_FORMAT_R32G32B32A32_FLOAT;
      return true;
   case PIPE_FORMAT_R32_SINT:
      *out = PSBC_VERTEX_FORMAT_R32_SINT;
      return PS5_ENABLE_INTEGER_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R32G32_SINT:
      *out = PSBC_VERTEX_FORMAT_R32G32_SINT;
      return PS5_ENABLE_INTEGER_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R32G32B32_SINT:
      *out = PSBC_VERTEX_FORMAT_R32G32B32_SINT;
      return PS5_ENABLE_INTEGER_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R32G32B32A32_SINT:
      *out = PSBC_VERTEX_FORMAT_R32G32B32A32_SINT;
      return PS5_ENABLE_INTEGER_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R32_UINT:
      *out = PSBC_VERTEX_FORMAT_R32_UINT;
      return PS5_ENABLE_INTEGER_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R32G32_UINT:
      *out = PSBC_VERTEX_FORMAT_R32G32_UINT;
      return PS5_ENABLE_INTEGER_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R32G32B32_UINT:
      *out = PSBC_VERTEX_FORMAT_R32G32B32_UINT;
      return PS5_ENABLE_INTEGER_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R32G32B32A32_UINT:
      *out = PSBC_VERTEX_FORMAT_R32G32B32A32_UINT;
      return PS5_ENABLE_INTEGER_VERTEX_CANDIDATE;
   case PIPE_FORMAT_B8G8R8A8_UNORM:
      *out = PSBC_VERTEX_FORMAT_B8G8R8A8_UNORM;
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
      *out = PSBC_VERTEX_FORMAT_R10G10B10A2_UNORM;
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE;
   case PIPE_FORMAT_B10G10R10A2_UNORM:
      *out = PSBC_VERTEX_FORMAT_B10G10R10A2_UNORM;
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R10G10B10A2_SNORM:
      *out = PSBC_VERTEX_FORMAT_R10G10B10A2_SNORM;
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE;
   case PIPE_FORMAT_B10G10R10A2_SNORM:
      *out = PSBC_VERTEX_FORMAT_B10G10R10A2_SNORM;
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R10G10B10A2_USCALED:
      *out = PSBC_VERTEX_FORMAT_R10G10B10A2_USCALED;
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE;
   case PIPE_FORMAT_B10G10R10A2_USCALED:
      *out = PSBC_VERTEX_FORMAT_B10G10R10A2_USCALED;
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE;
   case PIPE_FORMAT_R10G10B10A2_SSCALED:
      *out = PSBC_VERTEX_FORMAT_R10G10B10A2_SSCALED;
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE;
   case PIPE_FORMAT_B10G10R10A2_SSCALED:
      *out = PSBC_VERTEX_FORMAT_B10G10R10A2_SSCALED;
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE;
   default:
      return false;
   }
}

static unsigned
ps5_vertex_format_size(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_R32_FLOAT:
      return 4;
   case PIPE_FORMAT_R32G32_FLOAT:
      return 8;
   case PIPE_FORMAT_R32G32B32_FLOAT:
      return 12;
   case PIPE_FORMAT_R32G32B32A32_FLOAT:
      return 16;
   default:
      if (PS5_ENABLE_INTEGER_VERTEX_CANDIDATE &&
          ps5_integer_vertex_format(format))
         return util_format_get_blocksize(format);
      return PS5_ENABLE_PACKED_VERTEX_CANDIDATE &&
             ps5_packed_vertex_format(format) ? 4 : 0;
   }
}

static bool
ps5_default_vertex_layout(const nir_shader *nir,
                          struct ps5_vertex_layout *layout)
{
   uint64_t inputs = nir->info.inputs_read >> VERT_ATTRIB_GENERIC0;
   unsigned location;

   memset(layout, 0, sizeof(*layout));
   for (location = 0; location < 16; ++location) {
      PsbcVertexAttribute *attribute;

      if (!(inputs & BITFIELD64_BIT(location)))
         continue;
      attribute = &layout->attributes[layout->count++];
      attribute->location = location;
      attribute->binding = layout->count - 1;
      attribute->format = PSBC_VERTEX_FORMAT_R32G32B32A32_FLOAT;
      attribute->stride = 16;
      attribute->alignment = 4;
   }
   return layout->count <= PSBC_MAX_VERTEX_ATTRIBUTES;
}

static bool
ps5_vertex_layout_from_state(const struct ps5_shader *shader,
                             const struct ps5_vertex_elements *elements,
                             struct ps5_vertex_layout *layout)
{
   uint64_t inputs = shader->nir->info.inputs_read >> VERT_ATTRIB_GENERIC0;
   unsigned location;
   unsigned element_index = 0;

   memset(layout, 0, sizeof(*layout));
   for (location = 0; location < 16; ++location) {
      const struct pipe_vertex_element *element;
      PsbcVertexAttribute *attribute;

      if (!(inputs & BITFIELD64_BIT(location)))
         continue;
      if (!elements || element_index >= elements->count ||
          layout->count >= PSBC_MAX_VERTEX_ATTRIBUTES)
         return false;
      element = &elements->elements[element_index++];
      attribute = &layout->attributes[layout->count++];
      if (element->dual_slot ||
          !ps5_vertex_format(element->src_format, &attribute->format))
         return false;
      attribute->location = location;
      attribute->binding = element->vertex_buffer_index;
      attribute->offset = element->src_offset;
      attribute->stride = element->src_stride;
      attribute->alignment = 4;
      attribute->instance_divisor = element->instance_divisor;
   }
   return element_index == (elements ? elements->count : 0);
}

static bool
ps5_append_ubo_descriptors(PsbcCompileOptions *options, unsigned first,
                           unsigned count)
{
   if (first + count > 2u * PS5_MAX_CONSTANT_BUFFERS ||
       PSBC_GALLIUM_UBO_BINDING_BASE + first + count >
          PSBC_MAX_DESCRIPTOR_BINDINGS ||
       options->descriptor_binding_count + count >
          PSBC_MAX_DESCRIPTOR_BINDINGS)
      return false;

   for (unsigned index = 0; index < count; ++index) {
      const unsigned binding = first + index;
      PsbcDescriptorBinding *descriptor =
         &options->descriptor_bindings[options->descriptor_binding_count++];

      descriptor->set = 0;
      descriptor->binding = PSBC_GALLIUM_UBO_BINDING_BASE + binding;
      descriptor->type = PSBC_DESCRIPTOR_UNIFORM_BUFFER;
      descriptor->array_size = 1;
      descriptor->offset = PS5_TEXTURE_DESCRIPTOR_BYTES + binding * 16u;
      descriptor->stride = 16;
   }
   return true;
}

static bool
ps5_append_texture_descriptor(PsbcCompileOptions *options, unsigned binding)
{
   PsbcDescriptorBinding *descriptor;

   if (binding >= PS5_MERGED_TEXTURE_UNITS)
      return false;
   for (unsigned index = 0; index < options->descriptor_binding_count;
        ++index) {
      descriptor = &options->descriptor_bindings[index];
      if (descriptor->set != 0 || descriptor->binding != binding)
         continue;
      return descriptor->type == PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER &&
             descriptor->array_size == 1 &&
             descriptor->offset == binding * PS5_TEXTURE_DESCRIPTOR_STRIDE &&
             descriptor->stride == PS5_TEXTURE_DESCRIPTOR_STRIDE;
   }
   if (options->descriptor_binding_count >= PSBC_MAX_DESCRIPTOR_BINDINGS)
      return false;

   descriptor =
      &options->descriptor_bindings[options->descriptor_binding_count++];
   descriptor->set = 0;
   descriptor->binding = binding;
   descriptor->type = PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER;
   descriptor->array_size = 1;
   descriptor->offset = binding * PS5_TEXTURE_DESCRIPTOR_STRIDE;
   descriptor->stride = PS5_TEXTURE_DESCRIPTOR_STRIDE;
   return true;
}

static bool
ps5_shader_compile_options(const struct ps5_shader *shader,
                           uint32_t address32_hi,
                           const struct ps5_vertex_layout *layout,
                           uint32_t primitive_type,
                           bool provoking_vtx_last,
                           PsbcCompileOptions *options)
{
   unsigned texture_count = 0;

   memset(options, 0, sizeof(*options));
   options->target = PSBC_TARGET_PS5;
   options->stage = shader->stage;
   options->entrypoint = "main";
   options->optimise = true;
   options->ngg = shader->stage == PSBC_STAGE_VERTEX;
   options->primitive_type = primitive_type;
   options->provoking_vtx_last = provoking_vtx_last;
   options->address32_hi = address32_hi;
   options->vertex_attribute_count = layout->count;
   memcpy(options->vertex_attributes, layout->attributes,
          layout->count * sizeof(layout->attributes[0]));
   for (unsigned binding = 0; binding < PS5_MAX_TEXTURE_UNITS; ++binding) {
      if (!BITSET_TEST(shader->nir->info.textures_used, binding))
         continue;
      if (!ps5_append_texture_descriptor(options, binding))
         return false;
      texture_count++;
   }
   if (shader->nir->info.num_ubos) {
      if (shader->nir->info.num_ubos >
             (PS5_ENABLE_UBO_CANDIDATE ? PS5_MAX_CONSTANT_BUFFERS : 1) ||
           !ps5_append_ubo_descriptors(options, 0,
                                       shader->nir->info.num_ubos))
         return false;
      if (options->descriptor_binding_count !=
             texture_count + shader->nir->info.num_ubos)
         return false;
   }
   return true;
}

struct ps5_ubo_offset_state {
   unsigned first;
   unsigned source_count;
   bool valid;
};

/* Gallium sampler slots are stage-local, even inside a linked GL program. */
static bool
ps5_offset_geometry_texture(nir_builder *builder, nir_instr *instruction,
                             void *data)
{
   bool *valid = data;
   nir_tex_instr *tex;

   (void)builder;
   if (instruction->type != nir_instr_type_tex)
      return false;
   tex = nir_instr_as_tex(instruction);
   if (tex->texture_index >= PS5_MAX_TEXTURE_UNITS ||
       tex->sampler_index >= PS5_MAX_TEXTURE_UNITS) {
      *valid = false;
      return false;
   }
   tex->texture_index += PS5_MAX_TEXTURE_UNITS;
   tex->sampler_index += PS5_MAX_TEXTURE_UNITS;
   return true;
}

static bool
ps5_offset_ubo_index(nir_builder *builder, nir_instr *instruction, void *data)
{
   struct ps5_ubo_offset_state *state = data;
   nir_intrinsic_instr *intrinsic;
   unsigned index;

   if (instruction->type != nir_instr_type_intrinsic)
      return false;
   intrinsic = nir_instr_as_intrinsic(instruction);
   if (intrinsic->intrinsic != nir_intrinsic_load_ubo)
      return false;
   builder->cursor = nir_before_instr(instruction);
   if (!nir_src_is_const(intrinsic->src[0])) {
      printf("[ps5-gallium] geometry-ubo index=dynamic first=%u count=%u\n",
             state->first, state->source_count);
      if (state->source_count != 1) {
         state->valid = false;
         return false;
      }
      nir_src_rewrite(&intrinsic->src[0],
                      nir_imm_int(builder, state->first));
      return true;
   }
   index = nir_src_as_uint(intrinsic->src[0]);
   printf("[ps5-gallium] geometry-ubo index=%u first=%u count=%u\n",
          index, state->first, state->source_count);
   if (index >= state->source_count ||
       state->first + index >= 2u * PS5_MAX_CONSTANT_BUFFERS) {
      state->valid = false;
      return false;
   }

   nir_src_rewrite(&intrinsic->src[0],
                   nir_imm_int(builder, state->first + index));
   return true;
}

static bool
ps5_stream_output_info_valid(const struct pipe_stream_output_info *info,
                             PsbcStage stage)
{
   unsigned used_buffers = 0;

   if (!info->num_outputs)
      return true;
   if (!PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE ||
       (stage != PSBC_STAGE_VERTEX && stage != PSBC_STAGE_GEOMETRY) ||
       info->num_outputs > PIPE_MAX_SO_OUTPUTS)
      return false;
   for (unsigned index = 0; index < info->num_outputs; ++index) {
      const struct pipe_stream_output *output = &info->output[index];

      if (output->output_buffer >= PIPE_MAX_SO_BUFFERS || output->stream ||
          !output->num_components || output->num_components > 4 ||
          output->start_component + output->num_components > 4 ||
          !info->stride[output->output_buffer] ||
          output->dst_offset + output->num_components >
             info->stride[output->output_buffer])
         return false;
      used_buffers |= BITFIELD_BIT(output->output_buffer);
   }
   for (unsigned buffer = 0; buffer < PIPE_MAX_SO_BUFFERS; ++buffer)
      if (!(used_buffers & BITFIELD_BIT(buffer)) && info->stride[buffer])
         return false;
   return true;
}

static nir_shader *
ps5_stream_output_carrier_nir(const nir_shader *source)
{
   nir_shader *carrier = nir_shader_clone(NULL, source);

   if (!carrier)
      return NULL;
   ralloc_free(carrier->xfb_info);
   carrier->xfb_info = NULL;
   carrier->info.has_transform_feedback_varyings = false;
   memset(carrier->info.xfb_stride, 0, sizeof(carrier->info.xfb_stride));
   return carrier;
}

static bool
ps5_stream_output_lower_instance_id(nir_builder *builder,
                                    nir_intrinsic_instr *intrinsic,
                                    void *data)
{
   nir_def *base_instance;
   nir_def *instance_id;

   (void)data;
   if (intrinsic->intrinsic != nir_intrinsic_load_instance_id)
      return false;

   builder->cursor = nir_after_instr(&intrinsic->instr);
   base_instance = nir_load_base_instance(builder);
   instance_id = nir_iadd(builder, &intrinsic->def, base_instance);
   nir_def_rewrite_uses_after(&intrinsic->def, instance_id);
   BITSET_SET(builder->shader->info.system_values_read,
              SYSTEM_VALUE_BASE_INSTANCE);
   return true;
}

static nir_shader *
ps5_stream_output_split_nir(const nir_shader *source)
{
   nir_shader *split = nir_shader_clone(NULL, source);

   if (!split)
      return NULL;
   nir_shader_intrinsics_pass(split, ps5_stream_output_lower_instance_id,
                              nir_metadata_control_flow, NULL);
   return split;
}

static bool
ps5_stream_output_metadata_matches(
   const struct pipe_stream_output_info *info,
   const PsbcShaderMetadata *metadata)
{
   unsigned mask = 0;

   if (!metadata->streamout_valid)
      return false;
   for (unsigned index = 0; index < info->num_outputs; ++index)
      mask |= BITFIELD_BIT(info->output[index].output_buffer);
   if (metadata->streamout_enabled_stream_buffers_mask != mask)
      return false;
   for (unsigned buffer = 0; buffer < PIPE_MAX_SO_BUFFERS; ++buffer)
      if (metadata->streamout_strides_dwords[buffer] !=
          info->stride[buffer])
         return false;
   return true;
}

static bool
ps5_lower_poly_line_smooth_enabled(nir_builder *builder,
                                   nir_intrinsic_instr *intrinsic,
                                   void *data)
{
   (void)data;
   if (intrinsic->intrinsic !=
       nir_intrinsic_load_poly_line_smooth_enabled)
      return false;

   builder->cursor = nir_before_instr(&intrinsic->instr);
   nir_def_replace(&intrinsic->def, nir_imm_true(builder));
   return true;
}

static bool
ps5_select_shader_variant(struct ps5_shader *shader, uint32_t address32_hi,
                          const struct ps5_vertex_layout *layout,
                          uint32_t primitive_type,
                          bool provoking_vtx_last, bool alpha_to_one,
                          bool poly_line_smooth,
                          const struct ps5_fragment_exports *exports)
{
   const struct ps5_fragment_exports no_exports = {.color_mask = 1};
   const bool broadcast_color = shader->stage == PSBC_STAGE_FRAGMENT &&
      (shader->nir->info.outputs_written & BITFIELD64_BIT(FRAG_RESULT_COLOR));
   struct ps5_shader_variant *variant;
   PsbcCompileOptions options;
   PsbcResult result;
   nir_shader *carrier = NULL;
   nir_shader *variant_nir = NULL;
   nir_shader *streamout_nir = NULL;
   const nir_shader *package_nir;

   if (!exports)
      exports = &no_exports;
   for (variant = shader->variants; variant; variant = variant->next) {
      if (variant->primitive_type == primitive_type &&
          variant->provoking_vtx_last == provoking_vtx_last &&
          variant->alpha_to_one == alpha_to_one &&
          variant->poly_line_smooth == poly_line_smooth &&
          variant->exports.formats == exports->formats &&
          variant->exports.int8_mask == exports->int8_mask &&
          variant->exports.int10_mask == exports->int10_mask &&
          variant->exports.color_mask == exports->color_mask &&
          !memcmp(&variant->layout, layout, sizeof(*layout))) {
         shader->active = variant;
         return true;
      }
   }

   variant = calloc(1, sizeof(*variant));
   if (!variant)
      return false;
   variant->layout = *layout;
   variant->primitive_type = primitive_type;
   variant->provoking_vtx_last = provoking_vtx_last;
   variant->alpha_to_one = alpha_to_one;
   variant->poly_line_smooth = poly_line_smooth;
   variant->exports = *exports;
   if (!ps5_shader_compile_options(shader, address32_hi, layout,
                                   primitive_type, provoking_vtx_last,
                                   &options)) {
      free(variant);
      return false;
   }
   options.spi_shader_col_format = exports->formats;
   options.color_is_int8 = exports->int8_mask;
   options.color_is_int10 = exports->int10_mask;
   if (poly_line_smooth ||
       (shader->stage == PSBC_STAGE_FRAGMENT &&
        shader->nir->info.fs.uses_sample_shading))
      options.rasterization_samples = 4;
   package_nir = shader->nir;
   if (alpha_to_one || poly_line_smooth || broadcast_color) {
      if (shader->stage != PSBC_STAGE_FRAGMENT ||
          !(variant_nir = nir_shader_clone(NULL, shader->nir))) {
         free(variant);
         return false;
      }
      if (broadcast_color) {
         unsigned mask = exports->color_mask;
         nir_shader_instructions_pass(variant_nir, ps5_lower_fragment_color,
                                      nir_metadata_control_flow, &mask);
         nir_shader_gather_info(variant_nir,
                                nir_shader_get_entrypoint(variant_nir));
      }
      if (alpha_to_one)
         nir_lower_alpha_to_one(variant_nir);
      if (poly_line_smooth) {
         nir_lower_poly_line_smooth(variant_nir, 4);
         nir_shader_intrinsics_pass(
            variant_nir, ps5_lower_poly_line_smooth_enabled,
            nir_metadata_control_flow, NULL);
      }
      package_nir = variant_nir;
   }
   if (shader->stream_output.num_outputs) {
      carrier = ps5_stream_output_carrier_nir(package_nir);
      if (!carrier) {
         ralloc_free(variant_nir);
         free(variant);
         return false;
      }
      package_nir = carrier;
   }
   result = psbc_compile_nir(package_nir, &options, &variant->output);
   ralloc_free(carrier);
   ralloc_free(variant_nir);
   printf("[ps5-gallium] compile-shader stage=%u primitive=%u provoking-last=%u smooth=%u variant-attrs=%u result=%d bytes=%zu hash=%08x user-sgprs=%u scratch=%u/%u table=%u\n",
          shader->stage, primitive_type, provoking_vtx_last,
          poly_line_smooth, layout->count,
          result,
          variant->output.machine_code_size,
          ps5_hash32(variant->output.machine_code,
                     variant->output.machine_code_size),
          variant->output.metadata.user_sgpr_count,
          variant->output.metadata.scratch_bytes_per_wave,
          variant->output.metadata.scratch_size_per_thread,
          variant->output.metadata.scratch_buffer_table_user_data_dword);
   if (variant->output.metadata.input_semantic_count)
      printf("[ps5-gallium] input-semantics count=%u first=%08x\n",
             variant->output.metadata.input_semantic_count,
             variant->output.metadata.input_semantics[0]);
   if (result != PSBC_RESULT_OK ||
       ps5_agc_package_build(&variant->output, 4, &variant->package,
                             &variant->package_size) != 0) {
      psbc_free_output(&variant->output);
      free(variant);
      return false;
   }
   if (shader->stream_output.num_outputs) {
      streamout_nir = ps5_stream_output_split_nir(shader->nir);
      if (!streamout_nir) {
         free(variant->package);
         psbc_free_output(&variant->output);
         free(variant);
         return false;
      }
      result = psbc_compile_nir(streamout_nir, &options,
                                &variant->streamout_output);
      ralloc_free(streamout_nir);
      printf("[ps5-gallium] compile-streamout result=%d bytes=%zu hash=%08x user-sgprs=%u mask=%x instance-base=%u/%u\n",
             result, variant->streamout_output.machine_code_size,
             ps5_hash32(variant->streamout_output.machine_code,
                        variant->streamout_output.machine_code_size),
             variant->streamout_output.metadata.user_sgpr_count,
             variant->streamout_output.metadata
                .streamout_enabled_stream_buffers_mask,
             variant->streamout_output.metadata.start_instance_valid,
             variant->streamout_output.metadata
                .start_instance_user_data_dword);
      if (result != PSBC_RESULT_OK ||
          !ps5_stream_output_metadata_matches(
             &shader->stream_output,
             &variant->streamout_output.metadata) ||
          ps5_agc_package_build(&variant->streamout_output, 4,
                                &variant->streamout_package,
                                &variant->streamout_package_size) != 0) {
         free(variant->package);
         free(variant->streamout_package);
         psbc_free_output(&variant->streamout_output);
         psbc_free_output(&variant->output);
         free(variant);
         return false;
      }
   }
   variant->next = shader->variants;
   shader->variants = variant;
   shader->active = variant;
   return true;
}

static void
ps5_release_geometry_pipeline(struct ps5_context *context)
{
   free(context->geometry_package);
   context->geometry_package = NULL;
   context->geometry_package_size = 0;
   psbc_free_output(&context->geometry_output);
   free(context->geometry_streamout_package);
   context->geometry_streamout_package = NULL;
   context->geometry_streamout_package_size = 0;
   psbc_free_output(&context->geometry_streamout_output);
   free(context->geometry_layout);
   context->geometry_layout = NULL;
   context->geometry_primitive_type = 0;
   context->geometry_provoking_vtx_last = false;
   context->geometry_vs = NULL;
   context->geometry_gs = NULL;
}

static bool
ps5_geometry_ring_itemsize(const PsbcShaderMetadata *metadata,
                           uint32_t *itemsize)
{
   for (uint32_t i = 0; i < metadata->context_register_count; ++i) {
      if (metadata->context_registers[i].offset == UINT16_C(0x2ab)) {
         /* RADV lowers the logical ESGS stride to an immediate on GFX9+.
          * Keep the hardware multiplier at one or GS vertex offsets are
          * scaled once by VGT and again by the generated shader. */
         *itemsize = 1u;
         return true;
      }
   }
   return false;
}

static bool
ps5_select_geometry_pipeline(struct ps5_context *context,
                             uint32_t address32_hi,
                             const struct ps5_vertex_layout *layout,
                             uint32_t primitive_type)
{
   PsbcCompileOptions options;
   PsbcShaderOutput output = {0};
   PsbcShaderOutput streamout_output = {0};
   nir_shader *geometry_nir = NULL;
   nir_shader *carrier_nir = NULL;
   const nir_shader *package_nir;
   struct ps5_ubo_offset_state ubo_offset = {0};
   uint8_t *package = NULL;
   uint8_t *streamout_package = NULL;
   size_t package_size = 0;
   size_t streamout_package_size = 0;
   unsigned merged_textures;
   unsigned vertex_ubos;
   unsigned geometry_ubos;
   bool texture_offset_valid = true;
   uint32_t ring_itemsize;
   uint32_t streamout_ring_itemsize;
   PsbcResult result;
   bool provoking_vtx_last = context->rasterizer &&
                             !context->rasterizer->flatshade_first;

   if (!context->gs)
      return true;
   if (!PS5_ENABLE_GEOMETRY_CANDIDATE)
      return false;
   if (context->geometry_vs == context->vs &&
       context->geometry_gs == context->gs && context->geometry_layout &&
       context->geometry_primitive_type == primitive_type &&
       context->geometry_provoking_vtx_last == provoking_vtx_last &&
       !memcmp(context->geometry_layout, layout, sizeof(*layout)))
      return true;
   if (!ps5_shader_compile_options(context->vs, address32_hi, layout,
                                   primitive_type,
                                   provoking_vtx_last, &options)) {
      printf("[ps5-gallium] geometry-select reject=vertex-options textures=%u ubos=%u\n",
             context->vs->nir->info.num_textures,
             context->vs->nir->info.num_ubos);
      return false;
   }
   merged_textures = ps5_shader_texture_count(context->vs);
   for (unsigned binding = 0; binding < PS5_MAX_TEXTURE_UNITS; ++binding) {
      if (!BITSET_TEST(context->gs->nir->info.textures_used, binding))
         continue;
      if (!ps5_append_texture_descriptor(&options,
                                         PS5_MAX_TEXTURE_UNITS + binding)) {
         printf("[ps5-gallium] geometry-select reject=texture-descriptor binding=%u\n",
                binding);
         return false;
      }
      merged_textures++;
   }
   if (merged_textures != ps5_texture_count(
                             context, context->vs,
                             &context->geometry_output.metadata) ||
       options.descriptor_binding_count !=
          merged_textures + context->vs->nir->info.num_ubos) {
      printf("[ps5-gallium] geometry-select reject=texture-count vertex=%u geometry=%u merged=%u descriptors=%u\n",
             context->vs->nir->info.num_textures,
             context->gs->nir->info.num_textures, merged_textures,
             options.descriptor_binding_count);
      return false;
   }
   vertex_ubos = context->vs->nir->info.num_ubos;
   geometry_ubos = context->gs->nir->info.num_ubos;
   if (vertex_ubos + geometry_ubos > 2u * PS5_MAX_CONSTANT_BUFFERS ||
       !ps5_append_ubo_descriptors(&options, vertex_ubos, geometry_ubos)) {
      printf("[ps5-gallium] geometry-select reject=descriptors vertex-ubos=%u geometry-ubos=%u descriptors=%u\n",
             vertex_ubos, geometry_ubos,
             options.descriptor_binding_count);
      return false;
   }
   geometry_nir = nir_shader_clone(NULL, context->gs->nir);
   if (!geometry_nir) {
      printf("[ps5-gallium] geometry-select reject=clone\n");
      return false;
   }
   nir_shader_instructions_pass(geometry_nir, ps5_offset_geometry_texture,
                                nir_metadata_control_flow,
                                &texture_offset_valid);
   if (!texture_offset_valid) {
      printf("[ps5-gallium] geometry-select reject=texture-remap\n");
      ralloc_free(geometry_nir);
      return false;
   }
   if (geometry_nir->info.num_textures)
      geometry_nir->info.num_textures += PS5_MAX_TEXTURE_UNITS;
   BITSET_SHL(geometry_nir->info.textures_used, PS5_MAX_TEXTURE_UNITS);
   BITSET_SHL(geometry_nir->info.textures_used_by_txf, PS5_MAX_TEXTURE_UNITS);
   BITSET_SHL(geometry_nir->info.texture_buffers, PS5_MAX_TEXTURE_UNITS);
   BITSET_SHL(geometry_nir->info.samplers_used, PS5_MAX_TEXTURE_UNITS);
   nir_foreach_uniform_variable(var, geometry_nir) {
      if (glsl_type_is_sampler(glsl_without_array(var->type)))
         var->data.binding += PS5_MAX_TEXTURE_UNITS;
   }
   ubo_offset.first = vertex_ubos;
   ubo_offset.source_count = geometry_ubos;
   ubo_offset.valid = true;
   if (geometry_ubos) {
      nir_shader_instructions_pass(geometry_nir, ps5_offset_ubo_index,
                                   nir_metadata_control_flow, &ubo_offset);
      geometry_nir->info.num_ubos = vertex_ubos + geometry_ubos;
   }
   if (!ubo_offset.valid) {
      printf("[ps5-gallium] geometry-select reject=ubo-remap\n");
      ralloc_free(geometry_nir);
      return false;
   }
   options.stage = PSBC_STAGE_GEOMETRY;
   options.ngg = true;
   package_nir = geometry_nir;
   if (context->gs->stream_output.num_outputs) {
      carrier_nir = ps5_stream_output_carrier_nir(geometry_nir);
      if (!carrier_nir) {
         ralloc_free(geometry_nir);
         return false;
      }
      package_nir = carrier_nir;
   }
   result = psbc_compile_nir_geometry_pipeline(
      context->vs->nir, package_nir, &options, &output);
   ralloc_free(carrier_nir);
   printf("[ps5-gallium] compile-geometry result=%d bytes=%zu hash=%08x user-sgprs=%u textures=%u/%u/%u ubos=%u/%u descriptors=%u set0=%u/%u address32=%08x\n",
          result, output.machine_code_size,
          ps5_hash32(output.machine_code, output.machine_code_size),
          output.metadata.user_sgpr_count,
          context->vs->nir->info.num_textures,
          context->gs->nir->info.num_textures, merged_textures,
          vertex_ubos, geometry_ubos,
          output.metadata.descriptor_binding_count,
          output.metadata.descriptor_set0_valid,
          output.metadata.descriptor_set0_user_data_dword,
          output.metadata.address32_hi);
   if (result != PSBC_RESULT_OK ||
       !ps5_geometry_ring_itemsize(&output.metadata, &ring_itemsize) ||
       ps5_agc_package_build(&output, ring_itemsize, &package,
                             &package_size) != 0) {
      ralloc_free(geometry_nir);
      free(package);
      psbc_free_output(&output);
      return false;
   }
   if (context->gs->stream_output.num_outputs) {
      options.ps5_global_streamout = true;
      result = psbc_compile_nir_geometry_pipeline(
         context->vs->nir, geometry_nir, &options, &streamout_output);
      printf("[ps5-gallium] compile-geometry-streamout result=%d bytes=%zu hash=%08x user-sgprs=%u mask=%x\n",
             result, streamout_output.machine_code_size,
             ps5_hash32(streamout_output.machine_code,
                        streamout_output.machine_code_size),
             streamout_output.metadata.user_sgpr_count,
             streamout_output.metadata
                .streamout_enabled_stream_buffers_mask);
      if (result != PSBC_RESULT_OK ||
          !ps5_stream_output_metadata_matches(
             &context->gs->stream_output, &streamout_output.metadata) ||
          !ps5_geometry_ring_itemsize(&streamout_output.metadata,
                                      &streamout_ring_itemsize) ||
          ps5_agc_package_build(&streamout_output,
                                streamout_ring_itemsize,
                                &streamout_package,
                                &streamout_package_size) != 0) {
         ralloc_free(geometry_nir);
         free(package);
         free(streamout_package);
         psbc_free_output(&output);
         psbc_free_output(&streamout_output);
         return false;
      }
   }
   ralloc_free(geometry_nir);
   ps5_release_geometry_pipeline(context);
   context->geometry_layout = malloc(sizeof(*layout));
   if (!context->geometry_layout) {
      free(package);
      free(streamout_package);
      psbc_free_output(&output);
      psbc_free_output(&streamout_output);
      return false;
   }
   *context->geometry_layout = *layout;
   context->geometry_vs = context->vs;
   context->geometry_gs = context->gs;
   context->geometry_primitive_type = primitive_type;
   context->geometry_provoking_vtx_last = provoking_vtx_last;
   context->geometry_output = output;
   context->geometry_package = package;
   context->geometry_package_size = package_size;
   context->geometry_streamout_output = streamout_output;
   context->geometry_streamout_package = streamout_package;
   context->geometry_streamout_package_size = streamout_package_size;
   return true;
}

static void *
ps5_create_shader_state(struct pipe_screen *screen,
                        const struct pipe_shader_state *templ, PsbcStage stage,
                        uint32_t address32_hi)
{
   struct ps5_shader *shader;
   struct ps5_vertex_layout layout;
   struct pipe_shader_state converted;

   /* u_blitter's simple VS/FS generators still emit TGSI. Reuse Mesa's
    * translator; the public shader path and PSBC backend remain NIR-only. */
   if (templ && templ->type == PIPE_SHADER_IR_TGSI && templ->tokens &&
       stage != PSBC_STAGE_GEOMETRY && !templ->stream_output.num_outputs) {
      converted = *templ;
      converted.type = PIPE_SHADER_IR_NIR;
      converted.ir.nir = tgsi_to_nir(templ->tokens, screen, false);
      if (converted.ir.nir)
         nir_lower_io_passes(converted.ir.nir, false);
      templ = &converted;
   }

   if (!templ || templ->type != PIPE_SHADER_IR_NIR || !templ->ir.nir ||
       !ps5_stream_output_info_valid(&templ->stream_output, stage) ||
       (templ->stream_output.num_outputs && !templ->ir.nir->xfb_info))
      return NULL;

   /* Mesa's default uniforms arrive as load_uniform intrinsics and CB0 data.
    * PSBC consumes descriptor-backed load_ubo, with the same vec4 layout. */
   if (templ->ir.nir->num_uniforms)
      nir_lower_uniforms_to_ubo(templ->ir.nir, false, false);

   printf("[ps5-gallium] create-shader stage=%u nir-stage=%d io-lowered=%u ubos=%u textures=%u default-ubo=%u uniforms=%u face=%u/%u\n",
          stage, templ->ir.nir->info.stage, templ->ir.nir->info.io_lowered,
          templ->ir.nir->info.num_ubos,
          templ->ir.nir->info.num_textures,
          templ->ir.nir->info.first_ubo_is_default_ubo,
          templ->ir.nir->num_uniforms,
          !!(templ->ir.nir->info.inputs_read & VARYING_BIT_FACE),
          BITSET_TEST(templ->ir.nir->info.system_values_read,
                      SYSTEM_VALUE_FRONT_FACE));
   if (stage == PSBC_STAGE_VERTEX && templ->ir.nir->info.io_lowered &&
       !PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE)
      nir_shader_instructions_pass(templ->ir.nir, ps5_remove_point_size,
                                   nir_metadata_control_flow, NULL);
   if (stage == PSBC_STAGE_VERTEX && templ->ir.nir->info.io_lowered) {
      uint64_t inputs = templ->ir.nir->info.inputs_read;
      if (inputs & BITFIELD64_MASK(VERT_ATTRIB_GENERIC0)) {
         if (util_bitcount64(inputs) > 16) {
            ralloc_free(templ->ir.nir);
            return NULL;
         }
         nir_shader_instructions_pass(templ->ir.nir,
                                      ps5_rebase_meta_vertex_input,
                                      nir_metadata_control_flow,
                                      &inputs);
      }
      nir_opt_algebraic(templ->ir.nir);
      nir_opt_constant_folding(templ->ir.nir);
      nir_opt_dce(templ->ir.nir);
      nir_remove_dead_variables(templ->ir.nir, nir_var_shader_out, NULL);
      nir_shader_gather_info(templ->ir.nir,
                             nir_shader_get_entrypoint(templ->ir.nir));
      templ->ir.nir->num_outputs = 1;
   }
   shader = calloc(1, sizeof(*shader));
   if (!shader) {
      ralloc_free(templ->ir.nir);
      return NULL;
   }
   shader->stage = stage;
   shader->nir = templ->ir.nir;
   shader->stream_output = templ->stream_output;
   if (stage == PSBC_STAGE_VERTEX) {
      if (!ps5_default_vertex_layout(shader->nir, &layout)) {
         ralloc_free(shader->nir);
         free(shader);
         return NULL;
      }
   } else {
      memset(&layout, 0, sizeof(layout));
   }
   if (stage != PSBC_STAGE_GEOMETRY &&
       !ps5_select_shader_variant(shader, address32_hi, &layout, 0, false,
                                  false, false, NULL)) {
      ralloc_free(shader->nir);
      free(shader);
      return NULL;
   }
   return shader;
}

static void *
ps5_create_vs_state(struct pipe_context *context,
                    const struct pipe_shader_state *templ)
{
   struct ps5_context *ps5 = (struct ps5_context *)context;
   struct ps5_resource *descriptors = (struct ps5_resource *)
      ps5->vertex_descriptor_table;

   if (!descriptors)
      return NULL;
   return ps5_create_shader_state(
      context->screen, templ, PSBC_STAGE_VERTEX,
      (uint32_t)((uintptr_t)descriptors->data >> 32));
}

static void *
ps5_create_fs_state(struct pipe_context *context,
                    const struct pipe_shader_state *templ)
{
   struct ps5_context *ps5 = (struct ps5_context *)context;
   struct ps5_resource *storage =
      (struct ps5_resource *)ps5->descriptor_storage[1];

   if (!storage)
      return NULL;
   return ps5_create_shader_state(
      context->screen, templ, PSBC_STAGE_FRAGMENT,
      (uint32_t)((uintptr_t)storage->data >> 32));
}

static void *
ps5_create_gs_state(struct pipe_context *context,
                    const struct pipe_shader_state *templ)
{
   struct ps5_context *ps5 = (struct ps5_context *)context;
   struct ps5_resource *descriptors = (struct ps5_resource *)
      ps5->vertex_descriptor_table;
   void *state;

   if (!PS5_ENABLE_GEOMETRY_CANDIDATE || !descriptors) {
      printf("[ps5-gallium] create-geometry state=failed\n");
      return NULL;
   }
   state = ps5_create_shader_state(
      context->screen, templ, PSBC_STAGE_GEOMETRY,
      (uint32_t)((uintptr_t)descriptors->data >> 32));
   printf("[ps5-gallium] create-geometry state=%s\n",
          state ? "ready" : "failed");
   return state;
}

static void
ps5_bind_vs_state(struct pipe_context *base, void *state)
{
   ((struct ps5_context *)base)->vs = state;
}

static void
ps5_bind_fs_state(struct pipe_context *base, void *state)
{
   ((struct ps5_context *)base)->fs = state;
}

static void
ps5_bind_gs_state(struct pipe_context *base, void *state)
{
   ((struct ps5_context *)base)->gs = state;
   printf("[ps5-gallium] bind-geometry state=%s\n",
          state ? "ready" : "null");
}

static void
ps5_bind_blend_state(struct pipe_context *base, void *state)
{
   struct ps5_context *context = (struct ps5_context *)base;

   context->blend = state;
   if (context->blend && context->blend->logicop_enable)
      context->logicop_used = true;
}

static void
ps5_bind_rasterizer_state(struct pipe_context *base, void *state)
{
   ((struct ps5_context *)base)->rasterizer = state;
}

static void
ps5_bind_depth_stencil_alpha_state(struct pipe_context *base, void *state)
{
   ((struct ps5_context *)base)->depth_stencil_alpha = state;
}

static void *
ps5_copy_state(const void *state, size_t size)
{
   void *copy = malloc(size);

   if (copy)
      memcpy(copy, state, size);
   return copy;
}

static void *
ps5_create_blend_state(struct pipe_context *base,
                       const struct pipe_blend_state *state)
{
   (void)base;
   return ps5_copy_state(state, sizeof(*state));
}

static void *
ps5_create_rasterizer_state(struct pipe_context *base,
                            const struct pipe_rasterizer_state *state)
{
   (void)base;
   return ps5_copy_state(state, sizeof(*state));
}

static void *
ps5_create_depth_stencil_alpha_state(
   struct pipe_context *base,
   const struct pipe_depth_stencil_alpha_state *state)
{
   (void)base;
   return ps5_copy_state(state, sizeof(*state));
}

static void *
ps5_create_vertex_elements_state(struct pipe_context *base,
                                 unsigned count,
                                 const struct pipe_vertex_element *elements)
{
   struct ps5_vertex_elements *state;

   (void)base;
   if (count > PIPE_MAX_ATTRIBS || (count && !elements))
      return NULL;
   state = calloc(1, sizeof(*state));
   if (!state)
      return NULL;
   state->count = count;
   if (count)
      memcpy(state->elements, elements, count * sizeof(*elements));
   return state;
}

static void
ps5_bind_vertex_elements_state(struct pipe_context *base, void *state)
{
   ((struct ps5_context *)base)->vertex_elements = state;
}

static void
ps5_delete_vertex_elements_state(struct pipe_context *base, void *state)
{
   struct ps5_context *context = (struct ps5_context *)base;

   if (context->vertex_elements == state)
      context->vertex_elements = NULL;
   free(state);
}

static void
ps5_delete_fixed_state(struct pipe_context *base, void *state)
{
   (void)base;
   free(state);
}

static void
ps5_set_blend_color(struct pipe_context *base,
                    const struct pipe_blend_color *color)
{
   if (color)
      ((struct ps5_context *)base)->blend_color = *color;
}

static void
ps5_set_scissor_states(struct pipe_context *base, unsigned start,
                       unsigned count,
                       const struct pipe_scissor_state *states)
{
   struct ps5_context *context = (struct ps5_context *)base;

   if (start || count != 1 || !states)
      return;
   context->scissor = states[0];
   context->scissor_valid = true;
}

static void
ps5_set_viewport_states(struct pipe_context *base, unsigned start,
                        unsigned count,
                        const struct pipe_viewport_state *states)
{
   struct ps5_context *context = (struct ps5_context *)base;

   if (start || count != 1 || !states)
      return;
   context->viewport = states[0];
   context->viewport_valid = true;
}

static void
ps5_set_polygon_stipple(struct pipe_context *base,
                        const struct pipe_poly_stipple *stipple)
{
   (void)base;
   (void)stipple;
}

static void
ps5_set_window_rectangles(struct pipe_context *base, bool include,
                          unsigned count,
                          const struct pipe_scissor_state *rectangles)
{
   (void)base;
   (void)include;
   (void)count;
   (void)rectangles;
}

static void
ps5_set_sampler_views(struct pipe_context *base, mesa_shader_stage shader,
                      unsigned start, unsigned count,
                      unsigned unbind_trailing,
                      struct pipe_sampler_view **views)
{
   struct ps5_context *context = (struct ps5_context *)base;
   unsigned index;
   unsigned slot;

   if (shader == MESA_SHADER_VERTEX)
      slot = 0;
   else if (shader == MESA_SHADER_FRAGMENT)
      slot = 1;
   else if (shader == MESA_SHADER_GEOMETRY &&
            PS5_ENABLE_GEOMETRY_CANDIDATE)
      slot = PS5_GEOMETRY_TEXTURE_SLOT;
   else
      return;
   if (start > PS5_MAX_TEXTURE_UNITS ||
       count > PS5_MAX_TEXTURE_UNITS - start ||
       unbind_trailing > PS5_MAX_TEXTURE_UNITS - start - count ||
       (count && !views))
      return;
   for (index = 0; index < count; ++index)
      pipe_sampler_view_reference(
         &context->sampler_views[slot][start + index], views[index]);
   for (; index < count + unbind_trailing; ++index)
      pipe_sampler_view_reference(
         &context->sampler_views[slot][start + index], NULL);
}

static void
ps5_bind_sampler_states(struct pipe_context *base, mesa_shader_stage shader,
                        unsigned start, unsigned count, void **states)
{
   struct ps5_context *context = (struct ps5_context *)base;
   unsigned slot;

   if (shader == MESA_SHADER_VERTEX)
      slot = 0;
   else if (shader == MESA_SHADER_FRAGMENT)
      slot = 1;
   else if (shader == MESA_SHADER_GEOMETRY &&
            PS5_ENABLE_GEOMETRY_CANDIDATE)
      slot = PS5_GEOMETRY_TEXTURE_SLOT;
   else
      return;
   if (start > PS5_MAX_TEXTURE_UNITS ||
       count > PS5_MAX_TEXTURE_UNITS - start ||
       (count && !states))
      return;
   for (unsigned index = 0; index < count; ++index)
      context->samplers[slot][start + index] = states[index];
}

static void *
ps5_create_sampler_state(struct pipe_context *base,
                         const struct pipe_sampler_state *state)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct ps5_sampler_state *copy;
   union pipe_color_union *table;
   unsigned index;
   bool linear_filter;
   bool uses_border;

   if (!state)
      return NULL;
   copy = calloc(1, sizeof(*copy));
   if (!copy)
      return NULL;
   copy->base = *state;
   if (!PS5_ENABLE_BORDER_COLOR_CANDIDATE)
      return copy;

   linear_filter = state->min_img_filter != PIPE_TEX_FILTER_NEAREST ||
                   state->mag_img_filter != PIPE_TEX_FILTER_NEAREST;
#define PS5_WRAP_USES_BORDER(wrap) \
   ((wrap) == PIPE_TEX_WRAP_CLAMP_TO_BORDER || \
    (wrap) == PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER || \
    (linear_filter && ((wrap) == PIPE_TEX_WRAP_CLAMP || \
                       (wrap) == PIPE_TEX_WRAP_MIRROR_CLAMP)))
   uses_border = PS5_WRAP_USES_BORDER(state->wrap_s) ||
                 PS5_WRAP_USES_BORDER(state->wrap_t) ||
                 PS5_WRAP_USES_BORDER(state->wrap_r);
#undef PS5_WRAP_USES_BORDER
   if (!uses_border)
      return copy;

#define PS5_SIMPLE_BORDER(elt) \
   do { \
      if (state->border_color.elt[0] == 0 && \
          state->border_color.elt[1] == 0 && \
          state->border_color.elt[2] == 0 && \
          state->border_color.elt[3] == 0) \
         return copy; \
      if (state->border_color.elt[0] == 0 && \
          state->border_color.elt[1] == 0 && \
          state->border_color.elt[2] == 0 && \
          state->border_color.elt[3] == 1) { \
         copy->border_color_type = 1; \
         return copy; \
      } \
      if (state->border_color.elt[0] == 1 && \
          state->border_color.elt[1] == 1 && \
          state->border_color.elt[2] == 1 && \
          state->border_color.elt[3] == 1) { \
         copy->border_color_type = 2; \
         return copy; \
      } \
   } while (0)
   if (state->border_color_is_integer)
      PS5_SIMPLE_BORDER(ui);
   else
      PS5_SIMPLE_BORDER(f);
#undef PS5_SIMPLE_BORDER

   if (!context->border_color_storage) {
      free(copy);
      return NULL;
   }
   table = (union pipe_color_union *)
      ((struct ps5_resource *)context->border_color_storage)->data;
   for (index = 0; index < context->border_color_count; ++index)
      if (memcmp(&table[index], &state->border_color,
                 sizeof(state->border_color)) == 0)
         break;
   if (index == context->border_color_count) {
      if (index == PS5_BORDER_COLOR_COUNT) {
         free(copy);
         return NULL;
      }
      table[index] = state->border_color;
      context->border_color_count++;
      ps5_flush_gpu_data(&table[index], sizeof(table[index]));
   }
   copy->border_color_ptr = (uint16_t)index;
   copy->border_color_type = 3;
   return copy;
}

static void
ps5_delete_sampler_state(struct pipe_context *base, void *state)
{
   struct ps5_context *context = (struct ps5_context *)base;

   for (unsigned slot = 0; slot < PS5_TEXTURE_STAGE_COUNT; ++slot)
      for (unsigned index = 0; index < PS5_MAX_TEXTURE_UNITS; ++index)
         if (context->samplers[slot][index] == state)
            context->samplers[slot][index] = NULL;
   free(state);
}

static struct pipe_sampler_view *
ps5_create_sampler_view(struct pipe_context *base,
                        struct pipe_resource *texture,
                        const struct pipe_sampler_view *templ)
{
   struct pipe_sampler_view *view;

   if (texture && templ && texture->target == PIPE_BUFFER) {
      if (!PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE ||
          templ->target != PIPE_BUFFER ||
          !ps5_texel_buffer_format(templ->format) ||
          templ->u.buf.offset > texture->width0 ||
          templ->u.buf.size > texture->width0 - templ->u.buf.offset)
         return NULL;
      view = calloc(1, sizeof(*view));
      if (!view)
         return NULL;
      *view = *templ;
      view->reference.count = 1;
      view->texture = NULL;
      pipe_resource_reference(&view->texture, texture);
      view->context = base;
      return view;
   }
   if (!texture || !templ ||
       !ps5_sampled_texture_target(texture->target) ||
       !ps5_sampled_texture_format(texture->format) ||
       templ->target != texture->target ||
       !ps5_sampled_texture_target(templ->target) ||
       templ->format != texture->format ||
       templ->u.tex.first_level > templ->u.tex.last_level ||
       templ->u.tex.last_level > texture->last_level ||
       (templ->u.tex.last_level && !PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE) ||
       ((texture->target == PIPE_TEXTURE_1D ||
         texture->target == PIPE_TEXTURE_2D ||
         texture->target == PIPE_TEXTURE_RECT) &&
        (templ->u.tex.first_layer || templ->u.tex.last_layer)) ||
       (texture->target == PIPE_TEXTURE_CUBE &&
        (templ->u.tex.first_layer ||
         templ->u.tex.last_layer != texture->array_size - 1)) ||
       ((texture->target == PIPE_TEXTURE_1D_ARRAY ||
         texture->target == PIPE_TEXTURE_2D_ARRAY) &&
        (templ->u.tex.first_layer > templ->u.tex.last_layer ||
         templ->u.tex.last_layer >= texture->array_size)) ||
       (texture->target == PIPE_TEXTURE_3D &&
        (templ->u.tex.first_layer || templ->u.tex.last_layer)))
      return NULL;
   view = calloc(1, sizeof(*view));
   if (!view)
      return NULL;
   *view = *templ;
   view->reference.count = 1;
   view->texture = NULL;
   pipe_resource_reference(&view->texture, texture);
   view->context = base;
   return view;
}

static void
ps5_sampler_view_destroy(struct pipe_context *base,
                         struct pipe_sampler_view *view)
{
   (void)base;
   pipe_resource_reference(&view->texture, NULL);
   free(view);
}

static void
ps5_set_stencil_ref(struct pipe_context *base,
                    const struct pipe_stencil_ref ref)
{
   ((struct ps5_context *)base)->stencil_ref = ref;
}

static void
ps5_set_sample_mask(struct pipe_context *base, unsigned sample_mask)
{
   ((struct ps5_context *)base)->sample_mask = sample_mask;
}

static void
ps5_set_constant_buffer(struct pipe_context *base, mesa_shader_stage shader,
                        unsigned index,
                        const struct pipe_constant_buffer *buffer)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct ps5_constant_state *state;
   struct ps5_resource *storage;
   unsigned slot;
   unsigned descriptor_slot;
   unsigned maximum_size;
   size_t destination_offset;
   size_t copied_size;

   if (index >= (PS5_ENABLE_UBO_CANDIDATE
                    ? PS5_MAX_CONSTANT_BUFFERS : 1))
      return;
   switch (shader) {
   case MESA_SHADER_VERTEX:
      slot = 0;
      descriptor_slot = 0;
      break;
   case MESA_SHADER_FRAGMENT:
      slot = 1;
      descriptor_slot = 1;
      break;
   case MESA_SHADER_GEOMETRY:
      if (!PS5_ENABLE_GEOMETRY_CANDIDATE)
         return;
      slot = PS5_GEOMETRY_CONSTANT_SLOT;
      descriptor_slot = 0;
      break;
   default:
      return;
   }
   state = &context->constants[slot][index];
   pipe_resource_reference(&state->buffer, NULL);
   state->offset = 0;
   state->valid = false;
   state->copied = false;
   state->size = 0;
   if (!buffer)
      return;
   maximum_size = PS5_ENABLE_UBO_CANDIDATE
                     ? PS5_MAX_CONSTANT_BUFFER_SIZE
                     : PS5_DIRECT_ALIGNMENT - PS5_CONSTANT_DATA_OFFSET;
   if (!buffer->buffer_size ||
       buffer->buffer_size > maximum_size ||
       (index && (buffer->buffer_offset & 15u)))
      return;
   if (buffer->user_buffer) {
      if (index)
         return;
      storage =
         (struct ps5_resource *)context->descriptor_storage[descriptor_slot];
      destination_offset = ps5_copied_constant_offset(slot);
      copied_size = (buffer->buffer_size + 15u) & ~15u;
      if (!storage || destination_offset > storage->size ||
          copied_size > storage->size - destination_offset)
         return;
      memcpy(storage->data + destination_offset, buffer->user_buffer,
             buffer->buffer_size);
      memset(storage->data + destination_offset + buffer->buffer_size, 0,
             copied_size - buffer->buffer_size);
      state->copied = true;
   } else {
      struct ps5_resource *resource =
         (struct ps5_resource *)buffer->buffer;

      if (!resource || resource->base.target != PIPE_BUFFER ||
          buffer->buffer_offset > resource->size ||
          buffer->buffer_size > resource->size - buffer->buffer_offset)
         return;
      pipe_resource_reference(&state->buffer, buffer->buffer);
      state->offset = buffer->buffer_offset;
   }
   state->size = buffer->buffer_size;
   state->valid = true;
}

static void
ps5_set_vertex_buffers(struct pipe_context *base, unsigned count,
                       const struct pipe_vertex_buffer *buffers)
{
   struct ps5_context *context = (struct ps5_context *)base;
   unsigned index;

   for (index = 0; index < context->vertex_buffer_count; ++index)
      pipe_resource_reference(
         &context->vertex_buffers[index].buffer.resource, NULL);
   memset(context->vertex_buffers, 0, sizeof(context->vertex_buffers));
   context->vertex_buffer_count = 0;
   if (!count)
      return;
   if (count > PIPE_MAX_ATTRIBS || !buffers)
      return;
   for (index = 0; index < count; ++index) {
      if (buffers[index].is_user_buffer || !buffers[index].buffer.resource)
         return;
   }
   for (index = 0; index < count; ++index) {
      context->vertex_buffers[index] = buffers[index];
      context->vertex_buffers[index].buffer.resource = NULL;
      pipe_resource_reference(
         &context->vertex_buffers[index].buffer.resource,
         buffers[index].buffer.resource);
   }
   context->vertex_buffer_count = count;
}

static struct pipe_stream_output_target *
ps5_create_stream_output_target(struct pipe_context *base,
                                struct pipe_resource *buffer,
                                unsigned buffer_offset,
                                unsigned buffer_size)
{
   struct ps5_stream_output_target *target;

   if (!PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE || !buffer ||
       buffer->target != PIPE_BUFFER || (buffer_offset & 3u) ||
       (buffer_size & 3u) || buffer_offset > buffer->width0 ||
       buffer_size > buffer->width0 - buffer_offset)
      return NULL;
   target = calloc(1, sizeof(*target));
   if (!target)
      return NULL;
   target->base.reference.count = 1;
   target->base.context = base;
   pipe_resource_reference(&target->base.buffer, buffer);
   target->base.buffer_offset = buffer_offset;
   target->base.buffer_size = buffer_size;
   return &target->base;
}

static void
ps5_stream_output_target_destroy(struct pipe_context *base,
                                 struct pipe_stream_output_target *pipe_target)
{
   struct ps5_stream_output_target *target =
      (struct ps5_stream_output_target *)pipe_target;

   (void)base;
   pipe_resource_reference(&target->base.buffer, NULL);
   free(target);
}

static uint32_t
ps5_stream_output_target_offset(struct pipe_stream_output_target *pipe_target)
{
   struct ps5_stream_output_target *target =
      (struct ps5_stream_output_target *)pipe_target;

   return target ? target->offset : 0;
}

static bool
ps5_stream_output_primitive_valid(enum mesa_prim primitive)
{
   return primitive == MESA_PRIM_POINTS || primitive == MESA_PRIM_LINES ||
          primitive == MESA_PRIM_TRIANGLES;
}

static void
ps5_set_stream_output_targets(struct pipe_context *base,
                              unsigned count,
                              struct pipe_stream_output_target **targets,
                              const unsigned *offsets,
                              enum mesa_prim output_primitive)
{
   struct ps5_context *context = (struct ps5_context *)base;
   unsigned append_mask = 0;
   unsigned index;

   if (!PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE ||
       count > PIPE_MAX_SO_BUFFERS ||
       (count && (!targets || !offsets ||
                  !ps5_stream_output_primitive_valid(output_primitive))))
      return;
   for (index = 0; index < count; ++index) {
      if (!targets[index])
         continue;
      if (targets[index]->context != base ||
          targets[index]->buffer->target != PIPE_BUFFER)
         return;
      if (offsets[index] == UINT32_MAX) {
         append_mask |= BITFIELD_BIT(index);
      } else if ((offsets[index] & 3u) ||
                 offsets[index] > targets[index]->buffer_size) {
         return;
      }
   }

   for (index = 0; index < count; ++index) {
      pipe_so_target_reference(&context->stream_output_targets[index],
                               targets[index]);
      if (targets[index] && !(append_mask & BITFIELD_BIT(index)))
         ((struct ps5_stream_output_target *)targets[index])->offset =
            offsets[index];
   }
   for (; index < context->stream_output_target_count; ++index)
      pipe_so_target_reference(&context->stream_output_targets[index], NULL);
   context->stream_output_target_count = count;
   context->stream_output_primitive = count ? output_primitive
                                             : MESA_PRIM_UNKNOWN;
}

static void
ps5_buffer_subdata(struct pipe_context *base, struct pipe_resource *resource,
                   unsigned usage, unsigned offset, unsigned size,
                   const void *data)
{
   struct ps5_resource *buffer = (struct ps5_resource *)resource;

   (void)base;
   (void)usage;
   if (!buffer || buffer->base.target != PIPE_BUFFER || !data ||
       offset > buffer->size || size > buffer->size - offset)
      return;
   memcpy(buffer->data + offset, data, size);
}

static void
ps5_sampler_view_release(struct pipe_context *base,
                         struct pipe_sampler_view *view)
{
   u_default_sampler_view_release(base, view);
}

static void
ps5_delete_shader_state(struct pipe_context *base, void *state)
{
   struct ps5_context *context = (struct ps5_context *)base;
   struct ps5_shader *shader = state;
   struct ps5_shader_variant *variant;

   if (!shader)
      return;
   printf("[ps5-gallium] delete-shader stage=%u state=%p\n",
          shader->stage, (void *)shader);
   if (context->vs == shader)
      context->vs = NULL;
   if (context->gs == shader)
      context->gs = NULL;
   if (context->fs == shader)
      context->fs = NULL;
   if (context->geometry_vs == shader || context->geometry_gs == shader)
      ps5_release_geometry_pipeline(context);
   while ((variant = shader->variants)) {
      shader->variants = variant->next;
      free(variant->package);
      free(variant->streamout_package);
      psbc_free_output(&variant->streamout_output);
      psbc_free_output(&variant->output);
      free(variant);
   }
   ralloc_free(shader->nir);
   free(shader);
}

int
ps5_shader_state_info(void *state, size_t *machine_code_size,
                      unsigned *hardware_stage, unsigned *unresolved_fields)
{
   struct ps5_shader *shader = state;

   if (!shader || !shader->active)
      return -1;
   if (machine_code_size)
      *machine_code_size = shader->active->output.machine_code_size;
   if (hardware_stage)
      *hardware_stage = shader->active->output.metadata.hardware_stage;
   if (unresolved_fields)
      *unresolved_fields = shader->active->output.metadata.unresolved_fields;
   return 0;
}

int
ps5_context_last_draw_status(struct pipe_context *base, unsigned *draw_calls)
{
   struct ps5_context *context = (struct ps5_context *)base;

   if (!context)
      return -1;
   if (draw_calls)
      *draw_calls = context->draw_calls;
   return context->last_draw_status;
}

static void
ps5_context_destroy(struct pipe_context *base)
{
   struct ps5_context *context = (struct ps5_context *)base;
   unsigned index;

   if (context->blitter)
      util_blitter_destroy(context->blitter);
   ps5_release_geometry_pipeline(context);
   util_unreference_framebuffer_state(&context->framebuffer);
   for (index = 0; index < context->vertex_buffer_count; ++index)
      pipe_resource_reference(
         &context->vertex_buffers[index].buffer.resource, NULL);
   for (index = 0; index < context->stream_output_target_count; ++index)
      pipe_so_target_reference(&context->stream_output_targets[index], NULL);
   pipe_resource_reference(&context->vertex_descriptor_table, NULL);
   for (unsigned slot = 0; slot < PS5_TEXTURE_STAGE_COUNT; ++slot)
      for (index = 0; index < PS5_MAX_TEXTURE_UNITS; ++index)
         pipe_sampler_view_reference(&context->sampler_views[slot][index],
                                     NULL);
   for (index = 0; index < PS5_CONSTANT_STAGE_COUNT; ++index) {
      unsigned binding;

      for (binding = 0; binding < PS5_MAX_CONSTANT_BUFFERS; ++binding)
         pipe_resource_reference(&context->constants[index][binding].buffer,
                                 NULL);
   }
   for (index = 0; index < PS5_DESCRIPTOR_STAGE_COUNT; ++index)
      pipe_resource_reference(&context->descriptor_storage[index], NULL);
   pipe_resource_reference(&context->border_color_storage, NULL);
   if (base->stream_uploader)
      u_upload_destroy(base->stream_uploader);
   if (base->screen->num_contexts)
      base->screen->num_contexts--;
   free(context);
}

static struct pipe_context *
ps5_context_create(struct pipe_screen *screen, void *priv, unsigned flags)
{
   struct ps5_context *context;
   struct pipe_resource descriptor_template;

   if (flags & ~PIPE_CONTEXT_PREFER_THREADED)
      return NULL;

   context = calloc(1, sizeof(*context));
   if (!context)
      return NULL;

   memset(&descriptor_template, 0, sizeof(descriptor_template));
   descriptor_template.target = PIPE_BUFFER;
   descriptor_template.format = PIPE_FORMAT_R8_UNORM;
   descriptor_template.width0 = PS5_DIRECT_ALIGNMENT;
   descriptor_template.height0 = 1;
   descriptor_template.depth0 = 1;
   descriptor_template.array_size = 1;
   context->vertex_descriptor_table =
      screen->resource_create(screen, &descriptor_template);
   if (!context->vertex_descriptor_table) {
      free(context);
      return NULL;
   }
   descriptor_template.width0 = PS5_ENABLE_UBO_CANDIDATE
                                   ? PS5_DESCRIPTOR_STORAGE_BYTES
                                   : PS5_DIRECT_ALIGNMENT;
   for (unsigned index = 0; index < PS5_DESCRIPTOR_STAGE_COUNT; ++index) {
      context->descriptor_storage[index] =
         screen->resource_create(screen, &descriptor_template);
      if (!context->descriptor_storage[index]) {
         unsigned release;

         for (release = 0; release < index; ++release)
            pipe_resource_reference(
               &context->descriptor_storage[release], NULL);
         pipe_resource_reference(&context->vertex_descriptor_table, NULL);
         free(context);
         return NULL;
      }
   }
   if (PS5_ENABLE_BORDER_COLOR_CANDIDATE) {
      descriptor_template.width0 = PS5_BORDER_COLOR_BYTES;
      context->border_color_storage =
         screen->resource_create(screen, &descriptor_template);
      if (!context->border_color_storage) {
         for (unsigned index = 0; index < PS5_DESCRIPTOR_STAGE_COUNT;
              ++index)
            pipe_resource_reference(&context->descriptor_storage[index],
                                    NULL);
         pipe_resource_reference(&context->vertex_descriptor_table, NULL);
         free(context);
         return NULL;
      }
   }

   context->base.screen = screen;
   context->base.priv = priv;
   context->sample_mask = UINT32_C(0xffff);
   context->queries_enabled = true;
   context->base.destroy = ps5_context_destroy;
   context->base.draw_vbo = ps5_draw_vbo;
   context->base.create_query = ps5_create_query;
   context->base.destroy_query = ps5_destroy_query;
   context->base.begin_query = ps5_begin_query;
   context->base.end_query = ps5_end_query;
   context->base.get_query_result = ps5_get_query_result;
   context->base.set_active_query_state = ps5_set_active_query_state;
   if (PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE)
      context->base.render_condition = ps5_render_condition;
   context->base.create_vs_state = ps5_create_vs_state;
   context->base.bind_vs_state = ps5_bind_vs_state;
   context->base.delete_vs_state = ps5_delete_shader_state;
   context->base.create_fs_state = ps5_create_fs_state;
   context->base.bind_fs_state = ps5_bind_fs_state;
   context->base.delete_fs_state = ps5_delete_shader_state;
   context->base.create_gs_state = ps5_create_gs_state;
   context->base.bind_gs_state = ps5_bind_gs_state;
   context->base.delete_gs_state = ps5_delete_shader_state;
   context->base.create_blend_state = ps5_create_blend_state;
   context->base.bind_blend_state = ps5_bind_blend_state;
   context->base.delete_blend_state = ps5_delete_fixed_state;
   context->base.create_sampler_state = ps5_create_sampler_state;
   context->base.bind_sampler_states = ps5_bind_sampler_states;
   context->base.delete_sampler_state = ps5_delete_sampler_state;
   context->base.create_rasterizer_state = ps5_create_rasterizer_state;
   context->base.bind_rasterizer_state = ps5_bind_rasterizer_state;
   context->base.delete_rasterizer_state = ps5_delete_fixed_state;
   context->base.create_depth_stencil_alpha_state =
      ps5_create_depth_stencil_alpha_state;
   context->base.bind_depth_stencil_alpha_state =
      ps5_bind_depth_stencil_alpha_state;
   context->base.delete_depth_stencil_alpha_state = ps5_delete_fixed_state;
   context->base.create_vertex_elements_state =
      ps5_create_vertex_elements_state;
   context->base.bind_vertex_elements_state = ps5_bind_vertex_elements_state;
   context->base.delete_vertex_elements_state =
      ps5_delete_vertex_elements_state;
   context->base.set_blend_color = ps5_set_blend_color;
   context->base.set_stencil_ref = ps5_set_stencil_ref;
   context->base.set_sample_mask = ps5_set_sample_mask;
   context->base.get_sample_position = u_default_get_sample_position;
   context->base.set_scissor_states = ps5_set_scissor_states;
   context->base.set_viewport_states = ps5_set_viewport_states;
   context->base.set_polygon_stipple = ps5_set_polygon_stipple;
   context->base.set_window_rectangles = ps5_set_window_rectangles;
   context->base.set_sampler_views = ps5_set_sampler_views;
   context->base.create_sampler_view = ps5_create_sampler_view;
   context->base.sampler_view_destroy = ps5_sampler_view_destroy;
   context->base.set_constant_buffer = ps5_set_constant_buffer;
   context->base.set_vertex_buffers = ps5_set_vertex_buffers;
   if (PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE) {
      context->base.create_stream_output_target =
         ps5_create_stream_output_target;
      context->base.stream_output_target_destroy =
         ps5_stream_output_target_destroy;
      context->base.set_stream_output_targets =
         ps5_set_stream_output_targets;
      context->base.stream_output_target_offset =
         ps5_stream_output_target_offset;
   }
   context->base.buffer_subdata = ps5_buffer_subdata;
   context->base.texture_subdata = ps5_texture_subdata;
   context->base.generate_mipmap = ps5_generate_mipmap;
   context->base.sampler_view_release = ps5_sampler_view_release;
   context->base.set_framebuffer_state = ps5_set_framebuffer_state;
   context->base.clear = ps5_clear;
   context->base.flush = ps5_flush;
   context->base.buffer_map = ps5_transfer_map;
   context->base.texture_map = ps5_transfer_map;
   context->base.transfer_flush_region = ps5_transfer_flush_region;
   context->base.buffer_unmap = ps5_transfer_unmap;
   context->base.texture_unmap = ps5_transfer_unmap;
   context->base.resource_copy_region = util_resource_copy_region;
   if (PS5_ENABLE_SOFTWARE_BLIT_CANDIDATE || PS5_ENABLE_MSAA4_CANDIDATE)
      context->base.blit = ps5_blit;
   context->base.resource_release = u_default_resource_release;
   context->base.stream_uploader = u_upload_create_default(&context->base);
   if (!context->base.stream_uploader) {
      for (unsigned index = 0; index < PS5_DESCRIPTOR_STAGE_COUNT; ++index)
         pipe_resource_reference(&context->descriptor_storage[index], NULL);
      pipe_resource_reference(&context->border_color_storage, NULL);
      pipe_resource_reference(&context->vertex_descriptor_table, NULL);
      free(context);
      return NULL;
   }
   context->base.const_uploader = context->base.stream_uploader;
   screen->num_contexts++;
   return &context->base;
}

static void
ps5_screen_destroy(struct pipe_screen *base)
{
   struct ps5_screen *screen = (struct ps5_screen *)base;

   simple_mtx_destroy(&screen->submit_mutex);
   simple_mtx_destroy(&screen->resource_mutex);
   free(base);
}

struct pipe_screen *
ps5_screen_create(void)
{
   struct ps5_screen *screen = calloc(1, sizeof(*screen));
   struct nir_shader_compiler_options *vs_options;
   struct nir_shader_compiler_options *gs_options;
   struct nir_shader_compiler_options *fs_options;
   struct pipe_caps *caps;
   struct pipe_shader_caps *vs_caps;
   struct pipe_shader_caps *gs_caps;
   struct pipe_shader_caps *fs_caps;

   if (!screen)
      return NULL;

   simple_mtx_init(&screen->resource_mutex, mtx_plain);
   simple_mtx_init(&screen->submit_mutex, mtx_plain);

   screen->base.destroy = ps5_screen_destroy;
   screen->base.get_name = ps5_get_name;
   screen->base.get_vendor = ps5_get_vendor;
   screen->base.get_device_vendor = ps5_get_device_vendor;
   screen->base.context_create = ps5_context_create;
   screen->base.is_format_supported = ps5_is_format_supported;
   screen->base.can_create_resource = ps5_can_create_resource;
   screen->base.resource_create = ps5_resource_create;
   screen->base.resource_destroy = ps5_resource_destroy;
   screen->base.fence_reference = ps5_fence_reference;
   screen->base.fence_finish = ps5_fence_finish;
   screen->base.get_timestamp = ps5_get_timestamp;

   caps = (struct pipe_caps *)&screen->base.caps;
   caps->graphics = true;
   caps->accelerated = 1;
   caps->uma = true;
   caps->npot_textures = true;
   caps->texture_shadow_map = true;
   caps->native_fp32_depth = true;
   caps->clear_scissored = true;
   caps->mixed_framebuffer_sizes = PS5_ENABLE_PADDED_FBO_CANDIDATE;
   caps->mixed_colorbuffer_formats = PS5_ENABLE_MRT_CANDIDATE;
   caps->dest_surface_srgb_control =
      PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE;
   caps->blend_equation_separate = true;
   caps->glsl_feature_level = PS5_ENABLE_GLSL_330_CANDIDATE ? 330 :
                              PS5_ENABLE_GEOMETRY_CANDIDATE ? 150 : 140;
   caps->glsl_feature_level_compatibility =
      PS5_ENABLE_GLSL_330_CANDIDATE ? 330 :
      PS5_ENABLE_GEOMETRY_CANDIDATE ? 150 : 140;
   caps->max_render_targets = PS5_ENABLE_MRT_CANDIDATE
                                  ? PS5_MAX_RENDER_TARGETS : 1;
   caps->indep_blend_enable = PS5_ENABLE_INDEPENDENT_BLEND_CANDIDATE;
   caps->indep_blend_func = PS5_ENABLE_INDEPENDENT_BLEND_CANDIDATE;
   caps->max_dual_source_render_targets =
      PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE ? 1 : 0;
   caps->max_texture_2d_size = PS5_MAX_TEXTURE_2D_SIZE;
   caps->max_texture_cube_levels =
      PS5_ENABLE_TEXTURE_CUBE_CANDIDATE ? PS5_MAX_TEXTURE_CUBE_LEVELS : 0;
   caps->max_texture_array_layers =
      PS5_ENABLE_TEXTURE_ARRAY_CANDIDATE ? PS5_MAX_TEXTURE_ARRAY_LAYERS : 0;
   caps->max_texture_3d_levels =
      PS5_ENABLE_TEXTURE_3D_CANDIDATE ? PS5_MAX_TEXTURE_3D_LEVELS : 0;
   caps->rasterizer_subpixel_bits = 8;
   caps->min_texel_offset = -8;
   caps->max_texel_offset = 7;
   /* GFX10 rasterization provides upper-left window coordinates.  Mesa's
    * state tracker lowers OpenGL's lower-left convention through CB0. */
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->fs_position_is_sysval = true;
   caps->fs_face_is_integer_sysval = true;
   caps->point_size_fixed = PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE
                               ? PIPE_POINT_SIZE_LOWER_NEVER
                               : PIPE_POINT_SIZE_LOWER_ALWAYS;
   caps->min_point_size = PS5_MIN_POINT_LINE_SIZE;
   caps->min_point_size_aa = PS5_MIN_POINT_LINE_SIZE;
   caps->max_point_size = PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE
                             ? PS5_MAX_POINT_LINE_SIZE : 1.0f;
   caps->max_point_size_aa = caps->max_point_size;
   caps->point_size_granularity = PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE
                                     ? 0.125f : 1.0f;
   caps->min_line_width = PS5_MIN_POINT_LINE_SIZE;
   caps->min_line_width_aa = PS5_MIN_POINT_LINE_SIZE;
   caps->max_line_width = PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE
                             ? PS5_MAX_POINT_LINE_SIZE : 1.0f;
   caps->max_line_width_aa = caps->max_line_width;
   caps->line_width_granularity = PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE
                                     ? 0.125f : 1.0f;
   caps->max_viewports = 1;
   caps->max_varyings = 16;
   caps->max_stream_output_buffers =
      PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE ? PIPE_MAX_SO_BUFFERS : 0;
   caps->max_stream_output_separate_components =
      PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE ? 4 : 0;
   caps->max_stream_output_interleaved_components =
      PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE ? 64 : 0;
   caps->max_geometry_output_vertices =
      PS5_ENABLE_GEOMETRY_CANDIDATE ? 256 : 0;
   caps->max_geometry_total_output_components =
      PS5_ENABLE_GEOMETRY_CANDIDATE ? 1024 : 0;
   caps->max_gs_invocations = PS5_ENABLE_GEOMETRY_CANDIDATE ? 32 : 0;
   caps->max_vertex_buffers = 16;
   caps->supported_prim_modes = (1u << MESA_PRIM_POINTS) |
                                (1u << MESA_PRIM_LINES) |
                                (1u << MESA_PRIM_LINE_STRIP) |
                                (1u << MESA_PRIM_TRIANGLES) |
                                (1u << MESA_PRIM_TRIANGLE_FAN) |
                                (1u << MESA_PRIM_TRIANGLE_STRIP) |
                                (1u << MESA_PRIM_LINES_ADJACENCY) |
                                (1u << MESA_PRIM_LINE_STRIP_ADJACENCY) |
                                (1u << MESA_PRIM_TRIANGLES_ADJACENCY) |
                                (1u << MESA_PRIM_TRIANGLE_STRIP_ADJACENCY);
   caps->primitive_restart = true;
   caps->supported_prim_modes_with_restart = caps->supported_prim_modes;
   caps->vs_instanceid = true;
   caps->vertex_element_instance_divisor = true;
   caps->texture_swizzle = PS5_ENABLE_TEXTURE_SWIZZLE_CANDIDATE;
   caps->fragment_shader_texture_lod = PS5_ENABLE_SHADER_TEXTURE_LOD_CANDIDATE;
   caps->max_texture_lod_bias =
      PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE ? 16.0f : 0.0f;
   caps->generate_mipmap = true;
   caps->max_constant_buffer_size =
      PS5_ENABLE_UBO_CANDIDATE ? PS5_MAX_CONSTANT_BUFFER_SIZE : 0;
   caps->constant_buffer_offset_alignment =
      PS5_ENABLE_UBO_CANDIDATE ? 16 : 0;
   caps->query_time_elapsed = PS5_ENABLE_TIMER_QUERY_CANDIDATE;
   caps->query_timestamp = PS5_ENABLE_TIMER_QUERY_CANDIDATE;
   caps->query_timestamp_bits = PS5_ENABLE_TIMER_QUERY_CANDIDATE ? 64 : 0;
   caps->occlusion_query = PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE;
   caps->conditional_render = PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE;
   caps->conditional_render_inverted =
      PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE;
   caps->depth_clip_disable = PS5_ENABLE_DEPTH_CLAMP_CANDIDATE;
   caps->texture_multisample = PS5_ENABLE_MSAA4_CANDIDATE;
   caps->sample_shading = PS5_ENABLE_MSAA4_CANDIDATE;
   caps->texture_buffer_objects = PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE;
   caps->max_texel_buffer_elements = PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE
                                        ? PS5_MAX_TEXEL_BUFFER_ELEMENTS : 0;
   caps->texture_buffer_offset_alignment = PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE
                                              ? 16 : 0;
   caps->fake_sw_msaa = !PS5_ENABLE_MSAA4_CANDIDATE &&
                        PS5_ENABLE_FAKE_SW_MSAA_CANDIDATE;
   caps->seamless_cube_map = PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE;
   caps->seamless_cube_map_per_texture =
      PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE;
   caps->gl_begin_end_buffer_size = 512 * 1024;
   caps->min_map_buffer_alignment = 64;
   vs_caps = (struct pipe_shader_caps *)
      &screen->base.shader_caps[MESA_SHADER_VERTEX];
   gs_caps = (struct pipe_shader_caps *)
      &screen->base.shader_caps[MESA_SHADER_GEOMETRY];
   fs_caps = (struct pipe_shader_caps *)
      &screen->base.shader_caps[MESA_SHADER_FRAGMENT];
   vs_caps->supported_irs = 1u << PIPE_SHADER_IR_NIR;
   fs_caps->supported_irs = 1u << PIPE_SHADER_IR_NIR;

   /*
    * This is the first deliberately small programmable-pipeline envelope.
    * Both stages accept NIR and have been compiled to loadable AGC packages
    * on the console.  The sampler counts describe compiler/hardware limits;
    * texture formats remain unadvertised until Gallium descriptors are wired.
    */
   vs_caps->max_instructions = 16384;
   vs_caps->max_alu_instructions = 16384;
   vs_caps->max_tex_instructions = 16384;
   vs_caps->max_tex_indirections = 16;
   vs_caps->max_control_flow_depth = 32;
   vs_caps->max_inputs = 16;
   vs_caps->max_outputs = 16;
   vs_caps->max_const_buffer0_size =
      PS5_MAX_DEFAULT_CONSTANT_BUFFER_SIZE;
   vs_caps->max_const_buffers =
      PS5_ENABLE_UBO_CANDIDATE ? PS5_MAX_CONSTANT_BUFFERS : 1;
   vs_caps->max_temps = 256;
   vs_caps->max_texture_samplers = 16;
   vs_caps->max_sampler_views = 16;
   vs_caps->cont_supported = true;
   vs_caps->indirect_temp_addr = true;
   vs_caps->indirect_const_addr = true;
   vs_caps->integers = true;

   if (PS5_ENABLE_GEOMETRY_CANDIDATE) {
      *gs_caps = *vs_caps;
      gs_caps->max_inputs = 16;
      gs_caps->max_outputs = 32;
      gs_caps->max_texture_samplers = PS5_MAX_TEXTURE_UNITS;
      gs_caps->max_sampler_views = PS5_MAX_TEXTURE_UNITS;
   }

   fs_caps->max_instructions = 16384;
   fs_caps->max_alu_instructions = 16384;
   fs_caps->max_tex_instructions = 16384;
   fs_caps->max_tex_indirections = 16;
   fs_caps->max_control_flow_depth = 32;
   fs_caps->max_inputs = 32;
   fs_caps->max_outputs = 8;
   fs_caps->max_const_buffer0_size =
      PS5_MAX_DEFAULT_CONSTANT_BUFFER_SIZE;
   fs_caps->max_const_buffers =
      PS5_ENABLE_UBO_CANDIDATE ? PS5_MAX_CONSTANT_BUFFERS : 1;
   fs_caps->max_temps = 256;
   fs_caps->max_texture_samplers = 16;
   fs_caps->max_sampler_views = 16;
   fs_caps->cont_supported = true;
   fs_caps->indirect_temp_addr = true;
   fs_caps->indirect_const_addr = true;
   fs_caps->integers = true;
   vs_options = (struct nir_shader_compiler_options *)(uintptr_t)
      psbc_get_nir_options(PSBC_STAGE_VERTEX);
   fs_options = (struct nir_shader_compiler_options *)(uintptr_t)
      psbc_get_nir_options(PSBC_STAGE_FRAGMENT);
   gs_options = (struct nir_shader_compiler_options *)(uintptr_t)
      psbc_get_nir_options(PSBC_STAGE_GEOMETRY);
   vs_options->io_options |= nir_io_has_intrinsics;
   gs_options->io_options |= nir_io_has_intrinsics;
   fs_options->io_options |= nir_io_has_intrinsics;
   screen->base.nir_options[MESA_SHADER_VERTEX] = vs_options;
   screen->base.nir_options[MESA_SHADER_GEOMETRY] = gs_options;
   screen->base.nir_options[MESA_SHADER_FRAGMENT] = fs_options;
   return &screen->base;
}
