#!/usr/bin/env python3
"""Verify the official GL 3.3 must-pass list against a CTS text case list."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("hierarchy", type=Path)
    parser.add_argument(
        "--mustpass",
        type=Path,
        default=(
            root
            / "third_party/VK-GL-CTS/external/openglcts/data/gl_cts/data"
            / "mustpass/gl/khronos_mustpass/main/gl33-main.txt"
        ),
    )
    arguments = parser.parse_args()

    available = {
        line.removeprefix("TEST: ").strip()
        for line in arguments.hierarchy.read_text().splitlines()
        if line.startswith("TEST: ")
    }
    expected_lines = [
        line.strip()
        for line in arguments.mustpass.read_text().splitlines()
        if line.strip()
    ]
    expected = set(expected_lines)
    if len(expected) != len(expected_lines):
        parser.error("official must-pass list contains duplicate names")
    missing = sorted(expected - available)
    if missing:
        for name in missing[:20]:
            print(f"missing={name}")
        print(
            f"cts-mustpass: FAIL package={len(available)} "
            f"mustpass={len(expected)} missing={len(missing)}"
        )
        return 1

    print(
        f"cts-mustpass: PASS package={len(available)} "
        f"mustpass={len(expected)} missing=0"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
