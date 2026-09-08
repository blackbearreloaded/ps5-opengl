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
#define PS5_ENABLE_PADDED_FBO_CANDIDATE 1
#define PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE 1
#define PS5_RENDER_WIDTH 1920
#define PS5_RENDER_HEIGHT 1080
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
enum { PIPE_BUFFER, PIPE_TEXTURE_2D, PIPE_TEXTURE_3D };
enum pipe_format { COLOR, PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT };
enum { PIPE_MAP_READ=1, PIPE_MAP_WRITE=2, PIPE_BIND_RENDER_TARGET=4, PIPE_BIND_DEPTH_STENCIL=8 };
struct pipe_resource { unsigned bind, target, format, width0, height0, depth0,
    array_size, last_level, nr_samples; };
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_transfer { struct pipe_resource *resource; unsigned level,usage;
    struct pipe_box box; unsigned stride,layer_stride,offset; };
struct pipe_context { int unused; };
''' + structs + r'''
static unsigned ps5_texture_format_size(unsigned f) { return f == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ? 8 : 4; }
static unsigned util_format_get_blockwidth(unsigned f) { (void)f; return 1; }
static unsigned util_format_get_blockheight(unsigned f) { (void)f; return 1; }
static unsigned ps5_texture_level_layers(const struct pipe_resource *r, unsigned l) { assert(!l); return r->array_size; }
static size_t ps5_tiled_depth_surface_size(unsigned w, unsigned h, unsigned s) { assert(s == 1); return (size_t)w*h*4; }
static size_t ps5_tiled_stencil_surface_size(unsigned w, unsigned h) { return (size_t)w*h; }
static size_t ps5_tiled_depth_offset(unsigned x, unsigned y, unsigned w) { return ((size_t)y*w+x)*4; }
static size_t ps5_tiled_stencil_offset(unsigned x, unsigned y, unsigned w) { return (size_t)y*w+x; }
static size_t ps5_tiled_color_offset(unsigned f, unsigned x, unsigned y, unsigned w) { assert(f == COLOR); return ((size_t)y*w+x)*4; }
static unsigned ps5_tiled_rgba8_width(const struct ps5_resource *r) { return r->base.width0; }
static bool ps5_linear_sampled_layout(const struct pipe_resource *r) { return !r->bind; }
static bool ps5_render_target_format(unsigned f) { return f == COLOR; }
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
''' + bounds + transfers + r'''
#undef malloc
#undef calloc
#undef free
#undef mmap
#undef munmap
static void idle(void) { assert(!heap_live && !mapped_live && maps == unmaps); }
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
    uint8_t *p=ps5_transfer_map(NULL,&r.base,0,PIPE_MAP_READ|PIPE_MAP_WRITE,&box,&t);
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
int main(void) {
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
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "transfer-staging")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-fsanitize=address,undefined",
                    "-g", "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], cwd=temporary, check=True)
