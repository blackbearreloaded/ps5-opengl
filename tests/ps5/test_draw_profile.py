#!/usr/bin/env python3
"""Compile the runtime's actual opt-in timing accumulator and reset logic."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.index("static uint64_t runtime_profile_ns")
body = source[start:source.index("#define PS5_PROFILE_MARK", start)]
present_start = source.index("int ps5_agc_gate2_present(unsigned buffer_index)")
present = source[present_start:source.index("\n#endif", source.index("    return result;", present_start))]
mock = r'''
static unsigned runtime_present_count, runtime_batch_active, runtime_batch_count, runtime_batch_faulted;
static int runtime_video_registered = 1, runtime_video_handle = 7;
static int idle_result, flip_result, vblank_result, step, flips, vblanks;
static int64_t clock_ns = 1;
static int64_t os_time_get_nano(void) { return clock_ns; }
static int runtime_video_wait_idle(void) {
    assert(step++ == 0); clock_ns += 1000000; return idle_result;
}
static int64_t runtime_next_render_marker(void) { return 123; }
static int submit_flip(int handle, int buffer, unsigned mode, int64_t marker) {
    assert(step++ == 1 && handle == 7 && buffer == 1 && mode == 1 && marker == 123);
    ++flips; clock_ns += 2000000; return flip_result;
}
static int wait_vblank(int handle) {
    assert(step++ == 2 && handle == 7);
    ++vblanks; clock_ns += 3000000; return vblank_result;
}
static int is_flip_pending(int handle) { (void)handle; return 0; }
static struct {
    int (*submit_flip)(int, int, unsigned, int64_t);
    int (*wait_vblank)(int);
    int (*is_flip_pending)(int);
} runtime_video_api = {submit_flip, wait_vblank, is_flip_pending};
'''
code = "#include <inttypes.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n" + body + mock + r'''
#define PS5_MULTIDRAW_BATCH 1
#define PS5_DRAW_PROFILE 1
#define PS5_PROFILE_MARK(i) profile_ticks[i] = os_time_get_nano()
''' + present + r'''
int main(void) {
    int64_t ticks[10] = {1, 1000001, 3000001, 3000001, 4000001, 4000001, 4000001, 4000001, 5000001, 6000001};
    runtime_profile_report(); /* empty reports stay quiet */
    runtime_profile_record(ticks, 1, 0);
    runtime_profile_record(ticks, 2, 0);
    assert(runtime_profile_calls == 2 && !runtime_profile_failures);
    assert(runtime_profile_ns[0] == 2000000 && runtime_profile_ns[1] == 4000000);
    runtime_profile_record(ticks, 9, 1);
    ticks[9] = ticks[8] - 1;
    runtime_profile_record(ticks, 9, 0);
    ticks[9] = 0;
    runtime_profile_record(ticks, 9, 0);
    assert(runtime_profile_calls == 2 && runtime_profile_failures == 3);
    assert(runtime_profile_sleeps == 3);
    runtime_profile_report();
    assert(!runtime_profile_calls && !runtime_profile_failures && !runtime_profile_sleeps);
    for (unsigned i = 0; i < 9; ++i) assert(!runtime_profile_ns[i]);
    runtime_profile_report();
    /* Instrument the actual presentation path: no change in call order or errors. */
    for (unsigned i = 0; i < 32; ++i) {
        step = 0;
        assert(ps5_agc_gate2_present(1) == 0 && step == 3);
    }
    assert(runtime_present_count == 32 && runtime_present_profile_calls == 2);
    assert(runtime_present_profile_ns[0] == 2000000);
    assert(runtime_present_profile_ns[1] == 4000000);
    assert(runtime_present_profile_ns[2] == 6000000);
    step = 0; idle_result = -7;
    assert(ps5_agc_gate2_present(1) == -1 && step == 1);
    step = 0; idle_result = 0; flip_result = -8;
    assert(ps5_agc_gate2_present(1) == -8 && step == 2);
    step = 0; flip_result = 0; vblank_result = -9;
    assert(ps5_agc_gate2_present(1) == -9 && step == 3);
    assert(runtime_present_count == 32 && runtime_present_profile_calls == 2);
    assert(runtime_present_profile_failures == 3 && flips == 34 && vblanks == 33);
    step = 0;
    assert(ps5_agc_gate2_present(2) == -1 && step == 0);
    runtime_batch_active = 1;
    assert(ps5_agc_gate2_present(1) == -1 && step == 0);
    runtime_batch_active = 0; runtime_batch_count = 1;
    assert(ps5_agc_gate2_present(1) == -1 && step == 0);
    runtime_batch_count = 0; runtime_batch_faulted = 1;
    assert(ps5_agc_gate2_present(1) == -1 && step == 0);
    int64_t bad[4] = {1, 2, 3, 0};
    runtime_present_profile_record(bad, 0);
    bad[3] = 2;
    runtime_present_profile_record(bad, 0);
    assert(runtime_present_profile_calls == 2 && runtime_present_profile_failures == 5);
    runtime_profile_report();
    assert(!runtime_present_profile_calls && !runtime_present_profile_failures);
    for (unsigned i = 0; i < 3; ++i) assert(!runtime_present_profile_ns[i]);
    runtime_profile_report();
    int64_t batch_ticks[5] = {1, 1000001, 3000001, 6000001, 10000001};
    runtime_batch_profile_record(batch_ticks, 1, 0);
    runtime_batch_profile_record(batch_ticks, 2, 0);
    runtime_batch_profile_record(batch_ticks, 9, -1);
    batch_ticks[4] = 0;
    runtime_batch_profile_record(batch_ticks, 9, 0);
    batch_ticks[4] = batch_ticks[3] - 1;
    runtime_batch_profile_record(batch_ticks, 9, 0);
    assert(runtime_batch_profile_calls == 2 && runtime_batch_profile_failures == 3);
    assert(runtime_batch_profile_sleeps == 3);
    runtime_profile_report();
    assert(!runtime_batch_profile_calls && !runtime_batch_profile_failures && !runtime_batch_profile_sleeps);
    for (unsigned i = 0; i < 4; ++i) assert(!runtime_batch_profile_ns[i]);
    runtime_profile_report();
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "profile.c"
    exe = Path(tmp) / "profile"
    c.write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
    output = subprocess.check_output([str(exe)], text=True)
    assert len(output.splitlines()) == 3
    assert "[ps5-batch-perf] calls=2 failures=3 warmup_frames=30 sleeps=3 submit_ms=1.000000 suspend_ms=2.000000 poll_ms=3.000000 cleanup_ms=4.000000 total_ms=10.000000" in output
    assert "calls=2 failures=3 warmup_frames=30" in output
    assert "scanout_flush_ms=2.000000" in output and "total_ms=6.000000" in output
    assert "[ps5-present-perf] calls=2 failures=5 warmup_frames=30 idle_ms=1.000000 flip_ms=2.000000 vblank_ms=3.000000 total_ms=6.000000" in output
print("PASS: draw/present timing, warmup, call order, failures, invalid clocks, report/reset")
