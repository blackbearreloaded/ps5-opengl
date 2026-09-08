#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix=${PS5_OPENGL_PREFIX:-"$root/build/sdk/ps5-opengl-core33"}
out="$root/build/staging-regressions-host"
mkdir -p "$out"
clang-18 -std=c11 -O2 -Wall -Wextra -Werror \
    -DPS5_FORMAT_HOST_REFERENCE -DPS5_STAGING_HOST_REFERENCE -I"$prefix/include" \
    "$root/tests/ps5/egl_public_core33_staging_regressions.c" \
    -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$out/check"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
    LP_NUM_THREADS=2 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 \
    MESA_SHADER_CACHE_DISABLE=true "$out/check" > "$out/receipt.log" 2>&1
grep -qxF '[ps5-egl-staging-regressions] gates=2 result=0' "$out/receipt.log"
python3 "$root/tools/summarize-staging-profile.py" --host "$out/receipt.log"
