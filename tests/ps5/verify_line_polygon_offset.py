#!/usr/bin/env python3
"""Verify paired one-pixel line-polygon color and D32F offset captures."""

import argparse
import array
import hashlib
import json
import sys
from pathlib import Path


EXPECTED_SIZE = 10 * 1024 * 1024
EXPECTED_COLOR = 0x40102080
MIN_COVERAGE = 1200
MAX_COVERAGE = 1800


def words(path):
    data = Path(path).read_bytes()
    result = array.array("I")
    result.frombytes(data)
    if sys.byteorder != "little":
        result.byteswap()
    return data, result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline_target")
    parser.add_argument("baseline_depth")
    parser.add_argument("candidate_target")
    parser.add_argument("candidate_depth")
    parser.add_argument("--output")
    args = parser.parse_args()

    target0_data, target0 = words(args.baseline_target)
    depth0_data, depth0 = words(args.baseline_depth)
    target1_data, target1 = words(args.candidate_target)
    depth1_data, depth1 = words(args.candidate_depth)
    failures = []

    for name, data in (
        ("baseline target", target0_data),
        ("baseline depth", depth0_data),
        ("candidate target", target1_data),
        ("candidate depth", depth1_data),
    ):
        if len(data) != EXPECTED_SIZE:
            failures.append(f"{name} size {len(data)} != {EXPECTED_SIZE}")

    target0_nonzero = [value for value in target0 if value]
    target1_nonzero = [value for value in target1 if value]
    depth0_nonzero = [value for value in depth0 if value]
    depth1_nonzero = [value for value in depth1 if value]
    depth0_values = sorted(set(depth0_nonzero))
    depth1_values = sorted(set(depth1_nonzero))
    coverage = len(target0_nonzero)

    if not MIN_COVERAGE <= coverage <= MAX_COVERAGE:
        failures.append(f"line coverage {coverage} outside {MIN_COVERAGE}..{MAX_COVERAGE}")
    if any(value != EXPECTED_COLOR for value in target0_nonzero + target1_nonzero):
        failures.append("unexpected nonzero color value")
    if target0_data != target1_data:
        failures.append("baseline and candidate color allocations differ")
    if len(depth0_nonzero) != coverage or len(depth1_nonzero) != coverage:
        failures.append("color/depth coverage counts differ")
    if depth0_values != [0x3f400000]:
        failures.append("baseline depth is not the uniform control")
    if len(depth1_values) != 1:
        failures.append("candidate depth is not uniform")

    mask_mismatches = 0
    wrong_depth = 0
    for before, after in zip(depth0, depth1):
        if bool(before) != bool(after):
            mask_mismatches += 1
        elif before and (before != 0x3f400000 or not 0x3f400001 <= after <= 0x3f400100):
            wrong_depth += 1
    if mask_mismatches:
        failures.append(f"depth nonzero-mask mismatches: {mask_mismatches}")
    if wrong_depth:
        failures.append(f"depth value mismatches: {wrong_depth}")

    report = {
        "result": "pass" if not failures else "fail",
        "expected_size": EXPECTED_SIZE,
        "accepted_coverage": [MIN_COVERAGE, MAX_COVERAGE],
        "coverage": coverage,
        "baseline_target_sha256": hashlib.sha256(target0_data).hexdigest(),
        "candidate_target_sha256": hashlib.sha256(target1_data).hexdigest(),
        "baseline_depth_sha256": hashlib.sha256(depth0_data).hexdigest(),
        "candidate_depth_sha256": hashlib.sha256(depth1_data).hexdigest(),
        "baseline_depth_values": [f"0x{value:08x}" for value in depth0_values],
        "candidate_depth_values": [f"0x{value:08x}" for value in depth1_values],
        "depth_nonzero_mask_mismatches": mask_mismatches,
        "depth_value_mismatches": wrong_depth,
        "failures": failures,
    }
    rendered = json.dumps(report, indent=2, sort_keys=True)
    print(rendered)
    if args.output:
        Path(args.output).write_text(rendered + "\n", encoding="utf-8")
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
