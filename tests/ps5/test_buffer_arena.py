#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise the real arena allocator and resource-creation eligibility on host."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()
start = source.index("static bool\nps5_render_arena_range_is_free(")
allocator = source[start:source.index("static void\nps5_release_resource_memory(", start)]
start = source.index("   if (PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE && ps5->render_pool &&")
eligibility = source[start:source.index("   direct_limit = sceKernelGetDirectMemorySize();", start)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE 1
#define PS5_RENDER_ARENA_SLOT_BYTES 64u
#define PS5_RENDER_ARENA_SLOT_COUNT 16u
#define PS5_RENDER_ARENA_OFFSET 256u
#define PS5_RENDER_POOL_BYTES (PS5_RENDER_ARENA_OFFSET + 1024u)
enum { PIPE_BUFFER, PIPE_TEXTURE_1D, PIPE_TEXTURE_2D, PIPE_TEXTURE_3D,
       PIPE_TEXTURE_CUBE, PIPE_TEXTURE_2D_ARRAY };
enum { PIPE_BIND_DISPLAY_TARGET=1, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT=10 };
struct pipe_resource { unsigned target, bind, format; };
struct ps5_resource {
    struct pipe_resource base;
    unsigned char *data;
    size_t allocation_size;
    struct pipe_resource *render_pool_owner;
    unsigned render_arena_first_slot, render_arena_slot_count;
};
struct ps5_screen {
    struct pipe_resource *render_pool;
    uint64_t render_arena_bitmap[1];
};
static unsigned references;
static void pipe_resource_reference(struct pipe_resource **dst, struct pipe_resource *src) {
    assert(!*dst && src); *dst=src; ++references;
}
''' + allocator + r'''
static bool try_arena(struct ps5_screen *ps5, struct ps5_resource *resource,
                      const struct pipe_resource *templ,
                      size_t allocation_size, size_t allocation_alignment) {
''' + eligibility + r'''
    return false; /* The original caller continues into checked direct allocation. */
primary_ready:
    return true;
}
int main(void) {
    unsigned char bytes[PS5_RENDER_POOL_BYTES];
    memset(bytes,0xa5,sizeof(bytes));
    struct ps5_resource pool={.data=bytes};
    struct ps5_screen screen={.render_pool=&pool.base};
    struct pipe_resource templ={.target=PIPE_BUFFER};
    struct ps5_resource buffers[16]={{0}};
    /* Textures cannot occupy even one arena slot. */
    for (unsigned target=PIPE_TEXTURE_1D; target<=PIPE_TEXTURE_2D_ARRAY; ++target) {
        templ.target=target;
        assert(!try_arena(&screen,&buffers[0],&templ,64,64));
        assert(!references && !screen.render_arena_bitmap[0]);
    }
    templ.target=PIPE_BUFFER;
    templ.bind=PIPE_BIND_DISPLAY_TARGET;
    assert(!try_arena(&screen,&buffers[0],&templ,64,64));
    templ.bind=0; templ.format=PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
    assert(!try_arena(&screen,&buffers[0],&templ,64,64));
    templ.format=0;
    for (unsigned i=0; i<16; ++i) {
        assert(try_arena(&screen,&buffers[i],&templ,64,64));
        assert(buffers[i].data==bytes+PS5_RENDER_ARENA_OFFSET+i*64);
        assert(buffers[i].render_pool_owner==&pool.base);
        assert(buffers[i].render_arena_first_slot==i && buffers[i].render_arena_slot_count==1);
        for (unsigned j=0; j<64; ++j) assert(!buffers[i].data[j]);
    }
    assert(references==16);
    for (unsigned i=0; i<PS5_RENDER_ARENA_OFFSET; ++i) assert(bytes[i]==0xa5);
    struct ps5_resource extra={0};
    assert(!try_arena(&screen,&extra,&templ,64,64));
    assert(!extra.data && !extra.render_pool_owner && references==16);
    /* Free a fragment, honor alignment and clear reused storage. */
    ps5_render_arena_mark(&screen,3,4,false);
    memset(bytes+PS5_RENDER_ARENA_OFFSET+3*64,0xcc,4*64);
    assert(try_arena(&screen,&extra,&templ,128,128));
    assert(extra.render_arena_first_slot==4 && extra.render_arena_slot_count==2);
    for (unsigned i=0; i<128; ++i) assert(!extra.data[i]);
    assert(bytes[PS5_RENDER_ARENA_OFFSET+3*64]==0xcc);
    struct ps5_resource bad={0};
    assert(!ps5_render_arena_allocate(&screen,&bad,0,64));
    assert(!ps5_render_arena_allocate(&screen,&bad,65,64));
    assert(!ps5_render_arena_allocate(&screen,&bad,1088,64));
    assert(!ps5_render_arena_allocate(&screen,&bad,64,0));
    assert(!ps5_render_arena_allocate(&screen,&bad,64,65));
    screen.render_pool=NULL;
    assert(!try_arena(&screen,&bad,&templ,64,64));
    assert(!ps5_render_arena_allocate(&screen,&bad,64,64));
    screen.render_pool=&pool.base; pool.data=NULL;
    assert(!try_arena(&screen,&bad,&templ,64,64));
    assert(!bad.data && !bad.render_pool_owner);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "arena"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-missing-braces", "-fsanitize=address,undefined",
                    "-x", "c", "-", "-o", str(exe)], input=code, text=True, check=True)
    subprocess.run([str(exe)], check=True)
print("buffer-arena: PASS buffer-only policy, alignment, exhaustion/fallback, ownership and zeroed reuse")
