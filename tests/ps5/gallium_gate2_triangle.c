#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "compiler/glsl_types.h"
#include "compiler/nir/nir_builder.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "ps5_screen.h"
#include "psbc_compile.h"

#if defined(PS5_POINT_PRIMITIVE_TEST) && \
    defined(PS5_LINE_PRIMITIVE_TEST)
#error "select exactly one primitive-topology test"
#endif

#if defined(PS5_POINT_PRIMITIVE_TEST) || \
    defined(PS5_LINE_PRIMITIVE_TEST) || \
    defined(PS5_MULTI_POINT_PRIMITIVE_TEST) || \
    defined(PS5_MULTI_LINE_PRIMITIVE_TEST) || \
    defined(PS5_LINE_STRIP_PRIMITIVE_TEST) || \
    defined(PS5_TRIANGLE_STRIP_PRIMITIVE_TEST) || \
    defined(PS5_TRIANGLE_FAN_PRIMITIVE_TEST)
#define PS5_PRIMITIVE_TOPOLOGY_TEST 1
#endif

#if defined(PS5_MULTI_POINT_PRIMITIVE_TEST) || \
    defined(PS5_MULTI_LINE_PRIMITIVE_TEST) || \
    defined(PS5_LINE_STRIP_PRIMITIVE_TEST) || \
    defined(PS5_TRIANGLE_STRIP_PRIMITIVE_TEST) || \
    defined(PS5_TRIANGLE_FAN_PRIMITIVE_TEST)
#define PS5_GRID_PRIMITIVE_TEST 1
#endif

#if defined(PS5_LINE_STRIP_PRIMITIVE_TEST) || \
    defined(PS5_TRIANGLE_STRIP_PRIMITIVE_TEST) || \
    defined(PS5_TRIANGLE_FAN_PRIMITIVE_TEST)
#define PS5_TOPOLOGY_RASTER_ONLY_TEST 1
#endif

#if defined(PS5_POLYGON_OFFSET_SLOPE_BASELINE) && \
    defined(PS5_POLYGON_OFFSET_SLOPE_CANDIDATE)
#error "select exactly one polygon-offset slope mode"
#endif

#if defined(PS5_POLYGON_OFFSET_SLOPE_BASELINE) || \
    defined(PS5_POLYGON_OFFSET_SLOPE_CANDIDATE) || \
    defined(PS5_POLYGON_OFFSET_NEGATIVE_FACTOR_TEST)
#define PS5_POLYGON_OFFSET_SLOPE_TEST 1
#endif

#if defined(PS5_LINE_POLYGON_BASELINE) || \
    defined(PS5_LINE_POLYGON_OFFSET_CANDIDATE)
#define PS5_LINE_POLYGON_TEST 1
#endif

#if defined(PS5_POINT_POLYGON_BASELINE) && \
    defined(PS5_POINT_POLYGON_OFFSET_CANDIDATE)
#error "select exactly one point-polygon mode"
#endif

#if defined(PS5_POINT_POLYGON_BASELINE) || \
    defined(PS5_POINT_POLYGON_OFFSET_CANDIDATE)
#define PS5_POINT_POLYGON_TEST 1
#endif

#if defined(PS5_POLYGON_OFFSET_TEST) || \
    defined(PS5_POLYGON_OFFSET_NEGATIVE_UNITS_TEST) || \
    defined(PS5_POLYGON_OFFSET_SLOPE_TEST) || \
    defined(PS5_LINE_POLYGON_TEST) || \
    defined(PS5_POINT_POLYGON_TEST) || \
    defined(PS5_PRIMITIVE_TOPOLOGY_TEST)
#define PS5_POLYGON_OFFSET_DEPTH_DUMP 1
#endif

static nir_shader *
build_vertex_shader(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, psbc_get_nir_options(PSBC_STAGE_VERTEX),
      "ps5-gallium-gate2-vs");
   nir_variable *position = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "gl_Position");
   nir_def *vertex_id = nir_load_vertex_id_zero_base(&b);
   nir_def *is_one = nir_ieq_imm(&b, vertex_id, 1);
#if defined(PS5_GRID_PRIMITIVE_TEST) || \
    !defined(PS5_LINE_PRIMITIVE_TEST)
   nir_def *is_two = nir_ieq_imm(&b, vertex_id, 2);
#endif
#ifdef PS5_GRID_PRIMITIVE_TEST
   nir_def *is_three = nir_ieq_imm(&b, vertex_id, 3);
#endif

   position->data.location = VARYING_SLOT_POS;
   nir_store_var(
      &b, position,
      nir_vec4(&b,
#ifdef PS5_GRID_PRIMITIVE_TEST
               nir_bcsel(&b, nir_ior(&b, is_two, is_three),
                          nir_imm_float(&b, -0.25f),
                          nir_imm_float(&b, -0.75f)),
#ifdef PS5_TRIANGLE_FAN_PRIMITIVE_TEST
               nir_bcsel(&b, nir_ior(&b, is_one, is_two),
                          nir_imm_float(&b, 0.5f),
                          nir_imm_float(&b, -0.5f)),
#else
               nir_bcsel(&b, nir_ior(&b, is_one, is_three),
                          nir_imm_float(&b, 0.5f),
                          nir_imm_float(&b, -0.5f)),
#endif
#elif defined(PS5_LINE_PRIMITIVE_TEST)
               nir_imm_float(&b, -0.5f),
               nir_bcsel(&b, is_one, nir_imm_float(&b, 0.5f),
                          nir_imm_float(&b, -0.5f)),
#else
               nir_bcsel(&b, is_one, nir_imm_float(&b, 0.5f),
                          nir_imm_float(&b, -0.5f)),
               nir_bcsel(&b, is_two, nir_imm_float(&b, 0.5f),
                          nir_imm_float(&b, -0.5f)),
#endif
#ifdef PS5_POLYGON_OFFSET_SLOPE_TEST
               nir_bcsel(&b, is_two, nir_imm_float(&b, 0.5f),
                          nir_imm_float(&b, 0.25f)),
#else
               nir_imm_float(&b, 0.5f),
#endif
               nir_imm_float(&b, 1.0f)),
      0xf);
   return b.shader;
}

static nir_shader *
build_fragment_shader(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, psbc_get_nir_options(PSBC_STAGE_FRAGMENT),
      "ps5-gallium-gate2-fs");
   nir_variable *color = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "color");

   color->data.location = FRAG_RESULT_DATA0;
   nir_store_var(&b, color,
                 nir_imm_vec4(&b, 1.0f, 0.25f, 0.125f,
#ifdef PS5_FIXED_STATE_TEST
                              0.5f
#else
                              1.0f
#endif
                              ),
                  0xf);
#ifdef PS5_MRT_TEST
   static const float colors[3][4] = {
      {0.0f, 1.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f, 1.0f},
      {1.0f, 1.0f, 1.0f, 1.0f},
   };
   for (unsigned i = 0; i < 3; ++i) {
      nir_variable *output = nir_variable_create(
         b.shader, nir_var_shader_out, glsl_vec4_type(), "mrt");

      output->data.location = FRAG_RESULT_DATA1 + i;
      nir_store_var(&b, output,
                    nir_imm_vec4(&b, colors[i][0], colors[i][1],
                                 colors[i][2], colors[i][3]), 0xf);
   }
#endif
   return b.shader;
}

int
main(void)
{
   struct pipe_screen *screen = ps5_screen_create();
   struct pipe_context *context = NULL;
   struct pipe_draw_info info;
   struct pipe_draw_start_count_bias draw;
   struct pipe_shader_state shader_state;
   struct pipe_resource resource_template;
   struct pipe_resource *target = NULL;
#ifdef PS5_MRT_TEST
   struct pipe_resource *mrt_targets[3] = {NULL, NULL, NULL};
   size_t mrt_nonzero[3] = {0, 0, 0};
#endif
   struct pipe_resource *depth = NULL;
#ifdef PS5_INDEXED_PRIMITIVE_TEST
   struct pipe_resource *index_buffer = NULL;
   void *index_address = NULL;
#endif
   struct pipe_framebuffer_state framebuffer;
   struct pipe_depth_stencil_alpha_state depth_state;
#ifdef PS5_FIXED_STATE_TEST
   struct pipe_blend_state blend_state;
   struct pipe_rasterizer_state rasterizer_state;
   struct pipe_blend_color blend_color;
   struct pipe_viewport_state viewport;
   struct pipe_scissor_state scissor;
   void *blend_state_object = NULL;
   void *rasterizer_state_object = NULL;
#endif
#ifdef PS5_PACKED_STENCIL_TEST
   struct pipe_stencil_ref stencil_ref;
#endif
   void *depth_state_object = NULL;
   void *vs = NULL;
   void *fs = NULL;
   size_t vs_size = 0;
   size_t fs_size = 0;
   unsigned vs_hw = 0;
   unsigned fs_hw = 0;
   unsigned vs_unresolved = 0;
   unsigned fs_unresolved = 0;
   void *target_address = NULL;
   size_t target_logical_size = 0;
   size_t target_allocation_size = 0;
   size_t target_nonzero = 0;
#ifdef PS5_FIXED_STATE_TEST
   const uint32_t expected_target_word =
#ifdef PS5_TOPOLOGY_RASTER_ONLY_TEST
      UINT32_C(0x802040ff);
#elif defined(PS5_COLOR_MASK_TEST)
      UINT32_C(0x00100080);
#else
      UINT32_C(0x40102080);
#endif
#if !defined(PS5_LINE_POLYGON_TEST) && \
    !defined(PS5_POINT_POLYGON_TEST) && \
    !defined(PS5_PRIMITIVE_TOPOLOGY_TEST)
   const size_t expected_coverage =
#if defined(PS5_CULL_BACK_TEST) || defined(PS5_CULL_FRONT_TEST)
      0;
#else
      194400;
#endif
#endif
   size_t target_unexpected = 0;
#endif
   void *depth_address = NULL;
   size_t depth_allocation_size = 0;
   size_t depth_nonzero = 0;
#ifdef PS5_POLYGON_OFFSET_DEPTH_DUMP
   uint32_t depth_min = UINT32_MAX;
   uint32_t depth_max = 0;
   int depth_dump_status = -1;
#endif
#ifdef PS5_PACKED_STENCIL_TEST
   size_t depth_matches = 0;
   size_t depth_unexpected = 0;
   uint32_t packed_depth_first = 0;
   uint32_t packed_depth_min = UINT32_MAX;
   uint32_t packed_depth_max = 0;
   int packed_depth_dump_status = -1;
   void *stencil_address = NULL;
   size_t stencil_allocation_size = 0;
   size_t stencil_nonzero = 0;
   size_t stencil_matches = 0;
   size_t stencil_unexpected = 0;
   int stencil_dump_status = -1;
#endif
   unsigned draw_calls = 0;
   unsigned advertised_render_targets = 0;
   int draw_status = -1;

   if (!screen)
      goto done;
   advertised_render_targets = screen->caps.max_render_targets;
   context = screen->context_create(screen, NULL, 0);
   if (!context)
      goto done;

   memset(&shader_state, 0, sizeof(shader_state));
   shader_state.type = PIPE_SHADER_IR_NIR;
   shader_state.ir.nir = build_vertex_shader();
   vs = context->create_vs_state(context, &shader_state);
   shader_state.ir.nir = build_fragment_shader();
   fs = context->create_fs_state(context, &shader_state);
   if (!vs || !fs)
      goto done;
   context->bind_vs_state(context, vs);
   context->bind_fs_state(context, fs);
   ps5_shader_state_info(vs, &vs_size, &vs_hw, &vs_unresolved);
   ps5_shader_state_info(fs, &fs_size, &fs_hw, &fs_unresolved);
   printf("[ps5-gallium-gate2] vs=%zu/hw%u/u%u fs=%zu/hw%u/u%u\n",
          vs_size, vs_hw, vs_unresolved, fs_size, fs_hw, fs_unresolved);

   memset(&resource_template, 0, sizeof(resource_template));
   resource_template.target = PIPE_TEXTURE_2D;
   resource_template.format = PIPE_FORMAT_R8G8B8A8_UNORM;
   resource_template.width0 = 1920;
   resource_template.height0 = 1080;
   resource_template.depth0 = 1;
   resource_template.array_size = 1;
   resource_template.nr_samples = 1;
   resource_template.nr_storage_samples = 1;
   resource_template.bind = PIPE_BIND_RENDER_TARGET;
   target = screen->resource_create(screen, &resource_template);
   if (!target)
      goto done;
#ifdef PS5_MRT_TEST
   for (unsigned i = 0; i < 3; ++i) {
      mrt_targets[i] = screen->resource_create(screen, &resource_template);
      if (!mrt_targets[i])
         goto done;
   }
#endif
   resource_template.format =
#ifdef PS5_PACKED_STENCIL_TEST
      PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
#else
      PIPE_FORMAT_Z32_FLOAT;
#endif
   resource_template.bind = PIPE_BIND_DEPTH_STENCIL;
   depth = screen->resource_create(screen, &resource_template);
   if (!depth)
      goto done;
   memset(&framebuffer, 0, sizeof(framebuffer));
   framebuffer.width = 1920;
   framebuffer.height = 1080;
   framebuffer.nr_cbufs =
#ifdef PS5_MRT_TEST
      4;
#else
      1;
#endif
   framebuffer.cbufs[0].texture = target;
   framebuffer.cbufs[0].format = PIPE_FORMAT_R8G8B8A8_UNORM;
#ifdef PS5_MRT_TEST
   for (unsigned i = 0; i < 3; ++i) {
      framebuffer.cbufs[i + 1].texture = mrt_targets[i];
      framebuffer.cbufs[i + 1].format = PIPE_FORMAT_R8G8B8A8_UNORM;
   }
#endif
   framebuffer.zsbuf.texture = depth;
   framebuffer.zsbuf.format = resource_template.format;
   context->set_framebuffer_state(context, &framebuffer);

#ifdef PS5_FIXED_STATE_TEST
   memset(&blend_state, 0, sizeof(blend_state));
#ifdef PS5_LOGICOP_TEST
   blend_state.logicop_enable = 1;
   blend_state.logicop_func = PIPE_LOGICOP_XOR;
#elif !defined(PS5_TOPOLOGY_RASTER_ONLY_TEST)
   blend_state.rt[0].blend_enable = 1;
#endif
   blend_state.rt[0].rgb_func = PIPE_BLEND_ADD;
   blend_state.rt[0].rgb_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA;
   blend_state.rt[0].rgb_dst_factor = PIPE_BLENDFACTOR_INV_SRC_ALPHA;
   blend_state.rt[0].alpha_func = PIPE_BLEND_ADD;
   blend_state.rt[0].alpha_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA;
   blend_state.rt[0].alpha_dst_factor = PIPE_BLENDFACTOR_INV_SRC_ALPHA;
   blend_state.rt[0].colormask =
#ifdef PS5_COLOR_MASK_TEST
      PIPE_MASK_R | PIPE_MASK_B;
#else
      PIPE_MASK_RGBA;
#endif
   blend_state_object = context->create_blend_state(context, &blend_state);
   if (!blend_state_object)
      goto done;
   context->bind_blend_state(context, blend_state_object);

   memset(&blend_color, 0, sizeof(blend_color));
   blend_color.color[0] = 0.125f;
   blend_color.color[1] = 0.25f;
   blend_color.color[2] = 0.5f;
   blend_color.color[3] = 1.0f;
   context->set_blend_color(context, &blend_color);

   memset(&rasterizer_state, 0, sizeof(rasterizer_state));
   rasterizer_state.front_ccw = 1;
   rasterizer_state.fill_front = PIPE_POLYGON_MODE_FILL;
   rasterizer_state.fill_back = PIPE_POLYGON_MODE_FILL;
#ifdef PS5_LINE_POLYGON_TEST
   rasterizer_state.fill_front = PIPE_POLYGON_MODE_LINE;
   rasterizer_state.fill_back = PIPE_POLYGON_MODE_LINE;
#elif defined(PS5_POINT_POLYGON_TEST)
   rasterizer_state.fill_front = PIPE_POLYGON_MODE_POINT;
   rasterizer_state.fill_back = PIPE_POLYGON_MODE_POINT;
#endif
   rasterizer_state.scissor = 1;
   rasterizer_state.line_width = 1.0f;
   rasterizer_state.point_size = 1.0f;
   rasterizer_state.depth_clip_near = 1;
   rasterizer_state.depth_clip_far = 1;
#ifdef PS5_CULL_BACK_TEST
   rasterizer_state.cull_face = PIPE_FACE_BACK;
#elif defined(PS5_CULL_FRONT_TEST)
   rasterizer_state.cull_face = PIPE_FACE_FRONT;
#endif
#ifdef PS5_LINE_POLYGON_TEST
   rasterizer_state.offset_line = 1;
   rasterizer_state.offset_scale = 0.0f;
#ifdef PS5_LINE_POLYGON_OFFSET_CANDIDATE
   rasterizer_state.offset_units = 2.0f;
#else
   rasterizer_state.offset_units = 0.0f;
#endif
#elif defined(PS5_POINT_POLYGON_TEST)
   rasterizer_state.offset_point = 1;
   rasterizer_state.offset_scale = 0.0f;
#ifdef PS5_POINT_POLYGON_OFFSET_CANDIDATE
   rasterizer_state.offset_units = 2.0f;
#else
   rasterizer_state.offset_units = 0.0f;
#endif
#elif defined(PS5_POLYGON_OFFSET_TEST)
   rasterizer_state.offset_tri = 1;
   rasterizer_state.offset_scale = 0.0f;
   rasterizer_state.offset_units = 2.0f;
#elif defined(PS5_POLYGON_OFFSET_NEGATIVE_UNITS_TEST)
   rasterizer_state.offset_tri = 1;
   rasterizer_state.offset_scale = 0.0f;
   rasterizer_state.offset_units = -2.0f;
#elif defined(PS5_POLYGON_OFFSET_SLOPE_TEST)
   rasterizer_state.offset_tri = 1;
#ifdef PS5_POLYGON_OFFSET_SLOPE_CANDIDATE
   rasterizer_state.offset_scale = 1.0f;
#elif defined(PS5_POLYGON_OFFSET_NEGATIVE_FACTOR_TEST)
   rasterizer_state.offset_scale = -1.0f;
#else
   rasterizer_state.offset_scale = 0.0f;
#endif
   rasterizer_state.offset_units = 0.0f;
#endif
   rasterizer_state_object = context->create_rasterizer_state(
      context, &rasterizer_state);
   if (!rasterizer_state_object)
      goto done;
   context->bind_rasterizer_state(context, rasterizer_state_object);

   memset(&viewport, 0, sizeof(viewport));
   viewport.scale[0] = 960.0f;
   viewport.translate[0] = 960.0f;
   viewport.scale[1] = -540.0f;
   viewport.translate[1] = 540.0f;
   viewport.scale[2] = 0.5f;
   viewport.translate[2] = 0.5f;
   context->set_viewport_states(context, 0, 1, &viewport);

   memset(&scissor, 0, sizeof(scissor));
   scissor.maxx = 960;
   scissor.maxy = 1080;
   context->set_scissor_states(context, 0, 1, &scissor);
#endif

   memset(&depth_state, 0, sizeof(depth_state));
   depth_state.depth_enabled = 1;
   depth_state.depth_writemask = 1;
   depth_state.depth_func = PIPE_FUNC_ALWAYS;
#ifdef PS5_PACKED_STENCIL_TEST
   depth_state.stencil[0].enabled = 1;
   depth_state.stencil[0].func = PIPE_FUNC_ALWAYS;
   depth_state.stencil[0].fail_op = PIPE_STENCIL_OP_KEEP;
   depth_state.stencil[0].zpass_op = PIPE_STENCIL_OP_REPLACE;
   depth_state.stencil[0].zfail_op = PIPE_STENCIL_OP_KEEP;
   depth_state.stencil[0].valuemask = 0xff;
   depth_state.stencil[0].writemask = 0xff;
   memset(&stencil_ref, 0, sizeof(stencil_ref));
   stencil_ref.ref_value[0] = 0x5a;
   context->set_stencil_ref(context, stencil_ref);
#endif
   depth_state_object = context->create_depth_stencil_alpha_state(
      context, &depth_state);
   if (!depth_state_object)
      goto done;
   context->bind_depth_stencil_alpha_state(context, depth_state_object);

   memset(&info, 0, sizeof(info));
   memset(&draw, 0, sizeof(draw));
#ifdef PS5_POINT_PRIMITIVE_TEST
   info.mode = MESA_PRIM_POINTS;
   draw.count = 1;
#elif defined(PS5_LINE_PRIMITIVE_TEST)
   info.mode = MESA_PRIM_LINES;
   draw.count = 2;
#elif defined(PS5_MULTI_POINT_PRIMITIVE_TEST)
   info.mode = MESA_PRIM_POINTS;
   draw.count = 3;
#elif defined(PS5_MULTI_LINE_PRIMITIVE_TEST)
   info.mode = MESA_PRIM_LINES;
   draw.count = 4;
#elif defined(PS5_LINE_STRIP_PRIMITIVE_TEST)
   info.mode = MESA_PRIM_LINE_STRIP;
   draw.count = 4;
#elif defined(PS5_TRIANGLE_STRIP_PRIMITIVE_TEST)
   info.mode = MESA_PRIM_TRIANGLE_STRIP;
   draw.count = 4;
#elif defined(PS5_TRIANGLE_FAN_PRIMITIVE_TEST)
   info.mode = MESA_PRIM_TRIANGLE_FAN;
   draw.count = 4;
#else
   info.mode = MESA_PRIM_TRIANGLES;
   draw.count = 3;
#endif
   info.instance_count = 1;

#ifdef PS5_INDEXED_PRIMITIVE_TEST
   {
      const uint16_t index_values[] = {UINT16_MAX, 0, 1, 2, 3};

      memset(&resource_template, 0, sizeof(resource_template));
      resource_template.target = PIPE_BUFFER;
      resource_template.format = PIPE_FORMAT_R8_UNORM;
      resource_template.width0 = (draw.count + 1) * sizeof(uint16_t);
      resource_template.height0 = 1;
      resource_template.depth0 = 1;
      resource_template.array_size = 1;
      resource_template.bind = PIPE_BIND_INDEX_BUFFER;
      index_buffer = screen->resource_create(screen, &resource_template);
      if (!index_buffer ||
          ps5_resource_info(index_buffer, &index_address, NULL, NULL) != 0)
         goto done;
      memcpy(index_address, index_values, resource_template.width0);
      info.index_size = sizeof(uint16_t);
      info.index.resource = index_buffer;
      info.index_bounds_valid = true;
      info.min_index = 0;
      info.max_index = draw.count - 1;
      draw.start = 1;
   }
#endif

   context->draw_vbo(context, &info, 0, NULL, &draw, 1);
   draw_status = ps5_context_last_draw_status(context, &draw_calls);
   if (ps5_resource_info(target, &target_address, &target_logical_size,
                         &target_allocation_size) == 0) {
      const uint32_t *words = target_address;
      for (size_t i = 0; i < target_allocation_size / sizeof(*words); ++i) {
         target_nonzero += words[i] != 0;
#ifdef PS5_FIXED_STATE_TEST
         target_unexpected +=
            words[i] != 0 && words[i] != expected_target_word;
#endif
      }
   }
#ifdef PS5_MRT_TEST
   for (unsigned target_index = 0; target_index < 3; ++target_index) {
      void *address = NULL;
      size_t allocation = 0;

      if (ps5_resource_info(mrt_targets[target_index], &address, NULL,
                            &allocation) == 0) {
         const uint32_t *words = address;
         for (size_t i = 0; i < allocation / sizeof(*words); ++i)
            mrt_nonzero[target_index] += words[i] != 0;
      }
   }
#endif
   if (ps5_resource_info(depth, &depth_address, NULL,
                         &depth_allocation_size) == 0) {
      const uint32_t *words = depth_address;
      for (size_t i = 0; i < depth_allocation_size / sizeof(*words); ++i) {
         if (!words[i])
            continue;
         depth_nonzero++;
#ifdef PS5_PACKED_STENCIL_TEST
         if (depth_nonzero == 1)
            packed_depth_first = words[i];
         if (words[i] == UINT32_C(0x3f000000))
            depth_matches++;
         else
            depth_unexpected++;
         if (words[i] < packed_depth_min)
            packed_depth_min = words[i];
         if (words[i] > packed_depth_max)
            packed_depth_max = words[i];
#endif
#ifdef PS5_POLYGON_OFFSET_DEPTH_DUMP
         if (words[i] < depth_min)
            depth_min = words[i];
         if (words[i] > depth_max)
            depth_max = words[i];
#endif
      }
#ifdef PS5_PACKED_STENCIL_TEST
      FILE *packed_depth_file = fopen(
         "/data/VdecHello/opengl33-gallium-packed-depth.raw", "wb");
      if (packed_depth_file) {
         const size_t written = fwrite(
            depth_address, 1, depth_allocation_size, packed_depth_file);
         const int close_status = fclose(packed_depth_file);
         packed_depth_dump_status =
            written == depth_allocation_size && close_status == 0 ? 0 : -1;
      }
#endif
#ifdef PS5_POLYGON_OFFSET_DEPTH_DUMP
      FILE *depth_file = fopen(
#ifdef PS5_POLYGON_OFFSET_SLOPE_BASELINE
         "/data/VdecHello/opengl33-gallium-polygon-offset-slope-baseline-depth.raw",
#elif defined(PS5_POLYGON_OFFSET_SLOPE_CANDIDATE)
         "/data/VdecHello/opengl33-gallium-polygon-offset-slope-candidate-depth.raw",
#elif defined(PS5_POLYGON_OFFSET_NEGATIVE_FACTOR_TEST)
         "/data/VdecHello/opengl33-gallium-polygon-offset-negative-factor-depth.raw",
#elif defined(PS5_LINE_POLYGON_BASELINE)
         "/data/VdecHello/opengl33-gallium-line-baseline-depth.raw",
#elif defined(PS5_LINE_POLYGON_OFFSET_CANDIDATE)
         "/data/VdecHello/opengl33-gallium-line-offset-depth.raw",
#elif defined(PS5_POINT_POLYGON_BASELINE)
         "/data/VdecHello/opengl33-gallium-point-baseline-depth.raw",
#elif defined(PS5_POINT_POLYGON_OFFSET_CANDIDATE)
         "/data/VdecHello/opengl33-gallium-point-offset-depth.raw",
#elif defined(PS5_POINT_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-point-primitive-depth.raw",
#elif defined(PS5_LINE_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-line-primitive-depth.raw",
#elif defined(PS5_INDEXED_PRIMITIVE_TEST) && \
      defined(PS5_MULTI_POINT_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-indexed-multi-point-depth.raw",
#elif defined(PS5_INDEXED_PRIMITIVE_TEST) && \
      defined(PS5_MULTI_LINE_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-indexed-multi-line-depth.raw",
#elif defined(PS5_INDEXED_PRIMITIVE_TEST) && \
      defined(PS5_LINE_STRIP_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-indexed-line-strip-depth.raw",
#elif defined(PS5_INDEXED_PRIMITIVE_TEST) && \
      defined(PS5_TRIANGLE_STRIP_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-indexed-triangle-strip-depth.raw",
#elif defined(PS5_INDEXED_PRIMITIVE_TEST) && \
      defined(PS5_TRIANGLE_FAN_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-indexed-triangle-fan-depth.raw",
#elif defined(PS5_MULTI_POINT_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-multi-point-depth.raw",
#elif defined(PS5_MULTI_LINE_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-multi-line-depth.raw",
#elif defined(PS5_LINE_STRIP_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-line-strip-v2-depth.raw",
#elif defined(PS5_TRIANGLE_STRIP_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-triangle-strip-v2-depth.raw",
#elif defined(PS5_TRIANGLE_FAN_PRIMITIVE_TEST)
         "/data/VdecHello/opengl33-gallium-triangle-fan-v3-depth.raw",
#elif defined(PS5_POLYGON_OFFSET_NEGATIVE_UNITS_TEST)
         "/data/VdecHello/opengl33-gallium-polygon-offset-negative-units-depth.raw",
#else
         "/data/VdecHello/opengl33-gallium-polygon-offset-depth.raw",
#endif
         "wb");
      if (depth_file) {
         const size_t written =
            fwrite(depth_address, 1, depth_allocation_size, depth_file);
         const int close_status = fclose(depth_file);
         depth_dump_status =
            written == depth_allocation_size && close_status == 0 ? 0 : -1;
      }
#endif
   }
#ifdef PS5_PACKED_STENCIL_TEST
   if (ps5_resource_stencil_info(depth, &stencil_address,
                                  &stencil_allocation_size) == 0) {
      const uint8_t *bytes = stencil_address;
      FILE *stencil_file;

      for (size_t i = 0; i < stencil_allocation_size; ++i) {
         if (bytes[i] == UINT8_C(0x5a))
            stencil_matches++;
         else if (bytes[i])
            stencil_unexpected++;
      }
      stencil_nonzero = stencil_matches + stencil_unexpected;
      stencil_file = fopen(
         "/data/VdecHello/opengl33-gallium-packed-stencil.raw", "wb");
      if (stencil_file) {
         const size_t written = fwrite(
            stencil_address, 1, stencil_allocation_size, stencil_file);
         const int close_status = fclose(stencil_file);
         stencil_dump_status =
            written == stencil_allocation_size && close_status == 0 ? 0 : -1;
      }
   }
#endif
   printf("[ps5-gallium-gate2] draw_status=%d draw_calls=%u graphics_advertised=%u\n",
          draw_status, draw_calls, screen->caps.graphics);
   printf("[ps5-gallium-gate2] target=%p logical=%zu allocation=%zu nonzero=%zu\n",
          target_address, target_logical_size, target_allocation_size,
          target_nonzero);
#ifdef PS5_MRT_TEST
   printf("[ps5-gallium-gate2] mrt=4 caps=%u nonzero=%zu,%zu,%zu,%zu\n",
          advertised_render_targets, target_nonzero, mrt_nonzero[0],
          mrt_nonzero[1], mrt_nonzero[2]);
#endif
#ifdef PS5_POLYGON_OFFSET_SLOPE_TEST
   printf("[ps5-gallium-gate2] depth=%p allocation=%zu nonzero=%zu func=ALWAYS write=1 clip_z=0.25,0.25,0.5\n",
#else
   printf("[ps5-gallium-gate2] depth=%p allocation=%zu nonzero=%zu func=ALWAYS write=1 clip_z=0.5\n",
#endif
          depth_address, depth_allocation_size, depth_nonzero);
#ifdef PS5_PACKED_STENCIL_TEST
   printf("[ps5-gallium-gate2] stencil=%p allocation=%zu nonzero=%zu matches=%zu unexpected=%zu ref=5a dump=%d\n",
          stencil_address, stencil_allocation_size, stencil_nonzero,
          stencil_matches, stencil_unexpected, stencil_dump_status);
   printf("[ps5-gallium-gate2] packed-depth matches=%zu unexpected=%zu value=3f000000 first=%08x min=%08x max=%08x dump=%d\n",
          depth_matches, depth_unexpected, packed_depth_first,
          packed_depth_min, packed_depth_max, packed_depth_dump_status);
#endif
#ifdef PS5_FIXED_STATE_TEST
   printf("[ps5-gallium-gate2] fixed_state=%s viewport=1920x1080 scissor=960x1080 cull=%s\n",
#ifdef PS5_TOPOLOGY_RASTER_ONLY_TEST
          "blend_disabled",
#else
          "blend_src_alpha",
#endif
#ifdef PS5_CULL_BACK_TEST
          "back");
#elif defined(PS5_CULL_FRONT_TEST)
          "front");
#else
          "none");
#endif
#if defined(PS5_LINE_POLYGON_TEST) || \
    defined(PS5_POINT_POLYGON_TEST) || \
    defined(PS5_PRIMITIVE_TOPOLOGY_TEST)
   printf("[ps5-gallium-gate2] fixed_expected=%08x target_unexpected=%zu",
          (unsigned)expected_target_word, target_unexpected);
#else
   printf("[ps5-gallium-gate2] fixed_expected=%08x coverage=%zu target_unexpected=%zu",
          (unsigned)expected_target_word, expected_coverage, target_unexpected);
#endif
#ifdef PS5_COLOR_MASK_TEST
   printf(" color_mask=R+B target_mask=00000005");
#endif
#ifdef PS5_CULL_BACK_TEST
   printf(" cull_face=BACK front_ccw=1 rasterizer_control=00080242");
#elif defined(PS5_CULL_FRONT_TEST)
   printf(" cull_face=FRONT front_ccw=1 rasterizer_control=00080241");
#elif defined(PS5_LINE_POLYGON_TEST)
   printf(" polygon_mode=LINE line_width=1 offset_line=1"
          " coverage_range=1200-1800 rasterizer_control=01083928");
#elif defined(PS5_POINT_POLYGON_TEST)
   printf(" polygon_mode=POINT point_size=1 offset_point=1"
          " expected_coverage=2 rasterizer_control=01083808");
#elif defined(PS5_POINT_PRIMITIVE_TEST)
   printf(" primitive=POINTS draw_count=1 native_primitive=1"
          " expected_coverage=1");
#elif defined(PS5_LINE_PRIMITIVE_TEST)
   printf(" primitive=LINES draw_count=2 native_primitive=2"
          " coverage_range=500-600");
#elif defined(PS5_MULTI_POINT_PRIMITIVE_TEST)
   printf(" primitive=POINTS draw_count=3 native_primitive=1"
          " expected_coverage=3");
#elif defined(PS5_MULTI_LINE_PRIMITIVE_TEST)
   printf(" primitive=LINES draw_count=4 native_primitive=2"
          " expected_coverage=1080");
#elif defined(PS5_LINE_STRIP_PRIMITIVE_TEST)
   printf(" primitive=LINE_STRIP draw_count=4 native_primitive=3"
          " coverage_range=1500-1700");
#elif defined(PS5_TRIANGLE_STRIP_PRIMITIVE_TEST)
   printf(" primitive=TRIANGLE_STRIP draw_count=4 native_primitive=6"
          " expected_coverage=259200");
#elif defined(PS5_TRIANGLE_FAN_PRIMITIVE_TEST)
   printf(" primitive=TRIANGLE_FAN draw_count=4 native_primitive=5"
          " expected_coverage=259200 vertex_order=BL-TL-TR-BR");
#endif
#ifdef PS5_INDEXED_PRIMITIVE_TEST
   printf(" indexed=1 index_start=1 skipped_sentinel=ffff"
          " index_base=%p index_selected=%p",
          index_address,
          (uint8_t *)index_address + draw.start * sizeof(uint16_t));
#endif
   printf("\n");
#endif
#ifdef PS5_POLYGON_OFFSET_TEST
   printf("[ps5-gallium-gate2] polygon_offset=tri factor=0 units=2 clamp=0 d32f"
          " control=3f400000 accepted=3f400001-3f400100"
          " observed=%08x-%08x depth_dump=%d\n",
          (unsigned)depth_min, (unsigned)depth_max, depth_dump_status);
#endif
#ifdef PS5_POLYGON_OFFSET_NEGATIVE_UNITS_TEST
   printf("[ps5-gallium-gate2] polygon_offset=tri factor=0 units=-2 clamp=0 d32f"
          " control=3f400000 accepted=3f3fff00-3f3fffff"
          " observed=%08x-%08x depth_dump=%d\n",
          (unsigned)depth_min, (unsigned)depth_max, depth_dump_status);
#endif
#ifdef PS5_POLYGON_OFFSET_SLOPE_TEST
   printf("[ps5-gallium-gate2] polygon_offset_slope=tri factor=%d units=0 clamp=0 d32f"
#ifdef PS5_POLYGON_OFFSET_NEGATIVE_FACTOR_TEST
          " negative_min=3f1fe000-3f1fffff"
#else
          " baseline_range=3f200000-3f3fffff candidate_max=3f400001-3f401fff"
#endif
          " observed=%08x-%08x depth_dump=%d\n",
#ifdef PS5_POLYGON_OFFSET_SLOPE_CANDIDATE
          1,
#elif defined(PS5_POLYGON_OFFSET_NEGATIVE_FACTOR_TEST)
          -1,
#else
          0,
#endif
          (unsigned)depth_min, (unsigned)depth_max, depth_dump_status);
#endif
#ifdef PS5_LINE_POLYGON_TEST
   printf("[ps5-gallium-gate2] line_polygon units=%u control=3f400000"
          " accepted_offset=3f400001-3f400100 observed=%08x-%08x"
          " target=%zu depth=%zu depth_dump=%d\n",
#ifdef PS5_LINE_POLYGON_OFFSET_CANDIDATE
          2u,
#else
          0u,
#endif
          (unsigned)depth_min, (unsigned)depth_max, target_nonzero,
          depth_nonzero, depth_dump_status);
#endif
#ifdef PS5_POINT_POLYGON_TEST
   printf("[ps5-gallium-gate2] point_polygon units=%u control=3f400000"
          " expected_offset=3f400002 observed=%08x-%08x"
          " target=%zu depth=%zu depth_dump=%d\n",
#ifdef PS5_POINT_POLYGON_OFFSET_CANDIDATE
          2u,
#else
          0u,
#endif
          (unsigned)depth_min, (unsigned)depth_max, target_nonzero,
          depth_nonzero, depth_dump_status);
#endif
#ifdef PS5_PRIMITIVE_TOPOLOGY_TEST
   printf("[ps5-gallium-gate2] primitive_topology=%s native=%u count=%u"
          " depth=%08x-%08x depth_dump=%d\n",
#ifdef PS5_POINT_PRIMITIVE_TEST
          "POINTS", 1u, 1u,
#elif defined(PS5_LINE_PRIMITIVE_TEST)
          "LINES", 2u, 2u,
#elif defined(PS5_MULTI_POINT_PRIMITIVE_TEST)
          "POINTS", 1u, 3u,
#elif defined(PS5_MULTI_LINE_PRIMITIVE_TEST)
          "LINES", 2u, 4u,
#elif defined(PS5_LINE_STRIP_PRIMITIVE_TEST)
          "LINE_STRIP", 3u, 4u,
#elif defined(PS5_TRIANGLE_STRIP_PRIMITIVE_TEST)
          "TRIANGLE_STRIP", 6u, 4u,
#else
          "TRIANGLE_FAN", 5u, 4u,
#endif
          (unsigned)depth_min, (unsigned)depth_max, depth_dump_status);
   printf("[ps5-gallium-gate2] raw_color_depth_mask=not-compared"
          " layouts=RGBA8-vs-D32F\n");
#endif
#ifdef PS5_LOGICOP_TEST
   printf("[ps5-gallium-gate2] logicop=XOR rop3=0x66 color_control=0x00660011\n");
#endif

done:
   if (context) {
#ifdef PS5_FIXED_STATE_TEST
      if (blend_state_object)
         context->delete_blend_state(context, blend_state_object);
      if (rasterizer_state_object)
         context->delete_rasterizer_state(context,
                                          rasterizer_state_object);
#endif
      if (depth_state_object)
         context->delete_depth_stencil_alpha_state(context,
                                                   depth_state_object);
      if (vs)
         context->delete_vs_state(context, vs);
      if (fs)
         context->delete_fs_state(context, fs);
      context->destroy(context);
   }
   if (target)
      screen->resource_destroy(screen, target);
#ifdef PS5_MRT_TEST
   for (unsigned i = 0; i < 3; ++i) {
      if (mrt_targets[i])
         screen->resource_destroy(screen, mrt_targets[i]);
   }
#endif
   if (depth)
      screen->resource_destroy(screen, depth);
#ifdef PS5_INDEXED_PRIMITIVE_TEST
   if (index_buffer)
      screen->resource_destroy(screen, index_buffer);
#endif
   if (screen)
      screen->destroy(screen);
   int passed = draw_status == 0 && draw_calls == 1 && vs_size && fs_size;
#ifdef PS5_FIXED_STATE_TEST
#ifdef PS5_LINE_POLYGON_TEST
   passed = passed && target_nonzero >= 1200 && target_nonzero <= 1800 &&
            target_nonzero == depth_nonzero && target_unexpected == 0;
#elif defined(PS5_POINT_POLYGON_TEST)
   passed = passed && target_nonzero == 2 && depth_nonzero == 2 &&
            target_unexpected == 0;
#elif defined(PS5_POINT_PRIMITIVE_TEST)
   passed = passed && target_nonzero == 1 && depth_nonzero == 1 &&
            target_unexpected == 0;
#elif defined(PS5_LINE_PRIMITIVE_TEST)
   passed = passed && target_nonzero >= 500 && target_nonzero <= 600 &&
            target_nonzero == depth_nonzero && target_unexpected == 0;
#elif defined(PS5_MULTI_POINT_PRIMITIVE_TEST)
   passed = passed && target_nonzero == 3 && depth_nonzero == 3 &&
            target_unexpected == 0;
#elif defined(PS5_MULTI_LINE_PRIMITIVE_TEST)
   passed = passed && target_nonzero == 1080 && depth_nonzero == 1080 &&
            target_unexpected == 0;
#elif defined(PS5_LINE_STRIP_PRIMITIVE_TEST)
   passed = passed && target_nonzero >= 1500 && target_nonzero <= 1700 &&
            target_nonzero == depth_nonzero && target_unexpected == 0;
#elif defined(PS5_TRIANGLE_STRIP_PRIMITIVE_TEST) || \
      defined(PS5_TRIANGLE_FAN_PRIMITIVE_TEST)
   passed = passed && target_nonzero == 259200 &&
            depth_nonzero == 259200 && target_unexpected == 0;
#else
   passed = passed && target_nonzero == expected_coverage &&
            target_unexpected == 0 && depth_nonzero == expected_coverage;
#endif
#else
#ifdef PS5_PACKED_STENCIL_TEST
   passed = passed && target_nonzero == 259200 &&
            depth_nonzero == 259200 && depth_matches == 259200 &&
            depth_unexpected == 0 && stencil_nonzero == 259200 &&
            stencil_matches == 259200 && stencil_unexpected == 0 &&
            stencil_dump_status == 0 && packed_depth_dump_status == 0;
#else
   passed = passed && target_nonzero && depth_nonzero;
#endif
#endif
#ifdef PS5_MRT_TEST
   passed = passed && advertised_render_targets == 4 &&
            mrt_nonzero[0] && mrt_nonzero[1] && mrt_nonzero[2];
#endif
#ifdef PS5_POLYGON_OFFSET_TEST
   passed = passed && depth_min == depth_max &&
            depth_min >= UINT32_C(0x3f400001) &&
            depth_min <= UINT32_C(0x3f400100) && depth_dump_status == 0;
#endif
#ifdef PS5_POLYGON_OFFSET_NEGATIVE_UNITS_TEST
   passed = passed && depth_min == depth_max &&
            depth_min >= UINT32_C(0x3f3fff00) &&
            depth_min < UINT32_C(0x3f400000) && depth_dump_status == 0;
#endif
#ifdef PS5_POLYGON_OFFSET_SLOPE_BASELINE
   passed = passed && depth_min >= UINT32_C(0x3f200000) &&
            depth_max < UINT32_C(0x3f400000) && depth_min < depth_max &&
            depth_dump_status == 0;
#elif defined(PS5_POLYGON_OFFSET_SLOPE_CANDIDATE)
   passed = passed && depth_min > UINT32_C(0x3f200000) &&
            depth_max > UINT32_C(0x3f400000) &&
            depth_max < UINT32_C(0x3f402000) && depth_min < depth_max &&
            depth_dump_status == 0;
#elif defined(PS5_POLYGON_OFFSET_NEGATIVE_FACTOR_TEST)
   passed = passed && depth_min >= UINT32_C(0x3f1fe000) &&
            depth_min < UINT32_C(0x3f200000) &&
            depth_max < UINT32_C(0x3f400000) && depth_min < depth_max &&
            depth_dump_status == 0;
#endif
#ifdef PS5_LINE_POLYGON_BASELINE
   passed = passed && depth_min == UINT32_C(0x3f400000) &&
            depth_max == UINT32_C(0x3f400000) && depth_dump_status == 0;
#elif defined(PS5_LINE_POLYGON_OFFSET_CANDIDATE)
   passed = passed && depth_min == depth_max &&
            depth_min >= UINT32_C(0x3f400001) &&
            depth_min <= UINT32_C(0x3f400100) && depth_dump_status == 0;
#endif
#ifdef PS5_POINT_POLYGON_BASELINE
   passed = passed && depth_min == UINT32_C(0x3f400000) &&
            depth_max == UINT32_C(0x3f400000) && depth_dump_status == 0;
#elif defined(PS5_POINT_POLYGON_OFFSET_CANDIDATE)
   passed = passed && depth_min == UINT32_C(0x3f400002) &&
            depth_max == UINT32_C(0x3f400002) && depth_dump_status == 0;
#endif
#ifdef PS5_PRIMITIVE_TOPOLOGY_TEST
   passed = passed && depth_min == UINT32_C(0x3f400000) &&
            depth_max == UINT32_C(0x3f400000) && depth_dump_status == 0;
#endif
   printf("[ps5-gallium-gate2] result=%d\n", passed ? 0 : 1);
   return passed ? 0 : 1;
}
