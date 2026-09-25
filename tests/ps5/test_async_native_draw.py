#!/usr/bin/env python3
"""Run the real native draw queue against concurrent state/order/failure checks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.index("#ifdef PS5_ASYNC_NATIVE_PREP\n/* The producer")
end = source.index("\n#endif\n\nint ps5_agc_gate2_batch_begin", start) + len("\n#endif")
queue = source[start:end]
queue = queue.replace('(void)worker;\n    return 0;', 'return test_pin(worker);', 1)
start = source.index('static int runtime_batch_queue(')
publish = source[start:source.index('\n}', start) + 2]
start = source.index('static int64_t runtime_next_render_marker(void)\n{')
marker = source[start:source.index('\n}', start) + 2]
fixture = r'''
#define _POSIX_C_SOURCE 200809L
#define PS5_ASYNC_NATIVE_PREP 1
#define PS5_ASYNC_HOST_TEST 1
#define PS5_MULTIDRAW_BATCH_CAPACITY 256
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
struct runtime_draw_inputs { int value; void *hs_package; };
struct ps5_agc_backend_draw_state { int value; };
static _Thread_local struct runtime_draw_inputs runtime_draw_state;
static _Thread_local struct ps5_agc_backend_draw_state ps5_agc_draw_state;
static _Thread_local uint32_t runtime_ngg_ge_pc_alloc_valid;
static _Thread_local uint32_t runtime_ngg_ge_pc_alloc;
static int runtime_batch_active;
typedef struct { int value; } agc_api_t;
struct runtime_batch_entry { int value; };
static int runtime_batch_append(const agc_api_t *, const struct runtime_batch_entry *);
#define RENDER_MARKER 256
static uint64_t runtime_render_marker = RENDER_MARKER;
static int fail_create=-1, fail_pin=-1, create_attempts, created_threads, joined_threads;
static int test_create(pthread_t *thread,const pthread_attr_t *attr,void *(*fn)(void*),void *arg) {
    if(create_attempts++==fail_create) return 11;
    int rc=pthread_create(thread,attr,fn,arg);
    if(!rc) ++created_threads;
    return rc;
}
static int test_join(pthread_t thread,void **value) {
    int rc=pthread_join(thread,value);
    if(!rc) ++joined_threads;
    return rc;
}
static int test_pin(unsigned worker) { return (int)worker==fail_pin ? -1 : 0; }
#define pthread_create test_create
#define pthread_join test_join
'''
fixture += marker + '\n' + queue + '\n' + publish
fixture += r'''
static int runtime_async_prepare_draw(void) { return 0; }
static atomic_int seen, visits[640], forced_reverse;
static int fail_at = -1;
static int appended, last_appended = -1;
static int reject_append;
static int runtime_batch_append(const agc_api_t *api, const struct runtime_batch_entry *entry)
{
    if (reject_append) return -1;
    assert(api->value == entry->value * 3);
    assert(entry->value > last_appended);
    last_appended = entry->value;
    ++appended;
    return 0;
}
int ps5_agc_gate2_run_sync(void)
{
    int n = runtime_draw_state.value;
    if (runtime_draw_state.hs_package) {
        assert(!runtime_async_output && appended==atomic_load(&seen));
        return 0;
    }
    assert(n>=0 && n<640 && atomic_fetch_add(&visits[n],1)==0);
    atomic_fetch_add(&seen,1);
    assert(ps5_agc_draw_state.value == n * 2);
    assert(runtime_ngg_ge_pc_alloc_valid == 1);
    assert(runtime_ngg_ge_pc_alloc == (uint32_t)n + 7);
    assert(runtime_async_output->marker == (int64_t)(n + 1) * 256);
#if PS5_NATIVE_PREP_WORKERS > 1
    if(n==0) {
        while(atomic_load_explicit(&runtime_async_jobs[1].ready,memory_order_acquire)!=2) {}
        atomic_store(&forced_reverse,1); /* Job1 really completed before job0. */
    }
#endif
    if (n == fail_at) return -7;
    agc_api_t api = {n * 3};
    struct runtime_batch_entry entry = {n};
    assert(runtime_batch_queue(&api, &entry) == 0);
    assert(runtime_batch_queue(&api, &entry) != 0); /* One result per job. */
    entry.value = -1; /* Output owns a value, not this stack variable. */
    return 0;
}
int main(void)
{
    runtime_batch_active = 1;
    for(int failure=0;failure<PS5_NATIVE_PREP_WORKERS;++failure) {
        fail_create=failure; create_attempts=0;
        assert(ps5_agc_gate2_run()!=0 && !runtime_async_started);
        assert(created_threads==joined_threads && !atomic_load(&runtime_async_produced));
    }
    fail_create=-1;
    for(int failure=0;failure<PS5_NATIVE_PREP_WORKERS;++failure) {
        fail_pin=failure;
        assert(ps5_agc_gate2_run()!=0 && !runtime_async_started);
        assert(created_threads==joined_threads && !atomic_load(&runtime_async_produced));
    }
    fail_pin=-1;
    for (int i = 0; i < 600; ++i) {
        runtime_draw_state.value = i;
        ps5_agc_draw_state.value = i * 2;
        runtime_ngg_ge_pc_alloc_valid = 1;
        runtime_ngg_ge_pc_alloc = i + 7;
        assert(ps5_agc_gate2_run() == 0);
    }
    assert(runtime_async_drain() == 0 && seen == 600 && appended == 600);
#if PS5_NATIVE_PREP_WORKERS > 1
    assert(atomic_load(&forced_reverse));
#else
    assert(!atomic_load(&forced_reverse));
#endif
    runtime_draw_state.hs_package=(void*)1;
    assert(!ps5_agc_gate2_run());
    runtime_draw_state.hs_package=NULL;
    runtime_async_stop();
    assert(created_threads==joined_threads);
    fail_at = 627;
    for (int i = 600; i < 640; ++i) {
        runtime_draw_state.value = i;
        ps5_agc_draw_state.value = i * 2;
        runtime_ngg_ge_pc_alloc = i + 7;
        if (ps5_agc_gate2_run() != 0) break;
    }
    assert(runtime_async_drain() == -7);
    runtime_async_stop();
    assert(appended == seen - 1); /* Failed job owns no result; others retained. */
    /* Also exercise a whole reverse-completed batch. */
    appended = 0; last_appended = -1;
    for (int i = 7; i >= 0; --i) {
        struct runtime_async_job *job = &runtime_async_jobs[i];
        job->api.value = i * 3;
        job->entry.value = i;
        job->has_entry = 1;
        atomic_store_explicit(&job->ready, i + 1, memory_order_release);
    }
    for (int i = 0; i < 8; ++i) {
        runtime_async_collect_one();
        assert(appended == i + 1 && last_appended == i);
    }
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        reject_append = 1;
        struct runtime_async_job *job = &runtime_async_jobs[8];
        job->has_entry = 1;
        atomic_store(&job->ready, 9);
        runtime_async_collect_one();
        _exit(99);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    runtime_render_marker = UINT64_MAX - 255;
    assert((uint64_t)runtime_next_render_marker() == UINT64_MAX - 255);
    assert(runtime_next_render_marker() == RENDER_MARKER);
    puts("PASS: async input/output ownership, ordered reverse-completion collection, ring wrap, duplicate result and failure");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(fixture)
    for workers in (1, 2):
        subprocess.run(["clang-18", "-std=c11", "-O1", "-g", "-pthread",
                        f"-DPS5_NATIVE_PREP_WORKERS={workers}",
                        "-fsanitize=address,undefined", "-Wall", "-Wextra", "-Werror",
                        str(path / "test.c"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True, timeout=30)
