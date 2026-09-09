#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
# Offline system-Mesa reference only; no native driver build or execution.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out=$(mktemp -d)
trap 'rm -f -- "$out/reference" "$out/reference.log" "$out/wrong" "$out/wrong.log"; rmdir -- "$out"' EXIT
source="$root/tests/ps5/egl_public_core33_msaa4_depth_array_texture.c"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_MSAA_DEPTH_ARRAY_HOST_REFERENCE
    -I"$root/third_party/mesa-26.2.0/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
export LP_NUM_THREADS=2 MESA_SHADER_CACHE_DISABLE=true
export MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
unset LD_LIBRARY_PATH LD_PRELOAD LIBGL_DRIVERS_PATH
ulimit -c 0
"${CC:-cc}" "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/reference"
"$out/reference" | tee "$out/reference.log"
grep -qF 'draw_counter=host-issued renderer=llvmpipe' "$out/reference.log"
"${CC:-cc}" "${flags[@]}" -UPS5_MSAA_DEPTH_ARRAY_HOST_REFERENCE -fsyntax-only "$source"
# Alter only the shader's layer-2 expectation. Initialization/resolve stay correct.
sed 's/0.375 : 0.4375/0.25 : 0.3125/' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong"
if "$out/wrong" > "$out/wrong.log" 2>&1; then
    cat "$out/wrong.log"
    echo 'FAIL: wrong depth oracle incorrectly accepted' >&2
    exit 1
else
    result=$?
fi
cat "$out/wrong.log"
[[ "$result" == 1 ]]
grep -qF 'draw_counter=host-issued renderer=llvmpipe' "$out/wrong.log"
tag='[host-egl-msaa4-depth-array-texture]'
for format in D32 D32S8; do
    stencil=0/0
    [[ "$format" != D32S8 ]] || stencil=1024/1024
    prefix="$tag format=$format samples=4 fixed=1 layers=2/3 resolve=1024/1024 stencil=$stencil"
    grep -qxF "$prefix sampled=1024/1024 rgba=255/255/255/255 draw_delta=1 cleanup=1 result=0" "$out/reference.log"
    grep -qxF "$prefix sampled=0/1024 rgba=0/255/255/255 draw_delta=1 cleanup=1 result=1" "$out/wrong.log"
done
grep -qxF "$tag final_draw=0/2 result=0" "$out/reference.log"
grep -qxF "$tag cleanup=1 result=0" "$out/reference.log"
grep -qxF "$tag final_draw=0/2 result=1" "$out/wrong.log"
grep -qxF "$tag cleanup=1 result=1" "$out/wrong.log"
echo 'PASS: D32/D32S8 MS-array sampling/resolve reference; native-branch syntax; wrong depth oracle rejected in both formats (exit 1)'
