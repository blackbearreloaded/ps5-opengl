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
start = code.index('static void\nps5_flush_texture_backing(')
publication = code[start:code.index('\n}\n', start)+3]
assert 'buffer->texture_publication_epoch = 0;' in code
assert '++ps5_texture_publication_epoch;' in code
assert code.count('resource->external_cpu_access = true;') == 3
assert 'ps5_deferred_batch_overlaps(&ps5_deferred, buffer)' in code
assert '&ps5_inflight[(ps5_inflight_head + i) % PS5_INFLIGHT_BATCH_CAPACITY], buffer)' in code
assert 'memset(batch, 0, sizeof(*batch));' in code
assert 'slot == 1 && !merged_geometry ? flush_cache : NULL, 2 + unit' in code
assert 'flush_cache, 2 + PS5_MAX_TEXTURE_UNITS + binding' in code
assert 'flush_cache, 2 + PS5_MAX_TEXTURE_UNITS + PIPE_MAX_ATTRIBS' in code
assert 'user_data_count, vertex_metadata, NULL)' in code
assert 'context->sampler_views[1][unit]->texture);' in code
assert code.count('#ifdef AGC_RUNTIME_DIAGNOSTICS') >= 3
assert '#ifdef AGC_RUNTIME_DIAGNOSTICS\n      if (sampler->compare_mode)' in code
assert '#ifdef AGC_RUNTIME_DIAGNOSTICS\n      if (info->index_size == 2' in code
# Exercise the actual tiled dispatch, including array/MSAA allocation extents.
start = code.index('         /* Batch eligibility excludes attachment aliases')
tiled_dispatch = code[start:code.index('\n      } else {', start)]
harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define PS5_MAX_TEXTURE_UNITS 16
#define PIPE_MAX_ATTRIBS 16
static unsigned flushes;
static void ps5_flush_gpu_data(const void *data, size_t bytes) {
    (void)data; (void)bytes; ++flushes;
}
''' + helper + r'''
enum { PIPE_TEXTURE_2D=2, PIPE_TEXTURE_2D_ARRAY=7 };
#define PIPE_BUFFER 0
#define PIPE_BIND_DISPLAY_TARGET 8
static uint64_t ps5_texture_publication_epoch=1;
struct ps5_resource { struct { unsigned target,bind; } base; void *data; size_t allocation_size,depth_staging_size,texture_published_bytes; uint64_t texture_publication_epoch; int external_cpu_access; };
''' + publication + r'''
static void tiled(struct ps5_batch_flush_cache *flush_cache, unsigned slot,
                  unsigned unit, int merged_geometry, int multisampled,
                  int tiled_depth_target, struct ps5_resource *texture, size_t tiled_size) {
''' + tiled_dispatch + r'''
}
static void tiled_checks(void) {
    char backing[256];
    struct ps5_resource texture={.base={.target=PIPE_TEXTURE_2D},.data=backing,.allocation_size=sizeof(backing)};
    for (unsigned array=0; array<2; ++array)
        for (unsigned msaa=0; msaa<2; ++msaa)
            for (unsigned depth=0; depth<2; ++depth) {
                struct ps5_batch_flush_cache cache={0};
                texture.texture_publication_epoch=0;
                texture.base.target=array ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
                unsigned before=flushes;
                for (unsigned draw=0; draw<100; ++draw)
                    tiled(&cache,1,0,0,msaa,depth,&texture,64);
#ifdef PS5_GPU_PRESENT_BATCH
                assert(flushes==before+1);
                assert(cache.size[2]==(!msaa && !array ? 64u : depth || array ? 256u : 64u));
#else
                assert(flushes==before+1);
#endif
                before=flushes;
                tiled(&cache,0,0,0,msaa,depth,&texture,64);
                tiled(&cache,1,0,1,msaa,depth,&texture,64);
                assert(flushes==before+2); /* Vertex/merged stages remain uncached. */
            }
    struct ps5_batch_flush_cache cache={0};
    texture.texture_publication_epoch=0;
    unsigned before=flushes;
    ps5_flush_texture_backing(&cache,2,&texture,64);
    memset(&cache,0,sizeof(cache));
    ps5_flush_texture_backing(&cache,2,&texture,64);
    assert(flushes==before+1); /* A fresh batch can reuse unchanged publication. */
    texture.texture_publication_epoch=0;
    memset(&cache,0,sizeof(cache));
    ps5_flush_texture_backing(&cache,2,&texture,64);
    assert(flushes==before+2); /* CPU resource access invalidates it. */
    ++ps5_texture_publication_epoch;
    memset(&cache,0,sizeof(cache));
    ps5_flush_texture_backing(&cache,2,&texture,64);
    assert(flushes==before+3); /* A full drain invalidates every texture. */
    ps5_flush_texture_backing(&cache,2,&texture,128);
    assert(flushes==before+4); /* Larger extent must be published. */
    for(unsigned exclusion=0;exclusion<4;++exclusion) {
        texture.external_cpu_access=exclusion==0;
        texture.base.target=exclusion==1 ? PIPE_BUFFER : PIPE_TEXTURE_2D;
        texture.base.bind=exclusion==2 ? PIPE_BIND_DISPLAY_TARGET : 0;
        texture.depth_staging_size=exclusion==3;
        before=flushes;
        for(unsigned batch=0;batch<2;++batch) {
            memset(&cache,0,sizeof(cache));
            ps5_flush_texture_backing(&cache,2,&texture,64);
        }
        assert(flushes==before+2);
    }
    flushes=0;
}
int main(void) {
    tiled_checks();
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
    ps5_flush_batch_backing(&cache, 2 + PS5_MAX_TEXTURE_UNITS, textures[0], 64);
    ps5_flush_batch_backing(&cache,
        2 + PS5_MAX_TEXTURE_UNITS + PIPE_MAX_ATTRIBS, textures[0], 64);
    memset(&cache, 0, sizeof(cache));
    ps5_flush_batch_backing(&cache, 2, textures[1], 32);
    ps5_flush_batch_backing(NULL, 2, textures[1], 32);
    ps5_flush_batch_backing(NULL, 2, textures[1], 32);
    assert(flushes == before + 9);
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
