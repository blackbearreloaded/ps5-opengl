#!/usr/bin/env python3
"""Actual CPU write publication, including holes in reused published ranges."""
from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[2]/'src/gallium/ps5/ps5_screen.c').read_text()
def fn(name):
 a=s.index('static void\n'+name+'(');return s[a:s.index('\n}',a)+2]
code=r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PIPE_BUFFER 1
#define PIPE_MAP_WRITE 2
#define PIPE_MAP_READ 4
#define PIPE_MAP_UNSYNCHRONIZED 8
struct pipe_resource {unsigned target;};
struct ps5_resource {struct pipe_resource base;size_t size;uint8_t *data;};
struct pipe_context {int unused;};
struct pipe_box {int x,y,z,width,height,depth;};
struct pipe_transfer {struct pipe_resource *resource;unsigned usage;struct pipe_box box;};
static unsigned drains,flushes;static uint8_t cpu[128],gpu[128];
static void ps5_draw_batch_drain_buffer(struct pipe_resource *r){(void)r;++drains;}
static void ps5_flush_gpu_data(const void *p,size_t n){
 size_t at=(const uint8_t*)p-cpu;assert(at+n<=sizeof(cpu));memcpy(gpu+at,p,n);++flushes;
}
'''+fn('ps5_publish_transfer_write')+fn('ps5_transfer_flush_region')+fn('ps5_buffer_subdata')+r'''
int main(){
 struct ps5_resource r={{PIPE_BUFFER},sizeof(cpu),cpu};
 struct pipe_transfer t={&r.base,PIPE_MAP_WRITE|PIPE_MAP_UNSYNCHRONIZED,{16,0,0,64,1,1}};
 struct pipe_box box={8,0,0,16,1,1};
 memset(cpu,1,sizeof(cpu));ps5_flush_gpu_data(cpu,sizeof(cpu));
 memset(cpu+24,2,16);ps5_transfer_flush_region(0,&t,&box);
 assert(!drains && flushes==2 && gpu[23]==1 && gpu[24]==2 && gpu[39]==2 && gpu[40]==1);
 uint8_t value[8];memset(value,3,sizeof(value));
 ps5_buffer_subdata(0,&r.base,PIPE_MAP_UNSYNCHRONIZED,60,8,value);
 assert(!drains && gpu[60]==3 && gpu[67]==3 && gpu[68]==1);
 ps5_buffer_subdata(0,&r.base,0,64,8,value);assert(drains==1 && gpu[71]==3);
 memset(cpu+16,4,64);ps5_publish_transfer_write(&t,0);assert(gpu[16]==4 && gpu[79]==4 && gpu[80]==1);
 unsigned before=flushes;
 box.x=-1;ps5_publish_transfer_write(&t,&box);
 box.x=60;box.width=8;ps5_publish_transfer_write(&t,&box);
 t.usage=PIPE_MAP_READ;ps5_publish_transfer_write(&t,0);
 t.usage=PIPE_MAP_WRITE;t.box.x=100;ps5_publish_transfer_write(&t,0);
 ps5_buffer_subdata(0,&r.base,0,125,8,value);
 assert(flushes==before);
 return 0;
}
'''
assert 'vertex_resource->external_cpu_access ? NULL : flush_cache' in s
assert 'index_resource->external_cpu_access ? NULL : flush_cache' in s
assert 'ps5_flush_batch_backing(texture->external_cpu_access ? NULL : batch, slot, data, bytes)' in s
assert 'if (!(transfer->usage & PIPE_MAP_FLUSH_EXPLICIT))\n      ps5_publish_transfer_write(transfer, NULL);' in s
with tempfile.TemporaryDirectory() as tmp:
 exe=Path(tmp)/'publish'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-x','c','-','-o',str(exe)],input=code,text=True,check=True)
 subprocess.run([str(exe)],check=True)
print('Buffer publication: PASS unsynchronized writes, explicit subranges, bounds, read exclusion and persistent draw cache bypass')
