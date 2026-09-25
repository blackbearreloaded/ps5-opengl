#!/usr/bin/env python3
"""Exercise actual snapshot function: recycled storage, fallbacks and failures.

Host ownership/copy check only; native vertex-fetch pixels remain required.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
start = source.index('static bool\nps5_batch_copy_descriptors(')
helper = source[start:source.index('\n}', start) + 2]
# The skipped prefix must still be cleared before texture preparation/enqueue.
constant = source[source.index('ps5_prepare_constant(struct'):source.index('ps5_tessellation_buffer_layout(')]
assert 'if (!expected_ubo_count)\n      return true;' in constant
assert 'if (!storage_fs)\n      memset(storage->data, 0, PS5_CONSTANT_DATA_OFFSET);' in constant
draw = source[source.index('ps5_draw_vbo_locked(struct'):source.index('ps5_multidraw_eligible(struct')]
for shader in ('vs', 'fs'):
    assert draw.index(f'ps5_prepare_constant(context, context->{shader},') < draw.index(f'ps5_prepare_texture(context, context->{shader},')
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PS5_NATIVE_TITLE_RUNTIME 1
#define PS5_DEFERRED_DRAW_BATCH 1
#define PS5_DIRECT_ALIGNMENT 16384
#define PS5_CONSTANT_DATA_OFFSET 4096
#define PIPE_BUFFER 1
#define MAX2(a,b) ((a)>(b)?(a):(b))
struct pipe_resource { unsigned target; size_t width0; };
struct pipe_context { int unused; };
struct nir { struct { unsigned num_ubos, num_ssbos, num_images; } info; };
struct ps5_shader { struct nir *nir; };
static bool ps5_shader_uses_storage(const struct ps5_shader *s) {
 return s && (s->nir->info.num_ssbos || s->nir->info.num_images);
}
struct ps5_context {
 struct pipe_context base;
 void *gs, *tcs, *tes;
 unsigned stream_output_target_count;
 struct ps5_shader *vs, *fs;
};
struct ps5_resource { struct pipe_resource base; unsigned char *data; size_t size; };
static unsigned char data[6][32768];
static struct ps5_resource resources[6];
static int fail_stage = -1;
static size_t copied;
static size_t ps5_descriptor_snapshot_size(const struct ps5_context *c, unsigned s) {
 (void)c; return s ? 24576 : 6144; // include private inline constants
}
static struct pipe_resource *ps5_descriptor_take(struct pipe_context *c, unsigned s,
                                                const struct pipe_resource *t) {
 (void)c;
 if ((int)s == fail_stage) return NULL;
 resources[s+3].base = *t; resources[s+3].size = t->width0;
 return &resources[s+3].base;
}
static void *counted_copy(void *dst, const void *src, size_t n) {
 copied += n; return memcpy(dst,src,n);
}
#define memcpy counted_copy
''' + helper + r'''
#undef memcpy
int main(void) {
 struct ps5_context c = {0};
 struct nir nir[2] = {0};
 struct ps5_shader shaders[2] = {{&nir[0]}, {&nir[1]}};
 struct pipe_resource *saved[3], *storage[3];
 for (unsigned i=0;i<6;++i) {
  resources[i]=(struct ps5_resource){{PIPE_BUFFER,32768}, data[i],32768};
  if(i<3) saved[i]=&resources[i].base;
 }
 resources[0].base.width0=resources[0].size=16384;
 // Reused slots retain poison, never borrowed source tables. All complex stage
 // combinations must preserve the full previous snapshot behavior.
 for(unsigned trial=0;trial<10000;++trial) {
  unsigned mask=trial%16;
  c.gs=mask&1?&c:NULL; c.tcs=mask&2?&c:NULL; c.tes=mask&4?&c:NULL;
  c.stream_output_target_count=!!(mask&8);
  c.vs=trial%7 ? &shaders[0] : NULL;
  c.fs=trial%11 ? &shaders[1] : NULL;
  for(unsigned i=0;i<2;++i) {
   nir[i].info.num_ubos=(trial/(16u<<i))%3;
   nir[i].info.num_ssbos=trial%5==i;
   nir[i].info.num_images=trial%13==i;
  }
  for(unsigned i=0;i<3;++i) {
   memset(data[i], (trial+i)%127, sizeof(data[i]));
   memset(data[i+3],0xa5,sizeof(data[i+3])); storage[i]=NULL;
  }
  copied=0;
  assert(ps5_batch_copy_descriptors(&c.base,saved,storage));
  size_t expected_bytes=6144+24576+(mask?16384:0);
  for(unsigned i=0;i<2;++i) {
   struct ps5_shader *shader=i?c.fs:c.vs;
   size_t n=i?24576:6144;
   bool rebuild=!(mask&7) && shader && nir[i].info.num_ubos &&
                !nir[i].info.num_ssbos && !nir[i].info.num_images;
   if(rebuild) {
    expected_bytes-=4096;
    for(unsigned j=0;j<4096;++j) assert(data[i+4][j]==0xa5);
    // Old copied table and new private table become identical at the existing
    // constant-preparation clear; inline constant bytes must already match.
    assert(!memcmp(data[i+1]+4096,data[i+4]+4096,n-4096));
    memset(data[i+1],0,4096); memset(data[i+4],0,4096);
   }
   assert(!memcmp(data[i+1],data[i+4],n));
  }
  assert(copied==expected_bytes);
  assert(data[4][6144]==0xa5 && data[5][24576]==0xa5);
  if(mask) assert(!memcmp(data[0],data[3],16384));
  else for(unsigned j=0;j<16384;++j) assert(data[3][j]==0xa5);
  for(unsigned i=0;i<3;++i) assert(storage[i]!=saved[i]);
 }
 for(fail_stage=0;fail_stage<3;++fail_stage)
  assert(!ps5_batch_copy_descriptors(&c.base,saved,storage));
 fail_stage=-1;
 resources[0].base.target=0;
 assert(!ps5_batch_copy_descriptors(&c.base,saved,storage));
 resources[0].base.target=PIPE_BUFFER;
 resources[2].size=16384; // inline constants exceed source allocation
 assert(!ps5_batch_copy_descriptors(&c.base,saved,storage));
 puts("PASS actual snapshot: 10000 recycled-slot cases, 16 stage combinations, allocation/type/bounds failures");
}
'''
with tempfile.TemporaryDirectory() as directory:
    exe = str(Path(directory) / 'check')
    subprocess.run(['clang-18', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-x', 'c', '-', '-o', exe], input=code, text=True, check=True)
    subprocess.run([exe], check=True, timeout=30)
