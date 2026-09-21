#!/usr/bin/env python3
"""Actual draw-range calculations and cache-line coverage, including tails."""
from pathlib import Path
import subprocess
import tempfile
s=(Path(__file__).resolve().parents[2]/'src/gallium/ps5/ps5_screen.c').read_text()
a=s.index('static void\nps5_flush_gpu_data(');flush=s[a:s.index('static bool\nps5_texel_buffer_descriptor(',a)]
flush=flush.replace('__asm__ volatile("clflush (%0)" : : "r"(at) : "memory");','lines[line_count++] = at;').replace('__asm__ volatile("mfence" ::: "memory");','++fences;')
a=s.index('      uint32_t binding_records[PIPE_MAX_ATTRIBS] = {0};')
a=s.index('         if (element->instance_divisor) {',a)
b=s.index('         binding_mask |=',a)
calc=s[a:b]
assert '(const void *)vertex_address, binding_bytes[binding]);' in s
assert 'index_resource->data + index_offset,\n         (size_t)draws[0].count * info->index_size);' in s
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define PIPE_BUFFER 1
static uintptr_t lines[32]; static unsigned line_count,fences;
'''+flush+r'''
struct ps5_resource { struct { unsigned target; } base; size_t size; };
struct vertex_buffer { unsigned buffer_offset; struct { struct ps5_resource *resource; } buffer; };
struct element { unsigned instance_divisor,src_offset,src_stride,vertex_buffer_index; };
struct context { unsigned split_instance_id; int last_draw_status; };
struct info { unsigned start_instance,instance_count; };
static void calculate(struct context *context, struct info *info, struct element *element,
 struct vertex_buffer *vertex_buffer, uint64_t records, unsigned format_size,
 uint32_t binding_records[1], size_t binding_bytes[1]) {
 struct ps5_resource *vertex_resource; uint64_t descriptor_records=records,required;
'''+calc+r'''
}
int main(void) {
 for(unsigned off=0;off<64;++off) for(unsigned n=0;n<=256;++n) {
  line_count=fences=0; ps5_flush_gpu_data((void*)(uintptr_t)(4096+off),n);
  assert(fences==1); assert(line_count==(n?(off+n+63)/64:0));
  for(unsigned i=0;i<line_count;++i) assert(lines[i]==4096+i*64);
 }
 struct context c={0}; struct info i={0,1}; struct element e={0,4,24,0};
 struct ps5_resource r={{PIPE_BUFFER},1<<24}; struct vertex_buffer v={100,{&r}};
 uint32_t records[1]={0};size_t bytes[1]={0};
 calculate(&c,&i,&e,&v,10,12,records,bytes); assert(!c.last_draw_status&&bytes[0]==232);
 /* Interleaved attributes must retain the largest end, including a short tail. */
 e.src_offset=20;calculate(&c,&i,&e,&v,10,4,records,bytes);assert(bytes[0]==240);
 e.src_offset=0;calculate(&c,&i,&e,&v,10,4,records,bytes);assert(bytes[0]==240);
 /* Instanced fetches include start instance and divisor rounding. */
 bytes[0]=0;e.instance_divisor=3;e.src_offset=4;i=(struct info){7,5};c.split_instance_id=2;
 calculate(&c,&i,&e,&v,1000,12,records,bytes);assert(!c.last_draw_status&&bytes[0]==232);
 /* Constant attributes still cover their value; rejected bounds don't grow ranges. */
 bytes[0]=0;e=(struct element){0,8,0,0};calculate(&c,&i,&e,&v,1,16,records,bytes);assert(bytes[0]==24);
 r.size=123;bytes[0]=0;calculate(&c,&i,&e,&v,1,16,records,bytes);assert(c.last_draw_status==-9&&bytes[0]==0);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('draw flush ranges: attribute union, instancing, bounds and all cache-line tails PASS')
