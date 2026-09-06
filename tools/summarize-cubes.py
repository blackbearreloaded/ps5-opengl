#!/usr/bin/env python3
"""Audit bounded 3D frame timings; do not confuse draw-only time with FPS."""
import argparse
import json
from pathlib import Path
import re
import statistics


def summarize(text, host=False):
    def require(ok, message):
        if not ok:
            raise ValueError(message)

    lines = [line for line in text.splitlines() if line.startswith("[ps5-cubes]")]
    require(len(lines) == 32, "Expected start, six oracles, 24 frames and completion")
    require(lines.pop(0) == "[ps5-cubes] start width=1920 height=1080 warmup=2 frames=8 triangles_per_object=12", "Wrong benchmark configuration")
    report = []
    for objects in (1, 8, 32):
        oracle = f"[ps5-cubes] oracle objects={objects} probes={1 + 4 * objects} PASS"
        require(lines.pop(0) == oracle, "Missing initial depth/texture oracle")
        samples = []
        for frame in range(8):
            match = re.fullmatch(rf"\[ps5-cubes\] objects={objects} frame={frame} clear_ns=(\d+) draw_ns=(\d+) swap_ns=(\d+) total_ns=(\d+)", lines.pop(0))
            require(match is not None, "Missing, duplicate or malformed frame")
            clear, draw, swap, total = map(int, match.groups())
            require(min(clear, draw, swap) > 0 and clear + draw + swap == total, "Invalid phase accounting")
            samples.append((clear, draw, swap, total))
        require(lines.pop(0) == oracle, "Missing final depth/texture oracle")
        means = [statistics.mean(row[i] for row in samples) / 1e6 for i in range(4)]
        report.append(dict(objects=objects, triangles=12 * objects, measured_frames=8,
            mean_clear_ms=means[0], mean_draw_ms=means[1], mean_swap_ms=means[2], mean_frame_ms=means[3],
            throughput_fps=1000 / means[3], median_frame_ms=statistics.median(row[3] for row in samples) / 1e6,
            max_frame_ms=max(row[3] for row in samples) / 1e6))
    require(lines == ["[ps5-cubes] completed=3 cleanup=1 result=0"], "Incomplete benchmark or cleanup failure")
    require(not re.search(r"\[ps5-gallium\] (?:draw-rejected|reject-|clear-gpu-color status=(?!0\b))", text), "Driver error")
    if not host:
        require(re.findall(r"\[pss-opengl-native\] gate completed status=(\d+)", text) == ["0"], "Native gate incomplete")
    return dict(mode="host-reference" if host else "PS5", width=1920, height=1080,
                note="Low-poly draw-call benchmark; full-frame CPU wall time with one glFinish and swap per frame. Not a GPU-throughput or full-game benchmark.", workloads=report)


def self_test():
    text = "[ps5-cubes] start width=1920 height=1080 warmup=2 frames=8 triangles_per_object=12\n"
    for objects in (1, 8, 32):
        oracle = f"[ps5-cubes] oracle objects={objects} probes={1 + 4 * objects} PASS\n"
        text += oracle
        text += "".join(f"[ps5-cubes] objects={objects} frame={f} clear_ns=1000000 draw_ns=2000000 swap_ns=1000000 total_ns=4000000\n" for f in range(8))
        text += oracle
    text += "[ps5-cubes] completed=3 cleanup=1 result=0\n[pss-opengl-native] gate completed status=0\n"
    assert summarize(text)["workloads"][0]["throughput_fps"] == 250
    assert summarize(text.replace("\n", "\r\n"))["workloads"][2]["triangles"] == 384
    for bad in (text + text, text.replace("PASS", "FAIL", 1), text.replace("frame=1", "frame=0", 1),
                text.replace("draw_ns=2000000", "draw_ns=0", 1), text.replace("total_ns=4000000", "total_ns=1", 1),
                text.replace("cleanup=1", "cleanup=0"), text.replace("status=0", "status=1")):
        try:
            summarize(bad)
        except ValueError:
            continue
        raise AssertionError("Invalid benchmark accepted")
    print("cubes-profile: self-test PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", nargs="?")
    parser.add_argument("--host", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if not args.receipt:
            parser.error("receipt is required")
        print(json.dumps(summarize(Path(args.receipt).read_text(), args.host), indent=2))
