"""Exercise the actual pacing/statistics helpers and strict matrix accounting."""
from pathlib import Path
import runpy
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
summarize = runpy.run_path(str(ROOT / "tools/summarize-imgui-benchmark.py"))["summarize"]
profile = runpy.run_path(str(ROOT / "tools/summarize-imgui-profile.py"))["summarize"]


def receipt():
    rows = []
    for i, (w, h, fps) in enumerate((w, h, fps) for w, h in ((1920, 1080), (2560, 1440), (3840, 2160))
                                    for fps in (30, 60, 90, 120)):
        rows.append(f"[ps5-imgui-bench] begin case={i} width={w} height={h} target={fps} mode=offscreen-completed")
        rows.extend(f"[ps5-imgui-bench] probe case={i} phase={phase} samples=3 status=0"
                    for phase in ("warmup", "final"))
        rows.append(f"[ps5-imgui-bench] result case={i} warmup=30 frames=90 seconds=30.000000 fps=3.000000 "
                    "render_mean_ms=10 render_p50_ms=10 render_p95_ms=10 render_p99_ms=10 "
                    "frame_p50_ms=333 frame_p95_ms=334 frame_p99_ms=334 render_misses=0 frame_misses=90 driver_draws=180 status=0")
        rows.extend(["[ps5-multidraw-batch] draws=3 attempted=3 waits=1 result=0",
                     "[ps5-deferred-batch] draws=3 result=0"])
    rows.extend(["[ps5-imgui-bench] finished cases=12 status=0", "[ps5-imgui] finished status=0",
                 "[pss-opengl-native] gate completed status=0",
                 "[ps5-agc] present-shutdown unregister=80290009 close=00000000 frames=12",
                 "[ps5-gpu-present] frames=12"])
    return "\n".join(rows)


class BenchmarkTest(unittest.TestCase):
    def test_audit(self):
        text = receipt()
        report = summarize(text)
        self.assertFalse(report["display_fps_measured"])
        self.assertEqual(len(report["cases"]), 12)
        self.assertTrue(all(not case["target_met"] for case in report["cases"]))
        for method in ("unregister=00000000", "method=close"):
            current = text.replace("unregister=80290009", method)
            self.assertEqual(len(summarize(current)["cases"]), 12)
            for bad in (current.replace("close=00000000", "close=ffffffff"),
                        current + "\n[ps5-agc] present-shutdown malformed",
                        current.replace(method + " close=", "method=unknown close=")):
                with self.assertRaises(ValueError):
                    summarize(bad)
        for old, new in (("fps=3.000000", "fps=120"), ("seconds=30.000000", "seconds=29.9"),
                         ("render_mean_ms=10", "render_mean_ms=nan"), ("render_p95_ms=10", "render_p95_ms=9"),
                         ("phase=final samples=3 status=0", "phase=final samples=3 status=1"),
                         ("case=11", "case=10"), ("frames=90", "frames=0"), ("frame_misses=90", "frame_misses=91"),
                         ("driver_draws=180", "driver_draws=179"), ("warmup=30", "warmup=31"),
                         ("attempted=3", "attempted=2"), ("close=00000000", "close=ffffffff"),
                         ("gate completed status=0", "gate completed status=1"),
                         ("[ps5-gpu-present] frames=12", "[ps5-gpu-present] frames=13")):
            with self.subTest(old=old), self.assertRaises(ValueError):
                summarize(text.replace(old, new, 1))
        with self.assertRaises(ValueError):
            summarize(text + "\n[ps5-imgui-bench] finished cases=12 status=0")
        self.assertEqual(len(summarize(text.replace("warmup=30", "warmup=2"))["cases"]), 12)

    def test_actual_pacing_and_statistics(self):
        source = (ROOT / "examples/core33-imgui/benchmark.h").read_text()
        helpers = (ROOT / "examples/core33-imgui/benchmark_timing.h").read_text()
        helpers = helpers[helpers.index("static bool bench_wait"):]
        warmup = source[source.index("    const double warmup_start"):source.index("    const unsigned warmup = frame")]
        code = r'''
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdlib>
static double now;
static int mode, sleeps;
static double bench_seconds() { return now; }
static int usleep(unsigned micros) {
    ++sleeps;
    if (mode == 1) { errno = EIO; return -1; }
    if (mode == 2 && sleeps == 1) { errno = EINTR; return -1; }
    if (mode == 3) now -= 0.01;
    else if (mode != 4) now += micros * 1e-6;
    return 0;
}
''' + helpers + r'''
static double cost;
static bool check(bool ok, const char*) { return ok; }
static bool bench_draw(unsigned, int, int, int, unsigned) { return true; }
static void glFinish() { now += cost; }
static int glGetError() { return 0; }
static const int GL_NO_ERROR = 0;
static unsigned warm_frames(double seconds) {
    cost = seconds; now = 0;
    unsigned frame = 0, warmup_limit = 30, fbo = 0;
    int width = 1920, height = 1080, target = 30;
    bool ok = true;
''' + warmup + r'''
    return ok ? frame : 0;
}
int main() {
    assert(warm_frames(0.25) == 4);
    assert(warm_frames(0.9) == 2);
    assert(warm_frames(0.001) == 30);
    assert(warm_frames(-0.01) == 0);
    double values[] = {6, 1, 4, 2, 5, 3};
    qsort(values, 6, sizeof(double), bench_compare);
    assert(bench_percentile(values, 6, 50) == 3);
    assert(bench_percentile(values, 6, 95) == 6);
    assert(bench_percentile(values, 6, 99) == 6);
    assert(bench_percentile(values, 1, 50) == 1);
    for (mode = 0; mode <= 4; ++mode) {
        now = 10; sleeps = 0;
        assert(bench_wait(10.01) == (mode == 0 || mode == 2));
        assert(sleeps <= 16);
    }
    now = -1; assert(!bench_wait(10));
    now = 12; sleeps = 0; assert(bench_wait(11) && sleeps == 0);
    now = 10; assert(!bench_wait(12));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            executable = str(Path(directory) / "benchmark-helpers")
            subprocess.run(["clang++-18", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-x", "c++", "-",
                            "-o", executable], input=code, text=True, check=True)
            subprocess.run([executable], check=True)

    def test_window_audit(self):
        text = """[ps5-imgui-tv] readback frame=0 rgba=45,215,245,255 PASS
[ps5-imgui-tv] readback frame=10 rgba=45,215,245,255 PASS
[ps5-imgui-perf] frames=1800 warmup=30 ui_ms=1 clear_ms=2 draw_ms=3 readback_ms=0 swap_ms=10 cpu_wall_ms=16 status=0
[ps5-imgui-tv] finished frames=1830 changes=0 status=0
[ps5-imgui] finished status=0
[pss-opengl-native] gate completed status=0
[ps5-present-perf] calls=1800 failures=0 warmup_frames=30 idle_ms=0 flip_ms=0 vblank_ms=0 total_ms=0
[ps5-batch-perf] calls=1800 failures=0 warmup_frames=30 sleeps=1800 submit_ms=0.1 suspend_ms=0.2 poll_ms=2.4 cleanup_ms=0.3 total_ms=3
[ps5-gpu-present] frames=1828
[ps5-agc] present-shutdown unregister=80290009 close=00000000 frames=1830
[ps5-imgui-window] begin width=1920 height=1080 target=60 mode=window-presented
[ps5-imgui-window] result frames=1800 seconds=30.000000 fps=60.000000 active_mean_ms=16 active_p50_ms=16 active_p95_ms=16.5 active_p99_ms=16.6 frame_p50_ms=16.666667 frame_p95_ms=17 frame_p99_ms=18 active_misses=0 frame_misses=3 status=0
""" + ("[ps5-multidraw-batch] draws=3 attempted=3 waits=1 result=0\n"
       "[ps5-deferred-batch] draws=3 result=0\n") * 1830
        self.assertTrue(profile(text, window_target=60)["window_benchmark"]["target_met"])
        for method in ("unregister=00000000", "method=close"):
            current = text.replace("unregister=80290009", method)
            self.assertTrue(profile(current, window_target=60)["window_benchmark"]["target_met"])
            for bad in (current.replace("close=00000000", "close=ffffffff"),
                        current + "\n[ps5-agc] present-shutdown malformed",
                        current.replace(method + " close=", "method=unknown close=")):
                with self.assertRaises(ValueError):
                    profile(bad, window_target=60)
        for old, new in (("seconds=30.000000", "seconds=29.900000"),
                         ("fps=60.000000", "fps=600"), ("target=60", "target=120"),
                         ("width=1920", "width=2560"), ("active_mean_ms=16", "active_mean_ms=15"),
                         ("active_p95_ms=16.5", "active_p95_ms=nan"),
                         ("frame_p95_ms=17", "frame_p95_ms=15"),
                         ("frame_misses=3", "frame_misses=1801"), ("changes=0", "changes=1"),
                         ("close=00000000", "close=ffffffff"),
                         ("[ps5-gpu-present] frames=1828", ""),
                         ("[ps5-batch-perf]", "[missing-batch-perf]")):
            with self.subTest(old=old), self.assertRaises(ValueError):
                profile(text.replace(old, new), window_target=60)
        with self.assertRaises(ValueError):
            profile(text + "\n[ps5-imgui-window] result malformed", window_target=60)
        # A missed performance target remains a valid measurement, not a pass claim.
        slow = text.replace("seconds=30.000000 fps=60.000000", "seconds=31.000000 fps=58.064516")
        self.assertFalse(profile(slow, window_target=60)["window_benchmark"]["target_met"])
        for height, width, size in ((1080, 1920, 0xa00000), (1440, 2560, 0x1000000), (2160, 3840, 0x2000000)):
            mode = text.replace("width=1920 height=1080", f"width={width} height={height}")
            status = "\n".join(f"[ps5-output] stage={stage} render_width={width} render_height={height} "
                f"buffer_bytes={size} resolution_rc=00000000 full_width=3840 full_height=2160 "
                "pane_width=1920 pane_height=1080 refresh_id=3 output_rc=00000000 output_refresh_id=3"
                for stage in ("warmup", "end"))
            mode += f"[ps5-output-register] width={width} height={height} offset={size} result=00000000\n"
            result = profile(mode + status, window_target=60, window_height=height, output_status=True)
            self.assertEqual(result["videoout_status"]["reported_refresh_hz"], 59.94)
            self.assertFalse(result["window_benchmark"]["output_mode_verified"])
            for old, new in (("resolution_rc=00000000", "resolution_rc=ffffffff"),
                             ("full_width=3840", "full_width=0"), ("output_refresh_id=3", "output_refresh_id=13"),
                             ("refresh_id=3", "refresh_id=999")):
                uncertain = profile(mode + status.replace(old, new, 1), window_target=60,
                                    window_height=height, output_status=True)
                self.assertIsNone(uncertain["videoout_status"]["reported_refresh_hz"])
            for bad in (status.replace(f"buffer_bytes={size}", "buffer_bytes=1", 1),
                        status.replace("stage=end", "stage=warmup"), "", status + "\n" + status):
                with self.assertRaises(ValueError):
                    profile(mode + bad, window_target=60, window_height=height, output_status=True)
            with self.assertRaises(ValueError):
                profile(mode.replace(f"offset={size}", "offset=0") + status,
                        window_target=60, window_height=height, output_status=True)
        for fps, refresh_id in ((90, 13), (120, 13)):
            frames = fps * 30
            hfr = text.split("[ps5-multidraw-batch]", 1)[0]
            for old, new in (("target=60", f"target={fps}"), ("1800", str(frames)),
                             ("1830", str(frames + 30)), ("1828", str(frames + 28)),
                             ("fps=60.000000", f"fps={fps}.000000"),
                             ("clear_ms=2 draw_ms=3", "clear_ms=1 draw_ms=1"),
                             ("swap_ms=10 cpu_wall_ms=16", "swap_ms=5 cpu_wall_ms=8"),
                             ("active_mean_ms=16 active_p50_ms=16 active_p95_ms=16.5 active_p99_ms=16.6",
                              "active_mean_ms=8 active_p50_ms=8 active_p95_ms=8 active_p99_ms=8")):
                hfr = hfr.replace(old, new)
            hfr += ("[ps5-multidraw-batch] draws=3 attempted=3 waits=1 result=0\n"
                    "[ps5-deferred-batch] draws=3 result=0\n") * (frames + 30)
            hfr += "[ps5-output-register] width=1920 height=1080 offset=10485760 result=00000000\n"
            hfr += "[ps5-output-mode] target=120 support=00000001 preset=00000000 vrr=ffffffff result=00000000\n"
            hfr += "[ps5-output-restore] result=00000000 wait=00000000\n"
            for stage, mode_id in (("warmup", refresh_id), ("end", refresh_id), ("restored", 3)):
                hfr += f"[ps5-output] stage={stage} render_width=1920 render_height=1080 " \
                       "buffer_bytes=10485760 resolution_rc=00000000 full_width=3840 full_height=2160 " \
                       f"pane_width=3840 pane_height=2160 refresh_id={mode_id} output_rc=00000000 output_refresh_id={mode_id}\n"
            report = profile(hfr, window_target=fps)
            self.assertTrue(report["window_benchmark"]["target_met"])
            self.assertTrue(report["videoout_status"]["normal_output_restored"])
            self.assertEqual(report["videoout_status"]["application_paced_on_fixed_refresh"], fps == 90)
            self.assertFalse(report["videoout_status"]["physical_output_independently_verified"])
            for old, new in (("support=00000001", "support=00000000"),
                             ("support=00000001", "support=ffffffff"), ("preset=00000000", "preset=ffffffff"),
                             ("vrr=ffffffff", "vrr=8029001c"),
                             ("wait=00000000", "wait=ffffffff"), ("stage=restored", "stage=end"),
                             (f"refresh_id={refresh_id}", "refresh_id=3"),
                             ("[ps5-output-mode]", "[missing-output-mode]"),
                             ("[ps5-output-restore] result=00000000", "[ps5-output-restore] result=ffffffff")):
                with self.subTest(fps=fps, old=old), self.assertRaises(ValueError):
                    profile(hfr.replace(old, new, 1), window_target=fps)
            with self.assertRaises(ValueError):
                profile(hfr + "\n[ps5-output-restore]malformed", window_target=fps)
