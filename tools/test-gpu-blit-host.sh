#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/gpu-blit-host"
mkdir -p "$out"
source="$root/tests/ps5/egl_public_core33_gpu_blit.c"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_GPU_BLIT_HOST_REFERENCE
       -I"$root/third_party/mesa-26.2.0/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
"${CC:-cc}" "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/reference"
"$out/reference" | tee "$out/reference.log"
"${CC:-cc}" "${flags[@]}" -UPS5_GPU_BLIT_HOST_REFERENCE -fsyntax-only "$source"
# The full-image oracle must reject even a one-texel offset error.
sed 's/pattern(x-1,y-2,c)/pattern(x,y-2,c)/' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong"
if "$out/wrong" > "$out/wrong.log" 2>&1; then
    echo 'FAIL: wrong copy coordinates escaped the oracle' >&2; exit 1
fi
grep -q '\[gpu-blit\] mismatch' "$out/wrong.log"
grep -q '\[gpu-blit\] cleanup=1 result=1' "$out/wrong.log"
echo 'PASS: software GL copy/state oracle, native syntax, injected coordinate error rejected'
