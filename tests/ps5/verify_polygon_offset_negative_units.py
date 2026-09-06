#!/usr/bin/env python3
"""Verify the fixed color target and negative-units D32F capture."""

import argparse
import array
import hashlib
import json
import sys
from pathlib import Path


EXPECTED_SIZE = 10 * 1024 * 1024
EXPECTED_COVERAGE = 194_400
EXPECTED_TARGET_SHA256 = (
    "41672c78dfed0050aff37b7f3ba9562e73a20d48cd9fbc1b10bbffa477481102"
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("target")
    parser.add_argument("depth")
    parser.add_argument("--output")
    args = parser.parse_args()

    target = Path(args.target).read_bytes()
    depth = Path(args.depth).read_bytes()
    words = array.array("I")
    words.frombytes(depth)
    if sys.byteorder != "little":
        words.byteswap()
    nonzero = [word for word in words if word]
    unique = sorted(set(nonzero))
    target_sha256 = hashlib.sha256(target).hexdigest()
    depth_sha256 = hashlib.sha256(depth).hexdigest()
    observed = unique[0] if len(unique) == 1 else None

    failures = []
    if len(target) != EXPECTED_SIZE:
        failures.append(f"target size {len(target)} != {EXPECTED_SIZE}")
    if target_sha256 != EXPECTED_TARGET_SHA256:
        failures.append("target does not match the proven fixed-state color oracle")
    if len(depth) != EXPECTED_SIZE:
        failures.append(f"depth size {len(depth)} != {EXPECTED_SIZE}")
    if len(nonzero) != EXPECTED_COVERAGE:
        failures.append(f"depth coverage {len(nonzero)} != {EXPECTED_COVERAGE}")
    if observed is None:
        failures.append(f"depth is not uniform: {len(unique)} nonzero values")
    elif not 0x3f3fff00 <= observed < 0x3f400000:
        failures.append(f"depth 0x{observed:08x} is outside the negative range")

    report = {
        "result": "pass" if not failures else "fail",
        "target": str(Path(args.target)),
        "depth": str(Path(args.depth)),
        "bytes": len(depth),
        "target_sha256": target_sha256,
        "depth_sha256": depth_sha256,
        "nonzero_words": len(nonzero),
        "unique_nonzero_values": len(unique),
        "observed": None if observed is None else f"0x{observed:08x}",
        "accepted": "0x3f3fff00..0x3f3fffff",
        "unoffset_control": "0x3f400000",
        "failures": failures,
    }
    rendered = json.dumps(report, indent=2, sort_keys=True)
    print(rendered)
    if args.output:
        Path(args.output).write_text(rendered + "\n", encoding="utf-8")
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
