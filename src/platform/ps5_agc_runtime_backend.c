// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

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
/* Byte pitches: zero retains the existing 64KB_R_X target layout. */
static uint32_t ps5_agc_mrt_pitches[PS5_AGC_MRT_TARGETS];
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

   for (unsigned target = 0; target < ps5_agc_mrt_count; ++target) {
      if (ps5_agc_mrt_pitches[target] &&
          (ps5_agc_mrt_samples != 1 || ps5_agc_mrt_views[target]))
         return NULL;
   }

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
   /* Apply layouts after cloning MRT0: a linear MRT0 must not change tiled
    * siblings. Mesa ac_init_gfx10_cb_surface: standalone 2D, mip/slice zero;
    * ac_set_mutable_cb_surface_fields supplies COLOR_SW_MODE (bits 14..18).
    * Retain the existing RESOURCE_LEVEL and disabled-metadata policy. */
   if (records) {
      for (unsigned target = 0; target < ps5_agc_mrt_count; ++target) {
         int attrib3 = ps5_agc_find_register(records, count, 0x03b8u + target);
         int attrib = ps5_agc_find_register(records, count, 0x031du + 15u * target);

         if (ps5_agc_mrt_pitches[target] && attrib >= 0) {
            bool no_alpha = ps5_agc_mrt_color_info[target] == UINT32_C(0x8004) ||
                            ps5_agc_mrt_color_info[target] == UINT32_C(0x800c);
            /* ac_init_cb_surface: absent destination alpha is one. */
            records[attrib].value = (records[attrib].value & ~UINT32_C(0x20000)) |
                                    (no_alpha ? UINT32_C(0x20000) : 0);
         }

         if (attrib3 < 0)
            continue;
         uint32_t value = records[attrib3].value & ~UINT32_C(0x0007c000);
         if (ps5_agc_mrt_pitches[target])
            value = (value & ~UINT32_C(0x03f83fff)) | UINT32_C(0x01000000);
         else {
            value |= UINT32_C(0x0006c000); /* 64KB_R_X, including after reset. */
            /* Preserve absolute array slices instead of rebasing their XOR. */
            value = (value & ~UINT32_C(0x1fff)) |
                    (((ps5_agc_mrt_views[target] >> 13) & 0x7ffu) + 1u);
         }
         records[attrib3].value = value;
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
         /* Keep hardware sample indices aligned with u_default_get_sample_position. */
         if (ps5_agc_replace_or_append_register(
                records, &count, sample_location_offsets[i],
                raster4 ? UINT32_C(0x622ae6ae) : 0) != 0)
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

#ifdef PS5_NATIVE_TITLE_RUNTIME
#include "ps5_agc_package.h"

int
ps5_agc_compute_execute(struct pipe_screen *screen,
                        const PsbcShaderOutput *shader,
                        struct pipe_resource *descriptors,
                        struct pipe_resource *const *buffers,
                        unsigned buffer_count, const uint32_t groups[3])
{
   extern uint32_t *sceAgcDcbAcquireMem(void *, uint8_t, uint32_t, uint32_t,
                                       uint64_t, uint64_t, uint32_t);
   extern uint32_t *sceAgcCbDispatch(void *, uint32_t, uint32_t, uint32_t, uint32_t);
   void *addresses[PS5_AGC_COMPUTE_MAX_RESOURCES], *table = NULL, *program = NULL;
   size_t sizes[PS5_AGC_COMPUTE_MAX_RESOURCES], table_size = 0, package_size = 0;
   size_t allocation_sizes[PS5_AGC_COMPUTE_MAX_RESOURCES], table_allocation_size = 0;
   uint8_t *package = NULL, *memory = NULL;
   int64_t physical = -1;
   size_t memory_size = 0;
   uint32_t tmpring_size = 0;
   int result = -1;
   bool locked = false;
   agc_api_t agc = {0};
   video_api_t video = {0};
   uint32_t user_data[16] = {0};
   const uint8_t *header, *code;
   size_t header_size = 0, code_size = 0;
   if (!screen || !shader || !groups || buffer_count > PS5_AGC_COMPUTE_MAX_RESOURCES ||
       (buffer_count && !buffers) ||
       shader->metadata.hardware_stage != PSBC_HW_STAGE_COMPUTE)
      return -1;
   for (unsigned i = 0; i < 3; ++i)
      if (!groups[i] || groups[i] > 65535)
         return -1;

   if (ps5_agc_package_build(shader, 0, &package, &package_size) ||
       shader_sections(package, package_size, &header, &header_size, &code, &code_size) ||
       validate_shader_header(header, header_size, code_size, 0) ||
       header_size > 4096 || code_size > 16 * 1024 * 1024)
      goto cleanup;
   const PsbcShaderMetadata *m = &shader->metadata;
   for (unsigned i = 0; i < buffer_count; ++i)
      if (ps5_resource_info(buffers[i], &addresses[i], &sizes[i], &allocation_sizes[i]) ||
          !addresses[i] || !sizes[i] || sizes[i] > allocation_sizes[i] ||
          allocation_sizes[i] > UINT32_MAX)
         goto cleanup;
   if (m->descriptor_set0_valid) {
      if (ps5_resource_info(descriptors, &table, &table_size, &table_allocation_size) ||
          !table || ((uintptr_t)table & 15u) ||
          (uintptr_t)table >> 32 != m->address32_hi || !table_size ||
          table_size > table_allocation_size || table_allocation_size > UINT32_MAX ||
          !m->descriptor_binding_count || m->descriptor_binding_count > PSBC_MAX_DESCRIPTOR_BINDINGS)
         goto cleanup;
      for (unsigned i = 0; i < m->descriptor_binding_count; ++i) {
         const PsbcDescriptorBinding *bank = &m->descriptor_bindings[i];
         const bool image = bank->type == PSBC_DESCRIPTOR_STORAGE_IMAGE;
         const bool sampled = bank->type == PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER;
         if (bank->set || bank->stride != (sampled ? 48u : image ? 32u : 16u) || !bank->array_size ||
             (sampled && (bank->binding >= PS5_AGC_COMPUTE_MAX_TEXTURES ||
                          bank->array_size > PS5_AGC_COMPUTE_MAX_TEXTURES - bank->binding)) ||
             bank->array_size > (image ? PS5_AGC_COMPUTE_MAX_IMAGES : 32u) ||
             (bank->type != PSBC_DESCRIPTOR_STORAGE_BUFFER &&
              bank->type != PSBC_DESCRIPTOR_UNIFORM_BUFFER && !image && !sampled) || (bank->offset & 15u) ||
             (uint64_t)bank->offset + (uint64_t)bank->array_size * bank->stride > table_size)
            goto cleanup;
         for (unsigned slot = 0; slot < bank->array_size; ++slot) {
            const uint32_t *srd = (const uint32_t *)((const uint8_t *)table + bank->offset + slot * bank->stride);
            uint32_t nonzero = 0;
            for (unsigned word = 0; word < bank->stride / 4; ++word)
               nonzero |= srd[word];
            if (!nonzero)
               continue;
            if (image || sampled) {
               if (sampled) {
                  const uint32_t anisotropy = (srd[8] >> 9) & 7u;
                  const uint32_t sampler0 = (srd[8] & 0x71ffu) |
                     (anisotropy << 9) | ((anisotropy >> 1) << 16) |
                     (anisotropy << 21);
                  /* Zero for fetch/size; bounded repeat/mirror/edge and mip filtering for txl.
                   * No border-table pointers or unqualified descriptor bits. */
                  if (anisotropy > 4 || srd[8] != sampler0 ||
                      (srd[9] & 0xff000000u) || (srd[9] & 0xfffu) > 0xf00u ||
                      ((srd[9] >> 12) & 0xfffu) > 0xf00u ||
                      (srd[9] & 0xfffu) > ((srd[9] >> 12) & 0xfffu) ||
                      (srd[10] & ~UINT32_C(0x0c500000)) ||
                      (srd[10] >> 26) == 3 || srd[11])
                     goto cleanup;
               }
               bool owned = false;
               for (unsigned j = 0; j < buffer_count; ++j) {
                  const bool texel = (sampled || image) &&
                     !ps5_resource_texel_buffer_descriptor_owned(buffers[j], srd);
                  /* MSAA uses the SRD mip fields for log2(samples), not view
                   * levels. Reconstruct level zero, then compare all words. */
                  int rc = texel ? 0 :
                     sampled ? ps5_resource_sampled_image_descriptor_owned(
                        buffers[j], srd) :
                     ps5_resource_storage_image_descriptor_owned(buffers[j], srd);
                  if (!rc && (texel || image || sampled))
                     owned = true;
               }
               if (!owned)
                  goto cleanup;
               continue;
            }
            if (srd[1] & 0xffff0000u || !srd[2] || srd[3] != UINT32_C(0x31016fac))
               goto cleanup;
            const uint64_t address = srd[0] | ((uint64_t)srd[1] << 32);
            bool owned = false;
            for (unsigned j = 0; j < buffer_count; ++j) {
               const uintptr_t start = (uintptr_t)addresses[j];
               if (address >= start && address - start <= sizes[j] &&
                   srd[2] <= sizes[j] - (address - start))
                  owned = true;
            }
            if (!owned)
               goto cleanup;
         }
      }
      user_data[m->descriptor_set0_user_data_dword] = (uint32_t)(uintptr_t)table;
   }
   if (m->compute_grid_size_valid)
      memcpy(user_data + m->compute_grid_size_user_data_dword, groups, 3 * sizeof(uint32_t));
   /* Resource-info calls above drain pending graphics and take this lock
    * themselves. Do not call them from inside the submission critical section.
    * ponytail: serialize and retire before releasing memory; asynchronous
    * compute needs a complete resource-lifetime contract first. */
   ps5_screen_submit_lock(screen);
   locked = true;
   if (load_apis(NULL, NULL, NULL, &agc, &video))
      goto cleanup;
   if (!runtime_agc_initialized) {
      if (agc.init(8))
         goto cleanup;
      runtime_agc_initialized = 1;
   }
   /* Bounded command area, separate cache line for completion, then header/code. */
   const int64_t direct_size = sceKernelGetDirectMemorySize();
   struct ps5_agc_compute_memory_layout layout;
   /* GFX10 scratch descriptors require swizzling and an odd 1 KiB stride
    * multiple, matching Mesa's ac_compute_scratch_wavesize contract. */
   const uint32_t scratch_stride = m->scratch_buffer_backed ?
      (m->scratch_bytes_per_wave | 1024u) : 0;
   if (direct_size <= 0 || ps5_agc_compute_plan_memory(code_size, m->compute_private_stride,
         scratch_stride,
         m->compute_workgroup_size, groups, (size_t)direct_size, &layout))
      goto cleanup;
   memory_size = layout.allocation_size;
   if (sceKernelAllocateDirectMemory(0, direct_size, memory_size,
                                    0x4000, DIRECT_MEMORY_TYPE, &physical))
      goto cleanup;
   if (sceKernelMapDirectMemory((void **)&memory, memory_size, MAP_PROTECTION, 0,
                               physical, 0x4000)) {
      memory = NULL;
      goto cleanup;
   }
   memset(memory, 0, memory_size);
   memcpy(memory + 0x4000, header, header_size);
   memcpy(memory + 0x5000, code, code_size);
   const size_t auxiliary_offset = layout.private_size ? layout.private_offset : layout.scratch_offset;
   const size_t auxiliary_size = layout.private_size ? layout.private_size : layout.scratch_size;
   if (auxiliary_size) {
      memset(memory + auxiliary_offset - 0x4000u, 0xa5, 0x4000u);
      memset(memory + auxiliary_offset + auxiliary_size, 0xa5, 0x4000u);
      const uintptr_t address = (uintptr_t)memory + auxiliary_offset;
      user_data[0] = (uint32_t)address;
      user_data[1] = (uint32_t)(address >> 32) |
                     (layout.scratch_size ? UINT32_C(0x80000000) : 0);
   }
   if (layout.scratch_size)
      tmpring_size = PS5_AGC_COMPUTE_SCRATCH_WAVES |
                    (scratch_stride / 1024u << 12);
   if (agc.create_shader(&program, memory + 0x4000, memory + 0x5000) ||
       program != memory + 0x4000)
      goto cleanup;
   agc_register_t *registers = *(agc_register_t **)(memory + 0x4020);
   if (registers != (agc_register_t *)(memory + 0x4060) || memory[0x405c] != 8)
      goto cleanup;
   uint32_t *words = (uint32_t *)memory;
   agc_command_buffer_t command = {
      words, words + 0x3000 / 4, words, words + 0x3000 / 4,
      (uintptr_t)command_out_of_space, NULL, 0, 0
   };
   volatile uint32_t *marker = (volatile uint32_t *)(memory + 0x3fc0);
   const uint32_t expected = UINT32_C(0x43535035);
   out_of_space = 0;
   if (!agc.set_sh(&command, registers, 8) ||
       !agc.set_sh_direct(&command, 0x218, &tmpring_size, 1) ||
       !agc.set_sh_direct(&command, 0x240, user_data, m->user_sgpr_count))
      goto cleanup;
   /* ProsperoAI's proven acquire/release flags: invalidate before CS reads,
    * flush shader writes before signaling CPU completion. No VideoOut needed.
    * Physical storage can exceed logical bytes (tiled images/padding). Keep
    * logical sizes for descriptor ownership above, allocated spans for caches. */
   if (table) {
      flush_gpu_data(table, table_allocation_size);
      if (!sceAgcDcbAcquireMem(&command, 0, 0, 0x4380, (uintptr_t)table, table_allocation_size, 0xa0))
         goto cleanup;
   }
   for (unsigned i = 0; i < buffer_count; ++i) {
      flush_gpu_data(addresses[i], allocation_sizes[i]);
      if (!sceAgcDcbAcquireMem(&command, 0, 0, 0x4380,
                             (uintptr_t)addresses[i], allocation_sizes[i], 0xa0))
         goto cleanup;
   }
   if (auxiliary_size && !sceAgcDcbAcquireMem(&command, 0, 0, 0x4380,
         (uintptr_t)memory + auxiliary_offset, auxiliary_size, 0xa0))
      goto cleanup;
   if (!sceAgcCbDispatch(&command, groups[0], groups[1], groups[2],
                         m->compute_wave_size == 32 ? 0x8000 : 0) ||
       !agc.release_mem(&command, 40, 0x30c, 0, 0, (void *)marker, 1,
                        expected, 0, 0, 0, 0) || out_of_space)
      goto cleanup;
   agc_submit_description_t submit = {words, (uint32_t)(command.up - words), 0, {0}};
   flush_gpu_data(memory, memory_size);
   int submit_rc = agc.submit(&submit);
#ifdef PS5_FRAME_SUSPEND
   int suspend_rc = submit_rc == 0 ? runtime_defer_suspend(agc.suspend_point) : -1;
#else
   int suspend_rc = submit_rc == 0 ? agc.suspend_point() : -1;
#endif
   unsigned polls = 0;
   for (; submit_rc == 0 && polls < 2000; ++polls) {
      flush_gpu_data((const void *)marker, sizeof(*marker));
      if (*marker == expected)
         break;
      sceKernelUsleep(1000);
   }
   runtime_require_retirement(submit_rc == 0 && suspend_rc == 0 && polls < 2000);
   if (auxiliary_size) {
      flush_gpu_data(memory + auxiliary_offset - 0x4000u, auxiliary_size + 0x8000u);
      const uint32_t *before = (const uint32_t *)(memory + auxiliary_offset - 0x4000u);
      const uint32_t *after = (const uint32_t *)(memory + auxiliary_offset + auxiliary_size);
      for (unsigned i=0; i<0x4000u/4; ++i)
         runtime_require_retirement(before[i] == UINT32_C(0xa5a5a5a5) &&
                                    after[i] == UINT32_C(0xa5a5a5a5));
   }
   for (unsigned i = 0; i < buffer_count; ++i)
      flush_gpu_data(addresses[i], allocation_sizes[i]);
   result = 0;
cleanup:
   if (memory && munmap(memory, memory_size))
      runtime_require_retirement(0);
   if (physical >= 0 && sceKernelReleaseDirectMemory(physical, memory_size))
      runtime_require_retirement(0);
   free(package);
   if (locked)
      ps5_screen_submit_unlock(screen);
   return result;
}
#endif

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

static unsigned
ps5_agc_linear_color_bytes(uint32_t info)
{
   switch (info) {
   case UINT32_C(0x00008028): return 4; /* RGBA8_UNORM */
   case UINT32_C(0x00008628): return 4; /* RGBA8_SRGB: preserve native conversion bits. */
   case UINT32_C(0x00060718): return 4; /* R11G11B10_FLOAT: packed 32-bit texels. */
   case UINT32_C(0x00008004): return 1; /* R8_UNORM */
   case UINT32_C(0x0000800c): return 2; /* RG8_UNORM */
   case UINT32_C(0x00060730): return 8; /* RGBA16F */
   default: return 0;
   }
}

static int
ps5_agc_color_target_extent(uint32_t info, uint32_t width, uint32_t height,
                            uint32_t pitch, size_t size, uint32_t *attrib2)
{
   size_t required;
   unsigned bytes_per_pixel;
   unsigned encoded_width = width;

   if (!width || width > PS5_AGC_MAX_COLOR_WIDTH ||
       !height || height > PS5_AGC_MAX_COLOR_HEIGHT)
      return -1;
   if (pitch) {
      bytes_per_pixel = ps5_agc_linear_color_bytes(info);
      if (!bytes_per_pixel || ps5_agc_mrt_samples != 1 || (pitch & 255u) ||
          pitch / bytes_per_pixel < width ||
          pitch / bytes_per_pixel > UINT32_C(0x4000))
         return -1;
      encoded_width = pitch / bytes_per_pixel;
      /* Only the logical final row is required; Gallium bounds rendering to
       * width, and supplies the allocation remaining from the rebased mip. */
      size_t row_bytes = (size_t)width * bytes_per_pixel;
      if ((size_t)(height - 1u) > (SIZE_MAX - row_bytes) / pitch)
         return -1;
      required = (size_t)(height - 1u) * pitch + row_bytes;
   } else {
      unsigned tile_width = 128;
      unsigned tile_height = 128;

      switch ((info >> 2) & 0x1fu) {
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
      required = (size_t)((width + tile_width - 1u) / tile_width) *
                 ((height + tile_height - 1u) / tile_height) *
                 UINT32_C(0x10000);
   }
   if (size < required)
      return -1;
   /* GFX10.3 custom linear pitch is in texels in MIP0_WIDTH; MAX_MIP=0. */
   *attrib2 = (height - 1u) | ((encoded_width - 1u) << 14);
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
      if (ps5_agc_color_target_extent(
             ps5_agc_mrt_color_info[target], widths[target], heights[target],
             ps5_agc_mrt_pitches[target], ps5_agc_mrt_sizes[target],
             &ps5_agc_mrt_attrib2[target]) != 0)
         return -1;
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

      if ((views[target] & ~view_mask) || first > last ||
          (ps5_agc_mrt_pitches[target] && views[target]))
         return -1;
   }
   memcpy(ps5_agc_mrt_views, views, count * sizeof(views[0]));
   return 0;
}

static bool
ps5_agc_color_info_valid(uint32_t value)
{
   unsigned format = (value >> 2) & 0x1fu;
   unsigned number_type = (value >> 8) & 7u;
   unsigned swap = (value >> 11) & 3u;
   uint32_t expected = (format << 2) | (number_type << 8) | (swap << 11);

   if (number_type <= 1u || number_type == 6u)
      expected |= UINT32_C(1) << 15;
   else
      expected |= (UINT32_C(1) << 17) | (UINT32_C(1) << 18);
   if (number_type == 4u || number_type == 5u)
      expected |= UINT32_C(1) << 16;
   return value == expected && format &&
          format != 7u && format != 13u && format <= 14u;
}

int
ps5_agc_gate2_set_color_target_info(const uint32_t *values, unsigned count)
{
   if (!values || !count || count > PS5_AGC_MRT_TARGETS ||
       count != ps5_agc_mrt_count)
      return -1;
   for (unsigned target = 0; target < count; ++target) {
      if (!ps5_agc_color_info_valid(values[target]) ||
          (ps5_agc_mrt_pitches[target] &&
           values[target] != ps5_agc_mrt_color_info[target]))
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
   memset(ps5_agc_mrt_pitches, 0, sizeof(ps5_agc_mrt_pitches));
   memcpy(ps5_agc_mrt_targets, targets, count * sizeof(targets[0]));
   memcpy(ps5_agc_mrt_sizes, sizes, count * sizeof(sizes[0]));
   ps5_agc_mrt_count = count;
   return 0;
}

/* Optional mixed-layout ABI; scanout must already be registered separately.
 * Nonzero pitches are bytes for a rebased, single-sample, single-level/layer
 * linear 2D view. sizes are the allocations remaining at those bases. The
 * caller must bound rasterization to the logical widths/heights, not pitch. */
int
ps5_agc_gate2_set_color_target_layouts(
   void *const *targets, const size_t *sizes, const uint32_t *infos,
   const uint32_t *widths, const uint32_t *heights, const uint32_t *pitches,
   unsigned count)
{
   void *new_targets[PS5_AGC_MRT_TARGETS] = {0};
   size_t new_sizes[PS5_AGC_MRT_TARGETS] = {0};
   uint32_t new_infos[PS5_AGC_MRT_TARGETS] = {0};
   uint32_t new_attrib2[PS5_AGC_MRT_TARGETS] = {0};
   uint32_t new_pitches[PS5_AGC_MRT_TARGETS] = {0};
   const uint64_t address_limit = UINT64_C(1) << 48;
   uintptr_t scanout = (uintptr_t)ps5_agc_scanout_target;

   if (!targets || !sizes || !infos || !widths || !heights || !pitches ||
       !count || count > PS5_AGC_MRT_TARGETS ||
       (ps5_agc_dual_source_blend && count != 1) ||
       !scanout || scanout >= address_limit ||
       (scanout & (PS5_AGC_FRAMEBUFFER_ALIGNMENT - 1u)) ||
       ps5_agc_scanout_size < PS5_AGC_FRAMEBUFFER_POOL_BYTES ||
       ps5_agc_scanout_size > address_limit - scanout)
      return -1;
   for (unsigned target = 0; target < count; ++target) {
      uintptr_t address = (uintptr_t)targets[target];
      size_t alignment = pitches[target] ? 256u : PS5_AGC_COLOR_TARGET_ALIGNMENT;

      if (!address || address >= address_limit || (address & (alignment - 1u)) ||
          !sizes[target] || sizes[target] > address_limit - address ||
          !ps5_agc_color_info_valid(infos[target]) ||
          ps5_agc_color_target_extent(infos[target], widths[target], heights[target],
                                      pitches[target], sizes[target],
                                      &new_attrib2[target]) != 0)
         return -1;
      new_targets[target] = targets[target];
      new_sizes[target] = sizes[target];
      new_infos[target] = infos[target];
      new_pitches[target] = pitches[target];
   }
   /* Never register an offscreen target as scanout. Nothing above changes
    * runtime or target state, including when a later MRT is invalid. */
   if (ps5_agc_gate2_set_framebuffer(ps5_agc_scanout_target,
                                    ps5_agc_scanout_size) != 0)
      return -1;
   memcpy(ps5_agc_mrt_targets, new_targets, sizeof(new_targets));
   memcpy(ps5_agc_mrt_sizes, new_sizes, sizeof(new_sizes));
   memcpy(ps5_agc_mrt_color_info, new_infos, sizeof(new_infos));
   memcpy(ps5_agc_mrt_attrib2, new_attrib2, sizeof(new_attrib2));
   memcpy(ps5_agc_mrt_pitches, new_pitches, sizeof(new_pitches));
   memset(ps5_agc_mrt_views, 0, sizeof(ps5_agc_mrt_views));
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
