#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compare actual color staging with scalar addressing, including untouched bytes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()


def section(start, end):
    return source[source.index(start):source.index(end)]


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PIPE_MAX_TEXTURE_LEVELS 16
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
#define MIN2(a,b) ((a) < (b) ? (a) : (b))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define BITFIELD_BIT(b) (1u << (b))
enum pipe_format { R8=1, RG8=2, RGB8=3, RGBA8=4, RGBA16=8, RGBA32=16 };
struct pipe_resource { unsigned width0, height0, array_size, last_level; };
struct pipe_surface { struct pipe_resource *texture; enum pipe_format format;
    unsigned level, first_layer, last_layer; };
static unsigned util_format_get_blocksize(enum pipe_format f) { return f; }
static unsigned ps5_texture_format_size(enum pipe_format f) { return f; }
static unsigned ps5_texture_level_layers(const struct pipe_resource *r, unsigned l) {
    assert(l <= r->last_level); return r->array_size;
}
static unsigned flushes;
static void ps5_flush_gpu_data(void *p, size_t n) { assert(p && n); ++flushes; }
''' + section("struct ps5_resource {", "struct ps5_transfer {") + section(
    "static size_t\nps5_tiled_color_surface_size(", "static uint32_t\nps5_color_target_info(") + section(
    "static size_t\nps5_tiled_affine_offset(", "static size_t\nps5_tiled_depth_offset(") + section(
    "static size_t\nps5_tiled_color_offset(", "static size_t\nps5_tiled_color_msaa4_offset(") + section(
    "static unsigned\nps5_surface_width(", "static unsigned\nps5_linear_color_pitch(") + section(
    "static bool\nps5_stage_color_surface(", "static bool\nps5_stage_depth_surface(") + r'''
/* Deliberately scalar: no lookup/decomposition shared with the staging loop. */
static void reference(const struct pipe_surface *s, bool to_staging) {
    struct ps5_resource *r = (struct ps5_resource *)s->texture;
    const unsigned w=ps5_surface_width(s), h=ps5_surface_height(s), bpp=s->format;
    const size_t tiled_size=ps5_tiled_color_surface_size(s->format,w,h);
    for (unsigned layer=s->first_layer; layer<=s->last_layer; ++layer)
        for (unsigned y=0; y<h; ++y)
            for (unsigned x=0; x<w; ++x) {
                size_t a=layer*r->layer_stride+r->level_offset[s->level]+
                         y*r->level_stride[s->level]+(size_t)x*bpp;
                size_t b=r->render_staging_offset+(layer-s->first_layer)*tiled_size+
                         ps5_tiled_color_offset(s->format,x,y,w);
                assert(a+bpp<=r->size && b+bpp<=r->allocation_size);
                if (to_staging) memcpy(r->data+b,r->data+a,bpp);
                else memcpy(r->data+a,r->data+b,bpp);
            }
}
static void check(enum pipe_format format, unsigned w, unsigned h, unsigned level) {
    struct ps5_resource r={.base={w<<level,h<<level,3,level}};
    size_t size=32+(w&3); /* Exercise aligned and unaligned linear storage. */
    for (unsigned l=0; l<=level; ++l) {
        r.level_offset[l]=size;
        r.level_stride[l]=(((w<<level)>>l)*format+255u)&~255u;
        size+=(size_t)r.level_stride[l]*((h<<level)>>l);
    }
    r.layer_stride=size+32; r.size=r.layer_stride*r.base.array_size;
    r.render_staging_offset=(r.size+255u)&~(size_t)255u;
    r.render_staging_size=ps5_tiled_color_surface_size(format,r.base.width0,r.base.height0)*3;
    r.allocation_size=r.render_staging_offset+r.render_staging_size;
    size_t allocated=r.allocation_size+32; /* Preserve tail padding as well. */
    r.data=malloc(allocated); assert(r.data);
    for (size_t i=0; i<allocated; ++i) r.data[i]=(uint8_t)(i*37u+(i>>8)+(i>>16));
    struct ps5_resource ref=r; ref.data=malloc(allocated); assert(ref.data);
    memcpy(ref.data,r.data,allocated);
    struct pipe_surface s={&r.base,format,level,1,2}, sr=s; sr.texture=&ref.base;
    for (unsigned pass=0; pass<4; ++pass) {
        bool to_staging=!(pass&1);
        if (pass) { /* Different CPU/GPU data on each ownership direction. */
            size_t begin=to_staging ? 0 : r.render_staging_offset;
            size_t end=to_staging ? r.size : r.allocation_size;
            for (size_t i=begin; i<end; ++i) r.data[i]^=(uint8_t)(91u+pass+i);
            memcpy(ref.data,r.data,allocated);
        }
        flushes=0;
        assert(ps5_stage_color_surface(&s,to_staging));
        assert(flushes == (to_staging ? 1u : 2u));
        reference(&sr,to_staging);
        assert(!memcmp(r.data,ref.data,allocated));
    }
    struct pipe_surface bad=s;
    bad.texture=NULL; assert(!ps5_stage_color_surface(&bad,true));
    bad=s; bad.level=level+1; assert(!ps5_stage_color_surface(&bad,true));
    bad=s; bad.first_layer=3; assert(!ps5_stage_color_surface(&bad,true));
    bad=s; bad.last_layer=3; assert(!ps5_stage_color_surface(&bad,true));
    bad=s; bad.format=0; assert(!ps5_stage_color_surface(&bad,true));
    bad=s; bad.format=RGB8; assert(!ps5_stage_color_surface(&bad,true));
    size_t saved=r.render_staging_size;
    r.render_staging_size=0; assert(!ps5_stage_color_surface(&s,true));
    r.render_staging_size=1; assert(!ps5_stage_color_surface(&s,true));
    r.render_staging_size=saved;
    --r.allocation_size; assert(!ps5_stage_color_surface(&s,true)); ++r.allocation_size;
    size_t last=s.last_layer*r.layer_stride+r.level_offset[level]+(h-1)*r.level_stride[level]+w*format;
    r.size=last-1; assert(!ps5_stage_color_surface(&s,true));
    assert(!ps5_stage_color_surface(&s,false));
    free(r.data); free(ref.data);
}
int main(void) {
    assert(!ps5_stage_color_surface(NULL,true));
    for (unsigned bpp=1; bpp<=16; bpp*=2) {
        check(bpp,1,1,0);
        check(bpp,257,259,0);
        check(bpp,385,193,1);
        check(bpp,65,67,2);
        check(bpp,8192,3,0);
    }
    check(RGBA8,1920,1080,0);
    const unsigned edges[]={2,3,4,5,7,8,9,127,128,129,255,256,257};
    for (unsigned i=0; i<ARRAY_SIZE(edges); ++i)
        for (unsigned level=0; level<3; ++level)
            check(RGBA8,edges[i],129,level);
    puts("color-staging: PASS scalar equivalence, 1/2/4/8/16-byte formats, tiles, mip/layer slices, bounds, flushes");
}
'''
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / "color-staging")
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g", "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], cwd=directory, check=True)
