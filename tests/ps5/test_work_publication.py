#!/usr/bin/env python3
"""Check actual command publication spans, padding, and tessellation fallback."""
from pathlib import Path
import subprocess
import tempfile
s=(Path(__file__).resolve().parents[2]/'src/platform/ps5_agc_native_runtime.c').read_text()
a=s.index('static void runtime_zero_work(')
body=s[a:s.index('\n#endif',a)]
code=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#define WORK_BYTES 0x10000u
typedef struct {uint32_t *bottom,*top,*up,*down; uintptr_t callback;} agc_command_buffer_t;
static uint8_t memory[0x14000], published[sizeof(memory)];
static void flush_gpu_data(const void *p,size_t n) {
 size_t at=(const uint8_t*)p-memory; assert(at+n<=sizeof(memory));
 memset(published+at,1,n);
}
'''+body+r'''
int main(void) {
 memset(memory,0xa5,sizeof(memory));
 runtime_zero_work(memory,sizeof(memory),0);
 for(size_t i=0;i<sizeof(memory);++i)
  assert(memory[i]==((i>=0x4000 && i<0xc000)||i>=WORK_BYTES ? 0:0xa5));
 for(unsigned count=0;count<=4096;count+=17) {
  unsigned tail=(4096-count)/3;
  agc_command_buffer_t c={(void*)(memory+0x8000),(void*)(memory+0xc000),
   (void*)(memory+0x8000+count*4),(void*)(memory+0xc000-tail*4),0};
  memset(published,0,sizeof(published));
  runtime_publish_work(memory,sizeof(memory),&c,0);
  for(size_t i=0;i<sizeof(memory);++i) {
   int expected=(i>=0x4000 && i<0x8000+count*4)||
    (i>=0xc000-tail*4 && i<0xc000)||i>=WORK_BYTES;
   assert(published[i]==expected);
  }
 }
 agc_command_buffer_t c={(void*)(memory+0x8000),(void*)(memory+0xc000),
  (void*)(memory+0xc000),(void*)(memory+0xc000),0};
 for(int tess=0;tess<2;++tess) {
  if(!tess) c.down=c.bottom; /* Overlapping packet/data ends: conservative flush. */
  memset(published,0,sizeof(published));
  runtime_publish_work(memory,sizeof(memory),&c,tess);
  for(size_t i=0;i<sizeof(memory);++i) assert(published[i]);
 }
 runtime_zero_work(memory,sizeof(memory),1);
 for(size_t i=0;i<sizeof(memory);++i) assert(!memory[i]);
 return 0;
}
'''
assert 'runtime_publish_work(memory, work_bytes, &command, runtime_hs_package != NULL);' in s
with tempfile.TemporaryDirectory() as tmp:
    exe=Path(tmp)/'spans'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-x','c','-','-o',str(exe)],input=code,text=True,check=True)
    subprocess.run([str(exe)],check=True)
print('work publication: PASS state/marker, both command ends, shader padding, tessellation and conservative fallback')
