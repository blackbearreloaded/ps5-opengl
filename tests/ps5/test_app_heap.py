#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check the existing app-heap wrappers, not the platform allocator implementation."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / "native-app/app_heap.c").read_text()
wrappers = source[source.index("void *__real_malloc(size_t size);"):]
code = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
static unsigned maps, unmaps, creates, real_calls[6], owned_calls[6];
static int mode, allocation_failure;
static void *pool;
static size_t used;
static void *test_map(void *p,size_t n,int prot,int flags,int fd,off_t offset) {
    assert(!p && n == 128u*1024u*1024u && prot == (PROT_READ|PROT_WRITE));
    assert(flags == (MAP_PRIVATE|MAP_ANON) && fd == -1 && offset == 0);
    ++maps;
    if (mode == 1) return MAP_FAILED;
    pool=mmap(p,n,prot,flags,fd,offset); assert(pool != MAP_FAILED); return pool;
}
static int test_unmap(void *p,size_t n) { assert(p == pool); ++unmaps; return munmap(p,n); }
#define mmap test_map
#define munmap test_unmap
''' + wrappers + r'''
#undef mmap
#undef munmap
void *__real_malloc(size_t n) { ++real_calls[0]; return malloc(n); }
void *__real_calloc(size_t n,size_t s) { ++real_calls[1]; return calloc(n,s); }
void *__real_realloc(void *p,size_t n) { ++real_calls[2]; return realloc(p,n); }
void __real_free(void *p) { ++real_calls[3]; assert(!ps5_heap_owns(p)); free(p); }
int __real_posix_memalign(void **p,size_t a,size_t n) { ++real_calls[4]; return posix_memalign(p,a,n); }
size_t __real_malloc_usable_size(const void *p) { assert(p && !ps5_heap_owns(p)); ++real_calls[5]; return 16; }
void *sceLibcMspaceCreate(const char *name,void *base,size_t n,unsigned flags) {
    assert(!strcmp(name,"PS5-OpenGL") && base == pool && n == PS5_OPENGL_HEAP_SIZE && !flags);
    ++creates;
    /* Reentrant allocation during initialization must stay on the real heap. */
    void *p=__wrap_malloc(7); assert(p && !ps5_heap_owns(p)); __wrap_free(p);
    return mode == 2 ? NULL : base;
}
static void *allocate(size_t n,size_t alignment) {
    if (allocation_failure || n > PS5_OPENGL_HEAP_SIZE) return NULL;
    used=(used+sizeof(size_t)+alignment-1)&~(alignment-1);
    assert(used <= PS5_OPENGL_HEAP_SIZE-n);
    void *p=(char *)pool+used; used += n ? n : 1;
    memcpy((char *)p-sizeof(size_t),&n,sizeof(n));
    return p;
}
void *sceLibcMspaceMalloc(void *space,size_t n) { assert(space == pool); ++owned_calls[0]; return allocate(n,16); }
void *sceLibcMspaceCalloc(void *space,size_t n,size_t s) {
    assert(space == pool); ++owned_calls[1];
    if (s && n > SIZE_MAX/s) return NULL;
    void *p=allocate(n*s,16); if (p) memset(p,0,n*s); return p;
}
void *sceLibcMspaceRealloc(void *space,void *p,size_t n) {
    assert(space == pool && ps5_heap_owns(p)); ++owned_calls[2];
    if (allocation_failure || !n) return NULL;
    void *q=allocate(n,16);
    if (q) {
        size_t before=sceLibcMspaceMallocUsableSize(p);
        memcpy(q,p,before<n ? before : n);
    }
    return q;
}
void sceLibcMspaceFree(void *space,void *p) { assert(space == pool && ps5_heap_owns(p)); ++owned_calls[3]; }
int sceLibcMspacePosixMemalign(void *space,void **p,size_t a,size_t n) {
    assert(space == pool); ++owned_calls[4];
    if (a < sizeof(void *) || (a&(a-1))) return EINVAL;
    void *q=allocate(n,a); if (!q) return ENOMEM; *p=q; return 0;
}
size_t sceLibcMspaceMallocUsableSize(const void *p) {
    assert(ps5_heap_owns(p)); ++owned_calls[5];
    size_t n; memcpy(&n,(const char *)p-sizeof(size_t),sizeof(n)); return n;
}
int main(int argc,char **argv) {
    assert(argc == 2); mode=atoi(argv[1]);
    void *p=__wrap_malloc(16); assert(p);
    assert(ps5_heap_owns(p) == (mode == 0));
    assert(maps == 1 && creates == (mode != 1) && unmaps == (mode == 2));
    memset(p,0xa5,16);
    void *q=__wrap_realloc(p,32); assert(q);
    if (!mode) {
        assert(q != p && atomic_load(&ps5_heap_live_bytes) == 32);
        for (unsigned i=0;i<16;++i) assert(((unsigned char *)q)[i] == 0xa5);
        q=__wrap_realloc(q,8); assert(q && atomic_load(&ps5_heap_live_bytes) == 8);
    }
    __wrap_free(q);
    q=__wrap_calloc(4,4); assert(q);
    for (unsigned i=0;i<16;++i) assert(((unsigned char *)q)[i] == 0);
    __wrap_free(q);
    assert(!__wrap_posix_memalign(&q,64,16) && !((uintptr_t)q%64));
    assert(__wrap_malloc_usable_size(q) == 16); __wrap_free(q);
    q=__wrap_realloc(NULL,8); assert(q); __wrap_free(q); __wrap_free(NULL);
    /* Foreign allocations keep their original realloc/free/usable-size owner. */
    p=__real_malloc(16); assert(p && !ps5_heap_owns(p));
    assert(__wrap_malloc_usable_size(p) == 16);
    p=__wrap_realloc(p,32); assert(p); __wrap_free(p);
    if (!mode) {
        assert(!atomic_load(&ps5_heap_live_bytes) && !atomic_load(&ps5_heap_blocks));
        assert(atomic_load(&ps5_heap_peak_bytes) == 32);
        for (unsigned i=0;i<6;++i) assert(owned_calls[i]);
        unsigned real_malloc=real_calls[0];
        p=__wrap_malloc(16); assert(p); allocation_failure=1;
        assert(!__wrap_malloc(16) && !__wrap_realloc(p,32));
        q=(void *)(uintptr_t)1;
        assert(__wrap_posix_memalign(&q,64,16) == ENOMEM && q == (void *)(uintptr_t)1);
        assert(real_calls[0] == real_malloc); /* No cross-heap fallback on owned-heap OOM. */
        assert(atomic_load(&ps5_heap_live_bytes) == 16 && atomic_load(&ps5_heap_blocks) == 1);
        assert(atomic_load(&ps5_heap_failures) == 3);
        __wrap_free(p);
        assert(!atomic_load(&ps5_heap_live_bytes) && !atomic_load(&ps5_heap_blocks));
        allocation_failure=0;
        p=__wrap_malloc(16); assert(p && !__wrap_realloc(p,0));
        assert(atomic_load(&ps5_heap_ambiguous_zero_reallocs) == 1);
        __wrap_free(p); /* This mock's realloc(p,0) leaves p alive. */
        ps5_opengl_heap_snapshot("host",0);
        assert(!ps5_heap_owns(NULL));
        assert(!ps5_heap_owns((void *)((uintptr_t)pool-1)));
        assert(!ps5_heap_owns((void *)((uintptr_t)pool+PS5_OPENGL_HEAP_SIZE)));
        assert(munmap(pool,PS5_OPENGL_HEAP_SIZE) == 0); /* Host-only teardown. */
    } else {
        for (unsigned i=0;i<6;++i) assert(!owned_calls[i] && real_calls[i]);
        assert(atomic_load(&ps5_heap_state) == -1 && !ps5_heap_base);
        assert(!atomic_load(&ps5_heap_live_bytes) && !atomic_load(&ps5_heap_blocks));
    }
    assert(maps == 1); /* Failure is not repeatedly retried by every allocation. */
    puts("app-heap: PASS wrapper routing, initialization/reentrancy/failure, owned and foreign allocations");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "app-heap")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-g", "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    for mode in (0, 1, 2):
        subprocess.run([executable, str(mode)], cwd=temporary, check=True)
