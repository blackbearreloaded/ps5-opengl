#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Audit the opt-in matched cubes profile; timings are completed CPU wall time."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics


def require(ok, message):
    if not ok:
        raise ValueError(message)


def timing(values, budget_hz):
    ordered = sorted(values)
    count = len(ordered)
    return dict(mean_ms=statistics.mean(ordered) / 1e6,
                p50_ms=ordered[(count * 50 + 99) // 100 - 1] / 1e6,
                p95_ms=ordered[(count * 95 + 99) // 100 - 1] / 1e6,
                p99_ms=ordered[(count * 99 + 99) // 100 - 1] / 1e6,
                max_ms=ordered[-1] / 1e6,
                budget_misses=sum(value * budget_hz > 1_000_000_000 for value in ordered))


def summarize(text, host=False, objects=128, seconds=30, swap_completed=1, budget_hz=60):
    require(objects in (32, 128, 512) and 1 <= seconds <= 60 and
            swap_completed in (0, 1) and math.isfinite(budget_hz) and 0 < budget_hz <= 1000,
            "Invalid requested configuration")
    lines = iter(line for line in text.splitlines()
                 if line.startswith(("[ps5-cubes]", "[ps5-cubes-profile]")))

    def take():
        return next(lines, "")

    config = (f"[ps5-cubes-profile] config version=2 width=1920 height=1080 objects={objects} "
              f"modes=2 warmup=30 seconds={seconds} swap_completed={swap_completed} "
              f"host={int(host)} max_frames=16384")
    require(take() == config, "Wrong or missing profile configuration")
    report = []
    for mode in (0, 1):
        oracle = f"[ps5-cubes] oracle mode={mode} objects={objects} probes={1 + 4 * objects} PASS"
        require(take() == oracle, "Missing initial depth/texture oracle")
        match = re.fullmatch(
            rf"\[ps5-cubes-profile\] calibration mode={mode} objects={objects} "
            r"public_draws=(\d+) native_draws_per_frame=(\d+) clear_path=(cpu|gpu|host)", take())
        require(match is not None, "Missing or malformed draw calibration")
        public_draws, draws_per_frame = map(int, match.groups()[:2])
        clear_path = match.group(3)
        require(public_draws == (1 if mode else objects) and
                ((clear_path == "host" and draws_per_frame == 0) if host else
                 (clear_path in ("cpu", "gpu") and
                  draws_per_frame == public_draws + (clear_path == "gpu"))),
                "Invalid calibrated draw count or clear path")
        require(take() == oracle, "Missing final depth/texture oracle")
        samples = []
        line = take()
        while line.startswith("[ps5-cubes-profile] frame "):
            match = re.fullmatch(
                rf"\[ps5-cubes-profile\] frame mode={mode} objects={objects} frame={len(samples)} "
                r"clear_ns=(\d+) submit_ns=(\d+) finish_ns=(\d+) swap_ns=(\d+) total_ns=(\d+) interval_ns=(\d+) native_draws=(\d+)", line)
            require(match is not None, "Missing, duplicated, reordered or malformed frame")
            clear, submit, finish, swap, total, interval, frame_draws = map(int, match.groups())
            require(frame_draws == draws_per_frame, "Per-frame native draw coverage mismatch")
            require(max(clear, submit, finish, swap, total, interval) <= (1 << 63) - 1 and
                    min(clear, submit, swap) > 0 and
                    ((finish == 0) if swap_completed else (finish > 0)) and
                    clear + submit + finish + swap == total and interval >= total,
                    "Invalid phase accounting or completion interval")
            samples.append((clear, submit, finish, swap, total, interval))
            require(len(samples) <= 16384, "Frame buffer limit exceeded")
            line = take()
        match = re.fullmatch(
            rf"\[ps5-cubes-profile\] mode mode={mode} objects={objects} frames=(\d+) measured_ns=(\d+) native_draws=(\d+)", line)
        require(match is not None and samples, "Missing measured mode summary")
        count, duration, native_draws = map(int, match.groups())
        require(count == len(samples) and duration == sum(row[5] for row in samples),
                "Frame count or measured interval accounting mismatch")
        require(duration >= seconds * 1_000_000_000 and
                duration - samples[-1][5] < seconds * 1_000_000_000,
                "Profile must stop at first completed frame reaching requested duration")
        expected_draws = (count + 30 + 2) * draws_per_frame
        require(native_draws == expected_draws, "Native draw coverage mismatch")
        phases = {name: timing([row[i] for row in samples], budget_hz)
                  for i, name in enumerate(("clear", "submit", "finish", "swap", "active", "interval"))}
        report.append(dict(path="instanced" if mode else "ordinary", frames=count,
                           measured_seconds=duration / 1e9, completed_fps=count * 1e9 / duration,
                           native_draws=native_draws, public_draws=public_draws,
                           native_draws_per_frame=draws_per_frame, clear_path=clear_path, phases=phases,
                           percentile_note="fewer than 100 samples; p99 is effectively the tail maximum"
                           if count < 100 else "nearest-rank percentiles"))
    require(take() == "[ps5-cubes-profile] completed=2 cleanup=1 result=0" and not take(),
            "Incomplete profile, failed cleanup or duplicate records")
    require(not re.search(r"\[ps5-gallium\] (?:draw-rejected|reject-|clear-gpu-color status=(?!0\b))", text),
            "Driver error")
    if not host:
        require(re.findall(r"\[pss-opengl-native\] gate completed status=(-?\d+)", text) == ["0"],
                "Native gate incomplete or failed")
    return dict(version=2, mode="host-reference" if host else "PS5", width=1920, height=1080,
                objects=objects, triangles=12 * objects, textures=2, texture_size=2,
                seconds=seconds, warmup=30, completion="swap" if swap_completed else "finish-before-swap",
                budget_hz=budget_hz, budget_ms=1000 / budget_hz, workloads=report,
                note="Draw-overhead workload with approximately fixed screen footprint for 32/128/512 objects; not a game or texture-bandwidth test. "
                     "Submit measures API wall time, which can include driver waits. Completed FPS uses full intervals, "
                     "including loop overhead. Budget misses are CPU-wall intervals, not observed HDMI missed refreshes. "
                     "Host pbuffer uses post-swap glFinish and is not native swap-retirement evidence.")


def compare(baseline, candidate):
    for field in ("version", "mode", "width", "height", "objects", "triangles", "textures",
                  "texture_size", "seconds", "warmup", "budget_hz"):
        require(baseline[field] == candidate[field], f"Incomparable {field}")
    return dict(baseline_completion=baseline["completion"], candidate_completion=candidate["completion"],
                workloads=[dict(path=b["path"], fps_ratio=c["completed_fps"] / b["completed_fps"],
                                interval_p99_delta_ms=c["phases"]["interval"]["p99_ms"] - b["phases"]["interval"]["p99_ms"],
                                budget_miss_rate_delta=c["phases"]["interval"]["budget_misses"] / c["frames"] -
                                b["phases"]["interval"]["budget_misses"] / b["frames"])
                           for b, c in zip(baseline["workloads"], candidate["workloads"])])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--host", action="store_true")
    parser.add_argument("--objects", type=int, choices=(32, 128, 512), default=128)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--swap-completed", type=int, choices=(0, 1), default=1)
    parser.add_argument("--budget-hz", type=float, default=60,
                        help="CPU-wall budget frequency, e.g. 60 or measured 59.94; does not set output mode")
    parser.add_argument("--compare", type=Path, help="Previously frozen receipt with matched scene/duration")
    parser.add_argument("--compare-swap-completed", type=int, choices=(0, 1),
                        help="Baseline completion mode; default is candidate mode")
    args = parser.parse_args()
    try:
        def read(path, completion):
            raw = path.read_bytes()
            result = summarize(raw.decode(), args.host, args.objects, args.seconds, completion, args.budget_hz)
            result["receipt_sha256"] = hashlib.sha256(raw).hexdigest()
            return result
        result = read(args.receipt, args.swap_completed)
        if args.compare:
            baseline = read(args.compare, args.swap_completed if args.compare_swap_completed is None
                            else args.compare_swap_completed)
            result["comparison"] = compare(baseline, result)
            result["comparison"]["baseline_receipt_sha256"] = baseline["receipt_sha256"]
        print(json.dumps(result, indent=2, allow_nan=False))
    except (ValueError, OSError) as error:
        parser.exit(1, f"cubes-profile: FAIL: {error}\n")
