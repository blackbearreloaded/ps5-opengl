#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Reference pixels/API semantics only; native draw counts and speed need native execution.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/gpu-clear-extended-host"
mkdir -p "$out"
source="$root/tests/ps5/egl_public_core33_gpu_clear_extended.c"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_GPU_CLEAR_EXTENDED_HOST_REFERENCE
       -I"$root/third_party/mesa-26.2.0/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
export LP_NUM_THREADS=2 MESA_SHADER_CACHE_DISABLE=true
export MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
"${CC:-cc}" "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/reference"
timeout 60s "$out/reference" | tee "$out/reference.log"
grep -q 'renderer=llvmpipe.*draw_counter=unavailable-host-reference' "$out/reference.log"
grep -qxF '[gpu-clear-extended] cleanup=1 result=0' "$out/reference.log"
[[ $(grep -c ' timing depth=' "$out/reference.log") == 4 ]]
for depth in D32 D32S8; do
    grep -q " timing depth=$depth mode=full-control size=1024x768 effective=1024x768 clears=8 .*result=0" "$out/reference.log"
    grep -q " timing depth=$depth mode=scissored size=1024x768 effective=970x696 clears=8 .*result=0" "$out/reference.log"
done
[[ $(grep -c ' state-reuse result=0' "$out/reference.log") == 8 ]]
"${CC:-cc}" "${flags[@]}" -UPS5_GPU_CLEAR_EXTENDED_HOST_REFERENCE -fsyntax-only "$source"

# Change the actual GL operation, leaving the CPU oracle intact. These faults
# exercise depth, stencil preservation, untouched pixels and float color storage.
for fault in depth stencil-mask scissor float-color; do
    case "$fault" in
        depth) change='s/glClearDepth(test->depth);/glClearDepth(0.5);/'; plane=depth;;
        stencil-mask) change='s/glStencilMaskSeparate(GL_FRONT, test->stencil_mask);/glStencilMaskSeparate(GL_FRONT, 0xff);/'; plane=stencil;;
        scissor) change='s/glScissor(test->x, test->y, test->w, test->h);/glScissor(0, 0, WIDTH, HEIGHT);/'; plane=depth;;
        float-color) change='s/glClearColor(color\[0\], color\[1\], color\[2\], color\[3\]);/glClearColor(color[0], floating ? 0.0f : color[1], color[2], color[3]);/'; plane=color;;
    esac
    sed "$change" "$source" |
        "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong-$fault"
    if timeout 60s "$out/wrong-$fault" > "$out/wrong-$fault.log" 2>&1; then
        echo "FAIL: wrong clear $fault escaped the oracle" >&2; exit 1
    fi
    grep -q "\[gpu-clear-extended\] mismatch .*plane=$plane " "$out/wrong-$fault.log"
    grep -qxF '[gpu-clear-extended] cleanup=1 result=1' "$out/wrong-$fault.log"
done
echo 'PASS: software GL extended clear matrix, native syntax; wrong depth/stencil-mask/scissor/float-color rejected'
