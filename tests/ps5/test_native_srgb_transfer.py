#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""ASan/UBSan on actual native 1/2/4/8-byte color policy and CPU transfers.

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
    PIPE_FORMAT_R8_UNORM, PIPE_FORMAT_R8G8_UNORM, PIPE_FORMAT_R16G16B16A16_FLOAT,
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
static const struct format_case {
    unsigned format, bpp, tile_w, tile_h, probe_3_5;
} formats[] = {
    {PIPE_FORMAT_R8G8B8A8_UNORM, 4, 128, 128, 0x005c},
    {PIPE_FORMAT_R8G8B8A8_SRGB, 4, 128, 128, 0x005c},
    {PIPE_FORMAT_R8_UNORM, 1, 256, 256, 0x0033},
    {PIPE_FORMAT_R8G8_UNORM, 2, 256, 128, 0x0056},
    {PIPE_FORMAT_R16G16B16A16_FLOAT, 8, 128, 64, 0x1038},
};
static unsigned ps5_texture_format_size(unsigned f) {
    for (unsigned i=0;i<ARRAY_SIZE(formats);++i)
        if (formats[i].format == f) return formats[i].bpp;
    return f == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ? 8 : 4;
}
static unsigned util_format_get_blocksize(enum pipe_format f) { return ps5_texture_format_size(f); }
static unsigned util_format_get_blockwidth(unsigned f) { (void)f; return 1; }
static unsigned util_format_get_blockheight(unsigned f) { (void)f; return 1; }
static unsigned ps5_texture_level_layers(const struct pipe_resource *r, unsigned l) { assert(!l); return r->array_size; }
/* Disabled capabilities and policy fallback guards live in test_native_color_layout.py. */
static bool ps5_sampled_texture_format(unsigned f) { return f <= PIPE_FORMAT_R16G16B16A16_FLOAT; }
static bool ps5_render_target_format(unsigned f) { return ps5_sampled_texture_format(f); }
static unsigned drains, flushes;
static void ps5_draw_batch_drain_buffer(struct pipe_resource *r) { assert(r); ++drains; }
static void ps5_flush_gpu_data(const void *p, size_t n) { assert(p && n); ++flushes; }
''' + section("static bool\nps5_linear_sampled_layout(", "static bool\nps5_color_render_target(") + section(
    "static size_t\nps5_tiled_depth_layer_xor(", "static bool\nps5_integer_texture_format(") + section(
    "static size_t\nps5_tiled_color_surface_size(", "static uint32_t\nps5_color_target_info(") + section(
    "static bool\nps5_map_bounds(", "static size_t\nps5_tiled_color_msaa4_offset(") + section(
    "static unsigned\nps5_tiled_rgba8_width(", "static unsigned\nps5_surface_width(") + section(
    "static bool\nps5_transfer_alloc_staging(", "static void\nps5_blit_scissor_bounds(") + section(
    "static void\nps5_texture_subdata(", "static bool\nps5_generate_mipmap(") + r'''
static void check(const struct format_case *test, unsigned w, unsigned h) {
    const unsigned format=test->format, bpp=test->bpp, stride=w*bpp+12;
    struct ps5_resource r={.base={.target=PIPE_TEXTURE_2D,.format=format,
        .width0=w,.height0=h,.depth0=1,.array_size=1,
        .bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW},
        .level_stride={w*bpp},.layer_stride=(size_t)w*h*bpp,.size=(size_t)w*h*bpp};
    assert(!ps5_linear_sampled_layout(&r.base));
    r.allocation_size=ps5_tiled_color_surface_size(format,w,h);
    const unsigned tiles_x=(w+test->tile_w-1)/test->tile_w;
    /* Fixed format geometry and coordinate probes are independent of the
     * driver's size/offset helpers. RGBA16F also XORs odd tile rows by 0x800. */
    assert(r.allocation_size == (size_t)tiles_x*((h+test->tile_h-1)/test->tile_h)*65536);
    assert(ps5_tiled_color_offset(format,0,0,w) == 0);
    assert(ps5_tiled_color_offset(format,1,0,w) == bpp);
    assert(ps5_tiled_color_offset(format,0,1,w) == 0x10);
    assert(ps5_tiled_color_offset(format,3,5,w) == test->probe_3_5);
    assert(ps5_tiled_color_offset(format,test->tile_w,0,w) == 65536);
    assert(ps5_tiled_color_offset(format,0,test->tile_h,w) ==
           (size_t)tiles_x*65536+(bpp == 8 ? 0x800 : 0));
    assert(ps5_tiled_color_offset(format,0,2*test->tile_h,w) == (size_t)2*tiles_x*65536);
    r.data=malloc(r.allocation_size);
    uint8_t *expected=malloc(r.allocation_size), *seen=calloc(1,r.allocation_size);
    uint8_t *upload=malloc((size_t)stride*h);
    assert(r.data && expected && seen && upload);
    memset(r.data,0xa5,r.allocation_size); memset(expected,0xa5,r.allocation_size);
    memset(upload,0x17,(size_t)stride*h);
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        size_t offset=ps5_tiled_color_offset(format,x,y,w);
        assert(offset+bpp<=r.allocation_size && !(offset%bpp));
        assert(offset/65536 == (size_t)(y/test->tile_h)*tiles_x+x/test->tile_w);
        /* A byte bitmap proves injectivity without assuming RGBA8 addressing;
         * whole-allocation comparisons below protect all unseen padding. */
        for (unsigned c=0;c<bpp;++c) {
            assert(!seen[offset+c]); seen[offset+c]=1;
            upload[y*stride+x*bpp+c]=(uint8_t)(x*37+y*13+c*59+(x>>8)*17+(y>>8)*23);
            expected[offset+c]=upload[y*stride+x*bpp+c];
        }
    }
    struct pipe_context ctx={ps5_transfer_map,ps5_transfer_unmap};
    struct pipe_box box={0,0,0,(int)w,(int)h,1};
    drains=flushes=0;
    ps5_texture_subdata(&ctx,&r.base,0,0,&box,upload,stride,(size_t)stride*h);
    assert(drains==2 && flushes==1 && !memcmp(r.data,expected,r.allocation_size));
    struct pipe_transfer *t=NULL;
    uint8_t *p=ps5_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ,&box,&t);
    assert(p && t && t->stride==w*bpp && t->layer_stride==w*h*bpp);
    assert(!!((struct ps5_transfer *)t)->staging_mapping_size == (r.size>=65536));
    for (unsigned y=0;y<h;++y) assert(!memcmp(p+y*t->stride,upload+y*stride,w*bpp));
    ps5_transfer_unmap(&ctx,t);
    assert(!memcmp(r.data,expected,r.allocation_size)); /* Read-only maps preserve padding. */
    box=(struct pipe_box){w>test->tile_w ? (int)test->tile_w-3 : 0,
                         h>test->tile_h ? (int)test->tile_h-2 : 0,0,
                         w>test->tile_w ? 4 : (int)w,h>test->tile_h ? 3 : (int)h,1};
    p=ps5_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ|PIPE_MAP_WRITE,&box,&t);
    assert(p && t);
    for (unsigned y=0;y<(unsigned)box.height;++y) for (unsigned x=0;x<(unsigned)box.width;++x) {
        size_t offset=ps5_tiled_color_offset(format,x+box.x,y+box.y,w);
        for (unsigned c=0;c<bpp;++c) {
            assert(p[y*t->stride+x*bpp+c]==expected[offset+c]);
            p[y*t->stride+x*bpp+c]^=0x7d; expected[offset+c]^=0x7d;
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
    if (r.size>1) { /* A 1x1 R8 image cannot be shortened to a positive allocation. */
        size_t allocation=r.allocation_size;
        box=(struct pipe_box){0,0,0,(int)w,(int)h,1};
        r.allocation_size=1; t=(struct pipe_transfer *)&r;
        assert(!ps5_transfer_map(&ctx,&r.base,0,PIPE_MAP_READ,&box,&t) && !t);
        r.allocation_size=allocation;
    }
    assert(!memcmp(r.data,expected,r.allocation_size));
    free(upload); free(seen); free(expected); free(r.data);
}
int main(void) {
    for (unsigned f=0;f<ARRAY_SIZE(formats);++f) {
        const unsigned tw=formats[f].tile_w, th=formats[f].tile_h;
        const unsigned sizes[][2]={{1,1},{63,65},{127,129},{tw-1,th-1},{tw,th},
            {tw+1,th+1},{2*tw+1,2*th+3},{8192,3},{3,8192}};
        for (unsigned i=0;i<ARRAY_SIZE(sizes);++i) check(&formats[f],sizes[i][0],sizes[i][1]);
    }
    puts("PASS: 45 actual RGBA8/sRGB/R8/RG8/RGBA16F tiled map/unmap/subdata cases, "
         "1/2/4/8-byte unique addresses, tile probes, padding, bounds and heap/mmap scratch");
}
'''
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / "native-srgb-transfer")
    subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-fsanitize=address,undefined", "-g",
                    "-fno-pie", "-no-pie", "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], check=True)
