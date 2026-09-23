"""Check diagnostic wrappers preserve one call and one argument evaluation."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
code = r'''
#include <assert.h>
#define PS5_DRAW_PROFILE 1
#include "src/platform/ps5_cache_profile.h"
static unsigned invoked;
static void flush(const void *p, size_t n) { assert(p && n == 8); ++invoked; }
#define flush(p, n) PS5_CACHE_MEASURE(flush, p, n)
int main(void) {
    char memory[16]; size_t n = 8; char *p = memory;
    flush(p++, n++);
    assert(invoked == 1 && p == memory + 1 && n == 9 && ps5_cache_total == 1);
    uint64_t calls = 0, bytes = 0;
    for (unsigned i = 0; i < 16384; ++i) {
        calls += ps5_cache_sites[i].calls; bytes += ps5_cache_sites[i].bytes;
    }
    assert(calls == 1 && bytes == 8);
    ps5_cache_record(16384, 17, ps5_cache_clock());
    assert(ps5_cache_sites[0].calls == 1 && ps5_cache_sites[0].bytes == 17);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    exe = str(Path(temporary) / 'cache-profile')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                    '-x', 'c', '-', '-o', exe], input=code, text=True, check=True)
    subprocess.run([exe], check=True)
print('PASS: cache attribution preserves flush and arguments; line overflow is bounded')

source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
backing = source[source.index('struct ps5_batch_flush_cache {'):
                 source.index('static void\nps5_flush_texture_backing(')]
code = r'''
#include <assert.h>
#include <string.h>
#define PS5_DRAW_PROFILE 1
#define PS5_GPU_PRESENT_BATCH 1
#define PS5_MAX_TEXTURE_UNITS 16
#define PIPE_MAX_ATTRIBS 16
#include "src/platform/ps5_cache_profile.h"
static unsigned invoked;
static void ps5_flush_gpu_data(const void *p, size_t n) { assert(p && n==64); ++invoked; }
#define ps5_flush_gpu_data(p,n) PS5_CACHE_MEASURE(ps5_flush_gpu_data,p,n)
''' + backing + r'''
int main(void) {
    char data[64];
    const unsigned slots[]={0,2,18,34};
    for (unsigned i=0;i<4;++i) {
        struct ps5_batch_flush_cache batch={0};
        ps5_flush_batch_backing(&batch,slots[i],data,sizeof(data));
        ps5_flush_batch_backing(&batch,slots[i],data,sizeof(data));
        ps5_flush_batch_backing(0,slots[i],data,sizeof(data));
    }
    assert(invoked==8 && ps5_cache_total==8);
    unsigned sites=0;
    for (unsigned i=0;i<16384;++i) if (ps5_cache_sites[i].calls) {
        assert(ps5_cache_sites[i].calls==1 && ps5_cache_sites[i].bytes==64); ++sites;
    }
    assert(sites==8);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    exe = str(Path(temporary) / 'cache-kinds')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                    '-x', 'c', '-', '-o', exe], input=code, text=True, check=True)
    subprocess.run([exe], check=True)
print('PASS: eight backing categories preserve exact flushes and retained interval reuse')
