#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Same VS/GS pixel discriminator, with only the EGL surface changed for Mesa.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/build/diagnostics"
clang-18 -std=c11 -O2 -Wall -Wextra -Werror -DPS5_GEOMETRY_HOST_REFERENCE \
    -I"$root/third_party/mesa-26.2.0/include" \
    "$root/tests/ps5/egl_public_core33_geometry_texture.c" \
    -l:libEGL.so.1 -l:libGL.so.1 -o "$root/build/diagnostics/geometry-texture-host"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 \
    MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 \
    "$root/build/diagnostics/geometry-texture-host"
