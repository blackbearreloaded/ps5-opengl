#!/usr/bin/env python3
"""Actual draw-range calculations and cache-line coverage, including tails."""
from pathlib import Path
import subprocess
import tempfile
s=(Path(__file__).resolve().parents[2]/'src/gallium/ps5/ps5_screen.c').read_text()
a=s.index('static void\nps5_flush_gpu_data(');flush=s[a:s.index('static bool\nps5_texel_buffer_descriptor(',a)]
mesa=Path(__file__).resolve().parents[2]/'third_party/mesa-26.2.0/src/util'
cache=(mesa/'cache_ops_x86.c').read_text()
cache='\n'.join(line for line in cache.splitlines() if not line.startswith('#include'))
opt=(mesa/'cache_ops_x86_clflushopt.c').read_text()
opt=opt[opt.index('void\nutil_clflushopt_range('):]
cache=(opt+cache).replace('__builtin_ia32_clflushopt(p);','optimized++; lines[line_count++] = (uintptr_t)p;').replace('__builtin_ia32_clflush(p);','lines[line_count++] = (uintptr_t)p;').replace('__builtin_ia32_clflush((char *)start + size - 1);','lines[line_count++] = ((uintptr_t)start+size-1)&~(uintptr_t)63;').replace('__builtin_ia32_mfence();','++fences;')
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
#define HAVE___BUILTIN_IA32_CLFLUSHOPT 1
struct util_cpu_caps_t { unsigned has_clflushopt, cacheline; };
static struct util_cpu_caps_t caps={0,64};
static const struct util_cpu_caps_t *util_get_cpu_caps(void) { return &caps; }
static uintptr_t lines[32]; static unsigned line_count,fences,optimized;
'''+cache+flush+r'''
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
 for(unsigned fast=0;fast<2;++fast) for(unsigned off=0;off<64;++off) for(unsigned n=0;n<=256;++n) {
  caps.has_clflushopt=fast;line_count=fences=optimized=0; ps5_flush_gpu_data((void*)(uintptr_t)(4096+off),n);
  unsigned count=n?(off+n+63)/64:0;
  assert(fences==(n?(fast?2:1):0)); assert(line_count==(n?count+1:0));
  for(unsigned j=0;j<count;++j) assert(lines[j]==4096+j*64);
  if(n) assert(lines[count]==4096+(count-1)*64);
  assert(optimized==(fast?line_count:0));
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
