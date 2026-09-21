#!/usr/bin/env python3
"""Compare the real driver/Mesa index scan with the original validation loop."""
from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[2]
def function(s,name):
    a=s.index(name+'(');a=s.rfind('\n',0,a)+1
    b=s.index('{',a);n=1;i=b+1
    while n:
        n+=(s[i]=='{')-(s[i]=='}');i+=1
    return s[a:i]
s=(r/'src/gallium/ps5/ps5_screen.c').read_text()
m=r/'third_party/mesa-26.2.0/src/mesa'
c=r'''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stdalign.h>
#include <smmintrin.h>
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define UNREACHABLE(x) assert(0)
#define USE_SSE41 1
typedef unsigned GLuint;typedef unsigned short GLushort;typedef unsigned char GLubyte;
static struct { bool has_sse4_1; } caps={true};
#define util_get_cpu_caps() (&caps)
'''
c+='void\n'+function((m/'main/sse_minmax.c').read_text(),'_mesa_uint_array_min_max')+'\n'
c+='void\n'+function((m/'vbo/vbo_minmax_index.c').read_text(),'vbo_get_minmax_index_mapped')+'\n'
c+='static uint32_t\n'+function(s,'ps5_index_value')+'\n'
c+='static bool\n'+function(s,'ps5_index_bounds')+'\n'
c+=r'''
static bool reference(const void *p,unsigned size,unsigned n,int bias,unsigned out[4]) {
 out[0]=out[2]=UINT32_MAX;out[1]=out[3]=0;
 for(unsigned i=0;i<n;++i) {
  unsigned x=ps5_index_value(p,size,i);
  if(bias<0 && x<(uint64_t)-(int64_t)bias)return false;
  unsigned effective=x+(uint32_t)bias;
  out[0]=MIN2(out[0],x);out[1]=MAX2(out[1],x);
  out[2]=MIN2(out[2],effective);out[3]=MAX2(out[3],effective);
 }
 return out[3]!=UINT32_MAX;
}
int main(void) {
 uint32_t state=1,data[260];uint16_t small[260];
 const int biases[]={0,1,-1,127,-127,INT_MAX,INT_MIN};
 for(unsigned trial=0;trial<10000;++trial) {
  for(unsigned i=0;i<260;++i) {state=state*1664525+1013904223;data[i]=state;small[i]=state;}
  if(trial%3==0)for(unsigned i=0;i<260;++i)data[i]&=65535;
  if(trial%11==0)data[trial%257]=UINT32_MAX;
  if(trial%13==0)data[trial%257]=0;
  unsigned n=1+trial%257,offset=trial%4;
  for(unsigned width=2;width<=4;width+=2)for(unsigned b=0;b<7;++b) {
   const void *p=width==2?(void*)(small+offset):(void*)(data+offset);
   unsigned actual[4],expected[4];
   bool good=ps5_index_bounds(p,width,n,biases[b],actual,actual+1,actual+2,actual+3);
   assert(good==reference(p,width,n,biases[b],expected));
   if(good)for(unsigned j=0;j<4;++j)assert(actual[j]==expected[j]);
  }
 }
}
'''
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'bounds'
    subprocess.run(['cc','-std=c11','-O2','-msse4.1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-x','c','-','-o',str(exe)],input=c,text=True,check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: Mesa SSE index bounds match original validation for 140000 biased, wrapping, underflow, width and alignment cases')
