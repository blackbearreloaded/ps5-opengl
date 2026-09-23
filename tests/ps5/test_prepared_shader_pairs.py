#!/usr/bin/env python3
"""Exercise the production prepared-pair cache, including reused source addresses."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[2]
s = (root/'src/platform/ps5_agc_native_runtime.c').read_text()
a = s.index('static struct runtime_shader_pair {')
b = s.index('\nstatic int runtime_work_take', a)
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define DIRECT_MEMORY_TYPE 12
#define MAP_PROTECTION 0x33
static const uint8_t *runtime_vs_package, *runtime_ps_package;
static unsigned runtime_vs_package_len=128, runtime_ps_package_len=128;
static uint32_t runtime_primitive_type=4;
static int allocations, releases, maps, unmaps, creates, links, flushes, fail_create, fail_map;
static int sceKernelAllocateDirectMemory(int64_t a,int64_t b,size_t n,size_t align,int type,int64_t *p) {
    assert(align==0x4000 && type==12); *p=++allocations; return 0;
}
static int sceKernelMapDirectMemory(void **p,size_t n,int prot,int flags,int64_t direct,size_t align) {
    if(fail_map) return -1;
    *p=calloc(1,n); ++maps; return *p ? 0 : -1;
}
static int munmap(void *p,size_t n) { ++unmaps; free(p); return 0; }
static int sceKernelReleaseDirectMemory(int64_t p,size_t n) { ++releases; return 0; }
typedef struct { uint32_t *bottom,*top,*up,*down; } agc_command_buffer_t;
static int spy;
static const void *ranges[3]; static size_t lengths[3];
static void flush_gpu_data(const void *p,size_t n) {
    if(spy) { assert(flushes<3); ranges[flushes]=p; lengths[flushes]=n; }
    ++flushes;
}
typedef struct { int (*create_shader)(void **,void *,void *); int (*link_shaders)(void *,void *,void *,void *,void *,uint32_t); } agc_api_t;
static int create(void **p,void *header,void *code) { ++creates; *p=header; return fail_create ? -1 : 0; }
static int link_pair(void *out,void *scratch,void *unused,void *vs,void *ps,uint32_t primitive) {
    ++links; memset(out,primitive,34*8); memset(scratch,0x5a,3*8); return 0;
}
''' + s[a:b] + r'''
typedef uint64_t agc_register_t;
static void restore(struct runtime_shader_pair *prepared, uint8_t *memory) {
    void *vertex, *pixel;
''' + s[s.index('        vertex = prepared->vertex;'):s.index('        hull_rc = vertex_rc = pixel_rc = link_rc = 0;')] + r'''
}
int main(void) {
    uint8_t vs[128]={1}, ps[128]={2};
    runtime_vs_package=vs; runtime_ps_package=ps;
    agc_api_t agc={create,link_pair};
#define GET() runtime_shader_pair_get(&agc,0,vs,96,vs+96,32,ps,96,ps+96,32)
    struct runtime_shader_pair *first=GET();
    assert(first && creates==2 && links==1 && flushes==1);
    uint8_t draw[0x7000]={0}; restore(first,draw);
    for(unsigned i=0;i<34*8;++i) assert(draw[0x5000+i]==4);
    for(unsigned i=0;i<3*8;++i) assert(draw[0x6000+i]==0x5a);
    assert(draw[0x6000+3*8]==0);
    assert(GET()==first && creates==2 && flushes==1);
    vs[127]=3; /* Same address and size, different bytecode must miss. */
    struct runtime_shader_pair *second=GET();
    assert(second && second!=first && creates==4);
    assert(first->source[127]==0 && ((uint8_t*)first->vertex)[0]==1);
    runtime_primitive_type=5;
    assert(GET()!=second && links==3);
    runtime_primitive_type=6; fail_create=1;
    assert(!GET() && runtime_shader_pair_count==3 && releases==1 && unmaps==1);
    fail_create=0; fail_map=1;
    assert(!GET() && releases==2 && unmaps==1);
    fail_map=0;
    size_t saved=runtime_shader_pair_bytes;
    runtime_shader_pair_bytes=64u*1024u*1024u;
    assert(!GET()); runtime_shader_pair_bytes=saved;
    for(unsigned i=6;i<515;++i) { runtime_primitive_type=i; assert(GET()); }
    runtime_primitive_type=999;
    assert(runtime_shader_pair_count==512 && !GET());
    assert(runtime_shader_pair_clear()==0 && !runtime_shader_pair_count && !runtime_shader_pair_bytes);
    assert(allocations==releases && maps==unmaps);
    assert(runtime_shader_pair_clear()==0);
    uint8_t work[0x10000]; memset(work,0xa5,sizeof(work));
    runtime_prepared_work_clear(work);
    for(unsigned i=0;i<sizeof(work);++i)
        assert(work[i]==(i>=0x4000 && i<0x8000 ? 0 : 0xa5));
    agc_command_buffer_t c={(uint32_t*)(work+0x8000),(uint32_t*)(work+0xc000),
        (uint32_t*)(work+0x8100),(uint32_t*)(work+0xbfc0)};
    spy=1; flushes=0;
    assert(!runtime_prepared_work_publish(work,&c) && flushes==3);
    assert(ranges[0]==work+0x4000 && lengths[0]==0x4000);
    assert(ranges[1]==work+0x8000 && lengths[1]==0x100);
    assert(ranges[2]==work+0xbfc0 && lengths[2]==64);
    flushes=0; c.down=c.up-1;
    assert(runtime_prepared_work_publish(work,&c)==-1 && flushes==0);
    c.down=c.top; c.bottom++;
    assert(runtime_prepared_work_publish(work,&c)==-1 && flushes==0);
    c.bottom--; c.top++;
    assert(runtime_prepared_work_publish(work,&c)==-1 && flushes==0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    src=Path(d)/'test.c'; exe=Path(d)/'test'; src.write_text(code)
    subprocess.run(['clang','-std=c11','-O1','-g','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS prepared-pair hits, address reuse, primitive changes, failures, limits and lifetime')
