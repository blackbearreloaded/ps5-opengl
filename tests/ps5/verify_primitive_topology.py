#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Verify the bounded PS5 Gallium point/line topology target and depth dumps."""

import argparse
import hashlib
import json
import struct
from pathlib import Path


EXPECTED_SIZE = 10 * 1024 * 1024
EXPECTED_BLENDED_COLOR = 0x40102080
EXPECTED_DIRECT_COLOR = 0x802040FF
EXPECTED_DEPTH = 0x3F400000


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "mode",
        choices=(
            "point",
            "line",
            "multi-point",
            "multi-line",
            "line-strip",
            "triangle-strip",
            "triangle-fan",
        ),
    )
    parser.add_argument("target", type=Path)
    parser.add_argument("depth", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()

    target_data = args.target.read_bytes()
    depth_data = args.depth.read_bytes()
    failures = []
    if len(target_data) != EXPECTED_SIZE:
        failures.append(f"target size {len(target_data)} != {EXPECTED_SIZE}")
    if len(depth_data) != EXPECTED_SIZE:
        failures.append(f"depth size {len(depth_data)} != {EXPECTED_SIZE}")

    target_nonzero = 0
    depth_nonzero = 0
    unexpected_colors = 0
    unexpected_depths = 0
    expected_color = (
        EXPECTED_DIRECT_COLOR
        if args.mode in ("line-strip", "triangle-strip", "triangle-fan")
        else EXPECTED_BLENDED_COLOR
    )
    for (color,) in struct.iter_unpack("<I", target_data):
        color_set = color != 0
        target_nonzero += color_set
        unexpected_colors += color_set and color != expected_color
    for (depth,) in struct.iter_unpack("<I", depth_data):
        depth_set = depth != 0
        depth_nonzero += depth_set
        unexpected_depths += depth_set and depth != EXPECTED_DEPTH

    if args.mode == "point":
        if target_nonzero != 1:
            failures.append(f"point coverage {target_nonzero} != 1")
    elif args.mode == "line" and not 500 <= target_nonzero <= 600:
        failures.append(f"line coverage {target_nonzero} outside 500..600")
    elif args.mode == "multi-point" and target_nonzero != 3:
        failures.append(f"multi-point coverage {target_nonzero} != 3")
    elif args.mode == "multi-line" and target_nonzero != 1080:
        failures.append(f"multi-line coverage {target_nonzero} != 1080")
    elif args.mode == "line-strip" and not 1500 <= target_nonzero <= 1700:
        failures.append(
            f"line-strip coverage {target_nonzero} outside 1500..1700"
        )
    elif (
        args.mode in ("triangle-strip", "triangle-fan")
        and target_nonzero != 259200
    ):
        failures.append(f"{args.mode} coverage {target_nonzero} != 259200")
    if depth_nonzero != target_nonzero:
        failures.append(
            f"depth coverage {depth_nonzero} != color coverage {target_nonzero}"
        )
    if unexpected_colors:
        failures.append(f"unexpected nonzero color words {unexpected_colors} != 0")
    if unexpected_depths:
        failures.append(f"unexpected nonzero depth words {unexpected_depths} != 0")

    report = {
        "result": "pass" if not failures else "fail",
        "mode": args.mode,
        "expected_size": EXPECTED_SIZE,
        "target_bytes": len(target_data),
        "depth_bytes": len(depth_data),
        "coverage": target_nonzero,
        "depth_coverage": depth_nonzero,
        "raw_color_depth_mask_comparison": "not-applicable-different-layouts",
        "expected_color": f"0x{expected_color:08x}",
        "unexpected_color_words": unexpected_colors,
        "expected_depth": f"0x{EXPECTED_DEPTH:08x}",
        "unexpected_depth_words": unexpected_depths,
        "target_sha256": hashlib.sha256(target_data).hexdigest(),
        "depth_sha256": hashlib.sha256(depth_data).hexdigest(),
        "failures": failures,
    }
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
