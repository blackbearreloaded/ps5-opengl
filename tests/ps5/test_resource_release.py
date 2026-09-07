#!/usr/bin/env python3
"""Actual resource destruction must not release still-mapped GPU backing."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()
helper = ""
if "static void\nps5_release_resource_memory(" in source:
    start = source.index("static void\nps5_release_resource_memory(")
    helper = source[start:source.index("static struct pipe_resource *\nps5_resource_create_unlocked", start)]
start = source.index("static void\nps5_resource_destroy(")
destroy = source[start:source.index("static bool\nps5_is_format_supported(", start)]
code = ("#define CHECK_PARTIALS\n" if helper else "") + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
enum { PIPE_FORMAT_Z32_FLOAT=1 };
struct pipe_resource { unsigned format; };
struct pipe_screen { int unused; };
struct ps5_resource {
    struct pipe_resource base;
    struct pipe_resource *render_pool_owner;
    unsigned render_arena_first_slot, render_arena_slot_count;
    void *data, *stencil_data;
    int64_t direct_start, stencil_direct_start;
    size_t allocation_size, stencil_allocation_size;
};
struct ps5_screen {
    struct pipe_screen base;
    unsigned resource_mutex;
    struct pipe_resource *render_pool;
    uint64_t render_arena_bitmap[2];
};
static struct ps5_screen screen;
static struct ps5_resource resource;
static struct pipe_resource parent;
static char storage[2];
static bool mapped[2], allocated[2], freed, stopped;
static unsigned locked, marked, unrefs, fault, fault_index;
static jmp_buf stop;
static void simple_mtx_lock(unsigned *p) { assert(p==&screen.resource_mutex && !locked); locked=1; }
static void simple_mtx_unlock(unsigned *p) { assert(p==&screen.resource_mutex && locked); locked=0; }
static void ps5_render_arena_mark(struct ps5_screen *s, unsigned first, unsigned count, bool used) {
    assert(s==&screen && locked && first==2 && count==3 && !used); ++marked;
}
static void pipe_resource_reference(struct pipe_resource **p, void *q) {
    assert(!locked && freed && *p==&parent && !q); ++unrefs; *p=NULL;
}
static int mock_unmap(void *p, size_t n) {
    unsigned i=p==&storage[0]?0:1;
    assert(!locked && p==&storage[i] && mapped[i] && allocated[i] && n==16384u*(i+1));
    if (fault==1 && fault_index==i) return -1;
    mapped[i]=false; return 0;
}
static int32_t sceKernelReleaseDirectMemory(int64_t direct, size_t n) {
    unsigned i=(unsigned)direct;
    assert(!locked && i<2 && !mapped[i] && allocated[i] && n==16384u*(i+1));
    if (fault==2 && fault_index==i) return -5;
    allocated[i]=false; return 0;
}
static void mock_free(void *p) { assert(p==&resource && !locked && !freed); freed=true; }
static void mock_exit(int rc) { assert(rc==EXIT_FAILURE); stopped=true; longjmp(stop,1); }
#define munmap mock_unmap
#define free mock_free
#define _Exit mock_exit
''' + helper + destroy + r'''
static void run(unsigned style, unsigned failure, unsigned index) {
    memset(&screen,0,sizeof(screen)); memset(&resource,0,sizeof(resource));
    memset(screen.render_arena_bitmap,0xff,sizeof(screen.render_arena_bitmap));
    mapped[0]=allocated[0]=true; mapped[1]=allocated[1]=style==1;
    freed=stopped=false; marked=unrefs=0; fault=failure; fault_index=index;
    resource.base.format=PIPE_FORMAT_Z32_FLOAT;
    resource.data=&storage[0]; resource.direct_start=0; resource.allocation_size=16384;
    resource.stencil_direct_start=-1;
    if (style==1) {
        resource.stencil_data=&storage[1]; resource.stencil_direct_start=1;
        resource.stencil_allocation_size=32768;
    }
    if (style==2) {
        resource.render_pool_owner=&parent; resource.render_arena_first_slot=2;
        resource.render_arena_slot_count=3; resource.direct_start=-1;
    }
    if (style==3) screen.render_pool=&resource.base;
    if (!setjmp(stop)) ps5_resource_destroy(&screen.base,&resource.base);
    assert(!locked && stopped==(failure!=0));
    if (failure) {
        assert(!freed && allocated[index] && mapped[index]==(failure==1));
        if (index==1) assert(mapped[0] && allocated[0]);
    } else {
        assert(freed && !mapped[1] && !allocated[1]);
        assert(mapped[0]==(style==2) && allocated[0]==(style==2));
        assert(marked==(style==2) && unrefs==(style==2));
        if (style==3) assert(!screen.render_pool && !screen.render_arena_bitmap[0] && !screen.render_arena_bitmap[1]);
    }
}
int main(void) {
    for (unsigned style=0;style<4;++style) run(style,0,0);
    for (unsigned failure=1;failure<=2;++failure) {
        run(0,failure,0); run(1,failure,0); run(1,failure,1);
    }
#ifdef CHECK_PARTIALS
    /* Failed map / untouched allocation cleanup shares this helper. */
    fault=0; allocated[0]=true; mapped[0]=false;
    ps5_release_resource_memory(NULL,0,-1);
    ps5_release_resource_memory(NULL,16384,0);
    assert(!allocated[0]);
#endif
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "release"
    for defines in ([], ["-DPS5_PUBLIC_STENCIL_TEST"]):
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                        *defines, "-x", "c", "-", "-o", str(exe)], input=code, text=True, check=True)
        subprocess.run([str(exe)], check=True)
create = source[source.index("ps5_resource_create_unlocked("):source.index("ps5_resource_info(")]
assert "munmap(" not in create + destroy and "sceKernelReleaseDirectMemory(" not in create + destroy
print("PASS: resource release order, failed-unmap retention, fail-stop, split stencil and arena ownership")
