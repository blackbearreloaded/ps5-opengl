#!/usr/bin/env python3
"""Verify the mutable CTS inputs archived beside one receipt."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("prefix", type=Path)
    parser.add_argument("--expected-arguments-sha256", required=True)
    parser.add_argument("--expected-case-list-sha256", required=True)
    args = parser.parse_args()

    files = {
        "arguments": (Path(f"{args.prefix}-cts-args.txt"), args.expected_arguments_sha256),
        "case-list": (Path(f"{args.prefix}-cts-shard.txt"), args.expected_case_list_sha256),
    }
    for label, (path, expected) in files.items():
        if not path.is_file():
            parser.error(f"missing archived {label}: {path}")
        actual = sha256(path)
        if actual != expected.lower():
            parser.error(f"{label} hash mismatch: expected {expected}, got {actual}")
    print(f"CTS receipt inputs verified: {args.prefix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
