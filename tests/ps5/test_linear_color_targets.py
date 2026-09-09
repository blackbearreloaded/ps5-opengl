#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""ASan/UBSan checks of actual layout setters/indirect register overrides.

Run with python3 -B tests/ps5/test_linear_color_targets.py on a Linux host.
Only the external framebuffer and indirect-register callbacks are stubbed;
fake GPU addresses are never dereferenced. No SDK, GPU, or Mesa build is used.
"""
import os
import re
import shlex
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
backend = (ROOT / "src/platform/ps5_agc_runtime_backend.c").read_text()


def function(name):
    match = re.search(r"^(?:static )?(?:int|bool|unsigned|uint32_t \*)\n" +
                      re.escape(name) + r"\(", backend, re.M)
    assert match, name
    return backend[match.start():backend.index("\n}", match.end()) + 2]


# Use the backend's real declarations/constants rather than duplicate globals.
code = backend[:backend.index("static bool\nps5_agc_streamout_enabled")]
code += r'''
#include <assert.h>
static unsigned runtime_depth_samples, framebuffer_calls, register_calls;
static void *submitted_target;
static size_t submitted_size;
static int framebuffer_result;
static int ps5_agc_gate2_set_framebuffer(void *target, size_t size)
{
   ++framebuffer_calls;
   if (framebuffer_result)
      return framebuffer_result;
   assert(target && size >= PS5_AGC_FRAMEBUFFER_BYTES);
   submitted_target = target;
   submitted_size = size;
   return 0;
}
'''
code += "\n".join(function(name) for name in (
    "ps5_agc_replace_or_append_register", "ps5_agc_find_register",
    "ps5_agc_set_cx_mrt", "ps5_agc_linear_color_bytes",
    "ps5_agc_color_target_extent", "ps5_agc_color_info_valid",
    "ps5_agc_gate2_set_multisample_state", "ps5_agc_gate2_set_scanout",
    "ps5_agc_gate2_set_framebuffers", "ps5_agc_gate2_set_color_target_layouts",
    "ps5_agc_gate2_set_color_target_extents", "ps5_agc_gate2_set_color_target_info",
    "ps5_agc_gate2_set_color_target_views", "ps5_agc_gate2_set_dual_source_blend",
))
code += r'''
static const uint32_t formats[] = {0x8028, 0x8004, 0x800c, 0x60730};
static const unsigned bpps[] = {4, 1, 2, 8};
static unsigned rejections;
struct layout {
   void *targets[8];
   size_t sizes[8];
   uint32_t infos[8], widths[8], heights[8], pitches[8];
   unsigned count;
};
struct snapshot {
   struct layout layout;
   uint32_t attrib2[8], views[8];
   void *scanout, *submitted;
   size_t scanout_size, submitted_size;
   unsigned calls;
};
static void snapshot(struct snapshot *s)
{
   memset(s, 0, sizeof(*s));
   memcpy(s->layout.targets, ps5_agc_mrt_targets, sizeof(s->layout.targets));
   memcpy(s->layout.sizes, ps5_agc_mrt_sizes, sizeof(s->layout.sizes));
   memcpy(s->layout.infos, ps5_agc_mrt_color_info, sizeof(s->layout.infos));
   memcpy(s->layout.pitches, ps5_agc_mrt_pitches, sizeof(s->layout.pitches));
   memcpy(s->attrib2, ps5_agc_mrt_attrib2, sizeof(s->attrib2));
   memcpy(s->views, ps5_agc_mrt_views, sizeof(s->views));
   s->layout.count = ps5_agc_mrt_count;
   s->scanout = ps5_agc_scanout_target;
   s->scanout_size = ps5_agc_scanout_size;
   s->submitted = submitted_target;
   s->submitted_size = submitted_size;
   s->calls = framebuffer_calls;
}
#define REJECT(expr) do { \
   struct snapshot before, after; snapshot(&before); \
   assert((expr) == -1); snapshot(&after); \
   assert(memcmp(&before, &after, sizeof(before)) == 0); ++rejections; \
} while (0)
#define BAD(field, value) do { \
   struct layout bad = good; bad.field = (value); REJECT(configure(&bad)); \
} while (0)
static int configure(const struct layout *c)
{
   return ps5_agc_gate2_set_color_target_layouts(c->targets, c->sizes, c->infos,
      c->widths, c->heights, c->pitches, c->count);
}
static struct layout mixed(bool linear_first)
{
   struct layout c = {0};
   c.count = 8;
   for (unsigned i = 0; i < c.count; ++i) {
      bool linear = (i % 2 == 0) == linear_first;
      c.targets[i] = (void *)(uintptr_t)(UINT64_C(0x123400000000) +
                                        i * 0x100000u + (linear ? 256u : 0));
      c.infos[i] = formats[i / 2];
      c.widths[i] = 17;
      c.heights[i] = 3;
      c.pitches[i] = linear ? 256 : 0;
      c.sizes[i] = linear ? 2u * 256u + 17u * bpps[i / 2] : 65536;
   }
   return c;
}
static struct ps5_agc_register captured[PS5_AGC_CX_RECORD_CAPACITY];
static uint32_t captured_count, marker;
static uint32_t template_attrib3 = 0x4d06c000;
static uint32_t template_alpha = 1u << 17;
static uint32_t *capture(void *command, const void *table, uint32_t count)
{
   assert(command == &marker && table && count <= PS5_AGC_CX_RECORD_CAPACITY);
   memcpy(captured, table, count * sizeof(captured[0]));
   captured_count = count;
   ++register_calls;
   return &marker;
}
static uint32_t reg(uint16_t offset)
{
   unsigned matches = 0;
   uint32_t value = 0;
   for (unsigned i = 0; i < captured_count; ++i) {
      if (captured[i].offset == offset) {
         value = captured[i].value;
         ++matches;
      }
   }
   assert(matches == 1);
   return value;
}
static void check_registers(const struct layout *c)
{
   /* The same 16 target records consumed from native append_target_state.
    * Pinned Mesa ac_descriptors.c / gfx10.json oracle: COLOR_SW_MODE=14..18,
    * MIP0_WIDTH=14..27, RESOURCE_TYPE=24..25. No production bit helper reused. */
   const uint16_t offsets[] = {0x318, 0x31b, 0x31c, 0x31d, 0x31e, 0x31f,
      0x321, 0x323, 0x324, 0x325, 0x390, 0x398, 0x3a0, 0x3a8, 0x3b0, 0x3b8};
   struct ps5_agc_register records[PS5_AGC_CX_RECORD_CAPACITY] = {0};
   for (unsigned i = 0; i < 16; ++i)
      records[i] = (struct ps5_agc_register){offsets[i], 0, 0};
   records[3].value = template_alpha;
   records[10].value = 0xabcde000;
   /* Start with the native tiled baseline + RESOURCE_LEVEL, then deliberately
    * reuse previous layout bits to catch a linear draw contaminating reset. */
   records[15].value = template_attrib3;
   records[16] = (struct ps5_agc_register){0x2d5, 0, 0x12345678};
   records[17] = (struct ps5_agc_register){0x777, 0, 0x87654321};
   assert(ps5_agc_set_cx_mrt(&marker, records, 18) == &marker);
   assert(reg(0x777) == 0x87654321 && reg(0x2d5) == 0x12345678);
   for (unsigned i = 0; i < c->count; ++i) {
      unsigned bpp = bpps[i / 2];
      for (unsigned f = 0; f < 4; ++f)
         if (c->infos[i] == formats[f]) bpp = bpps[f];
      uint64_t address = (uintptr_t)c->targets[i];
      unsigned encoded_width = c->pitches[i] ? c->pitches[i] / bpp : c->widths[i];
      uint32_t attrib3 = reg(0x3b8 + i);
      assert(reg(0x318 + 15 * i) == (uint32_t)(address >> 8));
      assert(reg(0x390 + i) == (0xabcde000u | (uint32_t)(address >> 40)));
      assert(reg(0x31b + 15 * i) == ps5_agc_mrt_views[i]);
      assert(reg(0x31c + 15 * i) == c->infos[i]);
      uint32_t alpha = c->pitches[i] ? (bpp <= 2 ? 1u << 17 : 0) : template_alpha;
      assert(reg(0x31d + 15 * i) == (alpha |
             (ps5_agc_mrt_samples == 4 ? 0x12000u : 0)));
      assert(reg(0x3b0 + i) == (c->heights[i] - 1u) + ((encoded_width - 1u) << 14));
      assert(((attrib3 >> 14) & 31) == (c->pitches[i] ? 0 : 27));
      assert((attrib3 & 0x1fff) == 0 && ((attrib3 >> 24) & 3) == 1);
      assert(attrib3 == (c->pitches[i] ? 0x4d000000u : 0x4d06c000u));
   }
   template_attrib3 = reg(0x3b8);
   template_alpha ^= 1u << 17; /* Test both setting and clearing absent alpha. */
}
int main(void)
{
   _Static_assert(sizeof(uintptr_t) == 8, "48-bit address tests require a 64-bit host");
   struct layout good = mixed(true);
   const uint64_t limit = UINT64_C(1) << 48;
   void *scanout = (void *)(uintptr_t)0x20000000;
   ps5_agc_set_cx = capture;
   REJECT(configure(&good)); /* New API never bootstraps scanout from an RT. */
   assert(ps5_agc_gate2_set_scanout(scanout, PS5_AGC_FRAMEBUFFER_POOL_BYTES) == 0);
   assert(configure(&good) == 0);
   assert(submitted_target == scanout && submitted_target != good.targets[0]);
   assert(submitted_size == PS5_AGC_FRAMEBUFFER_POOL_BYTES);
   check_registers(&good);

   BAD(count, 0); BAD(count, 9);
   REJECT(ps5_agc_gate2_set_color_target_layouts(NULL, good.sizes, good.infos,
      good.widths, good.heights, good.pitches, 8));
   REJECT(ps5_agc_gate2_set_color_target_layouts(good.targets, NULL, good.infos,
      good.widths, good.heights, good.pitches, 8));
   REJECT(ps5_agc_gate2_set_color_target_layouts(good.targets, good.sizes, NULL,
      good.widths, good.heights, good.pitches, 8));
   REJECT(ps5_agc_gate2_set_color_target_layouts(good.targets, good.sizes, good.infos,
      NULL, good.heights, good.pitches, 8));
   REJECT(ps5_agc_gate2_set_color_target_layouts(good.targets, good.sizes, good.infos,
      good.widths, NULL, good.pitches, 8));
   REJECT(ps5_agc_gate2_set_color_target_layouts(good.targets, good.sizes, good.infos,
      good.widths, good.heights, NULL, 8));
   for (unsigned i = 0; i < 8; ++i) {
      BAD(targets[i], NULL);
      BAD(targets[i], (void *)((uintptr_t)good.targets[i] + 1));
      BAD(targets[i], (void *)(uintptr_t)limit);
      BAD(targets[i], (void *)(uintptr_t)(UINTPTR_MAX - 255));
      BAD(sizes[i], SIZE_MAX);
      BAD(sizes[i], 0); BAD(sizes[i], good.sizes[i] - 1);
      BAD(widths[i], 0); BAD(widths[i], 8193);
      BAD(heights[i], 0); BAD(heights[i], 8193);
      BAD(infos[i], good.infos[i] | (1u << 28)); /* No DCC/extra CB bits. */
      BAD(infos[i], 0x801c); /* Reserved CB format. */
      if (good.pitches[i]) {
         BAD(pitches[i], 255); BAD(pitches[i], 257);
         BAD(pitches[i], UINT32_MAX & ~255u);
         BAD(widths[i], 256 / bpps[i / 2] + 1);
         BAD(targets[i], (void *)(uintptr_t)(limit - 256)); /* Span crosses limit. */
         BAD(infos[i], good.infos[i] | (1u << 11)); /* Canonical, wrong swap. */
      } else {
         BAD(targets[i], (void *)((uintptr_t)good.targets[i] + 256));
      }
   }
   const uint32_t unsupported[] = {0x8628, 0x70528, 0x8008, 0x802c, 0x60738};
   for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i)
      BAD(infos[0], unsupported[i]);

   /* Revalidate the old scanout registration, including its full allocation. */
   assert(ps5_agc_gate2_set_scanout((void *)(uintptr_t)limit,
                                 PS5_AGC_FRAMEBUFFER_POOL_BYTES) == 0);
   REJECT(configure(&good));
   assert(ps5_agc_gate2_set_scanout((void *)(uintptr_t)(limit - 0x200000),
                                 PS5_AGC_FRAMEBUFFER_POOL_BYTES) == 0);
   REJECT(configure(&good));
   assert(ps5_agc_gate2_set_scanout(scanout, PS5_AGC_FRAMEBUFFER_POOL_BYTES) == 0);
   struct snapshot before, after;
   snapshot(&before);
   framebuffer_result = -1;
   assert(configure(&good) == -1);
   framebuffer_result = 0;
   snapshot(&after);
   before.calls++;
   assert(memcmp(&before, &after, sizeof(before)) == 0);

   uint32_t views[8] = {0};
   views[1] = 1u | (1u << 13); /* A tiled sibling can still select a slice. */
   assert(ps5_agc_gate2_set_color_target_views(views, 8) == 0);
   check_registers(&good);
   views[6] = 1u | (1u << 13);
   REJECT(ps5_agc_gate2_set_color_target_views(views, 8));
   views[6] = 1u << 13;
   REJECT(ps5_agc_gate2_set_color_target_views(views, 8));
   views[6] = 1u << 24; /* Mip bits remain outside the legacy view contract. */
   REJECT(ps5_agc_gate2_set_color_target_views(views, 8));
   assert(configure(&good) == 0);
   for (unsigned i = 0; i < 8; ++i) assert(!ps5_agc_mrt_views[i]);
   assert(ps5_agc_gate2_set_color_target_info(good.infos, 8) == 0);
   assert(ps5_agc_gate2_set_color_target_extents(good.widths, good.heights, 8) == 0);
   uint32_t changed_infos[8];
   memcpy(changed_infos, good.infos, sizeof(changed_infos));
   changed_infos[0] = formats[1];
   REJECT(ps5_agc_gate2_set_color_target_info(changed_infos, 8));

   good = mixed(false); /* Tiled MRT0 and linear siblings in the reverse order. */
   assert(configure(&good) == 0);
   check_registers(&good);
   for (unsigned count = 1; count <= 8; ++count) {
      struct layout c = mixed(true);
      c.count = count;
      assert(configure(&c) == 0);
      check_registers(&c);
      for (unsigned i = count; i < 8; ++i) {
         assert(!ps5_agc_mrt_targets[i] && !ps5_agc_mrt_sizes[i]);
         assert(!ps5_agc_mrt_pitches[i] && !ps5_agc_mrt_views[i]);
      }
   }
   for (unsigned f = 0; f < 4; ++f) {
      struct layout c = mixed(true);
      c.count = 1; c.infos[0] = formats[f];
      c.widths[0] = c.heights[0] = 8192;
      c.pitches[0] = 8192 * bpps[f];
      c.sizes[0] = (size_t)c.pitches[0] * 8192;
      assert(configure(&c) == 0); check_registers(&c);
      c.widths[0] = c.heights[0] = 1;
      c.pitches[0] = 16384 * bpps[f]; /* Largest MIP0_WIDTH encoding. */
      c.sizes[0] = bpps[f]; /* Exact final logical row, not pitch*height. */
      assert(configure(&c) == 0); check_registers(&c);
      c.pitches[0] += 256;
      REJECT(configure(&c));
   }
   struct layout edge = mixed(true);
   edge.count = 1;
   edge.targets[0] = (void *)(uintptr_t)(limit - 256);
   edge.sizes[0] = 256; edge.widths[0] = 64; edge.heights[0] = 1;
   template_attrib3 |= 0x03f83fff; /* Linear views must clear depth/FMASK/type. */
   assert(configure(&edge) == 0); check_registers(&edge);
   edge.sizes[0]++;
   REJECT(configure(&edge));

   good = mixed(true);
   assert(configure(&good) == 0);
   assert(ps5_agc_gate2_set_dual_source_blend(1) == 0);
   REJECT(configure(&good));
   edge = good; edge.count = 1;
   assert(configure(&edge) == 0);
   assert(ps5_agc_gate2_set_dual_source_blend(0) == 0);
   assert(configure(&good) == 0);
   assert(ps5_agc_gate2_set_multisample_state(4, 0xffff, 1, 0, 0, 0) == 0);
   REJECT(configure(&good));
   struct ps5_agc_register untouched[PS5_AGC_CX_RECORD_CAPACITY] = {{0x3b8, 0, 0}};
   unsigned calls = register_calls;
   assert(ps5_agc_set_cx_mrt(&marker, untouched, 1) == NULL);
   assert(register_calls == calls && untouched[0].value == 0);

   /* Old API still rejects linear-sized/base-aligned targets, then resets all
    * pitch slots on success. Its normal MSAA-before-framebuffers order works. */
   REJECT(ps5_agc_gate2_set_framebuffers(good.targets, good.sizes, 8));
   struct layout tiled = mixed(false);
   for (unsigned i = 0; i < 8; ++i) {
      tiled.targets[i] = (void *)((uintptr_t)tiled.targets[i] & ~(uintptr_t)65535);
      tiled.pitches[i] = 0; tiled.sizes[i] = 65536;
   }
   assert(ps5_agc_gate2_set_framebuffers(tiled.targets, tiled.sizes, 8) == 0);
   for (unsigned i = 0; i < 8; ++i) assert(!ps5_agc_mrt_pitches[i]);
   assert(ps5_agc_gate2_set_color_target_info(tiled.infos, 8) == 0);
   assert(ps5_agc_gate2_set_color_target_extents(tiled.widths, tiled.heights, 8) == 0);
   check_registers(&tiled);
   assert(configure(&tiled) == 0); /* All-tiled optional API also retains MSAA4. */
   check_registers(&tiled);
   assert(ps5_agc_gate2_set_multisample_state(1, 0xffff, 0, 0, 0, 0) == 0);
   assert(configure(&good) == 0);
   assert(ps5_agc_gate2_set_framebuffers(tiled.targets, tiled.sizes, 1) == 0);
   for (unsigned i = 0; i < 8; ++i) assert(!ps5_agc_mrt_pitches[i]);
   tiled.count = 1;
   assert(ps5_agc_gate2_set_color_target_info(tiled.infos, 1) == 0);
   assert(ps5_agc_gate2_set_color_target_extents(tiled.widths, tiled.heights, 1) == 0);
   check_registers(&tiled);

   /* The shared extent helper retains the original tile geometry at both
    * sample counts, including a tile boundary in each dimension. */
   const unsigned tile_widths[] = {128, 256, 256, 128};
   const unsigned tile_heights[] = {128, 256, 128, 64};
   for (unsigned samples = 1; samples <= 4; samples += 3) {
      assert(ps5_agc_gate2_set_multisample_state(samples, 0xffff, 1, 0, 0, 0) == 0);
      for (unsigned f = 0; f < 4; ++f) {
         unsigned divisor = samples == 4 ? 2 : 1;
         tiled.infos[0] = formats[f];
         tiled.widths[0] = tile_widths[f] / divisor + 1;
         tiled.heights[0] = tile_heights[f] / divisor + 1;
         tiled.sizes[0] = 4u * 65536;
         assert(configure(&tiled) == 0); check_registers(&tiled);
         tiled.sizes[0]--;
         REJECT(configure(&tiled));
         assert(ps5_agc_gate2_set_framebuffers(tiled.targets, tiled.sizes, 1) == 0);
         assert(ps5_agc_gate2_set_color_target_extents(tiled.widths, tiled.heights, 1) == -1);
      }
   }
   printf("PASS linear color targets: %u atomic guard rejections, %u register checks\n",
          rejections, register_calls);
   return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="ps5-linear-color-") as directory:
    binary = Path(directory) / "linear-color-targets"
    command = shlex.split(os.environ.get("CC", "cc")) + [
        "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-function", "-Wno-unused-variable",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-fno-sanitize-recover=all", "-fno-pie", "-no-pie",
        "-I", str(ROOT / "src/gallium/ps5"), "-x", "c", "-o", str(binary), "-",
    ]
    subprocess.run(command, input=code, text=True, check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)
