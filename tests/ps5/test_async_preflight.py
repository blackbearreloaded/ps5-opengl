#!/usr/bin/env python3
"""Compile the real producer preflight against ordered/failing platform callbacks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/platform/ps5_agc_native_runtime.c').read_text()
start = source.index('static int runtime_async_prepare_draw(void)\n{')
function = source[start:source.index('\n}', start) + 2]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define FRAMEBUFFER_BYTES 4096
#define FRAMEBUFFER_POOL_BYTES 8192
#define FRAMEBUFFER_ALIGNMENT 256
typedef struct { int (*init)(unsigned); } agc_api_t;
typedef struct { int unused; } video_api_t;
static uint8_t *runtime_framebuffer, *runtime_video_framebuffer;
static size_t runtime_framebuffer_size, runtime_video_framebuffer_size;
static int runtime_agc_initialized, runtime_video_registered;
static char events[32]; static unsigned n;
static char fail;
static int event(char c) { events[n++]=c; return c==fail ? -1 : 0; }
static int init(unsigned flags) { assert(flags==8); return event('I'); }
static int load_apis(void *a,void *b,void *c,agc_api_t *api,video_api_t *video) {
    (void)a;(void)b;(void)c;(void)video; api->init=init; return event('A');
}
static int runtime_async_drain(void) { return event('D'); }
static int ps5_agc_gate2_prepare_present(void *ptr,size_t bytes) {
    assert(ptr==runtime_framebuffer);
    assert(bytes==(runtime_framebuffer_size>=8192 ? 8192 : 4096));
    if(event('P')) return -1;
    runtime_video_registered=1; runtime_video_framebuffer=ptr;
    runtime_video_framebuffer_size=bytes; return 0;
}
static int runtime_video_prepare_draw(void) { return event('W'); }
''' + function + r'''
static void check(int expected,const char *order) {
    assert(runtime_async_prepare_draw()==expected);
    assert(n==strlen(order) && !memcmp(events,order,n)); n=0;
}
int main(void) {
    check(-1,"");
    runtime_framebuffer=(void*)0x1001; runtime_framebuffer_size=4096;
    check(-1,"");
    runtime_framebuffer=(void*)0x1000; runtime_framebuffer_size=4095;
    check(-1,"");
    runtime_framebuffer_size=4096;
    fail='A'; check(-1,"A"); assert(!runtime_agc_initialized);
    fail='I'; check(-1,"AI"); assert(!runtime_agc_initialized);
    fail=0; check(0,"AIDPW"); assert(runtime_agc_initialized);
    check(0,"W");
    runtime_framebuffer_size=16384;
    fail='D'; check(-1,"D");
    fail='P'; check(-1,"DP");
    fail=0; check(0,"DPW");
    check(0,"W");
    fail='W'; check(-1,"W");
    fail=0; runtime_framebuffer=(void*)0x2000; check(0,"DPW");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / 'preflight'
    subprocess.run(['clang-18', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-x', 'c', '-', '-o', str(exe)],
                   input=code, text=True, check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
print('PASS: initialization, pool validation/reuse/replacement, drain-before-registration, scanout wait and failures')
