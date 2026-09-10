#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise actual startup accounting with synthetic time, not GPU/performance evidence."""
from pathlib import Path
import runpy
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "examples/core33-imgui/tv_demo.h").read_text()
start = source.index("struct DemoStartupTiming {")
accounting = source[start:source.index("\n#endif", start)]
# Retain the actual boundary-before-duration-break ordering, with rendering mocked below.
loop_start = source.index("    while (ok) {")
loop_head = source[loop_start:source.index("        io.DeltaTime = static_cast<float>", loop_start)]
code = "#include <cmath>\n#include <cstdio>\n" + accounting + r'''
static void run(unsigned count) {
    DemoStartupTiming timing;
    double elapsed = 0;
    for (unsigned frame = 0; frame < count; ++frame) {
        timing.boundary(frame, elapsed);
        double ticks[6] = {100 + elapsed, 0, 0, 0, 0, 0};
        for (unsigned i = 0; i < 5; ++i)
            ticks[i + 1] = ticks[i] + (i + 1) / 64.0 + (frame == 0 && i == 0 ? 2.0 : 0);
        timing.record(frame, ticks);
        elapsed = ticks[5] - 100;
        if (frame == 0) {
            timing.record_log(100 + elapsed, 100 + elapsed + 2.0 / 64);
            elapsed += 2.0 / 64;
        }
        elapsed += 1.0 / 64; // Unstaged loop bookkeeping, including the final frame's tail.
    }
    timing.boundary(count, elapsed);
    timing.report(elapsed);
}
static double fake_clock;
static double demo_seconds() { return fake_clock; }
static bool check(bool ok, const char*) { return ok; }
static void terminal_30s() {
    DemoStartupTiming startup;
    unsigned frame = 0;
    bool ok = true;
    const double start = 0, duration = 30;
    double previous = start;
''' + loop_head + r'''
#endif
        double ticks[6] = {now, now + 0.125, now + 0.25, now + 0.375, now + 0.5, now + 0.625};
        startup.record(frame, ticks);
        previous = now;
        fake_clock += 1; // 0.625s in stages plus 0.375s outside them, all synthetic.
        ++frame;
    }
    startup.report(demo_seconds() - start);
}
int main() {
    run(130); // Freeze the first window, but retain the independent frame-30 boundary.
    terminal_30s(); // Plain 30s profile: capture frame 30 and window completion before break.
    run(12);  // Short host reference: never claim 30 frames or a complete 30s window.
    DemoStartupTiming empty;
    empty.report(0.25); // Early failure: no completed stages; all elapsed time is outside them.
    DemoStartupTiming overshoot;
    overshoot.boundary(0, 0);
    double slow[6] = {0, 31, 31, 31, 31, 31};
    overshoot.record(0, slow);
    overshoot.record_log(31, 31.25);
    overshoot.boundary(1, 31.5);
    overshoot.report(40); // Reporting later must not extend the frozen interval.
    DemoStartupTiming bad_stage;
    double backwards[6] = {10, 11, 10, 12, 13, 14};
    bad_stage.record(0, backwards);
    bad_stage.report(5);
    DemoStartupTiming bad_log;
    bad_log.record_log(-1, 0);
    bad_log.report(1);
    DemoStartupTiming bad_end;
    bad_end.boundary(0, 1);
    bad_end.report(0);
    DemoStartupTiming nonfinite;
    nonfinite.boundary(0, NAN);
    nonfinite.report(NAN);
}
'''
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / "imgui-startup")
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-DPS5_IMGUI_PROFILE", "-x", "c++", "-", "-o", executable],
                   input=code, text=True, check=True)
    output = subprocess.check_output([executable], text=True)

lines = output.splitlines()
assert len(lines) == 9 and all(line.startswith("[ps5-imgui-startup] ") for line in lines)
rows = [dict(token.split("=", 1) for token in line.split()[1:]) for line in lines]
full, terminal, short, empty, overshoot = rows[:5]
for row, count in ((full, 112), (short, 12)):
    assert float(row["window_seconds"]) == 2.03125 + count * 0.25
    assert float(row["window_stage_ms"]) == (2 + count * 15 / 64) * 1000
    assert float(row["window_outside_stages_ms"]) == (count + 2) / 64 * 1000
    assert float(row["window_snapshot_log_ms"]) == 31.25
    assert row["clock_valid"] == "1"
    for group, frames in (("frame0", 1), ("first30", min(count, 30)), ("window", count)):
        assert int(row[f"{group}_frames"]) == frames
        for i, phase in enumerate(("ui", "clear", "draw", "readback", "swap")):
            expected = (frames * (i + 1) / 64 + (2 if i == 0 else 0)) * 1000
            assert float(row[f"{group}_{phase}_ms"]) == expected
assert full["window_complete"] == "1" and float(full["frame30_seconds"]) == 9.53125
assert terminal["window_complete"] == terminal["clock_valid"] == "1"
assert float(terminal["frame30_seconds"]) == float(terminal["window_seconds"]) == 30
assert terminal["first30_frames"] == terminal["window_frames"] == "30"
assert float(terminal["window_stage_ms"]) == 18750
assert float(terminal["window_outside_stages_ms"]) == 11250
assert float(terminal["window_snapshot_log_ms"]) == 0
assert short["window_complete"] == "0" and float(short["frame30_seconds"]) == -1
assert empty["window_complete"] == "0" and empty["window_frames"] == "0"
assert float(empty["window_stage_ms"]) == 0 and float(empty["window_outside_stages_ms"]) == 250
assert overshoot["window_complete"] == "1" and overshoot["window_frames"] == "1"
assert float(overshoot["window_seconds"]) == 31.5 and float(overshoot["frame30_seconds"]) == -1
assert float(overshoot["window_stage_ms"]) == 31000
assert float(overshoot["window_outside_stages_ms"]) == 500
assert float(overshoot["window_snapshot_log_ms"]) == 250
assert all(row["clock_valid"] == "0" for row in rows[5:])

# The additive receipt cannot turn a startup cadence miss into an accepted soak.
summarize = runpy.run_path(str(root / "tools/summarize-imgui-profile.py"))["summarize"]
legacy = """[ps5-imgui-tv] readback frame=0 rgba=45,215,245,255 PASS
[ps5-imgui-tv] readback frame=10 rgba=45,215,245,255 PASS
[ps5-imgui-tv] readback frame=3365 rgba=45,215,245,255 PASS
[ps5-imgui-tv] visible frame=0 elapsed=0.0 pad=0 changes=0 vertices=100
[ps5-imgui-tv] visible frame=3365 elapsed=30.0 pad=0 changes=0 vertices=100
[ps5-imgui-perf] frames=6930 warmup=30 ui_ms=1 clear_ms=1 draw_ms=1 readback_ms=0 swap_ms=5.34 cpu_wall_ms=8.34 status=0
[ps5-imgui-tv] finished frames=6960 changes=0 status=0
[ps5-imgui] finished status=0
[ps5-opengl-native] gate completed status=0
"""
options = dict(soak=True, soak_seconds=60, soak_target=120)
before = summarize(legacy, strict_soak_fps=False, **options)
after = summarize(legacy + lines[0] + "\n", strict_soak_fps=False, **options)
assert before == after and not after["soak"]["target_met"]
for text in (legacy, legacy + lines[0] + "\n"):
    try:
        summarize(text, **options)
    except ValueError as error:
        assert "sustained 120 Hz performance target not met" in str(error)
    else:
        raise AssertionError("Startup diagnostics changed soak acceptance")
print("PASS: startup stages, 30-frame/30s boundaries, bookkeeping, partial/invalid clocks, legacy acceptance")
