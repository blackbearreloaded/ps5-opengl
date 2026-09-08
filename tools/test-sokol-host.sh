#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sokol="$root/third_party/sokol"
test "$(git -C "$sokol" rev-parse HEAD)" = 48c85905aeaa1350feb17515961aecb6c75447d8
test -z "$(git -C "$sokol" status --porcelain)"
mkdir -p "$root/build/sokol-host"
clang-18 -std=gnu11 -O2 -Wall -Werror=implicit-function-declaration \
    -DPS5_SOKOL_HOST_REFERENCE -I"$root/build/sdk/ps5-opengl-core33/include" -I"$sokol" \
    "$root/examples/core33-sokol/main.c" -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$root/build/sokol-host/check"
extra=$(comm -23 \
    <(nm -u "$root/build/sokol-host/check" | sed 's/@.*//' | sed -n 's/.* U \(gl[A-Za-z0-9_]*\)$/\1/p' | sort -u) \
    <(sed -n 's/^ *"\(gl[A-Za-z0-9_]*\)".*/\1/p' "$root/tests/ps5/egl_public_core33_entrypoints.inc" | sort -u))
test -z "$extra" || { printf 'Non-Core-3.3 imports: %s\n' "$extra" >&2; exit 1; }
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 \
    MESA_GLSL_VERSION_OVERRIDE=330 "$root/build/sokol-host/check"
