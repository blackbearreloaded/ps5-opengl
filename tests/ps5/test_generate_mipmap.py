#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Run the real CPU mip filter and Mesa depth converters; no GPU/cache proof."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
mesa = root / 'third_party/mesa-26.2.0/src/util/format'


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
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
enum { PIPE_TEXTURE_2D_ARRAY, PIPE_TEXTURE_3D };
enum pipe_format { DEPTH, PACKED, COLOR };
struct pipe_resource { unsigned target,width0,height0,depth0,last_level,array_size; enum pipe_format format; };
struct pipe_context { int unused; };
struct ps5_resource { struct pipe_resource base; uint8_t *data;
    size_t size,layer_stride,level_offset[3]; unsigned level_stride[3]; };
static bool ps5_linear_sampled_layout(const struct pipe_resource *r) { return r != NULL; }
static bool ps5_sampled_texture_target(unsigned target) { return target == PIPE_TEXTURE_2D_ARRAY; }
static bool util_format_is_compressed(enum pipe_format f) { (void)f; return false; }
static bool util_format_is_pure_integer(enum pipe_format f) { (void)f; return false; }
static unsigned ps5_texture_format_size(enum pipe_format f) { return f == DEPTH ? 4 : f == PACKED ? 8 : 16; }
static const enum pipe_format *util_format_description(enum pipe_format f) {
    static enum pipe_format formats[]={DEPTH,PACKED,COLOR}; return &formats[f];
}
static bool util_format_has_depth(const enum pipe_format *f) { return *f != COLOR; }
static unsigned drains,flushes;
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
    assert(f==COLOR && n==1); memcpy(dst,src,16);
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
int main(void) {
    for (unsigned f=DEPTH;f<=COLOR;++f) for (unsigned base=0;base<2;++base) {
        check(f,4,4,base); check(f,7,5,base); check(f,7,1,base);
    }
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
                        '-fno-omit-frame-pointer', '-no-pie', '-x', 'c', '-', '-lm',
                        '-o', executable], input=text, text=True, check=True)
        run = subprocess.run([executable], cwd=directory, capture_output=True, text=True)
        if name == 'fixed':
            assert run.returncode == 0, run.stderr
        else:
            assert run.returncode != 0 and 'f==COLOR' in run.stderr, run.stderr
print('PASS: real mip filter/Mesa depth converters; D32/D32S8/color, odd/1D extents, '
      'layers 1-2, mip bases 0-1, stencil/padding/bounds; old RGBA dispatch rejected')
