#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ps5_screen.h"

#ifdef PS5_RUNTIME_QUIET
static int
ps5_runtime_printf(const char *format, ...)
{
   (void)format;
   return 0;
}
#define printf ps5_runtime_printf
#endif

typedef uint32_t *(*ps5_agc_set_instances_fn)(void *, uint32_t);
typedef uint32_t *(*ps5_agc_draw_auto_fn)(void *, uint32_t, uint64_t);
typedef uint32_t *(*ps5_agc_draw_index_fn)(void *, uint32_t, void *, uint64_t);
typedef uint32_t *(*ps5_agc_set_cx_fn)(void *, const void *, uint32_t);
#define PS5_AGC_MRT_TARGETS 8u
#define PS5_AGC_CX_RECORD_CAPACITY 256u
#define PS5_AGC_STREAMOUT_BUFFERS 4u
#define PS5_AGC_OCCLUSION_RBS 16u
#define PS5_AGC_OCCLUSION_BYTES (PS5_AGC_OCCLUSION_RBS * 16u)
#define PS5_AGC_MAX_COLOR_WIDTH PS5_MAX_RENDER_SIZE
#define PS5_AGC_MAX_COLOR_HEIGHT PS5_MAX_RENDER_SIZE
#define PS5_AGC_MAX_DEPTH_WIDTH PS5_MAX_RENDER_SIZE
#define PS5_AGC_MAX_DEPTH_HEIGHT PS5_MAX_RENDER_SIZE
#define PS5_AGC_FRAMEBUFFER_BYTES PS5_SCANOUT_BYTES
#define PS5_AGC_FRAMEBUFFER_POOL_BYTES PS5_SCANOUT_POOL_BYTES
#define PS5_AGC_FRAMEBUFFER_ALIGNMENT PS5_SCANOUT_ALIGNMENT
/* Preserve the legacy depth adapter's minimum independently of scanout size. */
#define PS5_AGC_DEPTH_LEGACY_MIN_BYTES UINT32_C(0xa00000)
#define PS5_AGC_COLOR_TARGET_ALIGNMENT UINT32_C(0x10000)
#define PS5_AGC_BORDER_COLOR_BYTES UINT32_C(0x10000)
#define PS5_AGC_BORDER_COLOR_ALIGNMENT UINT32_C(0x100)

struct ps5_agc_register {
   uint16_t offset;
   uint16_t padding;
   uint32_t value;
};

struct ps5_agc_command_buffer {
   uint32_t *bottom;
   uint32_t *top;
   uint32_t *up;
   uint32_t *down;
   uintptr_t callback;
   void *user_data;
   uint32_t reserved_dwords;
   uint32_t padding;
};

static int shader_sections(const uint8_t *, size_t, const uint8_t **,
                           size_t *, const uint8_t **, size_t *);
static int validate_shader_header(const uint8_t *, size_t, size_t, uint8_t);
static int ps5_agc_find_register(const struct ps5_agc_register *, uint32_t,
                                 uint16_t);

_Static_assert(sizeof(struct ps5_agc_register) == 8,
               "unexpected AGC register record size");

static ps5_agc_set_instances_fn ps5_agc_set_instances;
static ps5_agc_draw_auto_fn ps5_agc_draw_auto;
static ps5_agc_draw_index_fn ps5_agc_draw_index;
static ps5_agc_set_cx_fn ps5_agc_set_cx;
static uint32_t ps5_agc_instance_count = 1;
static void *ps5_agc_mrt_targets[PS5_AGC_MRT_TARGETS];
static size_t ps5_agc_mrt_sizes[PS5_AGC_MRT_TARGETS];
static void *ps5_agc_scanout_target;
static size_t ps5_agc_scanout_size;
static uint32_t ps5_agc_mrt_blend[PS5_AGC_MRT_TARGETS];
static uint32_t ps5_agc_mrt_color_info[PS5_AGC_MRT_TARGETS] = {
   UINT32_C(0x00008028), UINT32_C(0x00008028),
   UINT32_C(0x00008028), UINT32_C(0x00008028),
   UINT32_C(0x00008028), UINT32_C(0x00008028),
   UINT32_C(0x00008028), UINT32_C(0x00008028),
};
static uint32_t ps5_agc_mrt_attrib2[PS5_AGC_MRT_TARGETS] = {
   UINT32_C(0x01dfc437), UINT32_C(0x01dfc437),
   UINT32_C(0x01dfc437), UINT32_C(0x01dfc437),
   UINT32_C(0x01dfc437), UINT32_C(0x01dfc437),
   UINT32_C(0x01dfc437), UINT32_C(0x01dfc437),
};
static uint32_t ps5_agc_mrt_views[PS5_AGC_MRT_TARGETS];
static uint32_t ps5_agc_mrt_mask = UINT32_C(0xf);
static unsigned ps5_agc_mrt_count = 1;
static unsigned ps5_agc_mrt_samples = 1;
static uint32_t ps5_agc_sample_mask = UINT32_C(0xffff);
static bool ps5_agc_multisample_enable;
static bool ps5_agc_alpha_to_coverage;
static bool ps5_agc_poly_line_smooth;
static bool ps5_agc_sample_shading;
static bool ps5_agc_dual_source_blend;
static const uint8_t *ps5_agc_streamout_package;
static uint32_t ps5_agc_streamout_mask;
static void *ps5_agc_occlusion_query;
static bool ps5_agc_occlusion_precise;
static uint32_t ps5_agc_clip_control;
static bool ps5_agc_clip_control_valid;
static uint32_t ps5_agc_vs_out_control;
static bool ps5_agc_vs_out_control_valid;
static bool ps5_agc_color_to_texture_barrier;
static bool ps5_agc_depth_to_texture_barrier;
static uint32_t ps5_agc_depth_width = PS5_RENDER_WIDTH;
static uint32_t ps5_agc_depth_height = PS5_RENDER_HEIGHT;
static const void *ps5_agc_border_color_table;

static bool
ps5_agc_streamout_enabled(void)
{
   return ps5_agc_streamout_package && ps5_agc_streamout_mask;
}

static int
ps5_agc_replace_or_append_register(struct ps5_agc_register *records,
                                   uint32_t *count, uint16_t offset,
                                   uint32_t value)
{
   int index = ps5_agc_find_register(records, *count, offset);

   if (index >= 0) {
      records[index].value = value;
      return 0;
   }
   if (*count >= PS5_AGC_CX_RECORD_CAPACITY)
      return -1;
   records[(*count)++] = (struct ps5_agc_register){offset, 0, value};
   return 0;
}

static int
ps5_agc_find_register(const struct ps5_agc_register *records, uint32_t count,
                      uint16_t offset)
{
   for (uint32_t i = 0; i < count; ++i) {
      if (records[i].offset == offset)
         return (int)i;
   }
   return -1;
}

static bool
ps5_agc_emit(struct ps5_agc_command_buffer *command, uint32_t value)
{
   if (!command || !command->up || !command->top ||
       command->up >= command->top)
      return false;
   *command->up++ = value;
   return true;
}

#define PS5_AGC_PKT3(op, count) \
   (UINT32_C(0xc0000000) | ((uint32_t)(count) << 16) | \
    ((uint32_t)(op) << 8))

static bool
ps5_agc_emit_streamout_flush(struct ps5_agc_command_buffer *command)
{
   static const uint32_t flush[] = {
      PS5_AGC_PKT3(0x37, 3), 0, UINT32_C(0x0000c03f), 0, 0,
      PS5_AGC_PKT3(0x46, 0), 31,
      PS5_AGC_PKT3(0x3c, 5), 3, UINT32_C(0x0000c03f), 0, 1, 1, 4,
   };

   if (!command || !command->up || !command->top ||
       (size_t)(command->top - command->up) <
          sizeof(flush) / sizeof(flush[0]))
      return false;
   memcpy(command->up, flush, sizeof(flush));
   command->up += sizeof(flush) / sizeof(flush[0]);
   return true;
}

static bool
ps5_agc_emit_streamout_begin(void *buffer)
{
   (void)buffer;
   return true;
}

static bool
ps5_agc_emit_streamout_end(void *buffer)
{
   struct ps5_agc_command_buffer *command = buffer;

   if (!ps5_agc_streamout_enabled())
      return true;
   return ps5_agc_emit_streamout_flush(command);
}

static bool
ps5_agc_emit_occlusion_sample(struct ps5_agc_command_buffer *command,
                              uintptr_t address)
{
   /* GFX10 ZPASS_DONE: EVENT_WRITE, event type 21, event index 1. */
   return ps5_agc_emit(command, PS5_AGC_PKT3(0x46, 2)) &&
          ps5_agc_emit(command, UINT32_C(0x115)) &&
          ps5_agc_emit(command, (uint32_t)address) &&
          ps5_agc_emit(command, (uint32_t)(address >> 32));
}

static bool
ps5_agc_emit_texture_barrier(struct ps5_agc_command_buffer *command)
{
   static const uint32_t color_release[] = {
      UINT32_C(0xc0064900), UINT32_C(0x0070f52d),
      UINT32_C(0x00010000), 0, 0, 0, 0, 0,
   };
   static const uint32_t depth_release[] = {
      UINT32_C(0xc0064900), UINT32_C(0x0070f52b),
      UINT32_C(0x00010000), 0, 0, 0, 0, 0,
   };
   static const uint32_t combined_release[] = {
      UINT32_C(0xc0064900), UINT32_C(0x0070f514),
      UINT32_C(0x00010000), 0, 0, 0, 0, 0,
   };
   const uint32_t *release_mem;

   if (!ps5_agc_color_to_texture_barrier &&
       !ps5_agc_depth_to_texture_barrier)
      return true;
   release_mem = ps5_agc_color_to_texture_barrier &&
                 ps5_agc_depth_to_texture_barrier
                    ? combined_release
                 : ps5_agc_depth_to_texture_barrier
                    ? depth_release : color_release;
   if (!command || !command->up || !command->top ||
       (size_t)(command->top - command->up) <
          sizeof(color_release) / sizeof(color_release[0]))
      return false;
   /* CB/DB flush followed by GLM/GLV/GL1/GL2 writeback/invalidation. */
   memcpy(command->up, release_mem, sizeof(color_release));
   command->up += sizeof(color_release) / sizeof(color_release[0]);
   return true;
}

static uint32_t *
ps5_agc_set_cx_mrt(void *command, const void *table, uint32_t count)
{
   static const uint16_t target0_offsets[16] = {
      0x318, 0x31b, 0x31c, 0x31d, 0x31e, 0x31f, 0x321, 0x323,
      0x324, 0x325, 0x390, 0x398, 0x3a0, 0x3a8, 0x3b0, 0x3b8,
   };
   struct ps5_agc_register *records = (struct ps5_agc_register *)table;
   int source[16];
   bool initial_graphics_table =
      records && ps5_agc_find_register(records, count, 0x0318u) >= 0 &&
      ps5_agc_find_register(records, count, 0x02d5u) >= 0;

   if (records) {
      uintptr_t address = (uintptr_t)ps5_agc_mrt_targets[0];
      int color_base = ps5_agc_find_register(records, count, 0x0318u);
      int color_base_ext = ps5_agc_find_register(records, count, 0x0390u);
      int color_view = ps5_agc_find_register(records, count, 0x031bu);
      int color_info = ps5_agc_find_register(records, count, 0x031cu);
      int color_attrib = ps5_agc_find_register(records, count, 0x031du);
      int color_attrib2 = ps5_agc_find_register(records, count, 0x03b0u);

      if (address && color_base >= 0)
         records[color_base].value = (uint32_t)(address >> 8);
      if (address && color_base_ext >= 0)
         records[color_base_ext].value =
            (records[color_base_ext].value & UINT32_C(0xffffff00)) |
            (uint32_t)(address >> 40);
      if (color_info >= 0)
         records[color_info].value = ps5_agc_mrt_color_info[0];
      if (color_view >= 0)
         records[color_view].value = ps5_agc_mrt_views[0];
      if (color_attrib >= 0) {
         records[color_attrib].value &= ~UINT32_C(0x0001f000);
         if (ps5_agc_mrt_samples == 4)
            records[color_attrib].value |= UINT32_C(0x00012000);
      }
      if (color_attrib2 >= 0)
         records[color_attrib2].value = ps5_agc_mrt_attrib2[0];
      if (address && ps5_agc_mrt_samples == 4) {
         const uint16_t low_offsets[] = {0x031fu, 0x0321u};
         const uint16_t high_offsets[] = {0x0398u, 0x03a0u};

         for (unsigned i = 0; i < 2; ++i) {
            int low = ps5_agc_find_register(records, count, low_offsets[i]);
            int high = ps5_agc_find_register(records, count, high_offsets[i]);

            if (low >= 0)
               records[low].value = (uint32_t)(address >> 8);
            if (high >= 0)
               records[high].value =
                  (records[high].value & UINT32_C(0xffffff00)) |
                  (uint32_t)(address >> 40);
         }
      }
   }

   if ((ps5_agc_mrt_count > 1 || ps5_agc_dual_source_blend) && table) {
      bool target_table = ps5_agc_mrt_count > 1;

      if (ps5_agc_mrt_count > 1) {
         for (unsigned i = 0; i < 16; ++i) {
            source[i] = ps5_agc_find_register(records, count,
                                              target0_offsets[i]);
            target_table &= source[i] >= 0;
         }
      }
      if (target_table &&
          count + 16u * (ps5_agc_mrt_count - 1u) <=
             PS5_AGC_CX_RECORD_CAPACITY) {
         for (unsigned target = 1; target < ps5_agc_mrt_count; ++target) {
            uintptr_t address = (uintptr_t)ps5_agc_mrt_targets[target];

            for (unsigned i = 0; i < 16; ++i) {
               struct ps5_agc_register record = records[source[i]];

               record.offset += i < 10 ? 0xfu * target : target;
               if (i == 0)
                  record.value = (uint32_t)(address >> 8);
               else if (i == 1)
                  record.value = ps5_agc_mrt_views[target];
               else if (ps5_agc_mrt_samples == 4 && (i == 5 || i == 6))
                  record.value = (uint32_t)(address >> 8);
               else if (i == 2)
                  record.value = ps5_agc_mrt_color_info[target];
               else if (i == 10)
                  record.value = (record.value & UINT32_C(0xffffff00)) |
                                 (uint32_t)(address >> 40);
               else if (ps5_agc_mrt_samples == 4 && (i == 11 || i == 12))
                  record.value = (record.value & UINT32_C(0xffffff00)) |
                                 (uint32_t)(address >> 40);
               else if (i == 14)
                  record.value = ps5_agc_mrt_attrib2[target];
               records[count++] = record;
            }
         }
      }

      int target_mask = ps5_agc_find_register(records, count, 0x08e);
      int blend0 = ps5_agc_find_register(records, count, 0x01e0);
      if (ps5_agc_dual_source_blend && target_mask >= 0 && blend0 >= 0 &&
          ps5_agc_replace_or_append_register(
             records, &count, 0x01d8u, 0) != 0)
         return NULL;
      /* GFX10 pairs MRT0/MRT1 as source 0/source 1 only while blend slot 1
       * is enabled.  This does not attach or write a second color target. */
      if (ps5_agc_dual_source_blend && ps5_agc_mrt_count == 1 &&
          blend0 >= 0 &&
          ps5_agc_replace_or_append_register(
             records, &count, 0x01e1u, UINT32_C(1) << 30) != 0)
         return NULL;
      if (ps5_agc_mrt_count > 1 && target_mask >= 0 && blend0 >= 0 &&
          count + ps5_agc_mrt_count - 1u <=
             PS5_AGC_CX_RECORD_CAPACITY) {
         records[target_mask].value = ps5_agc_mrt_mask;
         records[blend0].value = ps5_agc_mrt_blend[0];
         for (unsigned target = 1; target < ps5_agc_mrt_count; ++target) {
            records[count++] = (struct ps5_agc_register){
               (uint16_t)(0x01e0u + target), 0,
               ps5_agc_mrt_blend[target],
            };
         }
      }
   }
   if (ps5_agc_occlusion_query && initial_graphics_table &&
       ps5_agc_replace_or_append_register(
          records, &count, 0x0001u,
          ps5_agc_occlusion_precise ? UINT32_C(0xff000f06)
                                    : UINT32_C(0xff000f02)) != 0)
      return NULL;
   if (ps5_agc_clip_control_valid && records &&
       ps5_agc_find_register(records, count, 0x0205u) >= 0 &&
       ps5_agc_replace_or_append_register(
          records, &count, 0x0204u, ps5_agc_clip_control) != 0)
      return NULL;
   if (ps5_agc_vs_out_control_valid && records &&
       ps5_agc_find_register(records, count, 0x0207u) >= 0 &&
       ps5_agc_replace_or_append_register(
          records, &count, 0x0207u, ps5_agc_vs_out_control) != 0)
      return NULL;
   if (initial_graphics_table) {
      bool msaa4 = ps5_agc_mrt_samples == 4 &&
                   ps5_agc_multisample_enable;
      bool sample_shading4 = msaa4 && ps5_agc_sample_shading;
      bool smooth4 = ps5_agc_mrt_samples == 1 &&
                     ps5_agc_poly_line_smooth;
      bool raster4 = msaa4 || smooth4;
      int sc_mode_cntl_1 = ps5_agc_find_register(records, count, 0x0293u);
      int spi_baryc_cntl = ps5_agc_find_register(records, count, 0x01b8u);
      uint32_t sc_mode_cntl_1_value =
         sc_mode_cntl_1 >= 0 ? records[sc_mode_cntl_1].value : 0;
      uint32_t spi_baryc_cntl_value =
         spi_baryc_cntl >= 0 ? records[spi_baryc_cntl].value : 0;
      uint32_t mask = ps5_agc_sample_mask & UINT32_C(0xffff);
      uint32_t packed_mask = mask | (mask << 16);
      static const uint16_t sample_location_offsets[] = {
         0x02feu, 0x0302u, 0x0306u, 0x030au,
      };

      if (ps5_agc_replace_or_append_register(
             records, &count, 0x0201u,
             sample_shading4 ? UINT32_C(0x00132222) :
             msaa4 ? UINT32_C(0x00132202) :
             smooth4 ? UINT32_C(0x02130000) :
                       UINT32_C(0x00130000)) != 0 ||
          ps5_agc_replace_or_append_register(
             records, &count, 0x0293u,
             (sc_mode_cntl_1_value & ~UINT32_C(0x00010000)) |
                (sample_shading4 ? UINT32_C(0x00010000) : 0)) != 0 ||
          ps5_agc_replace_or_append_register(
             records, &count, 0x01b8u,
             (spi_baryc_cntl_value & ~UINT32_C(0x00030000)) |
                (sample_shading4 ? UINT32_C(0x00020000) : 0)) != 0 ||
          /* GFX10 PA_SC_MODE_CNTL_0: preserve the canonical viewport-scissor
           * and alternate-RB baseline while toggling sample rasterization. */
          ps5_agc_replace_or_append_register(
             records, &count, 0x0292u,
             raster4 ? UINT32_C(0x00000023) :
                       UINT32_C(0x00000022)) != 0 ||
          /* Expand the single-sample line footprint for 4x over-rasterized
           * coverage while keeping color/depth storage single-sampled. */
          ps5_agc_replace_or_append_register(
             records, &count, 0x02f7u,
             smooth4 ? UINT32_C(0x00000200) : 0) != 0 ||
          ps5_agc_replace_or_append_register(
             records, &count, 0x02f5u,
             raster4 ? UINT32_C(0x32103210) : 0) != 0 ||
          ps5_agc_replace_or_append_register(
             records, &count, 0x02f6u,
             raster4 ? UINT32_C(0x32103210) : 0) != 0 ||
          ps5_agc_replace_or_append_register(
             records, &count, 0x02f8u,
             raster4 ? UINT32_C(0x0020c002) : 0) != 0 ||
          /* GFX10 DB_ALPHA_TO_MASK: RADV's dithered 4x thresholds. */
          ps5_agc_replace_or_append_register(
             records, &count, 0x02dcu,
             UINT32_C(0x00018700) |
                (msaa4 && ps5_agc_alpha_to_coverage ? 1u : 0u)) != 0 ||
          ps5_agc_replace_or_append_register(
             records, &count, 0x030eu, packed_mask) != 0 ||
          ps5_agc_replace_or_append_register(
             records, &count, 0x030fu, packed_mask) != 0)
         return NULL;
      for (unsigned i = 0; i < 4; ++i) {
         if (ps5_agc_replace_or_append_register(
                records, &count, sample_location_offsets[i],
                raster4 ? UINT32_C(0xe62a62ae) : 0) != 0)
            return NULL;
      }
      if (ps5_agc_border_color_table) {
         uintptr_t address = (uintptr_t)ps5_agc_border_color_table;

         if (ps5_agc_replace_or_append_register(
                records, &count, 0x0020u,
                (uint32_t)(address >> 8)) != 0 ||
             ps5_agc_replace_or_append_register(
                records, &count, 0x0021u,
                (uint32_t)(address >> 40)) != 0)
            return NULL;
      }
   }
   if (records) {
      int depth_size = ps5_agc_find_register(records, count, 0x0007u);

      if (depth_size >= 0)
         records[depth_size].value = (ps5_agc_depth_width - 1u) |
                                     ((ps5_agc_depth_height - 1u) << 16);
   }
   return ps5_agc_set_cx(command, records, count);
}

int
ps5_agc_gate2_set_instance_count(unsigned int count)
{
   if (!count)
      return -1;
   ps5_agc_instance_count = count;
   return 0;
}

static uint32_t *
ps5_agc_draw_auto_instanced(void *command, uint32_t count, uint64_t modifier)
{
   struct ps5_agc_command_buffer *buffer = command;
   uint32_t *result;

   if (ps5_agc_instance_count == 1 && !ps5_agc_streamout_enabled() &&
       !ps5_agc_occlusion_query && !ps5_agc_color_to_texture_barrier &&
       !ps5_agc_depth_to_texture_barrier)
      return ps5_agc_draw_auto(command, count, modifier);
   if (ps5_agc_instance_count != 1)
      ps5_agc_set_instances(command, ps5_agc_instance_count);
   if (!ps5_agc_emit_streamout_begin(command)) {
      if (ps5_agc_instance_count != 1)
         ps5_agc_set_instances(command, 1);
      return NULL;
   }
   if (ps5_agc_occlusion_query &&
       !ps5_agc_emit_occlusion_sample(
          buffer, (uintptr_t)ps5_agc_occlusion_query)) {
      if (ps5_agc_instance_count != 1)
         ps5_agc_set_instances(command, 1);
      return NULL;
   }
   result = ps5_agc_draw_auto(command, count, modifier);
   if (ps5_agc_occlusion_query &&
       !ps5_agc_emit_occlusion_sample(
          buffer, (uintptr_t)ps5_agc_occlusion_query + 8u))
      result = NULL;
   if (!ps5_agc_emit_streamout_end(command))
      result = NULL;
   if (!ps5_agc_emit_texture_barrier(buffer))
      result = NULL;
   if (ps5_agc_instance_count != 1)
      ps5_agc_set_instances(command, 1);
   return result;
}

static uint32_t *
ps5_agc_draw_index_instanced(void *command, uint32_t count, void *indices,
                             uint64_t modifier)
{
   struct ps5_agc_command_buffer *buffer = command;
   uint32_t *result;

   if (ps5_agc_instance_count == 1 && !ps5_agc_streamout_enabled() &&
       !ps5_agc_occlusion_query && !ps5_agc_color_to_texture_barrier &&
       !ps5_agc_depth_to_texture_barrier)
      return ps5_agc_draw_index(command, count, indices, modifier);
   if (ps5_agc_instance_count != 1)
      ps5_agc_set_instances(command, ps5_agc_instance_count);
   if (!ps5_agc_emit_streamout_begin(command)) {
      if (ps5_agc_instance_count != 1)
         ps5_agc_set_instances(command, 1);
      return NULL;
   }
   if (ps5_agc_occlusion_query &&
       !ps5_agc_emit_occlusion_sample(
          buffer, (uintptr_t)ps5_agc_occlusion_query)) {
      if (ps5_agc_instance_count != 1)
         ps5_agc_set_instances(command, 1);
      return NULL;
   }
   result = ps5_agc_draw_index(command, count, indices, modifier);
   if (ps5_agc_occlusion_query &&
       !ps5_agc_emit_occlusion_sample(
          buffer, (uintptr_t)ps5_agc_occlusion_query + 8u))
      result = NULL;
   if (!ps5_agc_emit_streamout_end(command))
      result = NULL;
   if (!ps5_agc_emit_texture_barrier(buffer))
      result = NULL;
   if (ps5_agc_instance_count != 1)
      ps5_agc_set_instances(command, 1);
   return result;
}

static int
ps5_agc_load_instance_builder(void *module)
{
   if (!ps5_agc_set_instances)
      ps5_agc_set_instances =
         (ps5_agc_set_instances_fn)dlsym(module,
                                         "sceAgcDcbSetNumInstances");
   return ps5_agc_set_instances != NULL;
}

static int
ps5_agc_runtime_bind_native_api(
   ps5_agc_set_instances_fn set_instances,
   ps5_agc_draw_auto_fn draw_auto, ps5_agc_draw_index_fn draw_index,
   ps5_agc_set_cx_fn set_cx, ps5_agc_draw_auto_fn *wrapped_draw_auto,
   ps5_agc_draw_index_fn *wrapped_draw_index,
   ps5_agc_set_cx_fn *wrapped_set_cx)
{
   if (!set_instances || !draw_auto || !draw_index || !set_cx ||
       !wrapped_draw_auto || !wrapped_draw_index || !wrapped_set_cx)
      return -1;
   ps5_agc_set_instances = set_instances;
   ps5_agc_draw_auto = draw_auto;
   ps5_agc_draw_index = draw_index;
   ps5_agc_set_cx = set_cx;
   *wrapped_draw_auto = ps5_agc_draw_auto_instanced;
   *wrapped_draw_index = ps5_agc_draw_index_instanced;
   *wrapped_set_cx = ps5_agc_set_cx_mrt;
   return 0;
}

static void *
ps5_agc_runtime_dlsym(void *module, const char *name)
{
   void *symbol = dlsym(module, name);

   if (!symbol)
      return NULL;
   if (strcmp(name, "sceAgcDcbDrawIndexAuto") == 0) {
      ps5_agc_draw_auto = (ps5_agc_draw_auto_fn)symbol;
      return ps5_agc_load_instance_builder(module)
                ? (void *)ps5_agc_draw_auto_instanced : NULL;
   }
   if (strcmp(name, "sceAgcDcbDrawIndex") == 0) {
      ps5_agc_draw_index = (ps5_agc_draw_index_fn)symbol;
      return ps5_agc_load_instance_builder(module)
                 ? (void *)ps5_agc_draw_index_instanced : NULL;
   }
   if (strcmp(name, "sceAgcDcbSetCxRegistersIndirect") == 0) {
      ps5_agc_set_cx = (ps5_agc_set_cx_fn)symbol;
      return (void *)ps5_agc_set_cx_mrt;
   }
   return symbol;
}

#define dlsym ps5_agc_runtime_dlsym
#define PS5_AGC_BIND_NATIVE_API(api, set_instances) \
   ps5_agc_runtime_bind_native_api( \
      (set_instances), (api)->draw_auto, (api)->draw_index, (api)->set_cx, \
      &(api)->draw_auto, &(api)->draw_index, &(api)->set_cx)
#define ps5_agc_gate2_set_depth_buffer ps5_agc_native_set_depth_buffer
#define ps5_agc_gate2_set_depth_stencil_buffer \
   ps5_agc_native_set_depth_stencil_buffer
#include "ps5_agc_native_runtime.c"
#undef ps5_agc_gate2_set_depth_stencil_buffer
#undef ps5_agc_gate2_set_depth_buffer
#undef PS5_AGC_BIND_NATIVE_API
#undef dlsym

int
ps5_agc_gate2_set_depth_buffer(void *depth, size_t size,
                               uint32_t depth_control)
{
   unsigned tile_shift = ps5_agc_mrt_samples == 4 ? 6u : 7u;
   unsigned tile_mask = (1u << tile_shift) - 1u;
   size_t required =
      (size_t)((ps5_agc_depth_width + tile_mask) >> tile_shift) *
      ((ps5_agc_depth_height + tile_mask) >> tile_shift) *
                     UINT32_C(0x10000);

   if (!depth)
      return ps5_agc_native_set_depth_buffer(depth, size, depth_control);
   if (size < required)
      return -1;
   return ps5_agc_native_set_depth_buffer(
      depth, size < PS5_AGC_DEPTH_LEGACY_MIN_BYTES
                ? PS5_AGC_DEPTH_LEGACY_MIN_BYTES : size,
      depth_control);
}

int
ps5_agc_gate2_set_depth_stencil_buffer(
   void *depth, size_t depth_size, void *stencil, size_t stencil_size,
   uint32_t depth_control, uint32_t stencil_control,
   uint32_t stencil_refmask, uint32_t stencil_refmask_bf)
{
   unsigned depth_shift = ps5_agc_mrt_samples == 4 ? 6u : 7u;
   unsigned stencil_shift = ps5_agc_mrt_samples == 4 ? 7u : 8u;
   unsigned depth_mask = (1u << depth_shift) - 1u;
   unsigned stencil_mask = (1u << stencil_shift) - 1u;
   size_t required_depth =
      (size_t)((ps5_agc_depth_width + depth_mask) >> depth_shift) *
      ((ps5_agc_depth_height + depth_mask) >> depth_shift) *
      UINT32_C(0x10000);
   size_t required_stencil =
      (size_t)((ps5_agc_depth_width + stencil_mask) >> stencil_shift) *
      ((ps5_agc_depth_height + stencil_mask) >> stencil_shift) *
      UINT32_C(0x10000);

   if (!depth || !stencil || depth_size < required_depth ||
       stencil_size < required_stencil)
      return -1;
   return ps5_agc_native_set_depth_stencil_buffer(
      depth, depth_size < PS5_AGC_DEPTH_LEGACY_MIN_BYTES
                ? PS5_AGC_DEPTH_LEGACY_MIN_BYTES : depth_size,
      stencil, stencil_size < UINT32_C(0x280000)
                  ? UINT32_C(0x280000) : stencil_size,
      depth_control, stencil_control, stencil_refmask,
      stencil_refmask_bf);
}

int
ps5_agc_gate2_set_depth_target_extents(uint32_t width, uint32_t height)
{
   if (!width || !height || width > PS5_AGC_MAX_DEPTH_WIDTH ||
       height > PS5_AGC_MAX_DEPTH_HEIGHT)
      return -1;
   ps5_agc_depth_width = width;
   ps5_agc_depth_height = height;
   return 0;
}

int
ps5_agc_gate2_set_border_color_table(const void *table, size_t size)
{
   if (!table) {
      if (size)
         return -1;
      ps5_agc_border_color_table = NULL;
      return 0;
   }
   if (size < PS5_AGC_BORDER_COLOR_BYTES ||
       ((uintptr_t)table & (PS5_AGC_BORDER_COLOR_ALIGNMENT - 1u)))
      return -1;
   ps5_agc_border_color_table = table;
   return 0;
}

int
ps5_agc_gate2_set_streamout(const void *vertex_shader,
                            size_t vertex_shader_size,
                            uint32_t enabled_mask,
                            const uint32_t size_dwords[4],
                            const uint32_t stride_dwords[4],
                            const uint32_t offset_dwords[4])
{
   if (!vertex_shader) {
      if (vertex_shader_size || enabled_mask)
         return -1;
      ps5_agc_streamout_package = NULL;
      ps5_agc_streamout_mask = 0;
      return 0;
   }
   if (!vertex_shader_size || !enabled_mask ||
       enabled_mask >> PS5_AGC_STREAMOUT_BUFFERS || !size_dwords ||
       !stride_dwords || !offset_dwords)
      return -1;
   for (unsigned index = 0; index < PS5_AGC_STREAMOUT_BUFFERS; ++index) {
      if (!(enabled_mask & (1u << index)))
         continue;
      if (!stride_dwords[index] ||
          offset_dwords[index] > UINT32_MAX / 4u ||
          offset_dwords[index] > size_dwords[index])
         return -1;
   }
   ps5_agc_streamout_package = vertex_shader;
   ps5_agc_streamout_mask = enabled_mask;
   return 0;
}

int
ps5_agc_gate2_set_occlusion_query(void *query, size_t size, bool precise)
{
   if (!query) {
      if (size)
         return -1;
      ps5_agc_occlusion_query = NULL;
      ps5_agc_occlusion_precise = false;
      return 0;
   }
   if (size < PS5_AGC_OCCLUSION_BYTES ||
       ((uintptr_t)query & (sizeof(uint64_t) - 1u)))
      return -1;
   ps5_agc_occlusion_query = query;
   ps5_agc_occlusion_precise = precise;
   return 0;
}

int
ps5_agc_gate2_set_clip_control(uint32_t control, uint32_t valid)
{
   if (valid > 1u)
      return -1;
   ps5_agc_clip_control = control;
   ps5_agc_clip_control_valid = valid != 0;
   return 0;
}

int
ps5_agc_gate2_set_vs_out_control(uint32_t control, uint32_t valid)
{
   if (valid > 1u)
      return -1;
   ps5_agc_vs_out_control = control;
   ps5_agc_vs_out_control_valid = valid != 0;
   return 0;
}

int
ps5_agc_gate2_set_color_to_texture_barrier(uint32_t enabled)
{
   if (enabled > 1u)
      return -1;
   ps5_agc_color_to_texture_barrier = enabled != 0;
   return 0;
}

int
ps5_agc_gate2_set_multisample_state(unsigned samples, uint32_t sample_mask,
                                    uint32_t enabled,
                                    uint32_t alpha_to_coverage,
                                    uint32_t poly_line_smooth,
                                    uint32_t sample_shading)
{
   if ((samples != 1 && samples != 4) || enabled > 1u ||
       alpha_to_coverage > 1u || poly_line_smooth > 1u ||
       sample_shading > 1u ||
       (samples != 1 && poly_line_smooth))
      return -1;
   ps5_agc_mrt_samples = samples;
   runtime_depth_samples = samples;
   ps5_agc_sample_mask = sample_mask;
   ps5_agc_multisample_enable = enabled != 0;
   ps5_agc_alpha_to_coverage = alpha_to_coverage != 0;
   ps5_agc_poly_line_smooth = poly_line_smooth != 0;
   ps5_agc_sample_shading = sample_shading != 0;
   return 0;
}

int
ps5_agc_gate2_set_depth_to_texture_barrier(uint32_t enabled)
{
   if (enabled > 1u)
      return -1;
   ps5_agc_depth_to_texture_barrier = enabled != 0;
   return 0;
}

int
ps5_agc_gate2_set_color_target_extents(const uint32_t *widths,
                                       const uint32_t *heights,
                                       unsigned count)
{
   if (!widths || !heights || !count || count > PS5_AGC_MRT_TARGETS ||
       count != ps5_agc_mrt_count)
      return -1;
   for (unsigned target = 0; target < count; ++target) {
      size_t required;
      unsigned tile_width = 128;
      unsigned tile_height = 128;
      unsigned format = (ps5_agc_mrt_color_info[target] >> 2) & 0x1fu;
      unsigned bytes_per_pixel;

      if (!widths[target] || widths[target] > PS5_AGC_MAX_COLOR_WIDTH ||
          !heights[target] || heights[target] > PS5_AGC_MAX_COLOR_HEIGHT)
         return -1;
      switch (format) {
      case 1: bytes_per_pixel = 1; break;
      case 2:
      case 3: bytes_per_pixel = 2; break;
      case 4:
      case 5:
      case 6:
      case 8:
      case 9:
      case 10: bytes_per_pixel = 4; break;
      case 11:
      case 12: bytes_per_pixel = 8; break;
      case 14: bytes_per_pixel = 16; break;
      default: return -1;
      }
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
      if (ps5_agc_mrt_samples == 4) {
         /* Match the allocator's format-specific 64 KiB MSAA tiles. */
         tile_width /= 2;
         tile_height /= 2;
      }
      required = (size_t)((widths[target] + tile_width - 1u) / tile_width) *
                 ((heights[target] + tile_height - 1u) / tile_height) *
                 UINT32_C(0x10000);
      if (ps5_agc_mrt_sizes[target] < required)
         return -1;
      ps5_agc_mrt_attrib2[target] =
         (heights[target] - 1u) | ((widths[target] - 1u) << 14);
   }
   return 0;
}

int
ps5_agc_gate2_set_color_target_views(const uint32_t *views, unsigned count)
{
   const uint32_t view_mask = UINT32_C(0x7ff) |
                              (UINT32_C(0x7ff) << 13);

   if (!views || !count || count > PS5_AGC_MRT_TARGETS ||
       count != ps5_agc_mrt_count)
      return -1;
   for (unsigned target = 0; target < count; ++target) {
      unsigned first = views[target] & UINT32_C(0x7ff);
      unsigned last = (views[target] >> 13) & UINT32_C(0x7ff);

      if ((views[target] & ~view_mask) || first > last)
         return -1;
   }
   memcpy(ps5_agc_mrt_views, views, count * sizeof(views[0]));
   return 0;
}

int
ps5_agc_gate2_set_color_target_info(const uint32_t *values, unsigned count)
{
   if (!values || !count || count > PS5_AGC_MRT_TARGETS ||
       count != ps5_agc_mrt_count)
      return -1;
   for (unsigned target = 0; target < count; ++target) {
      unsigned format = (values[target] >> 2) & 0x1fu;
      unsigned number_type = (values[target] >> 8) & 7u;
      unsigned swap = (values[target] >> 11) & 3u;
      uint32_t expected = (format << 2) | (number_type << 8) |
                          (swap << 11);

      if (number_type <= 1u || number_type == 6u)
         expected |= UINT32_C(1) << 15;
      else
         expected |= (UINT32_C(1) << 17) | (UINT32_C(1) << 18);
      if (number_type == 4u || number_type == 5u)
         expected |= UINT32_C(1) << 16;
      if (values[target] != expected || !format ||
          format == 7u || format == 13u || format > 14u)
         return -1;
   }
   memcpy(ps5_agc_mrt_color_info, values, count * sizeof(values[0]));
   return 0;
}

int
ps5_agc_gate2_set_scanout(void *framebuffer, size_t size)
{
   if (!framebuffer || size < PS5_AGC_FRAMEBUFFER_POOL_BYTES ||
       ((uintptr_t)framebuffer & (PS5_AGC_FRAMEBUFFER_ALIGNMENT - 1u)))
      return -1;
   ps5_agc_scanout_target = framebuffer;
   ps5_agc_scanout_size = size;
   return 0;
}

int
ps5_agc_gate2_set_framebuffers(void *const *targets, const size_t *sizes,
                               unsigned count)
{
   void *runtime_target;
   size_t runtime_size;

   if (!targets || !sizes || !count || count > PS5_AGC_MRT_TARGETS ||
       (ps5_agc_dual_source_blend && count != 1))
      return -1;
   for (unsigned i = 0; i < count; ++i) {
      if (!targets[i] || sizes[i] < UINT32_C(0x10000) ||
          ((uintptr_t)targets[i] &
           (PS5_AGC_COLOR_TARGET_ALIGNMENT - 1u)))
         return -1;
   }
   /* Legacy callers can seed scanout once. A large offscreen allocation
    * must never replace the explicitly registered display pool. */
   if (!ps5_agc_scanout_target &&
       sizes[0] >= PS5_AGC_FRAMEBUFFER_POOL_BYTES) {
      ps5_agc_scanout_target = targets[0];
      ps5_agc_scanout_size = sizes[0];
   }
   if (ps5_agc_scanout_target) {
      runtime_target = ps5_agc_scanout_target;
      runtime_size = ps5_agc_scanout_size;
   } else {
      runtime_target = targets[0];
      runtime_size = sizes[0];
   }
   if (!runtime_target || runtime_size < PS5_AGC_FRAMEBUFFER_BYTES ||
       ps5_agc_gate2_set_framebuffer(runtime_target, runtime_size) != 0)
      return -1;
#ifdef AGC_RUNTIME_DIAGNOSTICS
    const bool scanout_target = ps5_agc_scanout_target &&
       (uintptr_t)targets[0] >= (uintptr_t)ps5_agc_scanout_target &&
       (uintptr_t)targets[0] < (uintptr_t)ps5_agc_scanout_target +
          2u * PS5_AGC_FRAMEBUFFER_BYTES;
    printf("[ps5-agc] color-target actual=%zu runtime=%zu scanout=%u slot=%u\n",
           sizes[0], runtime_size,
           scanout_target ? 1u : 0u,
           scanout_target ?
              (unsigned)(((uintptr_t)targets[0] -
                          (uintptr_t)ps5_agc_scanout_target) /
                         PS5_AGC_FRAMEBUFFER_BYTES) : UINT32_MAX);
#endif
   memset(ps5_agc_mrt_targets, 0, sizeof(ps5_agc_mrt_targets));
   memset(ps5_agc_mrt_sizes, 0, sizeof(ps5_agc_mrt_sizes));
   memcpy(ps5_agc_mrt_targets, targets, count * sizeof(targets[0]));
   memcpy(ps5_agc_mrt_sizes, sizes, count * sizeof(sizes[0]));
   ps5_agc_mrt_count = count;
   return 0;
}

int
ps5_agc_gate2_set_dual_source_blend(uint32_t enabled)
{
   if (enabled > 1u)
      return -1;
   ps5_agc_dual_source_blend = enabled != 0;
   return 0;
}

int
ps5_agc_gate2_set_graphics_state_mrt(
   const uint32_t *blend_control, unsigned count, uint32_t target_mask,
   uint32_t color_control, uint32_t color_control_valid,
   const uint32_t blend_color[4], const uint32_t viewport[8],
   const uint32_t scissor[2], uint32_t rasterizer_control,
   uint32_t rasterizer_valid, const uint32_t polygon_offset[6],
   uint32_t polygon_offset_valid)
{
   /* The matching framebuffer list is installed later in the same draw. */
   if (!blend_control || !count || count > PS5_AGC_MRT_TARGETS ||
       (count < 8 && target_mask >> (4u * count)))
      return -1;
   memcpy(ps5_agc_mrt_blend, blend_control,
          count * sizeof(blend_control[0]));
   ps5_agc_mrt_mask = target_mask;
   return ps5_agc_gate2_set_graphics_state(
      blend_control[0], target_mask & UINT32_C(0xf), color_control,
      color_control_valid, blend_color, viewport, scissor,
      rasterizer_control, rasterizer_valid, polygon_offset,
      polygon_offset_valid);
}
