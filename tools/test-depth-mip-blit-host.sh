#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
# Offline system-Mesa oracle only; no native build, execution, or tiling claim.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out=$(mktemp -d)
trap 'rm -f -- "$out/reference" "$out/reference.log" "$out/wrong" "$out/wrong.log"; rmdir -- "$out"' EXIT
source="$root/tests/ps5/egl_public_core33_depth_mip_blit.c"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_DEPTH_MIP_BLIT_HOST_REFERENCE
    -I"$root/third_party/mesa-26.2.0/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
export LP_NUM_THREADS=2 MESA_SHADER_CACHE_DISABLE=true
export MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
unset LD_LIBRARY_PATH LD_PRELOAD LIBGL_DRIVERS_PATH
ulimit -c 0
"${CC:-cc}" "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/reference"
timeout 120 "$out/reference" | tee "$out/reference.log"
grep -qF '[depth-mip-blit] mode=host renderer=llvmpipe' "$out/reference.log"
tag='[depth-mip-blit]'
grep -qxF "$tag summary cases=112 passed=112 pixels=1373440 depth_pixels=1373440 stencil_pixels=1018240 errors=0 depth_errors=0 stencil_errors=0 gl_errors=0 fbo_errors=0 driver_errors=0 egl_error=0x3000 cleanup=1 result=0" "$out/reference.log"
[[ $(grep -cF "$tag case=" "$out/reference.log") == 112 ]]
[[ $(grep -cF 'stage=initial' "$out/reference.log") == 4 ]]
! grep -qF 'result=1' "$out/reference.log"
# Compile the PS5 macro branch without linking or building native artifacts.
"${CC:-cc}" "${flags[@]}" -UPS5_DEPTH_MIP_BLIT_HOST_REFERENCE -fsyntax-only "$source"
# Change exactly one CPU expected depth in the first blit's destination. All GL
# calls/data stay legal and unchanged; require one mismatch, exit 1, no case 2.
sed 's/float expected_depth = value->depth;/float expected_depth = value->depth + (cases == 1 \&\& resource == 1 \&\& level == 1 \&\& layer == 0 \&\& i == 33 ? 0.125f : 0.0f);/' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong"
if timeout 120 "$out/wrong" > "$out/wrong.log" 2>&1; then
    cat "$out/wrong.log"
    echo "$tag FAIL wrong_expected_accepted=1" >&2
    exit 1
else
    result=$?
fi
cat "$out/wrong.log"
[[ "$result" == 1 ]]
grep -qF "$tag mode=host renderer=llvmpipe" "$out/wrong.log"
[[ $(grep -cF "$tag case=" "$out/wrong.log") == 1 ]]
grep -qF "$tag mismatch case=1 resource=1 level=1 layer=0 xy=1,2 " "$out/wrong.log"
grep -qxF "$tag case=1 format=D32 target=2D stage=mip0-mip1 mask=depth src=0:0:0 dst=1:1:0 samples=1->1 pixels=4736 depth_errors=1 stencil_errors=0 result=1" "$out/wrong.log"
grep -qxF "$tag summary cases=1 passed=0 pixels=9472 depth_pixels=9472 stencil_pixels=0 errors=1 depth_errors=1 stencil_errors=0 gl_errors=0 fbo_errors=0 driver_errors=0 egl_error=0x3000 cleanup=1 result=1" "$out/wrong.log"
echo "$tag PASS host_cases=112 native_branch=syntax-only wrong_expected=rejected sample_isolation=0"
