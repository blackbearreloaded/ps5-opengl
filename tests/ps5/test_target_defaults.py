"""Compare production target-state generation with the pre-optimization code."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
path = 'src/platform/ps5_agc_native_runtime.c'
before = subprocess.check_output(['git', 'show', '607602a:' + path], cwd=root, text=True)
after = (root / path).read_text()

def extract(source, name):
    start = source.index('static int append_target_state(')
    end = source.index('\n}\n', start) + 3
    return source[start:end].replace('static int append_target_state',
                                    'static __attribute__((noinline)) int ' + name)

code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
typedef struct { uint16_t offset, reserved; uint32_t value; } agc_register_t;
static uint32_t float_bits(float f) { uint32_t n; memcpy(&n,&f,4); return n; }
''' + extract(before, 'reference') + extract(after, 'candidate') + r'''
static uint32_t seed=1;
static uint32_t random_word(void) { seed=seed*1664525u+1013904223u; return seed; }
static double seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
int main(void) {
    agc_register_t rows[1024], a[64], b[64];
    agc_register_t *blocks[]={rows};
    struct { void *blocks; uint8_t padding[24]; uint32_t count; } defaults={blocks,{0},1024};
    const uint16_t offsets[]={0x318,0x31b,0x31c,0x31d,0x31e,0x31f,0x321,0x323,0x324,0x325,0x390,0x398,0x3a0,0x3a8,0x3b0,0x3b8};
    for(unsigned trial=0;trial<1000;++trial) {
        for(unsigned i=0;i<1024;++i) rows[i]=(agc_register_t){0xffff,0,random_word()};
        for(unsigned i=0;i<16;++i) rows[i].offset=offsets[i];
        rows[16].offset=offsets[0]; // duplicate: first match wins
        for(unsigned i=1023;i;--i) { unsigned j=random_word()%(i+1); agc_register_t t=rows[i];rows[i]=rows[j];rows[j]=t; }
        if(trial%3==0) for(unsigned i=0;i<1024;++i) if(rows[i].offset==offsets[15]) rows[i].offset=0xffff;
        unsigned ac=0,bc=0;
        void *target=(void *)(uintptr_t)(0x200000000ull+((uint64_t)random_word()<<8));
        int ar=reference(a,&ac,&defaults,target), br=candidate(b,&bc,&defaults,target);
        assert(ar==br && ac==bc);
        if(!ar) assert(!memcmp(a,b,ac*sizeof(*a)));
    }
    defaults.count=0; unsigned ac=0,bc=0;
    assert(reference(a,&ac,&defaults,0)==candidate(b,&bc,&defaults,0));
    for(unsigned count=64;count<=1024;count*=4) {
        defaults.count=count;
        for(unsigned i=0;i<count;++i) rows[i]=(agc_register_t){0xffff,0,i};
        for(unsigned i=0;i<16;++i) rows[count-16+i].offset=offsets[i];
        volatile uint32_t sink=0;
        double start=seconds();
        for(unsigned i=0;i<20000;++i) { assert(!reference(a,&ac,&defaults,(void *)(uintptr_t)(i*256ull)));sink+=a[0].value; }
        double old=seconds()-start; start=seconds();
        for(unsigned i=0;i<20000;++i) { assert(!candidate(b,&bc,&defaults,(void *)(uintptr_t)(i*256ull)));sink+=b[0].value; }
        double current=seconds()-start;
        printf("rows=%u old=%.1fns new=%.1fns ratio=%.2fx checksum=%u\n",count,old/20000*1e9,current/20000*1e9,old/current,sink);
    }
    puts("PASS: 1000 reordered/mutated/duplicate/missing tables and dynamic target equivalence");
}
'''
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    source.write_text(code)
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
