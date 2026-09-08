#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Generate the disclosed platform adaptation; never edit the upstream checkout."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
for name in ("sokol", "sokol-samples"):
    source = ROOT / "third_party" / name
    pin = json.loads((ROOT / "dependencies.json").read_text())["repositories"][name]
    if subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() != pin["revision"]:
        raise ValueError(f"{name}: unexpected revision; checkout preserved")
    if subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"], text=True).strip():
        raise ValueError(f"{name}: checkout is not clean; changes preserved")

source = (ROOT / "third_party/sokol-samples/glfw/cube-glfw.c").read_text()
for old, new, count in (
    ('"glfw_glue.h"', '"native_glue.h"', 1),
    ('"../libs/vecmath/vecmath.h"', '"vecmath.h"', 1),
    ('int main()', 'static void run_upstream_cube(void)', 1),
    ('#version 410', '#version 330 core', 2),
    ('.sample_count = 4', '.sample_count = 1', 1),
    ('.logger.func = slog_func', '.logger.func = cube_log', 1),
):
    if source.count(old) != count:
        raise ValueError(f"upstream adaptation no longer applies exactly: {old}")
    source = source.replace(old, new)
output = ROOT / "build/sokol-cube-source/cube.inc"
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(
    "// Adapted from the pinned MIT-licensed Sokol sample; see LICENSES/Sokol-Samples.txt.\n"
    "// PS5 platform and OpenGL 3.3 adaptations by BlackBearReloaded, 2026.\n"
    "// Upstream copyright and MIT license are unchanged.\n" + source)
print(output)
