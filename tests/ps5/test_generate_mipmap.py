#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Run actual mip dispatch/CPU filtering and pinned Mesa depth converters.

PS5_TEST_MESA may name the Mesa 26.2.0 root when third_party is absent.
The external GPU helper is a checked callback, not GPU/cache emulation.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
mesa = Path(os.environ.get('PS5_TEST_MESA', root / 'third_party/mesa-26.2.0')) / 'src/util/format'


def function(text, name):
    start = re.search(r'(?:static )?(?:inline )?(?:void|bool)\n' + name + r'\(', text)
    assert start, name
    return text[start.start():text.index('\n}\n', start.end()) + 3]


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE 1
#define PS5_GPU_BLIT_MIN_PIXELS (512u * 512u)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
enum { PIPE_TEXTURE_2D_ARRAY, PIPE_TEXTURE_3D, PIPE_TEXTURE_2D };
enum { PIPE_MASK_RGBA=15, PIPE_TEX_FILTER_LINEAR=1 };
enum pipe_format { DEPTH, PACKED, COLOR };
struct pipe_resource { unsigned target,width0,height0,depth0,last_level,array_size; enum pipe_format format; };
struct pipe_context { int unused; };
struct ps5_context { struct pipe_context base; int last_draw_status; };
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_blit_info {
    struct { struct pipe_resource *resource; enum pipe_format format; unsigned level;
        struct pipe_box box; } src,dst;
    unsigned mask,filter;
};
struct ps5_resource { struct pipe_resource base; uint8_t *data;
    size_t size,layer_stride,level_offset[16]; unsigned level_stride[16]; };
static bool ps5_linear_sampled_layout(const struct pipe_resource *r) { return r != NULL; }
static bool ps5_sampled_texture_target(unsigned target)
{ return target == PIPE_TEXTURE_2D_ARRAY || target == PIPE_TEXTURE_2D; }
static bool util_format_is_compressed(enum pipe_format f) { (void)f; return false; }
static bool util_format_is_pure_integer(enum pipe_format f) { (void)f; return false; }
static unsigned ps5_texture_format_size(enum pipe_format f) { return f == DEPTH ? 4 : f == PACKED ? 8 : 16; }
static const enum pipe_format *util_format_description(enum pipe_format f) {
    static enum pipe_format formats[]={DEPTH,PACKED,COLOR}; return &formats[f];
}
static bool util_format_has_depth(const enum pipe_format *f) { return *f != COLOR; }
static unsigned drains,flushes;
static size_t cpu_packs;
static const void *expected_data;
static size_t expected_size;
static void ps5_draw_batch_drain(void) { ++drains; }
static void ps5_flush_gpu_data(const void *p,size_t n) {
    assert(p==expected_data && n==expected_size); ++flushes;
}
/* Model Mesa's absent RGBA callbacks for depth as a checked failure. */
static void util_format_unpack_rgba(enum pipe_format f,float *dst,const void *src,unsigned n) {
    assert(f==COLOR && n==1); memcpy(dst,src,16);
}
static void util_format_pack_rgba(enum pipe_format f,void *dst,const float *src,unsigned n) {
    assert(f==COLOR && n==1); memcpy(dst,src,16); ++cpu_packs;
}
enum { GPU_DECLINE, GPU_ACCEPT, GPU_FAIL };
static unsigned gpu_mode,gpu_calls;
static struct pipe_blit_info gpu_blits[16];
static float gpu_marker(unsigned level,unsigned layer) { return (float)(32+level+layer); }
static bool ps5_blit_gpu_color(struct ps5_context *context,const struct pipe_blit_info *blit) {
    assert(gpu_calls<ARRAY_SIZE(gpu_blits)); gpu_blits[gpu_calls++]=*blit;
    if (!context || gpu_mode==GPU_DECLINE) return false;
    struct ps5_resource *r=(struct ps5_resource *)blit->dst.resource;
    assert(r== (struct ps5_resource *)blit->src.resource && r->base.format==COLOR);
    unsigned level=blit->dst.level,layer=blit->dst.box.z;
    context->last_draw_status=gpu_mode==GPU_FAIL ? -77 : 0;
    if (gpu_mode==GPU_FAIL) {
        /* Model a partially written target: replay would overwrite this marker. */
        float partial=-13;
        memcpy(r->data+layer*r->layer_stride+r->level_offset[level],&partial,4);
        return true;
    }
    /* Deliberately distinguish callback output from CPU box averages, so the
     * test detects CPU replay and proves that the CPU tail consumes this level. */
    float pixel[4];
    for (unsigned c=0;c<4;++c) pixel[c]=gpu_marker(level,layer);
    for (int y=0;y<blit->dst.box.height;++y) for (int x=0;x<blit->dst.box.width;++x)
        memcpy(r->data+layer*r->layer_stride+r->level_offset[level]+
               y*r->level_stride[level]+x*16,pixel,16);
    return true;
}
'''
zs = (mesa / 'u_format_zs.c').read_text()
for name in ('z32_float', 'z32_float_s8x24_uint'):
    for operation in ('unpack', 'pack'):
        code += function(zs, f'util_format_{name}_{operation}_z_float')
code += r'''
static void util_format_unpack_z_float(enum pipe_format f,float *dst,const void *src,unsigned n) {
    assert(f!=COLOR && n==1);
    if (f==DEPTH) util_format_z32_float_unpack_z_float(dst,0,src,0,n,1);
    else util_format_z32_float_s8x24_uint_unpack_z_float(dst,0,src,0,n,1);
}
static void util_format_pack_z_float(enum pipe_format f,void *dst,const float *src,unsigned n) {
    assert(f!=COLOR && n==1);
    ++cpu_packs;
    if (f==DEPTH) util_format_z32_float_pack_z_float(dst,0,src,0,n,1);
    else util_format_z32_float_s8x24_uint_pack_z_float(dst,0,src,0,n,1);
}
''' + function(source, 'ps5_generate_mipmap') + r'''
static size_t offset(const struct ps5_resource *r,unsigned layer,unsigned level,unsigned x,unsigned y) {
    return layer*1024+r->level_offset[level]+y*r->level_stride[level]+x*ps5_texture_format_size(r->base.format);
}
static float read_float(const uint8_t *p) { float f; memcpy(&f,p,4); return f; }
static void check(enum pipe_format format,unsigned width,unsigned height,unsigned first_level) {
    uint8_t bytes[4224],before[4224],expected[4224];
    struct ps5_resource r={.base={PIPE_TEXTURE_2D_ARRAY,width,height,1,2,4,format},
        .data=bytes+64,.size=4096,.layer_stride=1024,.level_offset={16,640,768}};
    unsigned components=format==COLOR ? 4 : 1;
    memset(bytes,0xa5,sizeof(bytes));
    for (unsigned level=0;level<3;++level) {
        unsigned w=MAX2(width>>level,1),h=MAX2(height>>level,1);
        r.level_stride[level]=w*ps5_texture_format_size(format)+16;
        for (unsigned layer=0;layer<4;++layer) for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x)
            for (unsigned c=0;c<components;++c) {
                float value=(float)(layer*64+level*16+y*8+x+c)/512;
                memcpy(r.data+offset(&r,layer,level,x,y)+4*c,&value,4);
            }
    }
    memcpy(before,bytes,sizeof(bytes)); memcpy(expected,bytes,sizeof(bytes));
    for (unsigned level=first_level+1;level<3;++level) {
        unsigned sw=MAX2(width>>(level-1),1),sh=MAX2(height>>(level-1),1);
        unsigned dw=MAX2(width>>level,1),dh=MAX2(height>>level,1);
        for (unsigned layer=1;layer<=2;++layer) for (unsigned y=0;y<dh;++y) for (unsigned x=0;x<dw;++x)
            for (unsigned c=0;c<components;++c) {
                double sum=0; unsigned count=0;
                for (unsigned sy=y*sh/dh;sy<(y+1)*sh/dh;++sy)
                    for (unsigned sx=x*sw/dw;sx<(x+1)*sw/dw;++sx) {
                        sum+=read_float(expected+64+offset(&r,layer,level-1,sx,sy)+4*c); ++count;
                    }
                float value=(float)(sum/count);
                memcpy(expected+64+offset(&r,layer,level,x,y)+4*c,&value,4);
            }
    }
    expected_data=r.data; expected_size=r.size; drains=flushes=0;
    assert(ps5_generate_mipmap(NULL,&r.base,format,first_level,2,1,2));
    assert(drains==1 && flushes==2);
    for (unsigned level=first_level+1;level<3;++level)
        for (unsigned layer=1;layer<=2;++layer)
            for (unsigned y=0;y<MAX2(height>>level,1);++y)
                for (unsigned x=0;x<MAX2(width>>level,1);++x)
                    for (unsigned c=0;c<components;++c) {
                        size_t at=64+offset(&r,layer,level,x,y)+4*c;
                        assert(fabsf(read_float(bytes+at)-read_float(expected+at))<0.000001f);
                        memcpy(expected+at,bytes+at,4);
                    }
    /* All source levels, neighboring layers, row padding, stencil and guards. */
    assert(!memcmp(bytes,expected,sizeof(bytes)));
    memcpy(before,bytes,sizeof(bytes)); drains=flushes=0;
    assert(!ps5_generate_mipmap(NULL,&r.base,format,0,3,1,2));
    assert(!ps5_generate_mipmap(NULL,&r.base,format,1,1,1,2));
    assert(!ps5_generate_mipmap(NULL,&r.base,format,0,2,3,2));
    assert(!ps5_generate_mipmap(NULL,&r.base,format,0,2,1,4));
    assert(drains==4 && !flushes && !memcmp(bytes,before,sizeof(bytes)));
}
static void check_dispatch(unsigned width,unsigned height,unsigned base_level,unsigned last_level,
                           unsigned gpu_levels,unsigned mode,enum pipe_format format) {
    struct ps5_resource r={.base={PIPE_TEXTURE_2D_ARRAY,width,height,1,last_level,2,format}};
    struct ps5_context context={0};
    unsigned bpp=ps5_texture_format_size(format),components=format==COLOR ? 4 : 1;
    for (unsigned level=0;level<=last_level;++level) {
        r.level_offset[level]=r.layer_stride;
        r.level_stride[level]=MAX2(width>>level,1)*bpp;
        r.layer_stride+=(size_t)r.level_stride[level]*MAX2(height>>level,1);
    }
    r.size=2*r.layer_stride;
    uint8_t *bytes=malloc(r.size+128); assert(bytes);
    memset(bytes,0xa5,r.size+128); r.data=bytes+64;
    for (unsigned layer=0;layer<2;++layer) for (unsigned level=0;level<=last_level;++level)
        for (unsigned y=0;y<MAX2(height>>level,1);++y) for (unsigned x=0;x<MAX2(width>>level,1);++x)
            for (unsigned c=0;c<components;++c) {
                float value=level==base_level ? (float)(layer+1)/4 : -1;
                memcpy(r.data+layer*r.layer_stride+r.level_offset[level]+
                       y*r.level_stride[level]+x*bpp+4*c,&value,4);
            }
    expected_data=r.data; expected_size=r.size;
    drains=flushes=gpu_calls=0; cpu_packs=0; gpu_mode=mode;
    assert(ps5_generate_mipmap(&context.base,&r.base,format,base_level,last_level,0,1));
    assert(drains==1);
    unsigned expected_calls=0;
    size_t expected_packs=0;
    for (unsigned level=base_level+1;level<=last_level;++level) {
        if (gpu_levels & (1u<<level)) {
            for (unsigned layer=0;layer<2;++layer) {
                if (mode==GPU_FAIL && expected_calls) continue;
                assert(expected_calls<gpu_calls);
                const struct pipe_blit_info *b=&gpu_blits[expected_calls++];
                assert(b->src.resource==&r.base && b->dst.resource==&r.base);
                assert(b->src.format==format && b->dst.format==format);
                assert(b->src.level==level-1 && b->dst.level==level);
                assert(!b->src.box.x && !b->src.box.y && !b->dst.box.x && !b->dst.box.y);
                assert(b->src.box.z==(int)layer && b->dst.box.z==(int)layer);
                assert(b->src.box.depth==1 && b->dst.box.depth==1);
                assert(b->src.box.width==(int)MAX2(width>>(level-1),1));
                assert(b->src.box.height==(int)MAX2(height>>(level-1),1));
                assert(b->dst.box.width==(int)MAX2(width>>level,1));
                assert(b->dst.box.height==(int)MAX2(height>>level,1));
                assert(b->mask==PIPE_MASK_RGBA && b->filter==PIPE_TEX_FILTER_LINEAR);
            }
        }
        if (mode!=GPU_FAIL && (mode==GPU_DECLINE || !(gpu_levels & (1u<<level))))
            expected_packs+=(size_t)2*MAX2(width>>level,1)*MAX2(height>>level,1);
    }
    assert(gpu_calls==expected_calls && cpu_packs==expected_packs);
    assert(context.last_draw_status==(mode==GPU_FAIL ? -77 : 0));
    assert(flushes==(mode==GPU_FAIL ? 1 : 2+(mode==GPU_ACCEPT ? gpu_calls : 0)));
    for (unsigned layer=0;layer<2;++layer) {
        float value=(float)(layer+1)/4;
        for (unsigned level=0;level<=last_level;++level) {
            if (level>base_level && mode==GPU_ACCEPT && (gpu_levels & (1u<<level)))
                value=gpu_marker(level,layer);
            for (unsigned y=0;y<MAX2(height>>level,1);++y) for (unsigned x=0;x<MAX2(width>>level,1);++x)
                for (unsigned c=0;c<components;++c) {
                    float want=level<base_level || (level>base_level && mode==GPU_FAIL) ? -1 : value;
                    if (mode==GPU_FAIL && level==base_level+1 && !layer && !x && !y && !c) want=-13;
                    assert(read_float(r.data+layer*r.layer_stride+r.level_offset[level]+
                                      y*r.level_stride[level]+x*bpp+4*c)==want);
                }
        }
    }
    for (unsigned i=0;i<64;++i) assert(bytes[i]==0xa5 && bytes[64+r.size+i]==0xa5);
    free(bytes);
}
int main(void) {
    for (unsigned f=DEPTH;f<=COLOR;++f) for (unsigned base=0;base<2;++base) {
        check(f,4,4,base); check(f,7,5,base); check(f,7,1,base);
    }
    check_dispatch(1024,1024,0,3,1u<<1,GPU_ACCEPT,COLOR); /* Inclusive 512x512 floor + CPU tail. */
    check_dispatch(1024,1024,0,3,1u<<1,GPU_DECLINE,COLOR); /* Preflight decline uses CPU. */
    check_dispatch(1024,1024,0,3,1u<<1,GPU_FAIL,COLOR); /* Attempt failure does not replay or continue. */
    check_dispatch(1025,1024,0,2,0,GPU_ACCEPT,COLOR); /* Odd source width, despite large destination. */
    check_dispatch(1024,1025,0,2,0,GPU_ACCEPT,COLOR); /* Odd source height. */
    check_dispatch(1026,1024,0,2,1u<<1,GPU_ACCEPT,COLOR); /* Even NPOT is still an exact halving. */
    check_dispatch(1022,1024,0,2,0,GPU_ACCEPT,COLOR); /* 511x512 is below the floor. */
    check_dispatch(2048,2048,1,3,1u<<2,GPU_ACCEPT,COLOR); /* Nonzero base mip, correct source dimensions. */
    check_dispatch(2048,2048,0,3,(1u<<1)|(1u<<2),GPU_ACCEPT,COLOR); /* Two GPU levels, then CPU. */
    check_dispatch(1024,1024,0,2,0,GPU_ACCEPT,DEPTH); /* Depth never dispatches a color GPU blit. */
}
'''
old = code.replace('depth = util_format_has_depth(util_format_description(format));',
                   'depth = false;')
assert old != code
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / 'mipmap')
    for name, text in (('fixed', code), ('old-rgba-dispatch', old)):
        subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
                        '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer', '-fno-sanitize-recover=all', '-no-pie', '-x', 'c', '-', '-lm',
                        '-o', executable], input=text, text=True, check=True, timeout=60)
        run = subprocess.run([executable], cwd=directory, capture_output=True, text=True, timeout=60)
        if name == 'fixed':
            assert run.returncode == 0, run.stderr
        else:
            assert run.returncode != 0 and 'f==COLOR' in run.stderr, run.stderr
print('PASS: real mip filter/Mesa depth converters; D32/D32S8/color, odd/1D extents, '
      'layers 1-2, mip bases 0-1, stencil/padding/bounds; GPU exact-halves/NPOT/floor/tail, '
      'per-level/layer dispatch, partial-attempt no-replay; old RGBA dispatch rejected (ASan/UBSan)')
