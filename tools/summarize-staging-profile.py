#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Audit one fixed staging batch. Rates describe completed composite cycles."""
import argparse
import hashlib
import json
from pathlib import Path
import re

TAG = "[ps5-egl-staging-profile]"
CASES = (("rgba8-2d", 16), ("r8-2d", 16), ("rgba16f-2d", 16),
         ("rgba8-2d-mip1", 24), ("rgba8-array-layer1", 32), ("rgba8-3d-layer1", 32))
CONFIG = ("config version=1 host={host} cases=6 width=640 height=360 warmup=4 "
          "target_ns=3000000000 max_ns=5000000000 max_cycles=100000 completion=glFinish")


def require(ok, message):
    if not ok:
        raise ValueError(message)


def summarize(text, host=False):
    # Retain every tagged record: duplicates, failures and truncation fail closed.
    lines = iter(line.split(TAG, 1)[1].strip() for line in text.splitlines() if TAG in line)

    def take():
        return next(lines, "")

    def match(pattern, message):
        result = re.fullmatch(pattern, take())
        require(result is not None, message)
        return result

    require(take() == CONFIG.format(host=int(host)), "Wrong/missing configuration or host mode")
    renderer = match(r"renderer=(.+) version=(.+)", "Missing renderer/version").groups()
    setup = int(match(r"session_setup_ns=(\d+)", "Missing session setup")[1])
    require(0 < setup < 2**63, "Invalid session setup time")
    rows = []
    for name, probes in CASES:
        values = match(
            rf"case={name} setup_ns=(\d+) warmup_ns=(\d+) before=(\d+) cycles=(\d+) "
            r"previous_ns=(\d+) measured_ns=(\d+) after=(\d+) cleanup_ns=(\d+) result=0",
            f"Missing, reordered or failed case: {name}")
        setup_ns, warmup, before, cycles, previous, elapsed, after, cleanup = map(int, values.groups())
        require(max(setup_ns, warmup, previous, elapsed, cleanup) < 2**63 and
                min(setup_ns, warmup, cleanup) > 0, f"Invalid phase time: {name}")
        require(before == after == probes, f"Pixel/guard coverage mismatch: {name}")
        require(1 <= cycles <= 100000 and 0 <= previous < 3_000_000_000 <= elapsed <= 5_000_000_000
                and ((previous == 0) == (cycles == 1)), f"Invalid bounded measurement: {name}")
        rows.append(dict(case=name, setup_ms=setup_ns / 1e6, warmup_ms=warmup / 1e6,
                         before_probes=before, after_probes=after, cycles=cycles,
                         measured_seconds=elapsed / 1e9, completed_cycles_per_second=cycles * 1e9 / elapsed,
                         completed_cycle_mean_ms=elapsed / cycles / 1e6, cleanup_ms=cleanup / 1e6))
    cleanup = int(match(r"finished cases=6 session_cleanup_ns=(\d+) cleanup=1 result=0",
                        "Incomplete batch or failed cleanup")[1])
    require(0 < cleanup < 2**63 and not take(), "Invalid cleanup time or extra profile records")
    return dict(version=1, mode="host-reference" if host else "native-receipt",
                renderer=renderer[0], gl_version=renderer[1], width=640, height=360,
                warmup_cycles=4, target_seconds=3, completion="glFinish-per-cycle",
                draws_per_cycle=3, copied_pixels_per_cycle=634 * 350, uploaded_pixels_per_cycle=1,
                session_setup_ms=setup / 1e6, session_cleanup_ms=cleanup / 1e6, cases=rows,
                note="Composite draw/copy/upload/FBO-draw/sample CPU wall time, including completion, "
                     "error checks and loop overhead. Setup, warmup, probes and cleanup are excluded. "
                     "Formats include conversion costs; storage footprints differ. Sampled pixels use "
                     "RGBA8 readback with +/-1 tolerance. No bandwidth, HDMI, game-FPS, GPU-internals, "
                     "exhaustive-image or native-launch/teardown claims.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--host", action="store_true", help="Require the host-reference build marker")
    args = parser.parse_args()
    try:
        raw = args.receipt.read_bytes()
        report = summarize(raw.decode(), args.host)
        report["receipt_sha256"] = hashlib.sha256(raw).hexdigest()
        print(json.dumps(report, indent=2, allow_nan=False))
    except (ValueError, OSError) as error:
        parser.exit(1, f"staging-profile: FAIL: {error}\n")
