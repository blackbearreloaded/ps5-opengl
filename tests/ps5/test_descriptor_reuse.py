#!/usr/bin/env python3
"""Exercise production retired-descriptor cache ownership and bounded reuse."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'src/gallium/ps5/ps5_screen.c').read_text()
a=s.index('static struct pipe_resource *\nps5_descriptor_take(')
body=s[a:s.index('\n#endif',a)]
assert s.count('ps5_descriptor_recycle(batch->owner, batch->slots[slot].storage);')==1
release=s[s.index('static void\nps5_deferred_batch_release('):s.index('static bool\nps5_draw_batch_retire_one_locked(')]
assert release.index('ps5_collect_occlusion_query_resource') < release.index('ps5_descriptor_recycle') < release.index('ps5_deferred_slot_release')
retire=s[s.index('static bool\nps5_draw_batch_retire_one_locked('):s.index('static void\nps5_draw_batch_retire_locked(')]
assert retire.index('if (!status)') < retire.index('ps5_deferred_batch_release(')
destroy=s[s.index('static void\nps5_context_destroy('):]
assert destroy.index('ps5_draw_batch_drain();') < destroy.index('ps5_descriptor_cache_clear(context);')
assert s[s.index('struct ps5_context {'):].split('\n')[1].strip()=='struct pipe_context base;'
c=r'''#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#define PS5_MULTIDRAW_BATCH_CAPACITY 2
#define PS5_DIRECT_ALIGNMENT 16384
#define PIPE_BUFFER 0
struct pipe_resource { struct { int count; } reference; unsigned target,width0,format,bind,usage,flags; };
struct pipe_screen { struct pipe_resource *(*resource_create)(struct pipe_screen*,const struct pipe_resource*); };
struct pipe_context { struct pipe_screen *screen; };
struct ps5_context { struct pipe_context base; struct pipe_resource *descriptor_cache[3][2];unsigned descriptor_cache_count[3];uint64_t descriptor_cache_hits,descriptor_cache_misses; };
static unsigned allocations,frees;
static struct pipe_resource *create(struct pipe_screen *s,const struct pipe_resource *t) {
 assert(s);struct pipe_resource *p=malloc(sizeof(*p));assert(p);*p=*t;p->reference.count=1;++allocations;return p;
}
static void pipe_resource_reference(struct pipe_resource **p,struct pipe_resource *q) {
 assert(!q);if(*p && !--(*p)->reference.count) {free(*p);++frees;}*p=q;
}
'''+body+r'''
int main(void) {
 struct pipe_screen screen={create};struct ps5_context c={.base={&screen}},other={.base={&screen}};
 struct pipe_resource t={.target=PIPE_BUFFER,.width0=16384,.format=1,.bind=2,.usage=3,.flags=4};
 struct pipe_resource *held[3]={0};
 for(unsigned i=0;i<3;++i)held[i]=ps5_descriptor_take(&c.base,i,&t);
 assert(allocations==3 && c.descriptor_cache_misses==3 && !c.descriptor_cache_hits);
 struct pipe_resource *saved=held[0];ps5_descriptor_recycle(&c,held);
 for(unsigned i=0;i<3;++i)assert(!held[i] && c.descriptor_cache_count[i]==1);
 held[0]=ps5_descriptor_take(&c.base,0,&t);assert(held[0]==saved && allocations==3);
 struct pipe_resource *independent=ps5_descriptor_take(&other.base,0,&t);assert(independent!=saved && allocations==4);
 pipe_resource_reference(&independent,NULL);
 held[0]->reference.count=2;ps5_descriptor_recycle(&c,held);assert(held[0]);
 held[0]->reference.count=1;held[0]->width0=32768;ps5_descriptor_recycle(&c,held);assert(held[0]);
 held[0]->width0=16384;ps5_descriptor_recycle(&c,held);assert(!held[0]);
 for(unsigned field=0;field<4;++field) {
  struct pipe_resource changed=t;
  if(field==0)changed.format++;if(field==1)changed.bind++;if(field==2)changed.usage++;if(field==3)changed.flags++;
  held[0]=ps5_descriptor_take(&c.base,0,&changed);assert(held[0]!=saved && c.descriptor_cache_count[0]==1);
  pipe_resource_reference(&held[0],NULL);
 }
 /* Full class remains bounded; caller releases excess snapshots normally. */
 held[0]=create(&screen,&t);ps5_descriptor_recycle(&c,held);assert(!held[0] && c.descriptor_cache_count[0]==2);
 held[0]=create(&screen,&t);ps5_descriptor_recycle(&c,held);assert(held[0] && c.descriptor_cache_count[0]==2);
 pipe_resource_reference(&held[0],NULL);
 ps5_descriptor_cache_clear(&c);ps5_descriptor_cache_clear(&c);ps5_descriptor_cache_clear(&other);
 assert(allocations==frees && !c.descriptor_cache_count[0] && !c.descriptor_cache_count[1] && !c.descriptor_cache_count[2]);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'reuse'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-x','c','-','-o',str(p)],input=c,text=True,check=True)
 subprocess.run([str(p)],check=True)
print('PASS: retired descriptor reuse, context/class isolation, exclusive ownership, capacity and shutdown release')
