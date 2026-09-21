#!/usr/bin/env python3
"""Production CPU staging pool: ownership, bounds, failures and concurrency."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];s=(root/'src/gallium/ps5/ps5_screen.c').read_text()
a=s.index('#ifdef PS5_NATIVE_TITLE_RUNTIME\n/* CPU-only scratch');b=s.index('static void *\nps5_transfer_map(',a)
assert 'ps5_staging_clear();' in s[s.index('ps5_screen_destroy(struct pipe_screen *base)'):]
code=r"""
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sched.h>
#define PS5_NATIVE_TITLE_RUNTIME 1
#define PS5_DIRECT_ALIGNMENT 16384u
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define simple_mtx_t pthread_mutex_t
#define SIMPLE_MTX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define simple_mtx_lock pthread_mutex_lock
#define simple_mtx_unlock pthread_mutex_unlock
struct ps5_transfer { void *staging;size_t staging_mapping_size; };
static bool fail_unmap;
static int test_unmap(void *p,size_t n){if(fail_unmap)return -1;return munmap(p,n);}
#define munmap test_unmap
"""+s[a:b]+r"""
static void *worker(void *arg){
 for(unsigned i=0;i<1000;i++){
  struct ps5_transfer t={0};assert(ps5_transfer_alloc_staging(&t,65536));
  *(uintptr_t*)t.staging=(uintptr_t)arg;sched_yield();assert(*(uintptr_t*)t.staging==(uintptr_t)arg);
  ps5_transfer_free_staging(&t);assert(!t.staging&&!t.staging_mapping_size);
 }return NULL;
}
int main(void){
 struct ps5_transfer a={0},b={0};
 assert(!ps5_transfer_alloc_staging(&a,0));assert(!ps5_transfer_alloc_staging(&a,SIZE_MAX));
 assert(ps5_transfer_alloc_staging(&a,65537));assert(a.staging_mapping_size==81920);
 void *first=a.staging;assert(ps5_transfer_alloc_staging(&b,65537));assert(b.staging!=first);
 ps5_transfer_free_staging(&a);assert(ps5_transfer_alloc_staging(&a,65537));assert(a.staging==first);
 ps5_transfer_free_staging(&a);ps5_transfer_free_staging(&b);assert(ps5_staging_count==2);
 fail_unmap=true;ps5_staging_clear();assert(ps5_staging_count==2);fail_unmap=false;ps5_staging_clear();assert(!ps5_staging_count&&!ps5_staging_bytes);
 struct ps5_transfer many[9]={0};
 for(unsigned i=0;i<9;i++)assert(ps5_transfer_alloc_staging(&many[i],16u*1024u*1024u));
 for(unsigned i=0;i<9;i++)ps5_transfer_free_staging(&many[i]);
 assert(ps5_staging_count==8&&ps5_staging_bytes==128u*1024u*1024u);ps5_staging_clear();
 assert(ps5_transfer_alloc_staging(&a,1024));assert(!a.staging_mapping_size);ps5_transfer_free_staging(&a);
 pthread_t threads[4];for(uintptr_t i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,worker,(void*)(i+1)));
 for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
 ps5_staging_clear();ps5_staging_clear();assert(!ps5_staging_count&&!ps5_staging_bytes);return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True)
 subprocess.run([str(p.with_suffix(''))],check=True)
print('CPU scratch exclusive reuse, exact sizes, 128MiB bound, failure retention, shutdown and concurrent ownership PASS')
