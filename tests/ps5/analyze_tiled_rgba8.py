#!/usr/bin/env python3
"""Report logical coverage for the proven PS5 1920x1080 swizzle-27 target."""

import argparse
import hashlib
import json
import struct
from pathlib import Path


WIDTH = 1920
HEIGHT = 1080
ALLOCATION_SIZE = 10 * 1024 * 1024
SAMPLE_ROWS = (269, 270, 271, 404, 539, 540, 674, 808, 809, 810)


def tiled_offset(x: int, y: int) -> int:
    inside = (
        ((y << 4) & 0x70)
        ^ ((y << 5) & 0xF00)
        ^ ((y << 9) & 0x1000)
        ^ ((y << 8) & 0x4000)
        ^ ((x << 2) & 0x0C)
        ^ ((x << 5) & 0x380)
        ^ ((x << 4) & 0x400)
        ^ ((x << 6) & 0x800)
        ^ ((x << 9) & 0xA000)
    )
    blocks_per_row = (WIDTH + 127) >> 7
    return (((y >> 7) * blocks_per_row + (x >> 7)) << 16) + inside


def decode(path: Path, expected: int) -> tuple[dict, bytearray]:
    data = path.read_bytes()
    if len(data) != ALLOCATION_SIZE:
        raise ValueError(f"{path}: {len(data)} bytes != {ALLOCATION_SIZE}")

    mask = bytearray(WIDTH * HEIGHT)
    rows = []
    coverage = 0
    unexpected = 0
    min_x, min_y = WIDTH, HEIGHT
    max_x = max_y = -1
    quadrants = {"top_left": 0, "top_right": 0, "bottom_left": 0, "bottom_right": 0}

    for y in range(HEIGHT):
        row_count = 0
        row_min = WIDTH
        row_max = -1
        for x in range(WIDTH):
            value = struct.unpack_from("<I", data, tiled_offset(x, y))[0]
            if not value:
                continue
            mask[y * WIDTH + x] = 1
            coverage += 1
            row_count += 1
            row_min = min(row_min, x)
            row_max = x
            min_x = min(min_x, x)
            max_x = max(max_x, x)
            min_y = min(min_y, y)
            max_y = y
            unexpected += value != expected
            if 240 <= x < 720 and 270 <= y < 810:
                vertical = "top" if y < 540 else "bottom"
                horizontal = "left" if x < 480 else "right"
                quadrants[f"{vertical}_{horizontal}"] += 1
        rows.append({"count": row_count, "min_x": row_min if row_count else None,
                     "max_x": row_max if row_count else None})

    report = {
        "path": str(path),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "expected_word": f"0x{expected:08x}",
        "coverage": coverage,
        "unexpected_words": unexpected,
        "bounds": None if not coverage else [min_x, min_y, max_x, max_y],
        "grid_rectangle_quadrants": quadrants,
        "sample_rows": {str(y): rows[y] for y in SAMPLE_ROWS},
    }
    return report, mask


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("target", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("--expected", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--compare", type=Path)
    args = parser.parse_args()

    report, mask = decode(args.target, args.expected)
    if args.compare:
        comparison, other = decode(args.compare, args.expected)
        report["compare"] = {
            "path": comparison["path"],
            "sha256": comparison["sha256"],
            "coverage": comparison["coverage"],
            "intersection": sum(left and right for left, right in zip(mask, other)),
            "target_only": sum(left and not right for left, right in zip(mask, other)),
            "compare_only": sum(right and not left for left, right in zip(mask, other)),
        }

    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["unexpected_words"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
