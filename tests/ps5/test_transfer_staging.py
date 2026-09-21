#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise actual transfer map/unmap under a small heap; mock layout, not ownership."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()
structs = source[source.index("struct ps5_resource {"):
                 source.index("struct ps5_vertex_elements {")]
bounds = source[source.index("static bool\nps5_map_bounds("):
                source.index("static size_t\nps5_tiled_affine_offset(")]
start = source.index("static void *\nps5_transfer_map(")
if "static bool\nps5_transfer_alloc_staging(" in source:
    start = source.index("static bool\nps5_transfer_alloc_staging(")
transfers = source[start:source.index("static void\nps5_blit_scissor_bounds(")]
# Exercise the actual color branch and mapper together; depth/MSAA blit dispatch
# is outside this regression. MSAA write maps are covered below.
blit = source[source.index("static void\nps5_blit("):
              source.index("static void\nps5_texture_subdata(")]
color_blit = (blit[:blit.index("   ps5_draw_batch_drain();")] +
              blit[blit.index("   if (!info || !info->src.resource"):])
scissor = source[source.index("static void\nps5_blit_scissor_bounds("):
                 source.index("static bool\nps5_color_view_format_compatible(")]
view_policy = source[source.index("static bool\nps5_color_view_format_compatible("):
                     source.index("static void\nps5_resolve_color_msaa4(")]
subdata = source[source.index("static void\nps5_buffer_subdata("):
                 source.index("static bool\nps5_copy_identical_image(")]
code = r'''
#define _GNU_SOURCE
#include <assert.h>
#define PIPE_MAP_UNSYNCHRONIZED (1u<<8)
#define PIPE_MAP_PERSISTENT (1u<<9)
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#define PIPE_MAX_TEXTURE_LEVELS 16
#define PS5_DIRECT_ALIGNMENT 0x4000
#define PS5_ENABLE_PADDED_FBO_CANDIDATE 1
#define PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE 1
#define PS5_RENDER_WIDTH 1920
#define PS5_RENDER_HEIGHT 1080
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
#define MIN2(a,b) ((a) < (b) ? (a) : (b))
#define CLAMP(v,lo,hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))
enum { PIPE_BUFFER, PIPE_TEXTURE_2D, PIPE_TEXTURE_3D, PIPE_TEXTURE_2D_ARRAY };
enum pipe_format { COLOR, PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT, COLOR_UINT, COLOR16, COMPRESSED };
enum { PIPE_MAP_READ=1, PIPE_MAP_WRITE=2, PIPE_BIND_RENDER_TARGET=4, PIPE_BIND_DEPTH_STENCIL=8 };
struct pipe_resource { unsigned bind, target, format, width0, height0, depth0,
    array_size, last_level, nr_samples, nr_storage_samples; };
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_transfer { struct pipe_resource *resource; unsigned level,usage;
    struct pipe_box box; unsigned stride,layer_stride,offset; };
struct pipe_context { int unused; };
struct ps5_context { int unused; };
enum { PIPE_MASK_RGBA=15, PIPE_TEX_FILTER_NEAREST, PIPE_TEX_FILTER_LINEAR };
struct pipe_blit_info {
    struct { struct pipe_resource *resource; unsigned level,format; struct pipe_box box; } src,dst;
    unsigned mask,filter,dst_sample,num_window_rectangles;
    bool sample0_only,swizzle_enable,alpha_blend,render_condition_enable,scissor_enable;
    uint8_t swizzle[4];
    struct { unsigned minx,miny,maxx,maxy; } scissor;
};
union pipe_color_union { unsigned ui[4]; float f[4]; };
''' + structs + r'''
static unsigned ps5_texture_format_size(unsigned f) { return f == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ? 8 : 4; }
static unsigned util_format_get_blockwidth(unsigned f) { return f == COMPRESSED ? 4 : 1; }
static unsigned util_format_get_blockheight(unsigned f) { (void)f; return 1; }
static unsigned util_format_get_blocksize(unsigned f) { return f == COLOR16 ? 2 : ps5_texture_format_size(f); }
static unsigned util_format_linear(unsigned f) { return f; }
static bool util_format_is_depth_or_stencil(unsigned f) { return f == PIPE_FORMAT_Z32_FLOAT || f == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT; }
static unsigned ps5_texture_level_layers(const struct pipe_resource *r, unsigned l) { assert(!l); return r->array_size; }
static size_t ps5_tiled_depth_surface_size(unsigned w, unsigned h, unsigned s) { assert(s == 1); return (size_t)w*h*4; }
static size_t ps5_tiled_stencil_surface_size(unsigned w, unsigned h) { return (size_t)w*h; }
static size_t ps5_tiled_depth_offset(unsigned x, unsigned y, unsigned w, unsigned layer) { (void)layer; return ((size_t)y*w+x)*4; }
static size_t ps5_tiled_stencil_offset(unsigned x, unsigned y, unsigned w, unsigned layer) { (void)layer; return (size_t)y*w+x; }
static size_t ps5_tiled_color_offset(unsigned f, unsigned x, unsigned y, unsigned w, unsigned layer) { assert(f == COLOR || f == COLOR_UINT); (void)layer; return ((size_t)y*w+x)*4; }
static size_t ps5_tiled_color_msaa4_offset(unsigned f, unsigned x, unsigned y, unsigned s, unsigned w, unsigned layer) { return ps5_tiled_color_offset(f,x,y,w,layer)*4+s*4; }
static bool ps5_msaa4_color_format(unsigned f) { return f == COLOR || f == COLOR_UINT; }
static unsigned ps5_tiled_rgba8_width(const struct ps5_resource *r) { return r->base.width0; }
static bool force_tiled;
static bool ps5_linear_sampled_layout(const struct pipe_resource *r) { return !force_tiled && (!r->bind || r->last_level); }
static bool ps5_render_target_format(unsigned f) { return f == COLOR || f == COLOR_UINT; }
static bool util_format_is_pure_uint(unsigned f) { return f == COLOR_UINT; }
static bool util_format_is_pure_sint(unsigned f) { (void)f; return false; }
static bool util_format_is_pure_integer(unsigned f) { return f == COLOR_UINT; }
static bool ps5_render_condition_passes(struct ps5_context *p) { (void)p; return true; }
static void util_format_unpack_rgba(unsigned f, unsigned *out, const void *in, unsigned n) {
    assert((f == COLOR || f == COLOR_UINT) && n == 1);
    union pipe_color_union c;
    for (unsigned i=0;i<4;++i) {
        if (f == COLOR_UINT) c.ui[i]=((const uint8_t *)in)[i];
        else c.f[i]=((const uint8_t *)in)[i]/255.0f;
    }
    memcpy(out,&c,sizeof(c));
}
static void util_format_pack_rgba(unsigned f, void *out, const unsigned *in, unsigned n) {
    assert((f == COLOR || f == COLOR_UINT) && n == 1);
    union pipe_color_union c; memcpy(&c,in,sizeof(c));
    for (unsigned i=0;i<4;++i) ((uint8_t *)out)[i]=f == COLOR_UINT ? (uint8_t)c.ui[i] : (uint8_t)(c.f[i]*255.0f+0.5f);
}
/* Mesa utility contract, as used by the existing MSAA resolve path. */
static void util_format_apply_color_swizzle(union pipe_color_union *dst,
    const union pipe_color_union *src, const uint8_t swizzle[4], bool integer) {
    assert(dst != src);
    for (unsigned i=0;i<4;++i) {
        assert(swizzle[i] <= 5);
        if (swizzle[i] < 4) dst->ui[i]=src->ui[swizzle[i]];
        else if (integer) dst->ui[i]=swizzle[i] == 5;
        else dst->f[i]=swizzle[i] == 5 ? 1.0f : 0.0f;
    }
}
static void ps5_flush_gpu_data(const void *p, size_t n) { assert(p && n); }
static unsigned drains;
static void ps5_draw_batch_drain_buffer(struct pipe_resource *r) { (void)r; ++drains; }
static unsigned heap_live, mapped_live, maps, unmaps;
static unsigned fail_heap;
static bool fail_map;
static void *mapped[4];
static size_t lengths[4];
static void *small_alloc(size_t n, bool zero) {
    if ((fail_heap && !--fail_heap) || n >= 65536) return NULL;
    void *p = zero ? calloc(1,n) : malloc(n);
    if (p) ++heap_live;
    return p;
}
static void small_free(void *p) {
    if (!p) return;
    for (unsigned i=0;i<4;++i) assert(p != mapped[i]);
    assert(heap_live); --heap_live; free(p);
}
static void *cpu_map(void *p, size_t n, int prot, int flags, int fd, off_t off) {
    assert(!p && n && !(n % PS5_DIRECT_ALIGNMENT));
    assert(prot == (PROT_READ|PROT_WRITE) && flags == (MAP_PRIVATE|MAP_ANONYMOUS));
    assert(fd == -1 && off == 0);
    if (fail_map) return MAP_FAILED;
    p = mmap(p,n,prot,flags,fd,off); assert(p != MAP_FAILED);
    for (unsigned i=0;i<4;++i) if (!mapped[i]) {
        mapped[i]=p; lengths[i]=n; ++maps; ++mapped_live; return p;
    }
    abort();
}
static int cpu_unmap(void *p, size_t n) {
    for (unsigned i=0;i<4;++i) if (mapped[i] == p) {
        assert(n == lengths[i] && mapped_live);
        int rc=munmap(p,n); assert(!rc); mapped[i]=NULL; --mapped_live; ++unmaps; return rc;
    }
    abort();
}
#define malloc(n) small_alloc(n,false)
#define calloc(n,s) small_alloc((n)*(s),true)
#define free small_free
#define mmap cpu_map
#define munmap cpu_unmap
''' + bounds + transfers + subdata + scissor + view_policy + color_blit + r'''
#undef malloc
#undef calloc
#undef free
#undef mmap
#undef munmap
static void idle(void) { assert(!heap_live && !mapped_live && maps == unmaps); }
static void check_reinterpreted_copy(bool tiled) {
    uint32_t input[]={0x60000000,0xffffffff,0x12345678,0x89abcdef}, output[16];
    memset(output,0xa5,sizeof(output));
    struct ps5_resource src={.base={.target=PIPE_TEXTURE_2D,.format=COLOR,
        .width0=2,.height0=2,.array_size=1,.bind=tiled ? PIPE_BIND_RENDER_TARGET : 0},
        .level_stride={8},.layer_stride=sizeof(input),.data=(uint8_t *)input,
        .size=sizeof(input),.allocation_size=sizeof(input)};
    struct ps5_resource dst={.base={.target=PIPE_TEXTURE_2D,.format=COLOR_UINT,
        .width0=4,.height0=4,.array_size=1,.bind=tiled ? PIPE_BIND_RENDER_TARGET : 0},
        .level_stride={16},.layer_stride=sizeof(output),.data=(uint8_t *)output,
        .size=sizeof(output),.allocation_size=sizeof(output)};
    struct pipe_blit_info b={.src={&src.base,0,COLOR_UINT,{0,0,0,2,2,1}},
        .dst={&dst.base,0,COLOR_UINT,{1,1,0,2,2,1}},.mask=PIPE_MASK_RGBA,.filter=PIPE_TEX_FILTER_NEAREST};
    ps5_blit(NULL,&b); idle();
    for (unsigned y=0;y<4;++y) for (unsigned x=0;x<4;++x)
        assert(output[y*4+x] == (x>=1 && x<3 && y>=1 && y<3 ? input[(y-1)*2+x-1] : 0xa5a5a5a5));
    assert(!ps5_color_view_format_compatible(COLOR,COLOR16));
    assert(!ps5_color_view_format_compatible(COLOR,COMPRESSED));
    assert(!ps5_color_view_format_compatible(COLOR,PIPE_FORMAT_Z32_FLOAT));
    assert(!ps5_color_view_format_compatible(PIPE_FORMAT_Z32_FLOAT,COLOR));
}
static void check_swizzle(unsigned filter, bool integer, bool tiled) {
    uint8_t input[2*2*4], output[6*6*4];
    for (unsigned i=0;i<sizeof(input);++i) input[i]=35+(i%4)*32;
    memset(output,0xa5,sizeof(output));
    struct ps5_resource src={.base={.target=PIPE_TEXTURE_2D,.format=integer ? COLOR_UINT : COLOR,
        .width0=2,.height0=2,.array_size=1},.level_stride={2*4},.layer_stride=sizeof(input),
        .data=input,.size=sizeof(input),.allocation_size=sizeof(input)};
    struct ps5_resource dst={.base={.target=PIPE_TEXTURE_2D,.format=src.base.format,
        .width0=6,.height0=6,.array_size=1,.bind=tiled ? PIPE_BIND_RENDER_TARGET : 0},
        .level_stride={6*4},.layer_stride=sizeof(output),.data=output,
        .size=sizeof(output),.allocation_size=sizeof(output)};
    struct pipe_blit_info b={.src={&src.base,0,src.base.format,{0,0,0,2,2,1}},
        .dst={&dst.base,0,dst.base.format,{1,1,0,4,4,1}},.mask=PIPE_MASK_RGBA,
        .filter=filter,.swizzle_enable=true,.swizzle={2,4,0,5}}; /* Z, zero, X, one */
    ps5_blit(NULL,&b); idle();
    const uint8_t expected[]={99,0,35,integer ? 1 : 255};
    for (unsigned y=0;y<6;++y) for (unsigned x=0;x<6;++x)
        for (unsigned c=0;c<4;++c)
            assert(output[(y*6+x)*4+c] ==
                (x>=1 && x<5 && y>=1 && y<5 && !(integer && filter == PIPE_TEX_FILTER_LINEAR)
                 ? expected[c] : 0xa5));
}
static void check_blit(unsigned target, unsigned src_level, unsigned dst_level, bool flip) {
    struct ps5_resource src={.base={.target=target,.format=COLOR,.width0=32,.height0=32,
        .depth0=4,.array_size=target == PIPE_TEXTURE_2D ? 1 : 4,.last_level=1,
        .bind=PIPE_BIND_RENDER_TARGET}, .layer_stride=32*32*4+16*16*4,
        .level_stride={32*4,16*4},.level_offset={0,32*32*4}};
    struct ps5_resource dst=src;
    src.size=src.allocation_size=src.layer_stride*src.base.array_size;
    dst.size=dst.allocation_size=src.size;
    src.data=malloc(src.size); dst.data=malloc(dst.size);
    uint8_t *expected=malloc(dst.size); assert(src.data && dst.data && expected);
    for (size_t i=0;i<src.size;++i) src.data[i]=(uint8_t)(i*13+i/128);
    memset(dst.data,0xa5,dst.size); memset(expected,0xa5,dst.size);
    unsigned sl=target == PIPE_TEXTURE_3D ? (4u>>src_level)-1 : src.base.array_size-1;
    unsigned dl=target == PIPE_TEXTURE_3D ? (4u>>dst_level)-1 : dst.base.array_size-1;
    struct pipe_blit_info b={
        .src={&src.base,src_level,COLOR,{flip ? 9 : 1,2,(int)sl,flip ? -8 : 8,6,1}},
        .dst={&dst.base,dst_level,COLOR,{3,5,(int)dl,8,6,1}},
        .mask=PIPE_MASK_RGBA,.filter=PIPE_TEX_FILTER_NEAREST,
        .scissor_enable=flip,.scissor={4,6,10,10}};
    ps5_blit(NULL,&b); idle();
    for (unsigned y=0;y<6;++y) for (unsigned x=0;x<8;++x) {
        if (flip && (x+3 < 4 || x+3 >= 10 || y+5 < 6 || y+5 >= 10)) continue;
        size_t si=sl*src.layer_stride+src.level_offset[src_level]+
            (y+2)*src.level_stride[src_level]+(1+(flip ? 7-x : x))*4;
        size_t di=dl*dst.layer_stride+dst.level_offset[dst_level]+(y+5)*dst.level_stride[dst_level]+(x+3)*4;
        memcpy(expected+di,src.data+si,4);
    }
    assert(!memcmp(expected,dst.data,dst.size)); /* Includes all mip/layer guards. */
    /* Invalid source/destination mips, destination extent and 3D layer must
     * leave storage untouched and release even an already-mapped source. */
    for (unsigned invalid=0;invalid<4;++invalid) {
        struct pipe_blit_info bad=b;
        if (invalid == 0) bad.src.level=2;
        if (invalid == 1) bad.dst.level=2;
        if (invalid == 2) bad.dst.box.x=32;
        if (invalid == 3) bad.dst.box.z=(int)dl+1;
        ps5_blit(NULL,&bad); idle(); assert(!memcmp(expected,dst.data,dst.size));
    }
    /* A native tiled destination still cannot address nonzero mips. */
    if (dst_level) {
        force_tiled=true; b.src.level=0; b.src.box.z=0;
        ps5_blit(NULL,&b); idle(); force_tiled=false;
        assert(!memcmp(expected,dst.data,dst.size));
    }
    free(expected); free(src.data); free(dst.data);
}
static void check_linear_halo(unsigned flip, bool inset, bool tiled, bool up) {
    uint8_t input[32*24*4], output[32*24*4];
    for (unsigned y=0;y<24;++y) for (unsigned x=0;x<32;++x)
        for (unsigned c=0;c<4;++c) input[(y*32+x)*4+c]=(uint8_t)(x*7+y*13+c*53);
    memset(output,0xa5,sizeof(output));
    struct ps5_resource src={.base={.target=PIPE_TEXTURE_2D,.format=COLOR,
        .width0=32,.height0=24,.array_size=1},.level_stride={32*4},
        .layer_stride=sizeof(input),.data=input,.size=sizeof(input),.allocation_size=sizeof(input)};
    struct ps5_resource dst=src; dst.data=output; dst.base.bind=tiled ? PIPE_BIND_RENDER_TARGET : 0;
    int x0=inset ? 3:0, y0=inset ? 5:0, sw=8, sh=6, dw=up ? 16:4, dh=up ? 12:3;
    struct pipe_blit_info b={.src={&src.base,0,COLOR,{x0+(flip&1 ? sw:0),y0+(flip&2 ? sh:0),0,
        flip&1 ? -sw:sw,flip&2 ? -sh:sh,1}},.dst={&dst.base,0,COLOR,{3,4,0,dw,dh,1}},
        .mask=PIPE_MASK_RGBA,.filter=PIPE_TEX_FILTER_LINEAR};
    ps5_blit(NULL,&b); idle();
    for (int y=0;y<24;++y) for (int x=0;x<32;++x) for (unsigned c=0;c<4;++c) {
        double expected=0xa5;
        if (x>=3 && x<3+dw && y>=4 && y<4+dh) {
            double sx=b.src.box.x+(x-3+.5)*b.src.box.width/dw-.5;
            double sy=b.src.box.y+(y-4+.5)*b.src.box.height/dh-.5;
            int ix=(int)sx-(sx<(int)sx), iy=(int)sy-(sy<(int)sy);
            double wx=sx-ix, wy=sy-iy;
            /* Full-image clamping, independent of the mapped halo rectangle. */
            int xa=CLAMP(ix,0,31), xb=CLAMP(ix+1,0,31);
            int ya=CLAMP(iy,0,23), yb=CLAMP(iy+1,0,23);
            expected=(1-wy)*((1-wx)*input[(ya*32+xa)*4+c]+wx*input[(ya*32+xb)*4+c])+
                     wy*((1-wx)*input[(yb*32+xa)*4+c]+wx*input[(yb*32+xb)*4+c]);
        }
        assert(abs((int)output[(y*32+x)*4+c]-(int)(expected+.5))<=1);
    }
}
static void check(unsigned format, unsigned width, unsigned height, unsigned layers) {
    struct ps5_resource r={.base={.target=PIPE_TEXTURE_2D,.format=format,
        .width0=width,.height0=height,.array_size=layers,
        .bind=format == COLOR ? PIPE_BIND_RENDER_TARGET : PIPE_BIND_DEPTH_STENCIL}};
    unsigned bpp=ps5_texture_format_size(format);
    r.layer_stride=(size_t)width*height*bpp; r.level_stride[0]=width*bpp;
    r.size=r.layer_stride*layers; r.allocation_size=(size_t)width*height*layers*4;
    r.stencil_allocation_size=(size_t)width*height*layers;
    r.data=malloc(r.allocation_size); r.stencil_data=malloc(r.stencil_allocation_size);
    assert(r.data && r.stencil_data);
    memset(r.data,0x3a,r.allocation_size); memset(r.stencil_data,0x2b,r.stencil_allocation_size);
    struct pipe_box box={0,0,0,(int)width,(int)height,(int)layers};
    struct pipe_transfer *t=NULL;
    unsigned before=maps, before_drains=drains;
    uint8_t *p=ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ|PIPE_MAP_WRITE|PIPE_MAP_PERSISTENT,&box,&t);
    assert(r.external_cpu_access);
    assert(p && t); /* Red before fix: full-image staging cannot fit the simulated heap. */
    assert(drains == before_drains + 1);
    assert(maps == before + (r.size >= 65536));
    assert(t->stride == width*bpp && t->layer_stride == width*height*bpp);
    for (size_t i=0;i<r.size;++i) {
        uint8_t expected=bpp == 8 && i%8 >= 4 ? (i%8 == 4 ? 0x2b : 0) : 0x3a;
        assert(p[i] == expected);
    }
    memset(p,0xc7,r.size);
    ps5_transfer_flush_region(NULL,t,&box);
    ps5_transfer_unmap(NULL,t); idle();
    assert(drains == before_drains + 3); /* Map, explicit flush, unmap boundaries. */
    for (size_t i=0;i<r.allocation_size;++i) assert(r.data[i] == 0xc7);
    if (bpp == 8) for (size_t i=0;i<r.stencil_allocation_size;++i) assert(r.stencil_data[i] == 0xc7);
    /* Allocation failures return no transfer and retain no scratch. */
    for (unsigned call=1;call <= (r.size < 65536 ? 2u : 1u);++call) {
        fail_heap=call; t=NULL;
        assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t) && !t); idle();
    }
    if (r.size >= 65536) {
        fail_map=true; t=NULL;
        assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t) && !t); idle(); fail_map=false;
    }
    /* Bounds errors discovered after allocation release either backing kind. */
    --r.allocation_size; t=NULL;
    assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t) && !t); idle(); ++r.allocation_size;
    if (bpp == 8) {
        uint8_t *stencil=r.stencil_data; r.stencil_data=NULL;
        assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t) && !t); idle(); r.stencil_data=stencil;
        /* A present but undersized stencil plane fails after scratch allocation. */
        --r.stencil_allocation_size; before=maps;
        t=(struct pipe_transfer *)&r; /* Failure must clear a stale output value. */
        assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t) && !t); idle();
        assert(maps == before + (r.size >= 65536));
        assert(r.stencil_data == stencil);
        ++r.stencil_allocation_size;
        p=ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t);
        assert(p && t);
        for (size_t i=0;i<r.size;++i) assert(p[i] == (i%8 < 5 ? 0xc7 : 0));
        ps5_transfer_unmap(NULL,t); idle();
    }
    /* Linear resources still map directly and need no staging allocation. */
    if (format == COLOR) {
        r.base.bind=0;
        assert(ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&t) == r.data);
        assert(!((struct ps5_transfer *)t)->staging);
        ps5_transfer_unmap(NULL,t); idle();
    }
    free(r.data); free(r.stencil_data);
}
static void check_streaming_waits(void) {
    uint8_t bytes[64] = {0};
    struct ps5_resource r={.base={.target=PIPE_BUFFER,.format=COLOR,.width0=64,
        .height0=1,.depth0=1,.array_size=1},.data=bytes,.size=64,.allocation_size=64,
        .level_stride={64},.layer_stride=64};
    struct pipe_box box={16,0,0,16,1,1};
    const unsigned usages[]={PIPE_MAP_WRITE, PIPE_MAP_WRITE|PIPE_MAP_UNSYNCHRONIZED,
        PIPE_MAP_READ|PIPE_MAP_UNSYNCHRONIZED,
        PIPE_MAP_READ|PIPE_MAP_WRITE|PIPE_MAP_UNSYNCHRONIZED};
    for (unsigned i=0;i<4;++i) {
        unsigned before=drains;
        struct pipe_transfer *t=NULL;
        assert(ps5_transfer_map(NULL,&r.base,0,usages[i],&box,&t)==bytes+16);
        ps5_transfer_flush_region(NULL,t,&box);
        ps5_transfer_unmap(NULL,t); idle();
        assert(drains==before+(i==1 ? 0 : 3));
    }
    unsigned before=drains;
    const uint8_t data[4]={1,2,3,4};
    ps5_buffer_subdata(NULL,&r.base,PIPE_MAP_UNSYNCHRONIZED,24,4,data);
    assert(drains==before && !memcmp(bytes+24,data,4));
    ps5_buffer_subdata(NULL,&r.base,PIPE_MAP_WRITE,28,4,data);
    assert(drains==before+1 && !memcmp(bytes+28,data,4));
    uint8_t saved[64]; memcpy(saved,bytes,64); before=drains;
    ps5_buffer_subdata(NULL,&r.base,PIPE_MAP_WRITE,63,4,data);
    assert(drains==before && !memcmp(bytes,saved,64));
    box.x=63;
    struct pipe_transfer *t=NULL;
    assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_WRITE,&box,&t) && !t);
    assert(drains==before);
    r.base.target=PIPE_TEXTURE_2D; r.base.width0=16; box.x=0; box.width=16;
    assert(ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_WRITE|PIPE_MAP_UNSYNCHRONIZED,&box,&t)==bytes);
    ps5_transfer_flush_region(NULL,t,&box);
    ps5_transfer_unmap(NULL,t); idle();
    assert(drains==before+3);
    puts("streaming-waits: PASS unsynchronized buffer writes, synchronized reads/textures and rejected bounds");
}
int main(void) {
    check_streaming_waits();
    {
        uint32_t pixels[2*4*4*4];
        memset(pixels,0xa5,sizeof(pixels));
        struct ps5_resource r={.base={.target=PIPE_TEXTURE_2D_ARRAY,.format=COLOR,
            .width0=4,.height0=4,.depth0=1,.array_size=2,.nr_samples=4,.nr_storage_samples=4,
            .bind=PIPE_BIND_RENDER_TARGET},.data=(uint8_t *)pixels,.size=sizeof(pixels),
            .allocation_size=sizeof(pixels),.layer_stride=sizeof(pixels)/2,.level_stride={16}};
        struct pipe_box box={1,1,1,2,1,1};
        struct pipe_transfer *mapped=NULL;
        uint32_t *p=ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_WRITE,&box,&mapped);
        assert(p && mapped); p[0]=0x11223344; p[1]=0x55667788;
        ps5_transfer_unmap(NULL,mapped); idle();
        for (unsigned z=0;z<2;++z) for (unsigned y=0;y<4;++y)
        for (unsigned x=0;x<4;++x) for (unsigned s=0;s<4;++s)
            assert(pixels[((z*4+y)*4+x)*4+s] ==
                (z==1 && y==1 && (x==1 || x==2) ? (x==1 ? 0x11223344u : 0x55667788u) : 0xa5a5a5a5u));
        assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ,&box,&mapped) && !mapped);
        box.width=4;
        assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_WRITE,&box,&mapped) && !mapped);
        box.width=2; r.base.nr_storage_samples=2;
        assert(!ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_WRITE,&box,&mapped) && !mapped);
        idle(); puts("transfer-msaa-init: PASS all samples, subregion/layer isolation and rejected read/bounds/sample mismatch");
    }
    check_reinterpreted_copy(false);
    check_reinterpreted_copy(true);
    puts("transfer-reinterpreted-copy: PASS raw bits, tiled/linear and format guards");
    struct ps5_transfer t={0};
    assert(!ps5_transfer_alloc_staging(&t,0));
    assert(!ps5_transfer_alloc_staging(&t,SIZE_MAX));
    assert(!ps5_transfer_alloc_staging(&t,SIZE_MAX - PS5_DIRECT_ALIGNMENT + 2));
    assert(!t.staging && !t.staging_mapping_size); idle();
    check(COLOR,1920,1080,1);
    check(COLOR,1920,1,1);
    check(COLOR,128,128,1); /* Exact allocation threshold. */
    check(PIPE_FORMAT_Z32_FLOAT,128,64,2);
    check(PIPE_FORMAT_Z32_FLOAT_S8X24_UINT,128,64,2);
    check(PIPE_FORMAT_Z32_FLOAT_S8X24_UINT,8,4,2);
    puts("transfer-staging: PASS color/depth read+write, small/direct paths, OOM and bounds cleanup");
    puts("transfer-stencil-size: PASS heap/mmap scratch cleanup, cleared output and intact backing on host retry");
    const unsigned targets[]={PIPE_TEXTURE_2D,PIPE_TEXTURE_2D_ARRAY,PIPE_TEXTURE_3D};
    for (unsigned t=0;t<3;++t) for (unsigned s=0;s<2;++s)
        for (unsigned d=0;d<2;++d) for (unsigned flip=0;flip<2;++flip)
            check_blit(targets[t],s,d,flip);
    puts("transfer-color-mips: PASS mip pairs, layers, flip/scissor, guards and invalid-map cleanup");
    for (unsigned integer=0;integer<2;++integer) for (unsigned tiled=0;tiled<2;++tiled) {
        check_swizzle(PIPE_TEX_FILTER_NEAREST,integer,tiled);
        check_swizzle(PIPE_TEX_FILTER_LINEAR,integer,tiled);
    }
    puts("transfer-color-swizzle: PASS nearest/linear, tiled/mapped, constants/permutation and integer-filter rejection");
    for (unsigned flip=0;flip<4;++flip) for (unsigned inset=0;inset<2;++inset)
        for (unsigned tiled=0;tiled<2;++tiled) for (unsigned up=0;up<2;++up)
            check_linear_halo(flip,inset,tiled,up);
    puts("transfer-linear-halo: PASS 32 actual inset/edge/up/down/flip cases, both destination layouts and untouched borders");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "transfer-staging")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-fsanitize=address,undefined",
                    "-g", "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], cwd=temporary, check=True)
