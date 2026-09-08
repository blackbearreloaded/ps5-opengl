#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Audit bounded 3D frame timings; do not confuse draw-only time with FPS."""
import argparse
import json
from pathlib import Path
import re
import statistics


def summarize(text, host=False, uv=False, deferred_batches=False):
    def require(ok, message):
        if not ok:
            raise ValueError(message)

    lines = [line for line in text.splitlines() if line.startswith("[ps5-cubes]")]
    start = "[ps5-cubes] start width=1920 height=1080 warmup=2 frames=8 triangles_per_object=12"
    if uv:
        require(bool(lines) and lines[0] == start + " modes=2 diagnostic=uv", "Wrong UV diagnostic configuration")
        lines[0] = start + " modes=2"
    require(bool(lines) and lines[0] in (start, start + " modes=2"), "Wrong benchmark configuration")
    modes = 2 if lines.pop(0).endswith(" modes=2") else 1
    require(len(lines) == 30 * modes + 1, "Incomplete or duplicate benchmark records")
    report = []
    for mode, objects in ((m, n) for m in range(modes) for n in (1, 8, 32)):
        prefix = f"mode={mode} " if modes == 2 else ""
        oracle = f"[ps5-cubes] oracle {prefix}objects={objects} probes={1 + 4 * objects} PASS"
        require(lines.pop(0) == oracle, "Missing initial depth/texture oracle")
        samples = []
        for frame in range(8):
            match = re.fullmatch(rf"\[ps5-cubes\] {prefix}objects={objects} frame={frame} clear_ns=(\d+) draw_ns=(\d+) swap_ns=(\d+) total_ns=(\d+)", lines.pop(0))
            require(match is not None, "Missing, duplicate or malformed frame")
            clear, draw, swap, total = map(int, match.groups())
            require(min(clear, draw, swap) > 0 and clear + draw + swap == total, "Invalid phase accounting")
            samples.append((clear, draw, swap, total))
        require(lines.pop(0) == oracle, "Missing final depth/texture oracle")
        means = [statistics.mean(row[i] for row in samples) / 1e6 for i in range(4)]
        report.append(dict(path="instanced" if mode else "ordinary", objects=objects, triangles=12 * objects, measured_frames=8,
            mean_clear_ms=means[0], mean_draw_ms=means[1], mean_swap_ms=means[2], mean_frame_ms=means[3],
            throughput_fps=1000 / means[3], median_frame_ms=statistics.median(row[3] for row in samples) / 1e6,
            max_frame_ms=max(row[3] for row in samples) / 1e6))
    require(lines == [f"[ps5-cubes] completed={3 * modes} cleanup=1 result=0"], "Incomplete benchmark or cleanup failure")
    require(not re.search(r"\[ps5-gallium\] (?:draw-rejected|reject-|clear-gpu-color status=(?!0\b))", text), "Driver error")
    if not host:
        require(re.findall(r"\[pss-opengl-native\] gate completed status=(\d+)", text) == ["0"], "Native gate incomplete")
    result = dict(mode="host-reference" if host else "PS5", width=1920, height=1080,
                oracle="UV/material coordinates" if uv else "depth/texture pixels",
                note="Low-poly draw-call benchmark; full-frame CPU wall time with one glFinish and swap per frame. Not a GPU-throughput or full-game benchmark.", workloads=report)
    if deferred_batches:
        require(not host and not uv and modes == 2, "Batch audit requires the native textured comparison")
        chunks = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text)
        native = re.findall(r"\[ps5-multidraw-batch\] draws=(\d+) attempted=(\d+) waits=(\d+) result=0", text)
        require(len(chunks) == text.count("[ps5-deferred-batch]") ==
                len(native) == text.count("[ps5-multidraw-batch]"), "Missing or failed batch receipt")
        require(all(count == draws == attempted and int(waits) < 2000
                    for count, (draws, attempted, waits) in zip(chunks, native)), "Invalid batch retirement")
        # Twelve renders per cell: two oracle frames, two warmups, eight timed.
        expected = [1] * 12 + [8] * (12 + 48) + [1] * 36
        require(list(map(int, chunks)) == expected, "Wrong grouping: expected 108 chunks / 528 draws")
        result["deferred_batches"] = dict(chunks=len(chunks), draws=sum(expected), multi_draw_chunks=60)
    return result


def self_test():
    for modes in (1, 2):
        text = "[ps5-cubes] start width=1920 height=1080 warmup=2 frames=8 triangles_per_object=12"
        text += " modes=2\n" if modes == 2 else "\n"
        for mode, objects in ((m, n) for m in range(modes) for n in (1, 8, 32)):
            prefix = f"mode={mode} " if modes == 2 else ""
            oracle = f"[ps5-cubes] oracle {prefix}objects={objects} probes={1 + 4 * objects} PASS\n"
            text += oracle
            text += "".join(f"[ps5-cubes] {prefix}objects={objects} frame={f} clear_ns=1000000 draw_ns=2000000 swap_ns=1000000 total_ns=4000000\n" for f in range(8))
            text += oracle
        text += f"[ps5-cubes] completed={3 * modes} cleanup=1 result=0\n[pss-opengl-native] gate completed status=0\n"
        result = summarize(text)["workloads"]
        assert len(result) == modes * 3 and result[0]["throughput_fps"] == 250
        assert result[-1]["path"] == ("instanced" if modes == 2 else "ordinary")
        assert summarize(text.replace("\n", "\r\n"))["workloads"][2]["triangles"] == 384
        for bad in ("", text + text, text.replace("PASS", "FAIL", 1), text.replace("frame=1", "frame=0", 1),
                    text.replace("draw_ns=2000000", "draw_ns=0", 1), text.replace("total_ns=4000000", "total_ns=1", 1),
                    text.replace("cleanup=1", "cleanup=0"), text.replace("status=0", "status=1"),
                    text.replace("objects=32", "objects=8", 1), text.replace("probes=5 ", "probes=4 ", 1)):
            try:
                summarize(bad)
            except ValueError:
                continue
            raise AssertionError("Invalid benchmark accepted")
        if modes == 2:
            batches = "".join(f"[ps5-multidraw-batch] draws={n} attempted={n} waits=1 result=0\n"
                              f"[ps5-deferred-batch] draws={n} result=0\n"
                              for n in [1] * 12 + [8] * 60 + [1] * 36)
            assert summarize(text + batches, deferred_batches=True)["deferred_batches"]["draws"] == 528
            for bad in (text, text + batches.replace("=8", "=1"),
                        text + batches.replace("attempted=8", "attempted=7", 1),
                        text + batches.replace("waits=1", "waits=2000", 1),
                        text + batches.replace("result=0", "result=-1", 1),
                        text + batches + "[ps5-deferred-batch] malformed"):
                try:
                    summarize(bad, deferred_batches=True)
                except ValueError:
                    continue
                raise AssertionError("Invalid cube batches accepted")
            uv_text = text.replace("modes=2", "modes=2 diagnostic=uv")
            assert summarize(uv_text, uv=True)["oracle"] == "UV/material coordinates"
            for bad in (text.replace("mode=1 ", "mode=0 ", 1), text.replace("modes=2", "modes=3"),
                        text.replace("completed=6", "completed=3"), uv_text):
                try:
                    summarize(bad)
                except ValueError:
                    continue
                raise AssertionError("Invalid instancing comparison accepted")
    print("cubes-profile: self-test PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", nargs="?")
    parser.add_argument("--host", action="store_true")
    parser.add_argument("--uv", action="store_true", help="Audit UV diagnostic, never count it as texture validation")
    parser.add_argument("--deferred-batches", action="store_true", help="Require the exact native grouped-draw receipt")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if not args.receipt:
            parser.error("receipt is required")
        print(json.dumps(summarize(Path(args.receipt).read_text(), args.host, args.uv, args.deferred_batches), indent=2))
