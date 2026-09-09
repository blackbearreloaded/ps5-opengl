#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Actual CPU depth transfer/staging/clear callers, checked against AMD tables.

Gallium types and cache/queue operations are mocked. No GPU emulation or hardware
acceptance; allocations, map/unmap, addressing and copy/clear code are real.
"""
from pathlib import Path
import argparse
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
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "util/u_memset.h"
#define PIPE_MAX_TEXTURE_LEVELS 16
#define PS5_DIRECT_ALIGNMENT 0x4000
#define PS5_ENABLE_PADDED_FBO_CANDIDATE 1
#define PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE 1
#define PS5_RENDER_WIDTH 1920
#define PS5_RENDER_HEIGHT 1080
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
#define MIN2(a,b) ((a) < (b) ? (a) : (b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define BITFIELD_BIT(i) (1u << (i))
enum { PIPE_BUFFER, PIPE_TEXTURE_2D, PIPE_TEXTURE_3D, PIPE_TEXTURE_2D_ARRAY };
enum pipe_format { PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT };
enum { PIPE_MAP_READ=1, PIPE_MAP_WRITE=2, PIPE_BIND_RENDER_TARGET=4,
       PIPE_BIND_DEPTH_STENCIL=8, PIPE_CLEAR_DEPTH=16, PIPE_CLEAR_STENCIL=32 };
struct pipe_resource { unsigned bind,target,format,width0,height0,depth0,
    array_size,last_level,nr_samples; };
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_transfer { struct pipe_resource *resource; unsigned level,usage;
    struct pipe_box box; unsigned stride,layer_stride,offset; };
struct pipe_surface { struct pipe_resource *texture; unsigned level,first_layer,last_layer; };
struct pipe_context { int unused; };
struct pipe_scissor_state { unsigned minx,miny,maxx,maxy; };
struct ps5_context { struct { struct pipe_surface zsbuf; } framebuffer; bool framebuffer_valid; };
''' + source[source.index('struct ps5_resource {'):source.index('struct ps5_vertex_elements {')] + r'''
static unsigned ps5_texture_format_size(unsigned f) { return f == PIPE_FORMAT_Z32_FLOAT ? 4 : 8; }
static unsigned util_format_get_blockwidth(unsigned f) { (void)f; return 1; }
static unsigned util_format_get_blockheight(unsigned f) { (void)f; return 1; }
/* Any unexpected color dispatch fails; this test covers only depth/stencil. */
static bool ps5_linear_sampled_layout(const struct pipe_resource *r) { (void)r; abort(); }
static bool ps5_render_target_format(unsigned f) { (void)f; abort(); }
static size_t ps5_tiled_color_offset(unsigned f,unsigned x,unsigned y,unsigned w) {
    (void)f; (void)x; (void)y; (void)w; abort();
}
static unsigned drains, flush_count;
static struct pipe_resource *expected_drain;
static struct { const void *p; size_t n; } flushes[128];
static void ps5_draw_batch_drain_buffer(struct pipe_resource *r) { assert(r && r==expected_drain); ++drains; }
static void ps5_flush_gpu_data(const void *p,size_t n) {
    assert(p && n && flush_count < ARRAY_SIZE(flushes));
    flushes[flush_count].p=p; flushes[flush_count++].n=n;
}
static void check_flush(unsigned i,const void *p,size_t n) {
    assert(i<flush_count && flushes[i].p==p && flushes[i].n==n);
}
'''
for name in ('ps5_float_bits', 'ps5_tiled_depth_layer_xor', 'ps5_tiled_surface_size',
             'ps5_tiled_stencil_surface_size', 'ps5_tiled_stencil_surface_size_samples',
             'ps5_tiled_rgba8_msaa4_surface_size', 'ps5_tiled_depth_surface_size',
             'ps5_texture_level_layers', 'ps5_map_bounds', 'ps5_tiled_affine_offset',
             'ps5_tiled_depth_offset', 'ps5_tiled_depth_msaa4_offset',
             'ps5_tiled_stencil_offset', 'ps5_tiled_stencil_msaa4_offset',
             'ps5_tiled_rgba8_width', 'ps5_surface_width', 'ps5_surface_height',
             'ps5_surface_layer_count', 'ps5_stage_depth_surface',
             'ps5_transfer_alloc_staging', 'ps5_transfer_free_staging',
             'ps5_transfer_map', 'ps5_transfer_unmap', 'ps5_clear_bounds',
             'ps5_clear_depth_stencil'):
    code += function(name)
for samples, bpe, name, tile in ((1,4,'depth',128), (1,1,'stencil',256),
                                (4,4,'depth_msaa4',64), (4,1,'stencil_msaa4',128)):
    code += reference_function(samples, bpe, name, tile)

code += r'''
#define LAYERS 17
struct guarded { uint8_t *raw,*p,*expected; size_t n; };
static struct guarded allocate(size_t n) {
    struct guarded g={.n=n};
    g.raw=malloc(n+128); g.expected=malloc(n+128); assert(g.raw && g.expected);
    g.p=g.raw+64; memset(g.raw,0xa5,n+128); memcpy(g.expected,g.raw,n+128); return g;
}
static void same(struct guarded *g) { assert(!memcmp(g->raw,g->expected,g->n+128)); }
static void release(struct guarded *g) { same(g); free(g->raw); free(g->expected); }
static uint32_t value_at(unsigned x,unsigned y,unsigned layer) {
    return 0x3e000000u | ((x*71+y*359+layer*977)&0x7fffff);
}
static uint8_t stencil_at(unsigned x,unsigned y,unsigned layer) { return x*17+y*31+layer*13; }
static void put32(uint8_t *p,uint32_t value) { memcpy(p,&value,4); }
static uint32_t get32(const uint8_t *p) { uint32_t value; memcpy(&value,p,4); return value; }
/* Independent 64 KiB tile extents; never derive fixture bounds from the driver. */
static size_t tile_bytes(unsigned w,unsigned h,unsigned tile) {
    return ((size_t)(w+tile-1)/tile)*((h+tile-1)/tile)*65536;
}
static struct ps5_resource resource(unsigned format,unsigned w,unsigned h,unsigned samples) {
    struct ps5_resource r={.base={.target=PIPE_TEXTURE_2D_ARRAY,.format=format,
        .width0=w,.height0=h,.depth0=1,.array_size=LAYERS,.nr_samples=samples,
        .bind=PIPE_BIND_DEPTH_STENCIL}};
    r.level_stride[0]=w*ps5_texture_format_size(format);
    r.layer_stride=(size_t)r.level_stride[0]*h; r.size=r.layer_stride*LAYERS;
    size_t ds=tile_bytes(w,h,samples==4 ? 64 : 128),ss=tile_bytes(w,h,samples==4 ? 128 : 256);
    assert(ps5_tiled_depth_surface_size(w,h,samples)==ds);
    assert(ps5_tiled_stencil_surface_size_samples(w,h,samples)==ss);
    r.allocation_size=ds*LAYERS; r.stencil_allocation_size=ss*LAYERS;
    return r;
}
static void check_map(unsigned format,unsigned w,unsigned h,unsigned first) {
    struct ps5_resource r=resource(format,w,h,1);
    expected_drain=&r.base;
    const bool packed=format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
    struct guarded d=allocate(r.allocation_size), s=allocate(r.stencil_allocation_size);
    r.data=d.p; r.stencil_data=s.p;
    size_t ds=r.allocation_size/LAYERS, ss=r.stencil_allocation_size/LAYERS;
    unsigned bpe=ps5_texture_format_size(format);
    struct pipe_box box={w>128 ? (int)w-4 : 2,h>128 ? (int)h-5 : 3,(int)first,4,5,2};
    if (w>256 && first==1) box=(struct pipe_box){0,0,(int)first,(int)w,(int)h,2};
    for (unsigned z=first;z<first+2;++z)
        for (unsigned y=box.y;y<(unsigned)(box.y+box.height);++y)
            for (unsigned x=box.x;x<(unsigned)(box.x+box.width);++x) {
                size_t di=z*ds+reference_depth(x,y,0,w,z), si=z*ss+reference_stencil(x,y,0,w,z);
                put32(d.p+di,value_at(x,y,z)); s.p[si]=stencil_at(x,y,z);
            }
    memcpy(d.expected,d.raw,d.n+128); memcpy(s.expected,s.raw,s.n+128);
    struct pipe_transfer *t=NULL;
    drains=flush_count=0;
    uint8_t *p=ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ|PIPE_MAP_WRITE,&box,&t);
    assert(p && t && drains==1 && flush_count==(packed ? 2u : 1u));
    assert(t->stride==(unsigned)box.width*bpe && t->layer_stride==t->stride*(unsigned)box.height);
    assert(!!((struct ps5_transfer *)t)->staging_mapping_size == (t->layer_stride*2>=65536));
    check_flush(0,r.data,d.n);
    if (packed) check_flush(1,r.stencil_data,s.n);
    for (unsigned z=0;z<2;++z) for (unsigned y=0;y<(unsigned)box.height;++y)
        for (unsigned x=0;x<(unsigned)box.width;++x) {
            unsigned xx=x+box.x, yy=y+box.y, zz=z+first;
            uint8_t *pixel=p+z*t->layer_stride+y*t->stride+x*bpe;
            uint32_t value=value_at(xx,yy,zz); assert(get32(pixel)==value);
            value^=0x1555; put32(pixel,value);
            put32(d.expected+64+zz*ds+reference_depth(xx,yy,0,w,zz),value);
            if (packed) {
                assert(pixel[4]==stencil_at(xx,yy,zz) && !pixel[5] && !pixel[6] && !pixel[7]);
                pixel[4]^=0x5a; memset(pixel+5,0x7b,3);
                s.expected[64+zz*ss+reference_stencil(xx,yy,0,w,zz)]=pixel[4];
            }
        }
    ps5_transfer_unmap(NULL,t); assert(drains==2 && flush_count==(packed ? 4u : 2u));
    check_flush(packed ? 2 : 1,r.data,d.n);
    if (packed) check_flush(3,r.stencil_data,s.n);
    same(&d); same(&s);
    /* Read-only maps must preserve all backing and padding. */
    flush_count=0;
    p=ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t); assert(p && t);
    memset(p,0,t->layer_stride*2); ps5_transfer_unmap(NULL,t); same(&d); same(&s);
    assert(flush_count==(packed ? 2u : 1u)); check_flush(0,r.data,d.n);
    if (packed) check_flush(1,r.stencil_data,s.n);
    for (unsigned bad=0;bad<5;++bad) {
        struct pipe_box b=box;
        if (bad==0) b.x=-1;
        if (bad==1) b.z=LAYERS;
        if (bad==2) b.depth=0;
        if (bad==4) r.base.nr_samples=4; /* MSAA direct mapping is forbidden. */
        t=(struct pipe_transfer *)&r;
        assert(!ps5_transfer_map(NULL,&r.base,bad==3 ? 1 : 0,PIPE_MAP_READ,&b,&t) && !t);
        r.base.nr_samples=1; same(&d); same(&s);
    }
    r.allocation_size=(first+1)*ds;
    assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t) && !t);
    r.allocation_size=d.n;
    if (packed) {
        r.stencil_allocation_size=(first+2)*ss-1;
        assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t) && !t);
        r.stencil_allocation_size=s.n;
    }
    release(&s); release(&d);
}
static void clear_flushes(struct ps5_resource *r,unsigned first,size_t ds,size_t ss,bool packed) {
    assert(flush_count==(packed ? 4u : 2u));
    for (unsigned i=0;i<2;++i) {
        check_flush(i,r->data+(first+i)*ds,ds);
        if (packed) check_flush(2+i,r->stencil_data+(first+i)*ss,ss);
    }
}
static void check_clear(unsigned format,unsigned samples,unsigned first) {
    const unsigned w=257,h=259;
    struct ps5_resource r=resource(format,w,h,samples);
    bool packed=format==PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
    struct guarded d=allocate(r.allocation_size),s=allocate(r.stencil_allocation_size);
    r.data=d.p; r.stencil_data=s.p;
    size_t ds=r.allocation_size/LAYERS,ss=r.stencil_allocation_size/LAYERS;
    struct ps5_context c={.framebuffer={.zsbuf={&r.base,0,first,first+1}},.framebuffer_valid=true};
    unsigned buffers=PIPE_CLEAR_DEPTH | (packed ? PIPE_CLEAR_STENCIL : 0);
    flush_count=0;
    assert(ps5_clear_depth_stencil(&c,buffers,0x0f,NULL,.75,0x1a));
    clear_flushes(&r,first,ds,ss,packed);
    for (unsigned z=first;z<first+2;++z) {
        for (size_t i=0;i<ds;i+=4) put32(d.expected+64+z*ds+i,0x3f400000u);
        if (packed) memset(s.expected+64+z*ss,0xaa,ss);
    }
    same(&d); same(&s);
    struct pipe_scissor_state sc={63,127,131,259};
    flush_count=0;
    assert(ps5_clear_depth_stencil(&c,buffers,0xf0,&sc,.25,0x5f));
    clear_flushes(&r,first,ds,ss,packed);
    for (unsigned z=first;z<first+2;++z) for (unsigned y=sc.miny;y<sc.maxy;++y)
        for (unsigned x=sc.minx;x<sc.maxx;++x) for (unsigned q=0;q<samples;++q) {
            size_t di=samples==4 ? reference_depth_msaa4(x,y,q,w,z) : reference_depth(x,y,q,w,z);
            size_t si=samples==4 ? reference_stencil_msaa4(x,y,q,w,z) : reference_stencil(x,y,q,w,z);
            put32(d.expected+64+z*ds+di,0x3e800000u);
            if (packed) s.expected[64+z*ss+si]=0x5a;
        }
    same(&d); same(&s);
    /* Empty scissor and upfront invalid layer/depth leave every byte unchanged. */
    sc=(struct pipe_scissor_state){w+3,h+3,w+4,h+4}; flush_count=0;
    assert(ps5_clear_depth_stencil(&c,buffers,0xff,&sc,.5,0)); same(&d); same(&s);
    clear_flushes(&r,first,ds,ss,packed); flush_count=0;
    c.framebuffer.zsbuf.last_layer=LAYERS;
    assert(!ps5_clear_depth_stencil(&c,buffers,0xff,NULL,.5,0));
    c.framebuffer.zsbuf.last_layer=first+1;
    assert(!ps5_clear_depth_stencil(&c,buffers,0xff,NULL,NAN,0));
    r.allocation_size=(first+2)*ds-1;
    assert(!ps5_clear_depth_stencil(&c,buffers,0xff,NULL,.5,0));
    r.allocation_size=d.n;
    if (packed) {
        r.stencil_allocation_size=(first+2)*ss-1;
        assert(!ps5_clear_depth_stencil(&c,buffers,0xff,NULL,.5,0));
        r.stencil_allocation_size=s.n;
    }
    assert(!flush_count);
    release(&s); release(&d);
}
static void check_mip(unsigned format,unsigned level,unsigned first) {
    const unsigned bpe=ps5_texture_format_size(format), w=257>>level,h=259>>level;
    const bool packed=format==PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
    struct ps5_resource r=resource(format,257,259,1); r.base.last_level=2;
    r.layer_stride=0;
    for (unsigned l=0;l<3;++l) {
        r.level_offset[l]=r.layer_stride; r.level_stride[l]=(257>>l)*bpe+8;
        r.layer_stride+=(size_t)r.level_stride[l]*(259>>l)+32;
    }
    r.size=r.layer_stride*LAYERS;
    size_t ds=tile_bytes(w,h,128),ss=tile_bytes(w,h,256);
    assert(ps5_tiled_depth_surface_size(w,h,1)==ds && ps5_tiled_stencil_surface_size(w,h)==ss);
    r.depth_staging_offset=r.size+64; r.depth_staging_size=ds*LAYERS;
    r.allocation_size=r.depth_staging_offset+r.depth_staging_size;
    r.stencil_allocation_size=ss*LAYERS;
    struct guarded d=allocate(r.allocation_size),s=allocate(r.stencil_allocation_size);
    r.data=d.p; r.stencil_data=s.p;
    struct pipe_surface surface={&r.base,level,first,first+1};
    for (unsigned z=first;z<first+2;++z) for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        size_t at=z*r.layer_stride+r.level_offset[level]+y*r.level_stride[level]+x*bpe;
        put32(d.p+at,value_at(x,y,z)); if (packed) d.p[at+4]=stencil_at(x,y,z);
    }
    memcpy(d.expected,d.raw,d.n+128);
    flush_count=0;
    assert(ps5_stage_depth_surface(&surface,true));
    assert(flush_count==(packed ? 2u : 1u));
    check_flush(0,d.p+r.depth_staging_offset,ds*LAYERS);
    if (packed) check_flush(1,s.p,s.n);
    for (unsigned z=first;z<first+2;++z) for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        size_t di=r.depth_staging_offset+z*ds+reference_depth(x,y,0,w,z);
        size_t si=z*ss+reference_stencil(x,y,0,w,z);
        put32(d.expected+64+di,value_at(x,y,z)); if (packed) s.expected[64+si]=stencil_at(x,y,z);
    }
    same(&d); same(&s);
    /* Simulate GPU writes using the independent address table, then copy back. */
    for (unsigned z=first;z<first+2;++z) for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        size_t di=r.depth_staging_offset+z*ds+reference_depth(x,y,0,w,z),si=z*ss+reference_stencil(x,y,0,w,z);
        size_t at=z*r.layer_stride+r.level_offset[level]+y*r.level_stride[level]+x*bpe;
        uint32_t value=value_at(x,y,z)^0x1777;
        put32(d.p+di,value); put32(d.expected+64+di,value); put32(d.expected+64+at,value);
        if (packed) {
            uint8_t st=stencil_at(x,y,z)^0x53;
            s.p[si]=s.expected[64+si]=st; d.expected[64+at+4]=st;
        }
    }
    flush_count=0; assert(ps5_stage_depth_surface(&surface,false));
    assert(flush_count==(packed ? 3u : 2u)); same(&d); same(&s);
    check_flush(0,d.p+r.depth_staging_offset,ds*LAYERS);
    if (packed) check_flush(1,s.p,s.n);
    check_flush(packed ? 2 : 1,d.p,r.size);
    /* Real mip maps alias the linear storage, not the tiled staging allocation. */
    struct pipe_box box={3,5,(int)first,13,9,2}; struct pipe_transfer *t=NULL;
    size_t offset=first*r.layer_stride+r.level_offset[level]+5*r.level_stride[level]+3*bpe;
    drains=flush_count=0; expected_drain=&r.base;
    uint8_t *p=ps5_transfer_map(NULL,&r.base,level,PIPE_MAP_READ|PIPE_MAP_WRITE,&box,&t);
    assert(p==d.p+offset && t && drains==1 && !flush_count);
    assert(t->offset==offset && t->stride==r.level_stride[level] && t->layer_stride==r.layer_stride);
    assert(!((struct ps5_transfer *)t)->staging);
    for (unsigned z=0;z<2;++z) for (unsigned y=0;y<9;++y) for (unsigned x=0;x<13;++x) {
        size_t relative=z*r.layer_stride+y*r.level_stride[level]+x*bpe;
        assert(get32(p+relative)==(value_at(x+3,y+5,z+first)^0x1777));
        uint32_t value=get32(p+relative)^0x37;
        put32(p+relative,value); put32(d.expected+64+offset+relative,value);
        if (packed) {
            assert(p[relative+4]==(uint8_t)(stencil_at(x+3,y+5,z+first)^0x53));
            p[relative+4]^=0x29; d.expected[64+offset+relative+4]=p[relative+4];
        }
    }
    ps5_transfer_unmap(NULL,t); assert(drains==2 && !flush_count); same(&d); same(&s);
    p=ps5_transfer_map(NULL,&r.base,level,PIPE_MAP_READ,&box,&t);
    assert(p==d.p+offset && t); ps5_transfer_unmap(NULL,t);
    assert(drains==4 && !flush_count); same(&d); same(&s);
    if (!packed) {
        struct ps5_context c={.framebuffer={.zsbuf=surface},.framebuffer_valid=true};
        struct pipe_scissor_state sc={3,5,13,15}; flush_count=0;
        assert(ps5_clear_depth_stencil(&c,PIPE_CLEAR_DEPTH,0,&sc,.5,0));
        assert(flush_count==1 && flushes[0].p==d.p && flushes[0].n==r.size);
        for (unsigned z=first;z<first+2;++z) for (unsigned y=5;y<15;++y) for (unsigned x=3;x<13;++x)
            put32(d.expected+64+z*r.layer_stride+r.level_offset[level]+y*r.level_stride[level]+x*4,0x3f000000u);
        same(&d);
    }
    /* Upfront malformed-capacity rejection must not partially stage any bytes. */
    for (unsigned bad=0;bad<(packed ? 4u : 3u);++bad) {
        size_t original_offset=r.depth_staging_offset,original_size=r.depth_staging_size;
        if (bad==0) r.depth_staging_offset=r.allocation_size;
        if (bad==1) r.depth_staging_size++;
        if (bad==2) r.depth_staging_size=(first+2)*ds-1;
        if (bad==3) r.stencil_allocation_size=(first+2)*ss-1;
        flush_count=0;
        assert(!ps5_stage_depth_surface(&surface,true) && !flush_count); same(&d); same(&s);
        r.depth_staging_offset=original_offset; r.depth_staging_size=original_size;
        r.stencil_allocation_size=s.n;
    }
    surface.last_layer=LAYERS;
    assert(!ps5_stage_depth_surface(&surface,true));
    surface.last_layer=first+1; surface.level=3;
    assert(!ps5_stage_depth_surface(&surface,false));
    release(&s); release(&d);
}
int main(void) {
    const unsigned first[]={1,3,7,15};
    for (unsigned f=0;f<2;++f) for (unsigned i=0;i<ARRAY_SIZE(first);++i) {
        check_map(f,37,39,first[i]); check_map(f,129,131,first[i]); check_map(f,257,259,first[i]);
        check_clear(f,1,first[i]); check_clear(f,4,first[i]);
        check_mip(f,1,first[i]); check_mip(f,2,first[i]);
    }
    puts("PASS: actual depth/stencil transfers, mip staging, full/masked/scissored clears; nonzero layers, 1x/4x, guards and rejected mappings");
}
'''
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fault-checks', action='store_true', help='also reject five deliberately broken production helpers')
args = parser.parse_args()
variants = [('baseline', code)]
if args.fault_checks:
    for name, helper, old, new in (
        ('wrong-tile-size', 'ps5_tiled_surface_size', '0x10000', '0x20000'),
        ('wrong-clear-bits', 'ps5_float_bits', 'return bits;', 'return bits ^ 1u;'),
        ('short-clear-flush', 'ps5_clear_depth_stencil',
         'ps5_flush_gpu_data(layer_data, depth_layer_size);', 'ps5_flush_gpu_data(layer_data, 1);'),
        ('wrong-mip-offset', 'ps5_map_bounds', 'resource->level_offset[level]', 'resource->level_offset[0]'),
        ('missing-staging-guard', 'ps5_stage_depth_surface',
         'resource->depth_staging_size >\n          resource->allocation_size - resource->depth_staging_offset', 'false'),
    ):
        body = function(helper)
        assert body.count(old) == 1 and code.count(body) == 1
        variants.append((name, code.replace(body, body.replace(old, new))))
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / 'depth-subresources')
    for name, candidate in variants:
        subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-DHAVE_ENDIAN_H=1', '-I' + str(root / 'third_party/mesa-26.2.0/src'),
                        '-fsanitize=address,undefined', '-g', '-fno-pie', '-no-pie',
                        '-x', 'c', '-o', executable, '-'], input=candidate, text=True, check=True)
        if name == 'baseline':
            subprocess.run([executable], check=True)
        else:
            result = subprocess.run([executable], cwd=directory, capture_output=True, text=True)
            assert result.returncode != 0 and 'Assertion' in result.stderr, name
            print('PASS: rejected', name)
