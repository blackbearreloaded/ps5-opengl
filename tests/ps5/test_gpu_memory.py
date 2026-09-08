#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise actual opt-in wrappers, including failures, reuse and table overflow."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
code = (root / 'native-app/gpu_memory.c').read_text() + r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
static int fail_alloc, fail_map, fail_release, fail_unmap;
static unsigned calls[4];
static int64_t next_physical;
static uintptr_t next_mapping = 0x10000000;
int32_t __real_sceKernelAllocateDirectMemory(int64_t first, int64_t last,
    size_t bytes, size_t alignment, int type, int64_t *physical) {
    assert(first == 0 && last == 0x100000000 && bytes == 16384 && alignment == 16384 && type == 3);
    ++calls[0]; if (fail_alloc) return -12;
    *physical = next_physical; next_physical += bytes; return 0;
}
int32_t __real_sceKernelMapDirectMemory(void **address, size_t bytes, int protection,
    int flags, int64_t physical, size_t alignment) {
    assert(bytes == 16384 && protection == 0x33 && flags == 0 && physical >= 0 && alignment == 16384);
    ++calls[1]; if (fail_map) return -22;
    *address = (void *)next_mapping; next_mapping += bytes; return 0;
}
int32_t __real_sceKernelReleaseDirectMemory(int64_t physical, size_t bytes) {
    assert(physical >= 0 && bytes == 16384); ++calls[2]; return fail_release ? -5 : 0;
}
int __real_munmap(void *address, size_t bytes) {
    assert(address && bytes); ++calls[3];
    if (fail_unmap) { errno = EIO; return -1; } return 0;
}
static int allocate(int64_t *physical) {
    return __wrap_sceKernelAllocateDirectMemory(0, 0x100000000, 16384, 16384, 3, physical);
}
static int map(void **address, int64_t physical) {
    return __wrap_sceKernelMapDirectMemory(address, 16384, 0x33, 0, physical, 16384);
}
static void baseline(void) {
    assert(!totals[DIRECT].bytes && !totals[DIRECT].count);
    assert(!totals[MAPPING].bytes && !totals[MAPPING].count && !invalid);
}
static void *worker(void *unused) {
    (void)unused;
    for (unsigned i=0; i<100; ++i) {
        int64_t physical; void *address = NULL;
        assert(!allocate(&physical) && !map(&address, physical));
        assert(!__wrap_munmap(address,16384));
        assert(!__wrap_sceKernelReleaseDirectMemory(physical,16384));
    }
    return NULL;
}
int main(void) {
    extern void snapshot_probe(void);
    snapshot_probe(); /* A second translation unit's weak default must be overridden. */
    pss_opengl_gpu_snapshot("begin",0);
    int64_t physical = -1; void *address = NULL;
    assert(!allocate(&physical) && physical == 0); /* Physical offset zero is valid. */
    assert(totals[DIRECT].bytes == 16384 && totals[DIRECT].count == 1);
    assert(!map(&address,physical) && totals[MAPPING].bytes == 16384);
    fail_unmap=1; errno=0;
    assert(__wrap_munmap(address,16384) == -1 && errno == EIO);
    assert(totals[MAPPING].bytes == 16384 && failures == 1);
    fail_unmap=0; assert(!__wrap_munmap(address,16384));
    fail_release=1;
    assert(__wrap_sceKernelReleaseDirectMemory(physical,16384) == -5);
    assert(totals[DIRECT].bytes == 16384 && failures == 2);
    fail_release=0; assert(!__wrap_sceKernelReleaseDirectMemory(physical,16384)); baseline();
    fail_alloc=1; physical=-1;
    assert(allocate(&physical) == -12 && physical == -1); baseline();
    fail_alloc=0; assert(!allocate(&physical)); fail_map=1; address=NULL;
    assert(map(&address,physical) == -22 && !address && !totals[MAPPING].bytes);
    assert(!__wrap_sceKernelReleaseDirectMemory(physical,16384)); baseline(); fail_map=0;
    assert(failures == 4);
    /* Unrelated CPU mappings do not pollute GPU evidence, even on failure. */
    assert(!__wrap_munmap((void *)0x10,16)); fail_unmap=1;
    assert(__wrap_munmap((void *)0x10,16) == -1); fail_unmap=0;
    assert(failures == 4); baseline();
    for (unsigned i=0; i<3; ++i) {
        next_physical=0; next_mapping=0x10000000;
        worker(NULL); baseline(); /* Reused virtual and physical addresses. */
    }
    pthread_t threads[4];
    for (unsigned i=0; i<4; ++i) assert(!pthread_create(&threads[i],NULL,worker,NULL));
    for (unsigned i=0; i<4; ++i) assert(!pthread_join(threads[i],NULL));
    baseline();
    assert(totals[DIRECT].peak >= 16384 && totals[MAPPING].peak >= 16384);
    for (unsigned i=0; i<4; ++i) assert(calls[i]);
    pss_opengl_gpu_snapshot("end",0);
    /* Partial unmaps and unknown direct releases invalidate evidence. */
    assert(!allocate(&physical) && !map(&address,physical));
    assert(!__wrap_munmap(address,8192) && invalid == 1);
    assert(totals[MAPPING].bytes == 16384);
    assert(!__wrap_munmap(address,16384));
    assert(!__wrap_sceKernelReleaseDirectMemory(physical,16384));
    assert(!__wrap_sceKernelReleaseDirectMemory(physical,16384) && invalid == 2);
    /* Reset only the host ledger for an independent overflow check. */
    memset(entries,0,sizeof(entries)); memset(totals,0,sizeof(totals)); invalid=failures=0;
    const unsigned capacity = sizeof(entries)/sizeof(entries[0]);
    for (unsigned i=0; i<=capacity; ++i) assert(!allocate(&physical));
    assert(totals[DIRECT].count == capacity && invalid == 1);
    printf("PASS: GPU allocation/map failures, release retention, address reuse, threads, bounded accounting\n");
}
'''
with tempfile.TemporaryDirectory() as temp:
    exe = Path(temp) / 'gpu-memory'
    heap = (root / 'native-app/app_heap.c').read_text()
    start = heap.index('__attribute__((weak)) void pss_opengl_gpu_snapshot(')
    stub = heap[start:heap.index('\n}', start) + 2]
    weak = Path(temp) / 'weak.c'
    weak.write_text(stub + '\nvoid snapshot_probe(void) { pss_opengl_gpu_snapshot("link",123); }\n')
    subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-pthread', '-fsanitize=address,undefined', str(weak), '-x', 'c', '-',
                    '-o', str(exe)], input=code, text=True, check=True)
    result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
    assert '[pss-opengl-gpu-memory] phase=link sample=123 ' in result.stdout
    print(result.stdout, end='')
for builder in ('build-native-test-app.sh', 'build-native-cts-app.sh'):
    text = (root / 'tools' / builder).read_text()
    assert 'if [[ ${PS5_GPU_MEMORY_PROFILE:-0} == 1 ]]; then' in text
    for symbol in ('sceKernelAllocateDirectMemory', 'sceKernelMapDirectMemory',
                   'sceKernelReleaseDirectMemory', 'munmap'):
        assert f'--wrap={symbol} ' in text
