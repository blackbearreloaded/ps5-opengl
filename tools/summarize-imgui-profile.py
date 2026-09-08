#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Audit one bounded demo profile; timings are CPU wall time, not GPU timers."""
import argparse
import json
import math
from pathlib import Path
import re


def summarize(text, host=False, submit_profile=False, deferred_batches=False, present_profile=False,
              clear_batches=False, soak=False, window_target=None, window_height=1080,
              output_status=False, prepare_profile=False, soak_seconds=300):
    def require(ok, message):
        if not ok:
            raise ValueError(message)

    require(60 <= soak_seconds <= 1800 and soak_seconds % 30 == 0, "invalid soak duration")

    if window_target is not None and not host:
        present_profile = clear_batches = True
        if window_target > 60:
            output_status = True
    require(not re.search(r"^\[ps5-gallium\] (?:draw-rejected|reject-|clear-gpu-color status=(?!0\b))",
                          text, re.M), "driver reported a failed operation")
    lines = re.findall(r"^\[ps5-imgui-perf\] (.+)$", text, re.M)
    require(len(lines) == 1, "expected exactly one profile")
    pairs = [token.split("=", 1) for token in lines[0].split()]
    require(all(len(pair) == 2 for pair in pairs), "malformed profile field")
    fields = dict(pairs)
    stages = ["ui_ms", "clear_ms", "draw_ms", "readback_ms", "swap_ms"]
    require(len(fields) == len(pairs) and set(fields) ==
            set(stages + ["frames", "warmup", "cpu_wall_ms", "status"]), "unexpected fields")
    frames, warmup = int(fields["frames"]), int(fields["warmup"])
    require(warmup == (2 if host else 30) and frames >= 2 * warmup,
            "insufficient frames or wrong profile mode")
    values = {name: float(fields[name]) for name in stages + ["cpu_wall_ms"]}
    require(all(math.isfinite(v) and v >= 0 for v in values.values()), "invalid timing")
    require(values["cpu_wall_ms"] > 0 and
            abs(sum(values[s] for s in stages) - values["cpu_wall_ms"]) <= 0.000004,
            "phase accounting mismatch")
    require(fields["status"] == "0", "profile failed")
    probes = re.findall(r"\[ps5-imgui-tv\] readback frame=(\d+) rgba=[0-9,]+ (\w+)", text)
    require(probes[:2] == [("0", "PASS"), ("10", "PASS")] and
            len(probes) == (soak_seconds // 30 + 1 if soak else 2) and all(p[1] == "PASS" for p in probes),
            "pixel probes missing or failed")
    finished = re.findall(r"\[ps5-imgui-tv\] finished frames=(\d+) changes=\d+ status=(\d+)", text)
    require(finished == [(str(frames + warmup), "0")], "frame count or cleanup mismatch")
    require(re.findall(r"\[ps5-imgui\] finished status=(\d+)", text) == ["0"], "EGL cleanup failed")
    if not host:
        require(re.findall(r"\[pss-opengl-native\] gate completed status=(\d+)", text) == ["0"],
                "native gate incomplete")
    report = dict(mode="host-reference" if host else "PS5", frames=frames, warmup=warmup, **values)
    if soak:
        require(not host, "soak requires the native workload")
        windows = re.findall(r"^\[ps5-imgui-tv\] visible frame=(\d+) elapsed=([0-9.]+) .+$", text, re.M)
        require(len(windows) == text.count("[ps5-imgui-tv] visible") == soak_seconds // 30,
                "missing or duplicate 30-second cadence windows")
        points = [(int(n), float(t)) for n, t in windows]
        require(points[0] == (0, 0) and all(abs(t - i * 30) <= 0.15 for i, (_, t) in enumerate(points)),
                "invalid cadence timestamps")
        require([int(n) for n, _ in probes[2:]] == [n for n, _ in points[1:]],
                "periodic pixel probes do not match cadence windows")
        points.append((frames + warmup, float(soak_seconds)))
        fps = [(n1 - n0) / (t1 - t0) for (n0, t0), (n1, t1) in zip(points, points[1:])]
        require(all(59.0 <= rate <= 60.5 for rate in fps) and values["cpu_wall_ms"] <= 1000 / 59.0,
                "sustained 60 Hz performance target not met")
        report["soak"] = dict(seconds=soak_seconds, window_fps=fps, pixel_probes=len(probes))
    if deferred_batches or clear_batches:
        chunks = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text)
        native = re.findall(r"\[ps5-multidraw-batch\] draws=(\d+) attempted=(\d+) waits=(\d+) result=0", text)
        require(not host and len(chunks) == text.count("[ps5-deferred-batch]") ==
                len(native) == text.count("[ps5-multidraw-batch]"), "missing or failed batch receipt")
        require(all(count == draws == attempted and 0 < int(count) <= 8 and int(waits) < 2000
                    for count, (draws, attempted, waits) in zip(chunks, native)), "invalid batch retirement")
        grouped = sum(int(count) > 1 for count in chunks)
        # Aggregate coverage, not a claim that each frame had a particular group.
        require(grouped >= frames, "insufficient actual multi-draw groups")
        report["deferred_batches"] = dict(chunks=len(chunks), draws=sum(map(int, chunks)),
                                         multi_draw_chunks=grouped)
        if clear_batches:
            combined = sum(count == "3" for count in chunks)
            require(combined >= frames, "insufficient combined clear/two-draw groups")
            report["deferred_batches"]["clear_two_draw_chunks"] = combined
    submit_lines = re.findall(r"^\[ps5-submit-perf\] (.+)$", text, re.M)
    if submit_lines or submit_profile:
        require(not host and len(submit_lines) == 1, "expected one native submission profile")
        pairs = [token.split("=", 1) for token in submit_lines[0].split()]
        require(all(len(pair) == 2 for pair in pairs), "malformed submission field")
        fields = dict(pairs)
        phases = ["setup_ms", "scanout_flush_ms", "video_ms", "command_ms",
                  "command_flush_ms", "submit_wait_ms", "cleanup_ms"]
        metadata = ["calls", "failures", "warmup_frames", "total_ms"]
        if "sleeps" in fields:
            phases[5:6] = ["submit_ms", "suspend_ms", "poll_ms"]
            metadata.append("sleeps")
            require(0 <= int(fields["sleeps"]) <= int(fields["calls"]) * 2000,
                    "invalid completion sleep count")
        require(len(fields) == len(pairs) and set(fields) ==
                set(phases + metadata),
                "unexpected submission fields")
        calls = int(fields["calls"])
        require(calls >= frames and fields["failures"] == "0" and
                fields["warmup_frames"] == "30", "submission count/failure/warmup mismatch")
        timings = {name: float(fields[name]) for name in phases + ["total_ms"]}
        require(all(math.isfinite(v) and v >= 0 for v in timings.values()) and
                timings["total_ms"] > 0 and
                abs(sum(timings[p] for p in phases) - timings["total_ms"]) <= 0.000006,
                "invalid submission phase accounting")
        report["submission_per_call"] = dict(calls=calls, **timings)
        if "sleeps" in fields:
            report["submission_per_call"]["sleeps"] = int(fields["sleeps"])
    prepare_lines = re.findall(r"^\[ps5-prepare-perf\] (.+)$", text, re.M)
    if prepare_lines or prepare_profile:
        require(not host and len(prepare_lines) == text.count("[ps5-prepare-perf]") == 1,
                "expected one deferred preparation profile")
        pairs = [token.split("=", 1) for token in prepare_lines[0].split()]
        require(all(len(pair) == 2 for pair in pairs), "malformed preparation field")
        fields = dict(pairs)
        phases = ["setup_ms", "scanout_flush_ms", "video_ms", "command_ms", "command_flush_ms"]
        require(len(fields) == len(pairs) and set(fields) ==
                set(phases + ["calls", "failures", "warmup_frames", "total_ms"]),
                "unexpected preparation fields")
        calls = int(fields["calls"])
        require(calls >= frames and fields["failures"] == "0" and fields["warmup_frames"] == "30",
                "preparation count/failure/warmup mismatch")
        if window_target is not None:
            require(calls == 3 * frames, "window must prepare one clear and two draws per measured frame")
        timings = {name: float(fields[name]) for name in phases + ["total_ms"]}
        require(all(math.isfinite(v) and v >= 0 for v in timings.values()) and timings["total_ms"] > 0 and
                abs(sum(timings[p] for p in phases) - timings["total_ms"]) <= 0.000004 and
                timings["total_ms"] * calls / frames <= values["clear_ms"] + values["draw_ms"] + 0.00001,
                "invalid preparation phase accounting")
        report["preparation_per_call"] = dict(calls=calls, **timings)
    present_lines = re.findall(r"^\[ps5-present-perf\] (.+)$", text, re.M)
    if present_lines or present_profile:
        require(not host and len(present_lines) == 1 and
                text.count("[ps5-present-perf]") == 1, "expected one native presentation profile")
        pairs = [token.split("=", 1) for token in present_lines[0].split()]
        require(all(len(pair) == 2 for pair in pairs), "malformed presentation field")
        fields = dict(pairs)
        phases = ["idle_ms", "flip_ms", "vblank_ms"]
        require(len(fields) == len(pairs) and set(fields) ==
                set(phases + ["calls", "failures", "warmup_frames", "total_ms"]),
                "unexpected presentation fields")
        require(int(fields["calls"]) == frames and fields["failures"] == "0" and
                fields["warmup_frames"] == "30", "presentation count/failure/warmup mismatch")
        timings = {name: float(fields[name]) for name in phases + ["total_ms"]}
        require(all(math.isfinite(v) and v >= 0 for v in timings.values()) and
                abs(sum(timings[p] for p in phases) - timings["total_ms"]) <= 0.000003 and
                timings["total_ms"] <= values["swap_ms"] + 0.000002,
                "invalid presentation phase accounting")
        report["presentation_per_frame"] = dict(calls=frames, **timings)
        # Remainder includes deferred draw retirement plus EGL bookkeeping, not GPU time.
        report["swap_other_ms"] = max(0, values["swap_ms"] - timings["total_ms"])
    batch_lines = re.findall(r"^\[ps5-batch-perf\] (.+)$", text, re.M)
    if batch_lines:
        require(not host and len(batch_lines) == text.count("[ps5-batch-perf]") == 1,
                "expected one batch profile")
        pairs = [token.split("=", 1) for token in batch_lines[0].split()]
        require(all(len(pair) == 2 for pair in pairs), "malformed batch field")
        fields = dict(pairs)
        phases = ["submit_ms", "suspend_ms", "poll_ms", "cleanup_ms"]
        require(len(fields) == len(pairs) and set(fields) ==
                set(phases + ["calls", "failures", "warmup_frames", "sleeps", "total_ms"]),
                "unexpected batch fields")
        calls, sleeps = int(fields["calls"]), int(fields["sleeps"])
        require(calls >= frames and 0 <= sleeps < calls * 2000 and
                fields["failures"] == "0" and fields["warmup_frames"] == "30",
                "batch count/failure/warmup mismatch")
        timings = {name: float(fields[name]) for name in phases + ["total_ms"]}
        require(all(math.isfinite(v) and v >= 0 for v in timings.values()) and
                abs(sum(timings[p] for p in phases) - timings["total_ms"]) <= 0.000004,
                "invalid batch phase accounting")
        report["batch_per_call"] = dict(calls=calls, sleeps=sleeps, **timings)
    gpu_present = re.findall(r"^\[ps5-gpu-present\] frames=(\d+)$", text, re.M)
    if gpu_present:
        require(not host and len(gpu_present) == text.count("[ps5-gpu-present]") == 1,
                "expected one GPU-presentation summary")
        # Pre-swap readbacks drain the batch and must use the CPU-flip fallback.
        require(int(gpu_present[0]) == frames + warmup - len(probes),
                "missing GPU-present frames or readback fallback")
        report["gpu_present_frames"] = int(gpu_present[0])
    if window_target is not None:
        require(not soak and window_target in (30, 60, 90, 120) and window_height in (1080, 1440, 2160),
                "unsupported window benchmark target")
        window_width = window_height * 16 // 9
        begins = re.findall(r"^\[ps5-imgui-window\] begin (.+)$", text, re.M)
        results = re.findall(r"^\[ps5-imgui-window\] result (.+)$", text, re.M)
        require(begins == [f"width={window_width} height={window_height} target={window_target} mode=window-presented"] and
                len(results) == 1 and text.count("[ps5-imgui-window]") == 2, "missing/wrong window benchmark")
        pairs = [token.split("=", 1) for token in results[0].split()]
        require(all(len(p) == 2 for p in pairs), "malformed window fields")
        fields = dict(pairs)
        timings = ["active_mean_ms", "active_p50_ms", "active_p95_ms", "active_p99_ms",
                   "frame_p50_ms", "frame_p95_ms", "frame_p99_ms"]
        require(len(pairs) == len(fields) and set(fields) ==
                set(timings + ["frames", "seconds", "fps", "active_misses", "frame_misses", "status"]),
                "unexpected window fields")
        require(fields["frames"] == str(frames) and fields["status"] == "0" and frames <= 8192,
                "window frame count/status mismatch")
        seconds, fps = float(fields["seconds"]), float(fields["fps"])
        require(math.isfinite(seconds) and math.isfinite(fps) and seconds > 0 and fps > 0 and
                abs(fps * seconds - frames) <= (fps + seconds) * 0.00000051 + 0.000001,
                "window FPS accounting mismatch")
        require(frames == 10 if host else 30 <= seconds <= 31 and fps <= window_target * 1.01,
                "wrong window measurement interval/pacing")
        timing = {key: float(fields[key]) for key in timings}
        require(all(math.isfinite(v) and v > 0 for v in timing.values()) and
                abs(timing["active_mean_ms"] - report["cpu_wall_ms"]) <= 0.000004 and
                timing["active_mean_ms"] <= seconds * 1000 / frames + 0.001,
                "window phase accounting mismatch")
        for prefix in ("active", "frame"):
            require(timing[f"{prefix}_p50_ms"] <= timing[f"{prefix}_p95_ms"] <= timing[f"{prefix}_p99_ms"],
                    "unordered window percentiles")
        for p in (50, 95, 99):
            require(timing[f"active_p{p}_ms"] <= timing[f"frame_p{p}_ms"] + 0.001,
                    "active percentile exceeds completed frame interval")
        misses = {key: int(fields[key]) for key in ("active_misses", "frame_misses")}
        require(all(0 <= n <= frames for n in misses.values()), "invalid window missed-budget count")
        require(not re.search(r"^\[ps5-imgui\] FAIL", text, re.M), "window operation failed")
        if not host:
            require(re.findall(r"\[ps5-imgui-tv\] finished frames=\d+ changes=(\d+) status=0", text) == ["0"],
                    "window benchmark contaminated by control changes")
            require(report.get("gpu_present_frames") == frames + warmup - 2 and
                    "batch_per_call" in report and
                    report["deferred_batches"]["clear_two_draw_chunks"] == frames + warmup and
                    report["deferred_batches"]["chunks"] == frames + warmup,
                    "window GPU flip/clear/draw coverage missing")
            shutdown = re.findall(r"^\[ps5-agc\] present-shutdown (.+)$", text, re.M)
            require(len(shutdown) == text.count("[ps5-agc] present-shutdown") == 1 and
                    shutdown in [[f"{method} close=00000000 frames={frames + warmup}"]
                                 for method in ("unregister=00000000", "unregister=80290009", "method=close")],
                    "window presenter cleanup failed")
        report["window_benchmark"] = dict(width=window_width, height=window_height, target_fps=window_target,
            achieved_fps=fps, seconds=seconds, target_met=fps >= window_target * 0.99,
            frame_mean_ms=seconds * 1000 / frames, frame_budget_tolerance_ms=0.25,
            output_mode_verified=False, **timing, **misses)
    if output_status:
        require(not host and window_target is not None, "output status requires a native window benchmark")
        buffer_bytes = {1080: 0xa00000, 1440: 0x1000000, 2160: 0x2000000}[window_height]
        registration = re.findall(r"^\[ps5-output-register\] (.+)$", text, re.M)
        require(registration == [f"width={window_width} height={window_height} offset={buffer_bytes} result=00000000"],
                "display registration/double-buffer offset mismatch")
        lines = re.findall(r"^\[ps5-output\] (.+)$", text, re.M)
        stages = ("warmup", "end", "restored") if window_target > 60 else ("warmup", "end")
        require(len(lines) == text.count("[ps5-output]") == len(stages), "missing/duplicate output snapshots")
        snapshots = []
        for line, stage in zip(lines, stages):
            pairs = [token.split("=", 1) for token in line.split()]
            require(all(len(p) == 2 for p in pairs), "malformed output fields")
            row = dict(pairs)
            numeric = ["render_width", "render_height", "buffer_bytes", "full_width", "full_height",
                       "pane_width", "pane_height", "refresh_id", "output_refresh_id"]
            require(len(row) == len(pairs) and set(row) == set(numeric + ["stage", "resolution_rc", "output_rc"]),
                    "unexpected output fields")
            require(row["stage"] == stage and all(re.fullmatch(r"[0-9]+", row[k]) for k in numeric) and
                    all(re.fullmatch(r"[0-9a-f]{8}", row[k]) for k in ("resolution_rc", "output_rc")),
                    "invalid output field values")
            values = {k: int(row[k]) for k in numeric}
            require(values["render_width"] == window_width and values["render_height"] == window_height and
                    values["buffer_bytes"] == buffer_bytes,
                    "EGL/presenter display-buffer layout mismatch")
            snapshots.append(dict(stage=stage, resolution_rc=row["resolution_rc"], output_rc=row["output_rc"], **values))
        # Preserve raw status. Unknown/error/disagreeing status never proves an output mode.
        first, last = snapshots[:2]
        keys = ("full_width", "full_height", "pane_width", "pane_height", "refresh_id", "output_refresh_id")
        valid = all(s["resolution_rc"] == s["output_rc"] == "00000000" and
                    0 < s["full_width"] <= 8192 and 0 < s["full_height"] <= 8192 and
                    0 < s["pane_width"] <= 8192 and 0 < s["pane_height"] <= 8192 and
                    s["refresh_id"] == s["output_refresh_id"] for s in snapshots[:2])
        stable = valid and all(first[k] == last[k] for k in keys)
        refresh = {3: 59.94, 13: 119.88, 35: 89.91}.get(last["refresh_id"]) if stable else None
        report["videoout_status"] = dict(snapshots=snapshots, stable_known_status=refresh is not None,
            reported_refresh_hz=refresh, physical_output_independently_verified=False)
        if window_target > 60:
            modes = re.findall(r"^\[ps5-output-mode\] target=(\d+) support=([0-9a-f]{8}) "
                               r"preset=([0-9a-f]{8}) vrr=([0-9a-f]{8}) result=([0-9a-f]{8})$", text, re.M)
            require(len(modes) == text.count("[ps5-output-mode]") == 1, "missing/duplicate HFR request")
            target, support, preset, vrr, result = modes[0]
            require(int(target) == 120 and 0 < int(support, 16) < 0x80000000 and
                    preset == result == "00000000" and vrr == "ffffffff",
                    "HFR support/configuration failed or mismatched")
            require(refresh is not None and refresh >= 120 * 0.99, "requested HFR output is unverified")
            require(text.count("[ps5-output-restore]") == 1 and
                    re.findall(r"^\[ps5-output-restore\] (.+)$", text, re.M) ==
                    ["result=00000000 wait=00000000"], "HFR restoration failed or missing")
            restored = snapshots[2]
            require(restored["resolution_rc"] == restored["output_rc"] == "00000000" and
                    restored["refresh_id"] == restored["output_refresh_id"] == 3 and
                    all(0 < restored[k] <= 8192 for k in keys[:4]), "normal output restoration is unverified")
            report["videoout_status"]["high_refresh_request_verified"] = True
            report["videoout_status"]["normal_output_restored"] = True
            report["videoout_status"]["requested_output_fps"] = 120
            report["videoout_status"]["application_paced_on_fixed_refresh"] = window_target == 90
        # Status reports full/pane extents; do not equate render size with physical HDMI output.
    return report


def self_test():
    text = """[ps5-imgui-tv] readback frame=0 rgba=45,215,245,255 PASS
[ps5-imgui-tv] readback frame=10 rgba=45,215,245,255 PASS
[ps5-imgui-perf] frames=100 warmup=30 ui_ms=1 clear_ms=2 draw_ms=3 readback_ms=0 swap_ms=4 cpu_wall_ms=10 status=0
[ps5-imgui-tv] finished frames=130 changes=0 status=0
[ps5-imgui] finished status=0
[pss-opengl-native] gate completed status=0
"""
    assert summarize(text)["cpu_wall_ms"] == 10
    preparation = ("[ps5-prepare-perf] calls=300 failures=0 warmup_frames=30 "
                   "setup_ms=0.5 scanout_flush_ms=0.5 video_ms=0 command_ms=0.25 "
                   "command_flush_ms=0.25 total_ms=1.5\n")
    assert summarize(text + preparation, prepare_profile=True)["preparation_per_call"]["calls"] == 300
    for bad in (text, text + preparation + preparation, text + preparation + "[ps5-prepare-perf]\n",
                text + preparation.replace("calls=300", "calls=99"),
                text + preparation.replace("calls=300", "calls=400"),
                text + preparation.replace("failures=0", "failures=1"),
                text + preparation.replace("warmup_frames=30", "warmup_frames=0"),
                text + preparation.replace("setup_ms=0.5", "setup_ms=nan"),
                text + preparation.replace("setup_ms=0.5", "setup_ms=-1"),
                text + preparation.replace("setup_ms=0.5", "setup_ms=0.5 setup_ms=0.5"),
                text + preparation.replace("setup_ms=0.5", "bad-field"),
                text + preparation.replace("total_ms=1.5", "total_ms=2")):
        try:
            summarize(bad, prepare_profile=True)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid preparation profile accepted")
    assert summarize(text.replace("\n", "\r\n"))["frames"] == 100
    batch = "[ps5-multidraw-batch] draws=2 attempted=2 waits=1 result=0\n[ps5-deferred-batch] draws=2 result=0\n"
    gpu_present = "[ps5-gpu-present] frames=128\n"
    assert summarize(text + gpu_present)["gpu_present_frames"] == 128
    soak_text = text.replace("frames=100", "frames=17970").replace("frames=130", "frames=18000")
    soak_text = soak_text.replace("swap_ms=4 cpu_wall_ms=10", "swap_ms=10.683 cpu_wall_ms=16.683")
    for i in range(10):
        soak_text += f"[ps5-imgui-tv] visible frame={i * 1800} elapsed={i * 30:.1f} pad=0 changes=0 vertices=100\n"
        if i:
            soak_text += f"[ps5-imgui-tv] readback frame={i * 1800} rgba=45,215,245,255 PASS\n"
    soak_text += "[ps5-gpu-present] frames=17989\n"
    assert summarize(soak_text, soak=True)["soak"]["window_fps"] == [60.0] * 10
    longer = soak_text.replace("frames=17970", "frames=35970").replace(
        "frames=18000", "frames=36000").replace("frames=17989", "frames=35979")
    for i in range(10, 20):
        longer += f"[ps5-imgui-tv] visible frame={i * 1800} elapsed={i * 30:.1f} pad=0 changes=0 vertices=100\n"
        longer += f"[ps5-imgui-tv] readback frame={i * 1800} rgba=45,215,245,255 PASS\n"
    assert summarize(longer, soak=True, soak_seconds=600)["soak"]["window_fps"] == [60.0] * 20
    for bad in (soak_text.replace("frame=1800 elapsed=30.0", "frame=900 elapsed=30.0"),
                soak_text.replace("readback frame=1800", "readback frame=1801"),
                soak_text.replace("elapsed=270.0", "elapsed=275.0"),
                soak_text.replace("frames=17989", "frames=17990")):
        try:
            summarize(bad, soak=True)
        except ValueError:
            continue
        raise AssertionError("Invalid soak accepted")
    for bad in (gpu_present * 2, gpu_present.replace("128", "127"), gpu_present.replace("128", "130")):
        try:
            summarize(text + bad)
        except ValueError:
            continue
        raise AssertionError("Invalid GPU-present coverage accepted")
    batch_perf = ("[ps5-batch-perf] calls=100 failures=0 warmup_frames=30 sleeps=100 "
                  "submit_ms=0.1 suspend_ms=0.2 poll_ms=2.4 cleanup_ms=0.3 total_ms=3\n")
    assert summarize(text + batch_perf)["batch_per_call"]["poll_ms"] == 2.4
    for bad in (batch_perf * 2, batch_perf.replace("calls=100", "calls=99"),
                batch_perf.replace("failures=0", "failures=1"),
                batch_perf.replace("sleeps=100", "sleeps=-1"),
                batch_perf.replace("poll_ms=2.4", "poll_ms=nan"),
                batch_perf.replace("total_ms=3", "total_ms=4")):
        try:
            summarize(text + bad)
        except ValueError:
            continue
        raise AssertionError("Invalid batch profile accepted")
    assert summarize(text + batch * 130, deferred_batches=True)["deferred_batches"]["draws"] == 260
    assert summarize(text + batch.replace("=2", "=3") * 130,
                     clear_batches=True)["deferred_batches"]["clear_two_draw_chunks"] == 130
    for bad in (text + batch * 130, text + batch.replace("=2", "=3") * 99):
        try:
            summarize(bad, clear_batches=True)
        except ValueError:
            pass
        else:
            raise AssertionError("uncombined or insufficient clear batches accepted")
    for bad in (text, text + batch * 99, text + batch.replace("=2", "=1") * 130,
                text + batch.replace("attempted=2", "attempted=1") * 130,
                text + batch.replace("waits=1", "waits=2000") * 130,
                text + batch.replace("result=0", "result=-1") * 130,
                text + batch * 130 + "[ps5-deferred-batch] malformed"):
        try:
            summarize(bad, deferred_batches=True)
        except ValueError:
            pass
        else:
            raise AssertionError("missing, ungrouped or failed batches accepted")
    submit = ("[ps5-submit-perf] calls=300 failures=0 warmup_frames=30 "
              "setup_ms=1 scanout_flush_ms=2 video_ms=0 command_ms=1 "
              "command_flush_ms=0 submit_wait_ms=1 cleanup_ms=1 total_ms=6\n")
    assert summarize(text + submit, submit_profile=True)["submission_per_call"]["calls"] == 300
    split_submit = submit.replace("warmup_frames=30", "warmup_frames=30 sleeps=300").replace(
        "submit_wait_ms=1", "submit_ms=0 suspend_ms=0 poll_ms=1")
    assert summarize(text + split_submit)["submission_per_call"]["sleeps"] == 300
    present = ("[ps5-present-perf] calls=100 failures=0 warmup_frames=30 "
               "idle_ms=0 flip_ms=1 vblank_ms=2 total_ms=3\n")
    assert summarize(text + present, present_profile=True)["swap_other_ms"] == 1
    for bad in (text, text + present + present, text + present + "[ps5-present-perf]\n",
                text + present.replace("calls=100", "calls=99"),
                text + present.replace("calls=100", "calls=101"),
                text + present.replace("failures=0", "failures=1"),
                text + present.replace("warmup_frames=30", "warmup_frames=0"),
                text + present.replace("idle_ms=0", "idle_ms=-1"),
                text + present.replace("idle_ms=0", "idle_ms=nan"),
                text + present.replace("idle_ms=0", "idle_ms=0 idle_ms=0"),
                text + present.replace("flip_ms=1", "malformed"),
                text + present.replace("total_ms=3", "total_ms=4"),
                text + present.replace("vblank_ms=2 total_ms=3", "vblank_ms=4 total_ms=5")):
        try:
            summarize(bad, present_profile=True)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid presentation profile accepted")
    for bad in (text + text, text + "[ps5-gallium] clear-gpu-color status=-3 draws=1\n",
                text.replace("PASS", "FAIL", 1),
                text.replace("frames=130", "frames=129"), text.replace("warmup=30", "warmup=2"),
                text.replace("clear_ms=2", "clear_ms=nan"), text.replace("clear_ms=2", "clear_ms=-1"),
                text.replace("cpu_wall_ms=10", "cpu_wall_ms=11"),
                text.replace("status=0", "status=1", 1), text.replace("gate completed", "incomplete"),
                text.replace("clear_ms=2", "clear_ms=2 clear_ms=2"),
                text + submit + submit, text + submit.replace("calls=300", "calls=99"),
                text + submit.replace("failures=0", "failures=1"),
                text + submit.replace("warmup_frames=30", "warmup_frames=0"),
                text + submit.replace("total_ms=6", "total_ms=5"),
                text + split_submit.replace("sleeps=300", "sleeps=-1"),
                text + split_submit.replace("sleeps=300", "sleeps=600001"),
                text + submit.replace("setup_ms=1", "setup_ms=nan")):
        try:
            summarize(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid profile accepted")
    try:
        summarize(text, submit_profile=True)
    except ValueError:
        pass
    else:
        raise AssertionError("missing required submission profile accepted")
    print("imgui-profile: self-test PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path, nargs="?")
    parser.add_argument("--host", action="store_true")
    parser.add_argument("--submit-profile", action="store_true")
    parser.add_argument("--deferred-batches", action="store_true")
    parser.add_argument("--present-profile", action="store_true")
    parser.add_argument("--prepare-profile", action="store_true")
    parser.add_argument("--clear-batches", action="store_true")
    parser.add_argument("--soak", action="store_true")
    parser.add_argument("--soak-seconds", type=int, default=300)
    parser.add_argument("--window-target", type=int, choices=(30, 60, 90, 120))
    parser.add_argument("--window-height", type=int, choices=(1080, 1440, 2160), default=1080)
    parser.add_argument("--output-status", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if not args.receipt:
            parser.error("receipt required")
        print(json.dumps(summarize(args.receipt.read_text(), args.host, args.submit_profile,
                                   args.deferred_batches, args.present_profile, args.clear_batches, args.soak,
                                   args.window_target, args.window_height, args.output_status,
                                   args.prepare_profile, args.soak_seconds), indent=2))
