#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""ASan/UBSan on actual native color policy, addressing, map/unmap and subdata.

Like test_transfer_staging.py, mock Gallium types and cache/queue operations.
No GPU emulation: descriptors and sRGB conversion need the public format gate.
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[2] / "src/gallium/ps5/ps5_screen.c").read_text()


def section(start, end):
    return source[source.index(start):source.index(end)]


code = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#define PIPE_MAX_TEXTURE_LEVELS 16
#define PS5_DIRECT_ALIGNMENT 0x4000
#define PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE 1
#define PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE 1
#define PS5_ENABLE_PADDED_FBO_CANDIDATE 1
#define PS5_MAX_COLOR_WIDTH 8192
#define PS5_MAX_COLOR_HEIGHT 8192
#define PS5_RENDER_WIDTH 1920
#define PS5_RENDER_HEIGHT 1080
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BITFIELD_BIT(b) (1u << (b))
enum { PIPE_BUFFER, PIPE_TEXTURE_2D, PIPE_TEXTURE_RECT, PIPE_TEXTURE_3D };
enum pipe_format { PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_FORMAT_R8G8B8A8_SRGB,
    PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT };
enum { PIPE_MAP_READ=1, PIPE_MAP_WRITE=2, PIPE_BIND_RENDER_TARGET=4,
    PIPE_BIND_DEPTH_STENCIL=8, PIPE_BIND_SAMPLER_VIEW=16 };
struct pipe_resource { unsigned bind, target, format, width0, height0, depth0,
    array_size, last_level, nr_samples, nr_storage_samples; };
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_transfer { struct pipe_resource *resource; unsigned level,usage;
    struct pipe_box box; unsigned stride,layer_stride,offset; };
struct pipe_context {
    void *(*texture_map)(struct pipe_context *, struct pipe_resource *, unsigned,
                        unsigned, const struct pipe_box *, struct pipe_transfer **);
    void (*texture_unmap)(struct pipe_context *, struct pipe_transfer *);
};
''' + section("struct ps5_resource {", "struct ps5_vertex_elements {") + r'''
static unsigned ps5_texture_format_size(unsigned f) { return f == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ? 8 : 4; }
static unsigned util_format_get_blocksize(enum pipe_format f) { return ps5_texture_format_size(f); }
static unsigned util_format_get_blockwidth(unsigned f) { (void)f; return 1; }
static unsigned util_format_get_blockheight(unsigned f) { (void)f; return 1; }
static unsigned ps5_texture_level_layers(const struct pipe_resource *r, unsigned l) { assert(!l); return r->array_size; }
static bool ps5_sampled_texture_format(unsigned f) { return f <= PIPE_FORMAT_R8G8B8A8_SRGB; }
static bool ps5_render_target_format(unsigned f) { return ps5_sampled_texture_format(f); }
static unsigned drains, flushes;
static void ps5_draw_batch_drain_buffer(struct pipe_resource *r) { assert(r); ++drains; }
static void ps5_flush_gpu_data(const void *p, size_t n) { assert(p && n); ++flushes; }
''' + section("static bool\nps5_linear_sampled_layout(", "static bool\nps5_color_render_target(") + section(
    "static size_t\nps5_tiled_surface_size(", "static bool\nps5_integer_texture_format(") + section(
    "static size_t\nps5_tiled_color_surface_size(", "static uint32_t\nps5_color_target_info(") + section(
    "static bool\nps5_map_bounds(", "static size_t\nps5_tiled_color_msaa4_offset(") + section(
    "static unsigned\nps5_tiled_rgba8_width(", "static unsigned\nps5_surface_width(") + section(
    "static bool\nps5_transfer_alloc_staging(", "static void\nps5_blit_scissor_bounds(") + section(
    "static void\nps5_texture_subdata(", "static bool\nps5_generate_mipmap(") + r'''
static void check(unsigned format, unsigned w, unsigned h) {
    struct ps5_resource r={.base={.target=PIPE_TEXTURE_2D,.format=format,
        .width0=w,.height0=h,.depth0=1,.array_size=1,
        .bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW},
        .level_stride={w*4},.layer_stride=(size_t)w*h*4,.size=(size_t)w*h*4};
    assert(!ps5_linear_sampled_layout(&r.base)); /* Fails on the G19 sRGB policy. */
    r.allocation_size=ps5_tiled_color_surface_size(format,w,h);
    r.data=malloc(r.allocation_size);
    uint8_t *expected=malloc(r.allocation_size), *seen=calloc(1,r.allocation_size);
    uint8_t *upload=malloc((size_t)(w*4+12)*h);
    assert(r.data && expected && seen && upload);
    memset(r.data,0xa5,r.allocation_size); memset(expected,0xa5,r.allocation_size);
    memset(upload,0x17,(size_t)(w*4+12)*h);
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        size_t offset=ps5_tiled_color_offset(format,x,y,w);
        assert(offset+4<=r.allocation_size && !(offset%4));
        /* Same physical layout as existing RGBA8; every texel is unique. */
        assert(offset == ps5_tiled_color_offset(PIPE_FORMAT_R8G8B8A8_UNORM,x,y,w));
        for (unsigned c=0;c<4;++c) {
            assert(!seen[offset+c]); seen[offset+c]=1;
            upload[y*(w*4+12)+x*4+c]=(uint8_t)(x*37+y*13+c*59);
            expected[offset+c]=upload[y*(w*4+12)+x*4+c];
        }
    }
    struct pipe_context ctx={ps5_transfer_map,ps5_transfer_unmap};
    struct pipe_box box={0,0,0,(int)w,(int)h,1};
    drains=flushes=0;
    ps5_texture_subdata(&ctx,&r.base,0,0,&box,upload,w*4+12,(w*4+12)*h);
    assert(drains==2 && flushes==1 && !memcmp(r.data,expected,r.allocation_size));
    struct pipe_transfer *t=NULL;
    uint8_t *p=ps5_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ,&box,&t);
    assert(p && t && t->stride==w*4 && t->layer_stride==w*h*4);
    assert(!!((struct ps5_transfer *)t)->staging_mapping_size == (r.size>=65536));
    for (unsigned y=0;y<h;++y) assert(!memcmp(p+y*t->stride,upload+y*(w*4+12),w*4));
    ps5_transfer_unmap(&ctx,t);
    assert(!memcmp(r.data,expected,r.allocation_size)); /* Read-only maps preserve padding. */
    box=(struct pipe_box){w>128 ? 125 : 0,h>128 ? 126 : 0,0,
                         w>128 ? 4 : (int)w,h>128 ? 3 : (int)h,1};
    p=ps5_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ|PIPE_MAP_WRITE,&box,&t);
    assert(p && t);
    for (unsigned y=0;y<(unsigned)box.height;++y) for (unsigned x=0;x<(unsigned)box.width;++x) {
        size_t offset=ps5_tiled_color_offset(format,x+box.x,y+box.y,w);
        for (unsigned c=0;c<4;++c) {
            assert(p[y*t->stride+x*4+c]==expected[offset+c]);
            p[y*t->stride+x*4+c]^=0x7d; expected[offset+c]^=0x7d;
        }
    }
    ps5_transfer_unmap(&ctx,t);
    assert(!memcmp(r.data,expected,r.allocation_size)); /* Odd subrect and all untouched bytes. */
    for (unsigned bad=0;bad<4;++bad) {
        struct pipe_box b=box;
        if (bad==0) b.x=-1;
        if (bad==1) b.z=1;
        if (bad==2) b.depth=2;
        t=(struct pipe_transfer *)&r;
        assert(!ps5_transfer_map(&ctx,&r.base,bad==3 ? 1 : 0,PIPE_MAP_READ,&b,&t) && !t);
        assert(!memcmp(r.data,expected,r.allocation_size));
    }
    /* Bound failure after scratch allocation must release it and clear output. */
    size_t allocation=r.allocation_size;
    r.allocation_size=1; t=(struct pipe_transfer *)&r;
    assert(!ps5_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ,&box,&t) && !t);
    r.allocation_size=allocation;
    assert(!memcmp(r.data,expected,r.allocation_size));
    free(upload); free(seen); free(expected); free(r.data);
}
int main(void) {
    const unsigned sizes[][2]={{1,1},{63,65},{127,129},{128,128},{129,129},{257,259},{8192,3}};
    for (unsigned f=PIPE_FORMAT_R8G8B8A8_UNORM;f<=PIPE_FORMAT_R8G8B8A8_SRGB;++f)
        for (unsigned i=0;i<ARRAY_SIZE(sizes);++i) check(f,sizes[i][0],sizes[i][1]);
    puts("PASS: actual RGBA8/sRGB tiled map/unmap/subdata, unique addresses, odd boxes, padding, bounds and heap/mmap scratch");
}
'''
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / "native-srgb-transfer")
    subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-fsanitize=address,undefined", "-g",
                    "-fno-pie", "-no-pie", "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], check=True)
