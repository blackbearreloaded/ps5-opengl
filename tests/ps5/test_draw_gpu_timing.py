#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/platform/ps5_agc_native_runtime.c').read_text()
a=s.index('static void runtime_draw_gpu_observe(')
helper=s[a:s.index('\n#endif',a)]
code=r"""
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <assert.h>
static int64_t os_time_get_nano(void) { return 99; }
static void flush_gpu_data(const void *p,size_t n) { assert(p && n==64); }
"""+helper+r"""
int main(void) {
    uint64_t memory[0x8000/8]={0};
    memory[0x4000/8]=10; memory[0x4000/8+1]=20; memory[0x4000/8+2]=25;
    memory[0x4000/8+8]=90;
    runtime_draw_gpu_timing(memory,sizeof(memory));
    memory[0x4000/8+2]=9;
    runtime_draw_gpu_timing(memory,sizeof(memory));
}
"""
with tempfile.TemporaryDirectory() as tmp:
    exe=Path(tmp)/'timing'
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-x','c','-o',str(exe),'-'],input=code,text=True,check=True)
    lines=subprocess.check_output([str(exe)],text=True).splitlines()
    assert 'begin=10 draw=20 end=25 submit_ns=90 first_ns=99 end_seen_ns=99 cpu_ns=99 valid=1' in lines[0]
    assert lines[1].endswith('valid=0')
print('GPU draw timing page offset, duration and invalid ordering PASS')
