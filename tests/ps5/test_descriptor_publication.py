#!/usr/bin/env python3
"""Actual descriptor-publication helper: visibility, fallback and draw ordering."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
s = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
a = s.index('static void\nps5_publish_descriptor_prefix(')
helper = s[a:s.index('\n}', a)+2]
a = s.index('ps5_draw_vbo_locked(struct pipe_context *base,')
b = s.index('\n}', a)
draw = s[a:b]
# Integration guard: each table's last coalesced writer precedes its publication.
for slot, stage in [(0, 'vs'), (1, 'fs')]:
    constant = draw.index(f'ps5_prepare_constant(context, context->{stage},')
    texture = draw.index(f'ps5_prepare_texture(context, context->{stage},', constant)
    publish = draw.index(f'if (descriptor_bytes[{slot}])', texture)
    assert constant < texture < publish < draw.rindex('ps5_agc_gate2_run(')
assert 'context->gs ? NULL : &descriptor_bytes[0]' in draw
assert 'vertex_metadata, NULL, NULL)' in draw  # tessellation stays immediate
assert 'ps5_flush_gpu_data((void *)data_address, state->size);' in s
assert 'ps5_flush_texture_backing(' in s
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MAX2(a,b) ((a)>(b)?(a):(b))
static unsigned char cpu[65536], gpu[65536], expected[65536];
static unsigned calls;
static size_t bytes_flushed;
static void ps5_flush_gpu_data(const void *p, size_t bytes) {
 assert(p == cpu && bytes <= sizeof(cpu));
 if (!bytes) return;
 ++calls; bytes_flushed += bytes; memcpy(gpu, cpu, bytes);
}
''' + helper + r'''
static uint32_t seed=7;
static uint32_t next(void) { seed=1664525u*seed+1013904223u; return seed; }
int main(void) {
 size_t old_bytes=0, new_bytes=0; unsigned old_calls=0, new_calls=0;
 for (unsigned trial=0; trial<10000; ++trial) {
  size_t lengths[2]={trial%3 ? 4096+(next()%4097) : 0, trial%5 ? 48*(1+next()%32) : 0};
  for(unsigned defer=0;defer<2;++defer) {
   memset(cpu,0xa5,sizeof(cpu)); memset(gpu,0xa5,sizeof(gpu));
   calls=0; bytes_flushed=0; size_t pending=0;
   for(unsigned writer=0;writer<2;++writer) {
    if(!lengths[writer])continue; // no UBO or no textures
    memset(cpu,writer+1,lengths[writer]);
    ps5_publish_descriptor_prefix(cpu,lengths[writer],defer?&pending:NULL);
   }
   if(defer) {
    assert(!calls); // no early publication of the incomplete table
    if(pending)ps5_flush_gpu_data(cpu,pending);
    assert(!memcmp(expected,gpu,sizeof(gpu)));
    assert(calls<=1);
    new_bytes+=bytes_flushed; new_calls+=calls;
   } else {
    memcpy(expected,gpu,sizeof(gpu)); old_bytes+=bytes_flushed; old_calls+=calls;
   }
  }
 }
 // Ordinary fallback remains immediate, including zero-length publication.
 calls=0; ps5_publish_descriptor_prefix(cpu,0,NULL); assert(!calls);
 ps5_publish_descriptor_prefix(cpu,sizeof(cpu),NULL); assert(calls==1);
 assert(new_bytes<old_bytes && new_calls<old_calls);
 printf("PASS 10000 table sequences; synthetic flushes %u -> %u, bytes %zu -> %zu (not FPS)\n",
        old_calls,new_calls,old_bytes,new_bytes);
}
'''
with tempfile.TemporaryDirectory() as d:
    exe=str(Path(d)/'test')
    subprocess.run(['clang-18','-std=c11','-O1','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    '-x','c','-o',exe,'-'],input=code,text=True,check=True)
    subprocess.run([exe],check=True,timeout=30)
