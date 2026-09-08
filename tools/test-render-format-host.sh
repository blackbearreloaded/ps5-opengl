#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Public format/sRGB oracles on host Mesa; only EGL surface type differs.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/build/render-format-host"
clang-18 -std=c11 -O2 -Wall -Wextra -Werror -DPS5_FORMAT_HOST_REFERENCE \
    -I"${PS5_FORMAT_INCLUDE:-$root/build/sdk/ps5-opengl-core33/include}" \
    "$root/tests/ps5/egl_public_core33_render_format_float.c" \
    -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$root/build/render-format-host/check"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 MESA_SHADER_CACHE_DISABLE=true \
    MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 \
    "$root/build/render-format-host/check"
