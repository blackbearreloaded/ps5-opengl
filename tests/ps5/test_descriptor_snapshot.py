#!/usr/bin/env python3
"""Exercise the actual descriptor snapshot size and copy with bounded backing."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
s = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
a = s.index('static bool\nps5_prepare_constant(')
publish = s[a:s.index('\n}\n', a)]
assert 'ps5_flush_gpu_data(storage->data, ps5_descriptor_snapshot_size(context, slot));' in publish
a = s.index('static size_t\nps5_descriptor_snapshot_size(')
helper = s[a:s.index('static unsigned\nps5_shader_storage_count(', a)]
a = s.index('static bool\nps5_batch_copy_descriptors(')
copy = s[a:s.index('#define PS5_BATCH_RESOURCE_COUNT', a)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define PS5_ENABLE_UBO_CANDIDATE 1
#define PS5_DESCRIPTOR_STORAGE_BYTES 280576
#define PS5_DIRECT_ALIGNMENT 16384
#define PS5_CONSTANT_DATA_OFFSET 4096
#define PIPE_BUFFER 0
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define ALIGN(a,b) (((a)+(b)-1)&~((b)-1))
struct pipe_resource { unsigned target,width0; };
struct ps5_resource { struct pipe_resource base; unsigned char *data; size_t size; };
struct pipe_screen { struct pipe_resource *(*resource_create)(struct pipe_screen *, const struct pipe_resource *); };
struct pipe_context { struct pipe_screen *screen; };
struct ps5_constant_state { bool valid,copied; unsigned size; };
struct ps5_context { struct pipe_context base; void *gs,*tcs,*tes; struct ps5_constant_state constants[5][13]; };
static unsigned char arena[6 * PS5_DESCRIPTOR_STORAGE_BYTES];
static struct ps5_resource resources[3];
static unsigned count;
static struct pipe_resource *create(struct pipe_screen *s, const struct pipe_resource *t) {
 (void)s; assert(count<3); struct ps5_resource *r=&resources[count];
 r->base=*t; r->size=t->width0; r->data=arena+(3+count++)*PS5_DESCRIPTOR_STORAGE_BYTES;
 memset(r->data,0xcc,PS5_DESCRIPTOR_STORAGE_BYTES); return &r->base;
}
''' + helper + copy + r'''
int main(void) {
 struct pipe_screen screen={create}; struct ps5_context c={.base.screen=&screen};
 struct ps5_resource src[3]; struct pipe_resource *saved[3], *out[3]={0};
 for(unsigned i=0;i<3;++i) { src[i]=(struct ps5_resource){{PIPE_BUFFER,PS5_DESCRIPTOR_STORAGE_BYTES},arena+i*PS5_DESCRIPTOR_STORAGE_BYTES,PS5_DESCRIPTOR_STORAGE_BYTES}; saved[i]=&src[i].base; memset(src[i].data,0x31+i,src[i].size); }
 src[0].base.width0=src[0].size=PS5_DIRECT_ALIGNMENT;
 for(unsigned n=1;n<=65536;n+=127) {
  c.constants[0][0]=(struct ps5_constant_state){true,true,n};
  c.constants[1][0]=(struct ps5_constant_state){true,true,65536}; count=0;
  assert(ps5_batch_copy_descriptors(&c.base,saved,out));
  for(unsigned i=0;i<3;++i) { struct ps5_resource *r=(void*)out[i];
   assert(r->size==(i?ps5_descriptor_snapshot_size(&c,i-1):PS5_DIRECT_ALIGNMENT));
   assert(!memcmp(r->data,src[i].data,r->size)); assert(r->data[r->size]==0xcc);
  }
 }
 c.constants[0][0]=(struct ps5_constant_state){true,false,65536};
 assert(ps5_descriptor_snapshot_size(&c,0)==16384);
 c.constants[0][0]=(struct ps5_constant_state){false,true,65536};
 assert(ps5_descriptor_snapshot_size(&c,0)==16384);
 void **stages[]={&c.gs,&c.tcs,&c.tes};
 for(unsigned i=0;i<3;++i) { *stages[i]=&c; assert(ps5_descriptor_snapshot_size(&c,0)==PS5_DESCRIPTOR_STORAGE_BYTES); assert(ps5_descriptor_snapshot_size(&c,1)==69632); *stages[i]=NULL; }
 src[2].size=4096;count=0; assert(!ps5_batch_copy_descriptors(&c.base,saved,out));
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d); (p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('descriptor snapshot: bounded copies, inline CB0 and multi-stage fallback PASS')
