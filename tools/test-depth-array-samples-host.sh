#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
# Offline system-Mesa reference only; does not build or run the native driver.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out=$(mktemp -d)
trap 'rm -f -- "$out/reference" "$out/reference.log" "$out/wrong" "$out/wrong.log"; rmdir -- "$out"' EXIT
source="$root/tests/ps5/egl_public_core33_depth_array_samples.c"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_DEPTH_ARRAY_SAMPLES_HOST_REFERENCE
    -I"$root/third_party/mesa-26.2.0/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
export LP_NUM_THREADS=2 MESA_SHADER_CACHE_DISABLE=true
export MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
unset LD_LIBRARY_PATH LD_PRELOAD LIBGL_DRIVERS_PATH
ulimit -c 0
"${CC:-cc}" "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/reference"
"$out/reference" | tee "$out/reference.log"
grep -qF 'draw_counter=host-issued renderer=llvmpipe' "$out/reference.log"
# Compile the native macro branch too, without linking or executing it.
"${CC:-cc}" "${flags[@]}" -UPS5_DEPTH_ARRAY_SAMPLES_HOST_REFERENCE -fsyntax-only "$source"
# An empty draw is legal GL. Keep the issued counter intact: pixels must reject it.
sed 's/glDrawArrays(GL_TRIANGLES, 0, 3)/glDrawArrays(GL_TRIANGLES, 0, 0)/' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong"
if "$out/wrong" > "$out/wrong.log" 2>&1; then
    cat "$out/wrong.log"
    echo 'FAIL: empty draw incorrectly accepted' >&2
    exit 1
else
    result=$?
fi
cat "$out/wrong.log"
[[ "$result" == 1 ]]
grep -qF 'draw_counter=host-issued renderer=llvmpipe' "$out/wrong.log"
grep -qxF '[host-egl-core33-depth-array-samples] samples=1 explicit_draws=2 cleanup=1 result=1' "$out/wrong.log"
grep -qxF '[host-egl-core33-depth-array-samples] samples=4 explicit_draws=2 cleanup=1 result=1' "$out/wrong.log"
grep -qxF '[host-egl-core33-depth-array-samples] final_draw=0/4 result=1' "$out/wrong.log"
grep -qxF '[host-egl-core33-depth-array-samples] cleanup=1 result=1' "$out/wrong.log"
echo 'PASS: 1x/4x reference; native-branch syntax; legal empty-draw fault rejected in both variants (exit 1)'
