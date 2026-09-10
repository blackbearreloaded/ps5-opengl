#!/usr/bin/env python3
# PS5 OpenGL - native application display metadata.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Match ordinary native title display capabilities to its runtime profile."""
import argparse
import importlib
import json
from pathlib import Path

HFR_FLAGS = 0x80040


def with_display_profile(metadata, fps):
    flags = metadata.get("attribute3")
    if type(flags) is not int or not 0 <= flags < 2**32 or fps not in (60, 120):
        raise ValueError("Invalid native display metadata or presentation rate")
    return dict(metadata, attribute3=(flags & ~HFR_FLAGS) | (HFR_FLAGS if fps == 120 else 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("--fps", type=int, choices=(60, 120), default=60)
    parser.add_argument("--sdk-prefix", type=Path)
    args = parser.parse_args()
    fps = (importlib.import_module("check-sdk-consumers").display_profile(args.sdk_prefix)["fps"]
           if args.sdk_prefix else args.fps)
    metadata = with_display_profile(json.loads(args.metadata.read_text()), fps)
    args.metadata.write_text(json.dumps(metadata, indent=2) + "\n")


if __name__ == "__main__":
    main()
