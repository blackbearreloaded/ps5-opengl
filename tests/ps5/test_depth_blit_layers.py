#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Actual CPU depth blit/resolve/replicate branches; independent tiled oracle.

Gallium structs and completed-GPU/cache operations are mocked, not GPU proof.
"""
from pathlib import Path
import re
import subprocess
import tempfile

from test_depth_layer_layout import reference_function

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()


def function(name):
    match = re.search(r'static [^\n]+\n' + name + r'\(', source)
    assert match, name
    return source[match.start():source.index('\n}\n', match.end()) + 3]


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define BITFIELD_BIT(i) (1u<<(i))
#define PS5_ENABLE_TEXTURE_1D_CANDIDATE 1
#define PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE 1
#define PIPE_MASK_Z 1
#define PIPE_MASK_S 2
#define PIPE_MASK_ZS 3
#define PIPE_TEX_FILTER_NEAREST 0
enum pipe_texture_target { PIPE_TEXTURE_2D, PIPE_TEXTURE_2D_ARRAY, PIPE_TEXTURE_3D,
                          PIPE_TEXTURE_1D, PIPE_TEXTURE_1D_ARRAY, PIPE_TEXTURE_CUBE };
enum { PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT };
struct pipe_resource { unsigned format,target,width0,height0,nr_samples,nr_storage_samples,array_size,last_level; };
struct ps5_resource { struct pipe_resource base; uint8_t *data,*stencil_data;
    size_t allocation_size,stencil_allocation_size,size,depth_staging_size,layer_stride,level_offset[16];
    unsigned level_stride[16]; };
struct pipe_context { unsigned unused; };
struct ps5_context { bool condition; };
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_blit_info {
    struct { struct pipe_resource *resource; unsigned level,format; struct pipe_box box; } src,dst;
    unsigned mask,dst_sample,num_window_rectangles,filter;
    bool sample0_only,swizzle_enable,alpha_blend,render_condition_enable,scissor_enable;
    struct { unsigned minx,miny,maxx,maxy; } scissor;
};
static bool ps5_render_condition_passes(struct ps5_context *p) { return p->condition; }
static struct { uint8_t *p; size_t n; } spans[4];
static void ps5_flush_gpu_data(const void *p,size_t n) {
    unsigned matches=0;
    for (unsigned i=0;i<4;++i) {
        uintptr_t delta=(uintptr_t)p-(uintptr_t)spans[i].p;
        matches += delta<=spans[i].n && n<=spans[i].n-delta;
    }
    assert(n && matches==1);
}
'''
for name in ('ps5_depth_render_target', 'ps5_tiled_depth_layer_xor', 'ps5_tiled_surface_size',
             'ps5_tiled_stencil_surface_size', 'ps5_tiled_rgba8_msaa4_surface_size',
             'ps5_tiled_depth_surface_size', 'ps5_tiled_stencil_surface_size_samples',
             'ps5_tiled_affine_offset', 'ps5_tiled_depth_offset',
             'ps5_tiled_depth_msaa4_offset', 'ps5_tiled_stencil_offset',
             'ps5_tiled_stencil_msaa4_offset', 'ps5_blit_scissor_bounds',
             'ps5_depth_blit_layer', 'ps5_depth_blit_offset', 'ps5_resolve_depth_stencil_msaa4',
             'ps5_replicate_depth_stencil_msaa4'):
    code += function(name)
start = source.index('   if (info && (info->mask & PIPE_MASK_ZS)) {')
end = source.index('\n   if (!info || !info->src.resource', start)
code += '''static void single_blit(struct pipe_context *context,const struct pipe_blit_info *info) {
    struct ps5_context *ps5=(struct ps5_context *)context;
    unsigned min_x,min_y,max_x,max_y;
''' + source[start:end] + '\n}\n'
for samples, bpe, name, tile in ((1, 4, 'depth', 128), (1, 1, 'stencil', 256),
                                (4, 4, 'depth_msaa4', 64), (4, 1, 'stencil_msaa4', 128)):
    code += reference_function(samples, bpe, name, tile)
code += r'''
#define W 32
#define LAYERS 4
#define PLANE (LAYERS*65536u)
static size_t offset(unsigned samples,unsigned stencil,unsigned x,unsigned y,unsigned sample,unsigned layer) {
    size_t local=stencil ? (samples==4 ? reference_stencil_msaa4(x,y,sample,W,layer) : reference_stencil(x,y,0,W,layer))
                        : (samples==4 ? reference_depth_msaa4(x,y,sample,W,layer) : reference_depth(x,y,0,W,layer));
    return layer*65536u+local;
}
static size_t pixel(const struct ps5_resource *r,unsigned level,unsigned stencil,
                    unsigned x,unsigned y,unsigned sample,unsigned layer) {
    if (r->depth_staging_size)
        return layer*r->layer_stride+r->level_offset[level]+y*r->level_stride[level]+x*(r->base.format?8u:4u)+stencil*4;
    return offset(r->base.nr_samples,stencil,x,y,sample,layer);
}
static void linear(struct ps5_resource *r,unsigned level) {
    r->depth_staging_size=65536; r->base.last_level=2;
    r->base.width0=W<<level; r->base.height0=W<<level;
    for (unsigned i=0;i<3;++i) {
        r->level_offset[i]=r->layer_stride;
        r->level_stride[i]=(r->base.width0>>i)*(r->base.format?8u:4u)+16;
        r->layer_stride+=r->level_stride[i]*(r->base.height0>>i);
    }
    r->size=r->layer_stride*LAYERS; assert(r->size<=PLANE);
}
static void run(unsigned mode,unsigned mask,unsigned source_layer,unsigned dest_layer,unsigned variant,unsigned packed,
                unsigned layout,unsigned level) {
    unsigned src_samples=mode==1 ? 4 : 1, dst_samples=mode==2 ? 4 : 1;
    if ((src_samples==4 && (layout&1)) || (dst_samples==4 && (layout&2))) return;
    uint8_t *raw[4],*expected[4];
    for (unsigned i=0;i<4;++i) {
        raw[i]=malloc(PLANE+128); expected[i]=malloc(PLANE+128); assert(raw[i] && expected[i]);
        memset(raw[i],0xa5,PLANE+128); spans[i].p=raw[i]+64; spans[i].n=PLANE;
    }
    struct ps5_resource src={.base={packed,source_layer ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D,
        W,W,src_samples,src_samples,source_layer ? LAYERS : 1,0},
        .data=spans[0].p,.stencil_data=spans[1].p,.allocation_size=PLANE,.stencil_allocation_size=PLANE};
    struct ps5_resource dst={.base={packed,dest_layer ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D,
        W,W,dst_samples,dst_samples,dest_layer ? LAYERS : 1,0},
        .data=spans[2].p,.stencil_data=spans[3].p,.allocation_size=PLANE,.stencil_allocation_size=PLANE};
    struct pipe_blit_info info={.src={&src.base,0,packed,{4,5,(int)source_layer,8,6,1}},
        .dst={&dst.base,0,packed,{9,10,(int)dest_layer,8,6,1}},.mask=mask};
    if (layout&1) { linear(&src,level); info.src.level=level; }
    if (layout&2) { linear(&dst,level); info.dst.level=level; }
    for (unsigned layer=0;layer<LAYERS;++layer) for (unsigned sample=0;sample<src_samples;++sample)
        for (unsigned y=0;y<W;++y) for (unsigned x=0;x<W;++x) {
            uint32_t depth=0x3e000000u+layer*4096+sample*1024+y*W+x;
            memcpy(spans[0].p+pixel(&src,info.src.level,0,x,y,sample,layer),&depth,4);
            if (packed) spans[layout&1 ? 0 : 1].p[pixel(&src,info.src.level,1,x,y,sample,layer)]=
                (uint8_t)(layer*29+sample*13+y+x);
        }
    for (unsigned i=0;i<4;++i) memcpy(expected[i],raw[i],PLANE+128);
    struct ps5_context ctx={true};
    if (variant==1) { info.scissor_enable=true; info.scissor=(typeof(info.scissor)){10,11,16,15}; }
    if (variant==2 && mode!=2) { info.src.box.x=12; info.src.box.width=-8; }
    if (variant==3 && mode==0) { info.dst.box.width=12; info.dst.box.height=10; }
    bool valid=variant<4;
    if (variant==4) info.src.box.z=-1;
    if (variant==5) info.dst.box.z=LAYERS;
    if (variant==6) src.allocation_size=layout&1 ? 1 : source_layer*65536u+65535u;
    if (variant==7) src.stencil_allocation_size=source_layer*65536u+65535u;
    if (variant==6 && !(mask & PIPE_MASK_Z) && !(layout&1)) valid=true;
    if (variant==7 && (!(mask & PIPE_MASK_S) || (layout&1))) valid=true;
    if (variant==8) { info.render_condition_enable=true; ctx.condition=false; }
    if (variant==9) info.src.level=16;
    if (variant==10) { dst.base.last_level=1; dst.depth_staging_size=0; }
    if (variant==11) src.base.target=PIPE_TEXTURE_3D;
    if (variant==12) { if (layout&1) src.layer_stride=SIZE_MAX; else info.src.box.width=INT_MIN; }
    if (variant==13) { if (layout&2) dst.level_offset[info.dst.level]=SIZE_MAX; else info.dst.box.x=INT_MAX; }
    if (valid) for (unsigned y=0;y<(unsigned)info.dst.box.height;++y) for (unsigned x=0;x<(unsigned)info.dst.box.width;++x) {
        unsigned dx=9+x,dy=10+y;
        if (info.scissor_enable && (dx<10 || dx>=16 || dy<11 || dy>=15)) continue;
        unsigned sx=4+(unsigned)(((2*x+1)*8)/(2*info.dst.box.width));
        unsigned sy=5+(unsigned)(((2*y+1)*6)/(2*info.dst.box.height));
        if (info.src.box.width<0) sx=15-sx;
        for (unsigned sample=0;sample<dst_samples;++sample) {
            if (mask & PIPE_MASK_Z) memcpy(expected[2]+64+pixel(&dst,info.dst.level,0,dx,dy,sample,dest_layer),
                spans[0].p+pixel(&src,info.src.level,0,sx,sy,0,source_layer),4);
            if (mask & PIPE_MASK_S) expected[layout&2 ? 2 : 3][64+pixel(&dst,info.dst.level,1,dx,dy,sample,dest_layer)]=
                spans[layout&1 ? 0 : 1].p[pixel(&src,info.src.level,1,sx,sy,0,source_layer)];
        }
    }
    struct ps5_resource before_src=src,before_dst=dst;
    if (mode==0) single_blit((struct pipe_context *)&ctx,&info);
    if (mode==1) ps5_resolve_depth_stencil_msaa4((struct pipe_context *)&ctx,&info);
    if (mode==2) ps5_replicate_depth_stencil_msaa4((struct pipe_context *)&ctx,&info);
    assert(!memcmp(&src,&before_src,sizeof(src)) && !memcmp(&dst,&before_dst,sizeof(dst)));
    for (unsigned i=0;i<4;++i) {
        if (memcmp(raw[i],expected[i],PLANE+128)) {
            fprintf(stderr,"mode=%u mask=%u layers=%u/%u variant=%u packed=%u plane=%u layout=%u level=%u\n",
                mode,mask,source_layer,dest_layer,variant,packed,i,layout,level); abort();
        }
        free(raw[i]); free(expected[i]);
    }
}
int main(void) {
    for (unsigned mode=0;mode<3;++mode) for (unsigned packed=0;packed<2;++packed)
    for (unsigned mask=1;mask<=(packed ? 3u : 1u);++mask)
    for (unsigned source=0;source<3;source+=2) for (unsigned dest=0;dest<4;dest+=3)
    for (unsigned layout=0;layout<4;++layout) for (unsigned level=0;level<(layout?2u:1u);++level)
    for (unsigned variant=0;variant<14;++variant) run(mode,mask,source,dest,variant,packed,layout,level);
}
'''
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / 'depth-blit')
    variants = {
        'fixed': code,
        'reject-array-layer': code.replace('if (level > resource->base.last_level',
                                            'if (layer || level > resource->base.last_level'),
        'missing-slice-xor': code.replace('info->src.box.z,', '0,').replace('info->dst.box.z,', '0,'),
        'reject-mip-chain': code.replace('if (resource->depth_staging_size) {',
                                          'if (resource->depth_staging_size) return false; if (resource->depth_staging_size) {'),
    }
    assert len(set(variants.values())) == 4
    for label, text in variants.items():
        subprocess.run(['cc', '-std=gnu11', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-no-pie', '-x', 'c', '-',
                        '-o', executable], input=text, text=True, check=True)
        result = subprocess.run([executable], cwd=directory, text=True, capture_output=True)
        if label == 'fixed':
            assert result.returncode == 0, result.stderr
        else:
            assert result.returncode != 0 and 'mode=' in result.stderr, (label, result.stderr)
print('PASS: actual depth blit/resolve/replicate branches, layer 0/2/3, D32/D32S8, '
      'mip levels 0/1, tiled/linear endpoints, masks, scissor, flip/scale, guards and invalid inputs; '
      'AMD-table oracle; old layer/mip rejection and missing slice XOR rejected')
