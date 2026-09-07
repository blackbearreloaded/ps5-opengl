#!/usr/bin/env python3
"""Audit completed offscreen work separately from preview/display throughput."""
import argparse
import json
import math
from pathlib import Path
import re


def summarize(text, host=False, case=-1):
    def require(ok, message):
        if not ok:
            raise ValueError(message)

    def records(kind):
        lines = re.findall(rf"^\[ps5-imgui-bench\] {kind} (.+)$", text, re.M)
        parsed = []
        for line in lines:
            pairs = [token.split("=", 1) for token in line.split()]
            require(all(len(pair) == 2 for pair in pairs), "malformed fields")
            row = dict(pairs)
            require(len(row) == len(pairs), "duplicate fields")
            parsed.append(row)
        return parsed

    begins, results, probes = records("begin"), records("result"), records("probe")
    require(case in range(-1, 12), "invalid selected case")
    expected = list(enumerate((w, h, fps) for w, h in ((1920, 1080), (2560, 1440), (3840, 2160))
                             for fps in (30, 60, 90, 120)))
    if case >= 0:
        expected = [expected[case]]
    cases = len(expected)
    require(len(begins) == len(results) == cases and len(probes) == 2 * cases,
            "incomplete/duplicate matrix or selected case")
    timings = ["render_mean_ms", "render_p50_ms", "render_p95_ms", "render_p99_ms",
               "frame_p50_ms", "frame_p95_ms", "frame_p99_ms"]
    report = []
    for position, (begin, result, (i, (width, height, target))) in enumerate(zip(begins, results, expected)):
        require(begin == dict(case=str(i), width=str(width), height=str(height), target=str(target),
                              mode="offscreen-completed"), "wrong matrix order or workload")
        require(set(result) == set(timings + ["case", "warmup", "frames", "seconds", "fps",
                                            "render_misses", "frame_misses", "driver_draws", "status"]), "unexpected fields")
        require(result["case"] == str(i) and result["status"] == "0", "failed/wrong case")
        count, warmup = int(result["frames"]), int(result["warmup"])
        require((warmup == 2 if host else 2 <= warmup <= 30) and 2 <= count <= 8192, "invalid sample count")
        driver_draws = int(result["driver_draws"])
        require(driver_draws == 0 if host else 2 * count <= driver_draws <= 8 * count,
                "missing/invalid retired driver draws")
        seconds, fps = float(result["seconds"]), float(result["fps"])
        require(math.isfinite(seconds) and math.isfinite(fps) and seconds > 0 and fps > 0,
                "invalid sample duration/throughput")
        # Both values are printed to six decimal places (host samples are very short).
        require(abs(fps * seconds - count) <= (fps + seconds) * 0.00000051 + 0.000001,
                "FPS accounting mismatch")
        require((count == 6) if host else (30 <= seconds <= 32 and fps <= target * 1.002),
                "wrong measurement window or pacing")
        values = {key: float(result[key]) for key in timings}
        require(all(math.isfinite(v) and v > 0 for v in values.values()), "invalid timing")
        for prefix in ("render", "frame"):
            require(values[f"{prefix}_p50_ms"] <= values[f"{prefix}_p95_ms"] <= values[f"{prefix}_p99_ms"],
                    "unordered percentiles")
        require(values["render_mean_ms"] <= seconds * 1000 / count + 0.001,
                "render time exceeds measured wall time")
        misses = {key: int(result[key]) for key in ("render_misses", "frame_misses")}
        require(all(0 <= v <= count for v in misses.values()), "invalid missed-budget count")
        require(probes[position * 2:position * 2 + 2] == [dict(case=str(i), phase=phase, samples="3", status="0")
                                          for phase in ("warmup", "final")], "missing/failed pixels")
        report.append(dict(width=width, height=height, target_fps=target, frames=count, seconds=seconds,
                           achieved_fps=fps, target_met=fps >= target * 0.99,
                           warmup_frames=warmup, driver_draws=driver_draws,
                           frame_budget_tolerance_ms=0.25, **values, **misses))
    require(records("finished") == [dict(cases=str(cases), status="0")], "matrix cleanup failed")
    require(re.findall(r"^\[ps5-imgui\] finished status=(\d+)$", text, re.M) == ["0"], "EGL cleanup failed")
    require(not re.search(r"^\[ps5-imgui\] FAIL|^\[ps5-gallium\] (?:draw-rejected|reject-|clear-gpu-color status=(?!0\b))",
                          text, re.M), "failed rendering operation")
    if not host:
        require(re.findall(r"^\[pss-opengl-native\] gate completed status=(\d+)$", text, re.M) == ["0"],
                "native gate incomplete")
        native = re.findall(r"\[ps5-multidraw-batch\] draws=(\d+) attempted=(\d+) waits=(\d+) result=0", text)
        deferred = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text)
        require(len(native) == len(deferred) == text.count("[ps5-multidraw-batch]") ==
                text.count("[ps5-deferred-batch]") and len(native) >= cases,
                "missing/failed native retirement")
        require(all(n == d == attempted and 0 < int(n) <= 8 and int(waits) < 2000
                    for n, (d, attempted, waits) in zip(deferred, native)), "batch accounting mismatch")
        shutdown = re.findall(r"^\[ps5-agc\] present-shutdown (.+)$", text, re.M)
        require(len(shutdown) == text.count("[ps5-agc] present-shutdown") == 1 and
                shutdown in [[f"{method} close=00000000 frames={cases}"]
                             for method in ("unregister=00000000", "unregister=80290009", "method=close")],
                "preview lifecycle mismatch")
        require(re.findall(r"\[ps5-gpu-present\] frames=(\d+)", text) == [str(cases)], "preview flip coverage mismatch")
    return dict(mode="host-reference" if host else "PS5", workload="imgui-offscreen-completed",
                display_fps_measured=False, deferred_batches=len(native) if not host else 0, cases=report)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--host", action="store_true")
    parser.add_argument("--case", type=int, choices=range(-1, 12), default=-1,
                        help="require exactly this matrix case; default: all twelve")
    args = parser.parse_args()
    print(json.dumps(summarize(args.receipt.read_text(), args.host, args.case), indent=2))
