#!/usr/bin/env python3
"""Report deterministic coverage and pixel statistics for linear BGRA8."""

import argparse
import json
from array import array
from pathlib import Path


def parse_sample(value):
    try:
        x, y = (int(part, 0) for part in value.split(",", 1))
    except (ValueError, TypeError) as error:
        raise argparse.ArgumentTypeError("sample must be x,y") from error
    return x, y


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--sample", action="append", type=parse_sample,
                        default=[])
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    data = args.input.read_bytes()
    expected = args.width * args.height * 4
    if len(data) != expected:
        raise SystemExit(f"expected {expected} bytes, got {len(data)}")
    pixels = array("I")
    pixels.frombytes(data)
    if pixels.itemsize != 4:
        raise SystemExit("host unsigned-int width is not 32 bits")

    nonzero = 0
    min_x, min_y = args.width, args.height
    max_x = max_y = -1
    values = set()
    row_coverage = []
    for y in range(args.height):
        row = pixels[y * args.width:(y + 1) * args.width]
        covered = 0
        for x, value in enumerate(row):
            if not value:
                continue
            covered += 1
            values.add(value)
            if x < min_x:
                min_x = x
            if x > max_x:
                max_x = x
        if covered:
            nonzero += covered
            row_coverage.append(covered)
            if y < min_y:
                min_y = y
            max_y = y

    bounds = None if not nonzero else [min_x, min_y, max_x, max_y]
    holes = outside = 0
    if bounds:
        for y in range(args.height):
            for x in range(args.width):
                covered = pixels[y * args.width + x] != 0
                inside = min_x <= x <= max_x and min_y <= y <= max_y
                holes += inside and not covered
                outside += covered and not inside

    samples = {}
    for x, y in args.sample:
        if not (0 <= x < args.width and 0 <= y < args.height):
            raise SystemExit(f"sample {x},{y} is outside the image")
        offset = (y * args.width + x) * 4
        samples[f"{x},{y}"] = list(data[offset:offset + 4])

    result = {
        "input": str(args.input),
        "width": args.width,
        "height": args.height,
        "nonzero": nonzero,
        "bounds": bounds,
        "distinct_nonzero_values": len(values),
        "min_nonzero_value": None if not values else f"0x{min(values):08x}",
        "max_nonzero_value": None if not values else f"0x{max(values):08x}",
        "covered_row_count": len(row_coverage),
        "min_covered_row_width": min(row_coverage, default=0),
        "max_covered_row_width": max(row_coverage, default=0),
        "holes_inside_bounds": holes,
        "covered_outside_bounds": outside,
        "samples_bgra": samples,
    }
    output = json.dumps(result, indent=2) + "\n"
    if args.json:
        args.json.write_text(output, encoding="utf-8")
    print(output, end="")


if __name__ == "__main__":
    main()
