#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Same unmodified renderer and pixel oracle on host software Mesa.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
nanovg="$root/third_party/nanovg"
test "$(git -C "$nanovg" rev-parse HEAD)" = ce3bf745eb2d2dbc14a50bf2446783f691ac4353
test -z "$(git -C "$nanovg" status --porcelain)"
mkdir -p "$root/build/nanovg-host"
clang-18 -std=gnu11 -O2 -Wall -Werror=implicit-function-declaration \
    -DPS5_NANOVG_HOST_REFERENCE -I"$root/build/sdk/ps5-opengl-core33/include" -I"$nanovg/src" \
    "$root/examples/core33-nanovg/main.c" "$nanovg/src/nanovg.c" \
    -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$root/build/nanovg-host/check"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 \
    MESA_GLSL_VERSION_OVERRIDE=330 "$root/build/nanovg-host/check"
