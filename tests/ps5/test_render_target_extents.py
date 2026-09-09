#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Host checks of the real extent/scissor code; no GPU execution or emulation."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
backend = (ROOT / "src/platform/ps5_agc_runtime_backend.c").read_text()
screen = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()


def function(name):
    match = re.search(r"^(?:static )?(?:int|unsigned)\n" +
                      re.escape(name) + r"\(", backend, re.M)
    assert match, name
    return backend[match.start():backend.index("\n}", match.end()) + 2]


defines = "\n".join(re.findall(
    r"^#define PS5_AGC_(?:MAX_\w+|MRT_TARGETS|FRAMEBUFFER_\w+|COLOR_TARGET_ALIGNMENT) .+$",
    backend, re.M))
scissor = screen[screen.index("   scissor = context->rasterizer"):]
scissor = scissor[:scissor.index("\n}")]
allocation = screen[screen.index("   if (PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE && ps5->render_pool &&"):]
allocation = allocation[:allocation.index("   direct_limit = sceKernelGetDirectMemorySize();")]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "src/gallium/ps5/ps5_screen.h"
#define MIN2(a,b) ((a) < (b) ? (a) : (b))
''' + defines + r'''
static bool ps5_agc_dual_source_blend;
static unsigned ps5_agc_mrt_count = 1, ps5_agc_mrt_samples = 1;
static void *ps5_agc_scanout_target, *ps5_agc_mrt_targets[8], *submitted_target;
static size_t ps5_agc_scanout_size, ps5_agc_mrt_sizes[8];
static uint32_t ps5_agc_mrt_color_info[8], ps5_agc_mrt_attrib2[8];
static uint32_t ps5_agc_mrt_pitches[8];
static uint32_t ps5_agc_depth_width, ps5_agc_depth_height;
static int ps5_agc_gate2_set_framebuffer(void *target, size_t size) {
    assert(size >= PS5_AGC_FRAMEBUFFER_BYTES);
    submitted_target = target;
    return 0;
}
''' + "\n".join(function(name) for name in (
    "ps5_agc_linear_color_bytes", "ps5_agc_color_target_extent",
    "ps5_agc_gate2_set_scanout", "ps5_agc_gate2_set_framebuffers",
    "ps5_agc_gate2_set_color_target_extents",
    "ps5_agc_gate2_set_depth_target_extents")) + r'''
struct pipe_scissor_state { unsigned minx, miny, maxx, maxy; };
struct rasterizer { bool scissor; };
struct context {
    struct { unsigned width, height; } framebuffer;
    struct rasterizer *rasterizer;
    bool scissor_valid;
    struct pipe_scissor_state scissor;
};
struct native { uint32_t scissor[2]; };
static bool encode_scissor(struct context *context, struct native *native) {
    const struct pipe_scissor_state *scissor;
    unsigned minx, miny, maxx, maxy;
''' + scissor + r'''
}
#define PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE 1
#define PIPE_BIND_DISPLAY_TARGET 1
#define PIPE_FORMAT_Z32_FLOAT_S8X24_UINT 1
#define PIPE_BUFFER 0
struct ps5_screen { void *render_pool; };
struct ps5_resource { int unused; };
struct pipe_resource { unsigned bind, format, nr_samples, target; };
static bool arena_available;
static unsigned direct_allocations;
static bool ps5_render_arena_allocate(struct ps5_screen *screen,
    struct ps5_resource *resource, size_t size, size_t alignment) {
    (void)screen; (void)resource; (void)size; (void)alignment;
    return arena_available;
}
static struct ps5_resource *allocate(size_t allocation_size) {
    struct ps5_screen storage = {(void *)(uintptr_t)1}, *ps5 = &storage;
    struct pipe_resource description = {.nr_samples = 1, .target = PIPE_BUFFER};
    struct pipe_resource *templ = &description;
    struct ps5_resource *resource = calloc(1, sizeof(*resource));
    size_t allocation_alignment = 0x200000;
    bool render_staging = false;
    (void)render_staging;
''' + allocation + r'''
    direct_allocations++;
primary_ready:
    return resource;
}
int main(void) {
    arena_available = true;
    struct ps5_resource *allocation = allocate(0x200000);
    assert(allocation && direct_allocations == 0);
    free(allocation);
    arena_available = false;
    allocation = allocate(36u * 1024u * 1024u);
    assert(allocation && direct_allocations == 1);
    free(allocation);
    allocation = allocate(64u * 1024u * 1024u);
    assert(allocation && direct_allocations == 2);
    free(allocation);
    void *display = (void *)(uintptr_t)0x200000;
    void *target = (void *)(uintptr_t)0x4000000;
    size_t bytes = 256u * 1024u * 1024u;
    uint32_t width = 8192, height = 8192;
    assert(ps5_agc_gate2_set_scanout(display, 0x1400000) == 0);
    assert(ps5_agc_gate2_set_framebuffers(&target, &bytes, 1) == 0);
    assert(submitted_target == display && ps5_agc_scanout_target == display);
    assert(ps5_agc_mrt_targets[0] == target);
    ps5_agc_mrt_color_info[0] = 10u << 2; /* RGBA8 */
    assert(ps5_agc_gate2_set_color_target_extents(&width, &height, 1) == 0);
    assert(ps5_agc_mrt_attrib2[0] == (8191u | (8191u << 14)));
    ps5_agc_mrt_sizes[0] = bytes - 1;
    assert(ps5_agc_gate2_set_color_target_extents(&width, &height, 1) == -1);
    ps5_agc_mrt_sizes[0] = bytes * 4;
    ps5_agc_mrt_samples = 4;
    assert(ps5_agc_gate2_set_color_target_extents(&width, &height, 1) == 0);
    ps5_agc_mrt_sizes[0]--;
    assert(ps5_agc_gate2_set_color_target_extents(&width, &height, 1) == -1);
    width = 8193;
    assert(ps5_agc_gate2_set_color_target_extents(&width, &height, 1) == -1);
    width = 0;
    assert(ps5_agc_gate2_set_color_target_extents(&width, &height, 1) == -1);
    /* 64 KiB color tiles depend on pixel size as well as sample count.
     * Exercise exact and one-byte-short storage, including NPOT padding. */
    const unsigned layouts[][3] = {
        {1, 256, 256}, {3, 256, 128}, {10, 128, 128},
        {12, 128, 64}, {14, 64, 64},
    };
    const uint32_t dimensions[][2] = {{129, 65}, {2048, 2048}, {8192, 8192}};
    for (unsigned format = 0; format < 5; ++format) {
        ps5_agc_mrt_color_info[0] = layouts[format][0] << 2;
        for (unsigned samples = 1; samples <= 4; samples += 3) {
            ps5_agc_mrt_samples = samples;
            unsigned tw = layouts[format][1] / (samples == 4 ? 2 : 1);
            unsigned th = layouts[format][2] / (samples == 4 ? 2 : 1);
            for (unsigned size = 0; size < 3; ++size) {
                uint32_t w = dimensions[size][0], h = dimensions[size][1];
                ps5_agc_mrt_sizes[0] = (size_t)((w + tw - 1) / tw) *
                    ((h + th - 1) / th) * 65536u;
                assert(ps5_agc_gate2_set_color_target_extents(&w, &h, 1) == 0);
                ps5_agc_mrt_sizes[0]--;
                assert(ps5_agc_gate2_set_color_target_extents(&w, &h, 1) == -1);
            }
        }
    }
    assert(ps5_agc_gate2_set_depth_target_extents(8192, 8192) == 0);
    assert(ps5_agc_gate2_set_depth_target_extents(8192, 8193) == -1);
    assert(ps5_agc_gate2_set_depth_target_extents(0, 64) == -1);
    assert(ps5_agc_depth_width == 8192 && ps5_agc_depth_height == 8192);
    struct context context = { .framebuffer = {64, 8192} };
    struct native native;
    assert(encode_scissor(&context, &native));
    assert(native.scissor[0] == 0x80000000u);
    assert(native.scissor[1] == (64u | (8192u << 16)));
    context.framebuffer.width = 8192;
    context.framebuffer.height = 64;
    assert(encode_scissor(&context, &native));
    assert(native.scissor[1] == (8192u | (64u << 16)));
    struct rasterizer rasterizer = {true};
    context.rasterizer = &rasterizer;
    context.scissor_valid = true;
    context.scissor = (struct pipe_scissor_state){8000, 1, 9000, 90};
    assert(encode_scissor(&context, &native));
    assert(native.scissor[0] == (0x80000000u | 8000u | (1u << 16)));
    assert(native.scissor[1] == (8192u | (64u << 16)));
    context.scissor = (struct pipe_scissor_state){9000, 90, 0, 0};
    assert(encode_scissor(&context, &native));
    assert((native.scissor[0] & 0x7fffffffu) == native.scissor[1]);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "extents")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT), "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: extent bounds, allocation footprints/fallback, scanout identity, framebuffer scissors")
