#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise retired command-allocation reuse and bounded ownership on host."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/platform/ps5_agc_native_runtime.c').read_text()
a=s.index('static struct runtime_work_allocation {')
body=s[a:s.index('\n#endif',a)]
a=s.index('static int runtime_batch_retire') if 'static int runtime_batch_retire' in s else s.index('int ps5_agc_gate2_batch_retire')
retire=s[a:s.index('\n#ifdef PS5_DRAW_PROFILE',s.index('runtime_work_put(entry->memory',a))]
assert retire.index('if (!complete)') < retire.index('runtime_work_put(entry->memory')
shutdown=s[s.index('int ps5_agc_gate2_shutdown_present(void)'):s.index('static int runtime_video_acquire')]
assert shutdown.index('runtime_pending_batches') < shutdown.index('runtime_work_cache_clear()')
assert 'runtime_require_retirement(runtime_work_cache_clear() == 0)' in shutdown
code=r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define PS5_MULTIDRAW_BATCH_CAPACITY 2
#define PS5_INFLIGHT_BATCH_CAPACITY 4
static unsigned unmaps,releases;static int fail_unmap,fail_release;
static int munmap(void *p,size_t n) {assert(p && n);++unmaps;return fail_unmap;}
static int sceKernelReleaseDirectMemory(int64_t p,size_t n) {assert(p>=0 && n);++releases;return fail_release;}
"""+body+r"""
int main(void) {
 void *memory=(void*)123;int64_t direct=123;
 assert(!runtime_work_take(16384,&memory,&direct) && memory==(void*)123 && direct==123);
 for(unsigned i=0;i<8;++i) assert(!runtime_work_put((void*)(uintptr_t)(0x10000+i*0x4000),i,16384));
 assert(runtime_work_free_count==8 && runtime_work_free_bytes==8*16384 && !unmaps);
 assert(!runtime_work_take(32768,&memory,&direct));
 assert(!runtime_work_put((void*)0x40000,8,16384) && unmaps==1 && releases==1);
 for(unsigned i=0;i<8;++i) {assert(runtime_work_take(16384,&memory,&direct));assert(direct>=0 && direct<8);}
 assert(!runtime_work_free_count && !runtime_work_free_bytes);
 assert(!runtime_work_cache_clear() && unmaps==1);
 for(unsigned i=0;i<5;++i) assert(!runtime_work_put((void*)(uintptr_t)(0x10000+i*0x4000),i,16*1024*1024));
 assert(runtime_work_free_count==4 && runtime_work_free_bytes==64*1024*1024 && unmaps==2);
 assert(!runtime_work_cache_clear() && !runtime_work_free_count && !runtime_work_free_bytes && unmaps==6 && releases==6);
 assert(!runtime_work_put((void*)0x10000,1,16384));
 fail_unmap=1;assert(runtime_work_cache_clear()==-1 && runtime_work_free_count==1 && releases==6);
 fail_unmap=0;fail_release=1;assert(runtime_work_cache_clear()==-1 && runtime_work_free_count==1);
 return 0; /* The production caller fail-stops after either release failure. */
}
"""
with tempfile.TemporaryDirectory() as tmp:
    exe=Path(tmp)/'pool'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-x','c','-','-o',str(exe)],input=code,text=True,check=True)
    subprocess.run([str(exe)],check=True)
print('work-pool: PASS retirement ordering, exact-size reuse, capacity/byte bounds, idempotent shutdown and release failures')
