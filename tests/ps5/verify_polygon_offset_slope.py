#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Verify a paired D32F polygon-offset slope-factor capture."""

import argparse
import array
import json
import math
import struct
import sys
from pathlib import Path


EXPECTED_SIZE = 10 * 1024 * 1024
EXPECTED_COVERAGE = 194_400
DELTA_MAGNITUDE = (0.0002, 0.0003)


def load_words(path):
    data = Path(path).read_bytes()
    words = array.array("I")
    words.frombytes(data)
    if sys.byteorder != "little":
        words.byteswap()
    return data, words


def as_float(word):
    return struct.unpack("<f", struct.pack("<I", word))[0]


def verify(baseline_path, candidate_path, direction):
    baseline_data, baseline = load_words(baseline_path)
    candidate_data, candidate = load_words(candidate_path)
    failures = []

    if len(baseline_data) != EXPECTED_SIZE:
        failures.append(f"baseline size {len(baseline_data)} != {EXPECTED_SIZE}")
    if len(candidate_data) != EXPECTED_SIZE:
        failures.append(f"candidate size {len(candidate_data)} != {EXPECTED_SIZE}")
    if len(baseline) != len(candidate):
        failures.append("capture word counts differ")

    covered = 0
    mask_mismatches = 0
    wrong_direction = 0
    delta_outside = 0
    baseline_min = 0xffffffff
    baseline_max = 0
    candidate_min = 0xffffffff
    candidate_max = 0
    delta_min = math.inf
    delta_max = -math.inf
    baseline_values = set()
    candidate_values = set()
    accepted_delta = (
        DELTA_MAGNITUDE
        if direction == "positive"
        else (-DELTA_MAGNITUDE[1], -DELTA_MAGNITUDE[0])
    )

    for before, after in zip(baseline, candidate):
        if bool(before) != bool(after):
            mask_mismatches += 1
            continue
        if before == 0:
            continue

        covered += 1
        baseline_min = min(baseline_min, before)
        baseline_max = max(baseline_max, before)
        candidate_min = min(candidate_min, after)
        candidate_max = max(candidate_max, after)
        baseline_values.add(before)
        candidate_values.add(after)

        if (direction == "positive" and after <= before) or (
            direction == "negative" and after >= before
        ):
            wrong_direction += 1
        delta = as_float(after) - as_float(before)
        delta_min = min(delta_min, delta)
        delta_max = max(delta_max, delta)
        if not accepted_delta[0] <= delta <= accepted_delta[1]:
            delta_outside += 1

    if covered != EXPECTED_COVERAGE:
        failures.append(f"paired coverage {covered} != {EXPECTED_COVERAGE}")
    if mask_mismatches:
        failures.append(f"nonzero-mask mismatches: {mask_mismatches}")
    if baseline_min < 0x3f200000 or baseline_max >= 0x3f400000:
        failures.append("baseline values are outside [0.625, 0.75)")
    if baseline_min >= baseline_max:
        failures.append("baseline depth plane is not varying")
    if direction == "positive" and (
        candidate_max <= 0x3f400000 or candidate_max >= 0x3f402000
    ):
        failures.append("candidate maximum does not discriminate the positive slope term")
    if direction == "negative" and not 0x3f1fe000 <= candidate_min < 0x3f200000:
        failures.append("candidate minimum does not discriminate the negative slope term")
    if wrong_direction:
        failures.append(f"wrong-direction corresponding depth shifts: {wrong_direction}")
    if delta_outside:
        failures.append(
            f"depth shifts outside {accepted_delta}: {delta_outside}"
        )

    report = {
        "result": "pass" if not failures else "fail",
        "direction": direction,
        "baseline": str(Path(baseline_path)),
        "candidate": str(Path(candidate_path)),
        "expected_size": EXPECTED_SIZE,
        "expected_coverage": EXPECTED_COVERAGE,
        "covered_pairs": covered,
        "nonzero_mask_mismatches": mask_mismatches,
        "baseline_min": f"0x{baseline_min:08x}",
        "baseline_max": f"0x{baseline_max:08x}",
        "candidate_min": f"0x{candidate_min:08x}",
        "candidate_max": f"0x{candidate_max:08x}",
        "baseline_unique_values": len(baseline_values),
        "candidate_unique_values": len(candidate_values),
        "wrong_direction_shifts": wrong_direction,
        "accepted_delta": list(accepted_delta),
        "observed_delta_min": delta_min if covered else None,
        "observed_delta_max": delta_max if covered else None,
        "delta_outside_range": delta_outside,
        "failures": failures,
    }
    return report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline")
    parser.add_argument("candidate")
    parser.add_argument("--direction", choices=("positive", "negative"), default="positive")
    parser.add_argument("--output")
    args = parser.parse_args()

    report = verify(args.baseline, args.candidate, args.direction)
    rendered = json.dumps(report, indent=2, sort_keys=True)
    print(rendered)
    if args.output:
        Path(args.output).write_text(rendered + "\n", encoding="utf-8")
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
