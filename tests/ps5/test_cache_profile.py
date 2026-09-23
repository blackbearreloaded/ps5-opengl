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
