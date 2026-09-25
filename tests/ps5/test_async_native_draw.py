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
struct runtime_draw_inputs { int value; };
struct ps5_agc_backend_draw_state { int value; };
static _Thread_local struct runtime_draw_inputs runtime_draw_state;
static _Thread_local struct ps5_agc_backend_draw_state ps5_agc_draw_state;
static _Thread_local uint32_t runtime_ngg_ge_pc_alloc_valid;
static _Thread_local uint32_t runtime_ngg_ge_pc_alloc;
static int runtime_batch_active;
'''
fixture += queue
fixture += r'''
static int seen, fail_at = -1;
int ps5_agc_gate2_run_sync(void)
{
    int n = runtime_draw_state.value;
    assert(n == seen++);
    assert(ps5_agc_draw_state.value == n * 2);
    assert(runtime_ngg_ge_pc_alloc_valid == 1);
    assert(runtime_ngg_ge_pc_alloc == (uint32_t)n + 7);
    return n == fail_at ? -7 : 0;
}
int main(void)
{
    runtime_batch_active = 1;
    for (int i = 0; i < 600; ++i) {
        runtime_draw_state.value = i;
        ps5_agc_draw_state.value = i * 2;
        runtime_ngg_ge_pc_alloc_valid = 1;
        runtime_ngg_ge_pc_alloc = i + 7;
        assert(ps5_agc_gate2_run() == 0);
    }
    assert(runtime_async_drain() == 0 && seen == 600);
    runtime_async_stop();
    fail_at = 627;
    for (int i = 600; i < 640; ++i) {
        runtime_draw_state.value = i;
        ps5_agc_draw_state.value = i * 2;
        runtime_ngg_ge_pc_alloc = i + 7;
        if (ps5_agc_gate2_run() != 0) break;
    }
    assert(runtime_async_drain() == -7);
    runtime_async_stop();
    puts("C29 async job-state, ordering, capacity and failure PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(fixture)
    subprocess.run(["clang-18", "-std=c11", "-O1", "-g", "-pthread",
                    "-fsanitize=address,undefined", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
