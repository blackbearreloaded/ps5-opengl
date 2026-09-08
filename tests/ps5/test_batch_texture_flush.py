#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise the actual batch cache with and without fragment-texture reuse."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
code = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
start = code.index('struct ps5_batch_flush_cache {')
end = code.index('\n}\n', code.index('ps5_flush_batch_backing(', start)) + 3
helper = code[start:end]
assert 'bool hazard = !base || base->target != PIPE_BUFFER;' in code
assert 'memset(&ps5_deferred, 0, sizeof(ps5_deferred));' in code
assert 'slot == 1 && !merged_geometry ? flush_cache : NULL, 2 + unit' in code
assert 'user_data_count, vertex_metadata, NULL)' in code
assert 'context->sampler_views[1][unit]->texture);' in code
harness = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#define PS5_MAX_TEXTURE_UNITS 16
static unsigned flushes;
static void ps5_flush_gpu_data(const void *data, size_t bytes) {
    (void)data; (void)bytes; ++flushes;
}
''' + helper + r'''
int main(void) {
    char textures[PS5_MAX_TEXTURE_UNITS][64] = {{0}};
    struct ps5_batch_flush_cache cache = {0};
    for (unsigned draw = 0; draw < 200; ++draw)
        for (unsigned unit = 0; unit < PS5_MAX_TEXTURE_UNITS; ++unit)
            ps5_flush_batch_backing(&cache, 2 + unit, textures[unit], 64);
#ifdef PS5_GPU_PRESENT_BATCH
    assert(flushes == PS5_MAX_TEXTURE_UNITS);
#else
    assert(flushes == 200 * PS5_MAX_TEXTURE_UNITS);
#endif
    unsigned before = flushes;
    /* Replacement/resize, depth planes and a new batch all require a flush. */
    ps5_flush_batch_backing(&cache, 2, textures[0], 32);
    ps5_flush_batch_backing(&cache, 2, textures[1], 32);
    ps5_flush_batch_backing(&cache, 0, textures[0], 64);
    ps5_flush_batch_backing(&cache, 1, textures[0], 64);
    memset(&cache, 0, sizeof(cache));
    ps5_flush_batch_backing(&cache, 2, textures[1], 32);
    ps5_flush_batch_backing(NULL, 2, textures[1], 32);
    ps5_flush_batch_backing(NULL, 2, textures[1], 32);
    assert(flushes == before + 7);
    /* Missing backing/empty flush cannot consume a remembered entry. */
    before = flushes;
    ps5_flush_batch_backing(&cache, 2, NULL, 32);
    ps5_flush_batch_backing(&cache, 2, textures[1], 0);
    ps5_flush_batch_backing(&cache, 2, textures[1], 32);
    assert(flushes == before + 3);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    test, executable = Path(temp) / 'cache.c', Path(temp) / 'cache'
    test.write_text(harness)
    for defines in ([], ['-DPS5_GPU_PRESENT_BATCH=1']):
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', *defines, str(test),
                        '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print('PASS: all texture slots, backing/size replacement, depth isolation, reset and uncached paths')
