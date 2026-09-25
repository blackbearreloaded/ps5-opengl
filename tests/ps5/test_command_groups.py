#!/usr/bin/env python3
"""Exercise production command grouping and retirement with delayed GPU markers."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.index("static struct runtime_batch_entry {")
body = source[start:source.index("\n#endif\n\n#ifdef PS5_DRAW_PROFILE", start)]
tail_start = source.index('    failure_phase = "release";', source.index('    draw_words ='))
tail = source[tail_start:source.index('    final_words =', tail_start)]
assert tail.index('completion_offset = draw_words - PS5_AGC_POST_DRAW_BARRIER_WORDS') < tail.index('release_mem(&command, 45, 12')
assert tail.index('release_mem(&command, 45, 12') < tail.index('if (!completion_offset)') < tail.index('runtime_release_completion(&agc, &command, completion_marker,')
assert 'if (command.down >= command.up && command.down <= command.top)' in source
assert 'entry.command_capacity = (uint32_t)(command.down - words);' in source
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "ps5_screen.h"
typedef struct { void *words; uint32_t word_count; uint8_t flag, padding[3]; } agc_submit_description_t;
typedef struct { int (*submit)(void *); int (*suspend_point)(void); } agc_api_t;
static uint32_t memory[PS5_MULTIDRAW_BATCH_CAPACITY][16];
static uint64_t markers[PS5_MULTIDRAW_BATCH_CAPACITY];
static unsigned allocations, submits, sleeps, unmaps, releases, consumed, flushes;
static unsigned completed[PS5_MULTIDRAW_BATCH_CAPACITY], completion_count;
static int fail_submit, fail_suspend, never_complete;
static jmp_buf fatal;
static void runtime_require_retirement(int ok) { if (!ok) { assert(!unmaps && !releases); longjmp(fatal,1); } }
static int submit(void *p) {
    agc_submit_description_t *d=p;
    const uint32_t *words=d->words;
    assert(d->word_count && !unmaps && !releases);
    ++submits;
    if (fail_submit) return -1;
    unsigned i=words[0]==999 ? 1 : 0;
    while (i+1<d->word_count && words[i]<3000) {
        assert(words[i++]==1000+consumed);
        assert(words[i++]==2000+consumed); /* Preserve every dependency barrier. */
        ++consumed;
    }
    assert(words[i++]==3000+consumed-1); /* Only the group-tail marker remains. */
    if (i<d->word_count) { /* Appended presentation and its final completion. */
        assert(words[i++]==4000);
        assert(words[i++]==5000);
    }
    assert(i==d->word_count);
    completed[completion_count++]=consumed-1;
    return 0;
}
static int suspend_point(void) { return fail_suspend ? -1 : 0; }
static void flush_gpu_data(const void *p,size_t n) { assert(p && n && n<=64); ++flushes; }
static int sceKernelUsleep(uint32_t n) {
    assert(n==1000 && !unmaps && !releases);
    if (++sleeps==3 && !never_complete)
        for (unsigned i=0;i<completion_count;++i) markers[completed[i]]=101+completed[i];
    return 0;
}
static unsigned runtime_completion_pause(int64_t *deadline) {
    (void)deadline; sceKernelUsleep(1000); return 1;
}
static int munmap(void *p,size_t n) {
    assert(p && n==64 && consumed==allocations);
    for (unsigned i=0;i<completion_count;++i) assert(markers[completed[i]]==101+completed[i]);
    ++unmaps; return 0;
}
static int sceKernelReleaseDirectMemory(int64_t p,size_t n) {
    assert(p>=0 && n==64 && unmaps>releases); ++releases; return 0;
}
''' + body + r'''
static void reset(unsigned count) {
    memset(runtime_batch_entries,0,sizeof(runtime_batch_entries));
    memset(runtime_pending,0,sizeof(runtime_pending));
    memset(markers,0,sizeof(markers));
    memset(memory,0xa5,sizeof(memory));
    runtime_batch_count=runtime_pending_head=runtime_pending_batches=0;
    runtime_batch_active=runtime_batch_faulted=0;
    allocations=count; submits=sleeps=unmaps=releases=consumed=flushes=completion_count=0;
    fail_submit=fail_suspend=never_complete=0;
    const agc_api_t api={submit,suspend_point};
    assert(!ps5_agc_gate2_batch_begin());
    for (unsigned i=0;i<count;++i) {
        memory[i][0]=1000+i; memory[i][1]=2000+i; memory[i][2]=3000+i;
        agc_submit_description_t d={memory[i],3,0,{0}};
        struct runtime_batch_entry entry={d,memory[i],i*64,64,&markers[i],101+i,2,16,0};
        assert(!runtime_batch_queue(&api,&entry));
        memset(&entry,0,sizeof(entry)); /* Batch owns a complete value snapshot. */
        assert(runtime_batch_entries[i].completion_offset==2);
        assert(runtime_batch_entries[i].command_capacity==16);
    }
}
int main(void) {
    assert(!ps5_agc_gate2_batch_begin_framebuffer() && runtime_batch_fixed_framebuffer);
    assert(ps5_agc_gate2_batch_begin_framebuffer()!=0);
    assert(!ps5_agc_gate2_batch_end());
    assert(!ps5_agc_gate2_batch_begin() && !runtime_batch_fixed_framebuffer);
    assert(!ps5_agc_gate2_batch_end());
    for (unsigned n=1;n<=PS5_MULTIDRAW_BATCH_CAPACITY;++n) {
        reset(n);
        assert(!ps5_agc_gate2_batch_submit());
        assert(submits==(n+6)/7 && consumed==n && !unmaps);
        assert(!ps5_agc_gate2_batch_retire(0) && !sleeps && !releases);
        assert(ps5_agc_gate2_batch_retire(1)==1 && unmaps==n && releases==n);
        for (unsigned i=0;i<n;++i) assert(memory[i][15]==0xa5a5a5a5); /* Capacity canary. */
    }
    /* Exact-fit capacity, opt-out, submit flags and invalid metadata split groups. */
    for (unsigned mode=0;mode<4;++mode) {
        reset(3);
        if (mode==0) runtime_batch_entries[0].command_capacity=5;
        if (mode==1) runtime_batch_entries[1].completion_offset=0;
        if (mode==2) runtime_batch_entries[1].submit.flag=1;
        if (mode==3) runtime_batch_entries[1].command_capacity=2;
        assert(!ps5_agc_gate2_batch_submit());
        assert(submits==(mode==0?2:3));
        assert(ps5_agc_gate2_batch_retire(1)==1 && releases==3);
    }
    reset(3);
    memory[2][3]=4000; memory[2][4]=5000;
    runtime_batch_entries[2].submit.word_count=5;
    assert(!ps5_agc_gate2_batch_submit() && submits==1);
    assert(ps5_agc_gate2_batch_retire(1)==1 && releases==3);
    /* One acquire per group; preserve every draw and dependency barrier. */
    reset(3);
    for (unsigned i=0;i<3;++i) {
        memmove(memory[i]+1,memory[i],3*sizeof(uint32_t));
        memory[i][0]=999;
        runtime_batch_entries[i].submit.word_count=4;
        runtime_batch_entries[i].completion_offset=3;
        runtime_batch_entries[i].upload_prefix_words=1;
    }
    assert(!ps5_agc_gate2_batch_submit() && submits==1 && consumed==3);
    assert(ps5_agc_gate2_batch_retire(1)==1 && releases==3);
    for (unsigned mode=0;mode<3;++mode) {
        reset(9);
        fail_submit=mode==0; fail_suspend=mode==1; never_complete=mode==2;
        if (!setjmp(fatal)) { ps5_agc_gate2_batch_end(); assert(!"failure returned"); }
        assert(!unmaps && !releases);
        if (mode==2) assert(sleeps==2000);
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "command-groups"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                   input=code, text=True, check=True)
    subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
print("PASS: command groups preserve bodies/barriers, bound capacity, retire all allocations from tail markers, and fail-stop before cleanup")

# Execute the actual tail selection for opt-in and conservative streams. The
# former drops only the post-draw release/wait, never the draw/query/state body.
tail_code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define AGC_TRIANGLE_SUBMIT 1
#define AGC_RUNTIME_PACKAGES 1
#define PS5_MULTIDRAW_BATCH 1
#define PS5_AGC_POST_DRAW_BARRIER_WORDS barrier_words
struct command { uint32_t *up; };
static uint32_t *release(void *p, ...) { struct command *c=p; *c->up++=45; return c->up; }
static const struct { uint32_t *(*release_mem)(void *, ...); } agc={release};
static uint32_t *runtime_release_completion(const void *a, struct command *c,
                                           void *marker, uint32_t expected) {
    assert(a && marker && expected==101); *c->up++=101; return c->up;
}
static unsigned check(int active, int fixed, unsigned barrier_words) {
    uint32_t words[64]={0}, draw_words=32, completion_offset=0;
    struct command command={words+draw_words};
    int runtime_batch_active=active, runtime_batch_fixed_framebuffer=fixed;
    const char *failure_phase;
    uint64_t marker=0, render_marker=101;
    void *completion_marker=&marker;
''' + tail + r'''
receipt:
    (void)failure_phase;
    assert(command.up==words+34 && words[32]==45 && words[33]==101);
    return completion_offset;
}
int main(void) {
    assert(check(1,1,15)==17); /* Draw/query/reset body ends before barrier. */
    assert(check(1,1,0)==32);  /* Coalesce unconditional color release too. */
    assert(check(0,1,15)==33 && check(1,0,15)==33);
    assert(check(1,1,32)==33); /* Invalid tail metadata retains old behavior. */
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / 'fixed-framebuffer-tail'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-x', 'c',
                    '-o', str(exe), '-'], input=tail_code, text=True, check=True)
    subprocess.run([str(exe)], check=True)
driver = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
framebuffer = driver[driver.index('static void\nps5_set_framebuffer_state('):]
assert framebuffer.index('ps5_draw_batch_submit();') < framebuffer.index('util_copy_framebuffer_state(')
assert 'util_framebuffer_state_equal(&context->framebuffer, framebuffer)' in framebuffer
print('PASS: fixed-framebuffer tail preserves final releases and conservative fallback; framebuffer switches split batches')
