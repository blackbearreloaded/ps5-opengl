#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Reference GLSL/pixels/API errors only: not native driver or tiling acceptance.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/depth-targets-host"
mkdir -p "$out"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_DEPTH_TARGETS_HOST_REFERENCE
    -I"$root/third_party/mesa-26.2.0/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
export LP_NUM_THREADS=2 MESA_SHADER_CACHE_DISABLE=true
export MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
array="$root/tests/ps5/egl_public_core33_depth_array.c"
mip="$root/tests/ps5/egl_public_core33_depth_mip_target.c"

for test in array mip; do
    "${CC:-cc}" "${flags[@]}" "${!test}" -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$out/$test"
    "$out/$test" > "$out/$test.log" 2>&1
    grep -F 'draw_counter=host-issued renderer=llvmpipe' "$out/$test.log"
done
grep -qxF '[host-egl-core33-depth-array] mip=1 pixel=0/255/255/255 draw=0/1 error=0x0 result=0' "$out/array.log"
grep -qxF '[host-egl-core33-depth-array] cleanup=1 result=0' "$out/array.log"
grep -qxF '[host-egl-core33-depth-mip-target] invalid_3d_error=0x502 expected=0x502 result=0' "$out/mip.log"
grep -qxF '[host-egl-core33-depth-mip-target] matching=5 depths=0.500000/0.500000/0.500000/0.500000/0.500000 draw=0/5 rejected_3d=1 error=0x0 result=0' "$out/mip.log"
grep -qxF '[host-egl-core33-depth-mip-target] cleanup=1 result=0' "$out/mip.log"

# Reject the original GLSL error, bad array data, missing depth draws, and a
# legal 3D color allocation substituted for the required invalid depth case.
for fault in raw-scalar array-data missing-draw valid-3d; do
    case "$fault" in
        raw-scalar) source="$array"; change='s/))\.r;/));/g'; tag=array;;
        array-data) source="$array"; change='s/source\[TEX_SIZE \* TEX_SIZE + i\] = 0.75f/source[TEX_SIZE * TEX_SIZE + i] = 0.25f/'; tag=array;;
        missing-draw) source="$mip"; change='s/glDrawArrays(GL_TRIANGLES, 0, 3)/glDrawArrays(GL_TRIANGLES, 0, 0)/'; tag=mip-target;;
        valid-3d) source="$mip"; change='/glTexImage3D(GL_TEXTURE_3D, 0, GL_DEPTH_COMPONENT32F,/{s/GL_DEPTH_COMPONENT32F/GL_RGBA32F/;n;n;s/GL_DEPTH_COMPONENT/GL_RGBA/;}'; tag=mip-target;;
    esac
    sed "$change" "$source" |
        "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$out/$fault"
    if "$out/$fault" > "$out/$fault.log" 2>&1; then
        echo "Depth oracle incorrectly accepted $fault" >&2; exit 1
    fi
    grep -qxF "[host-egl-core33-depth-$tag] cleanup=1 result=1" "$out/$fault.log"
done
grep -F 'shader=0x8b30 log=' "$out/raw-scalar.log"
grep -qxF '[host-egl-core33-depth-array] mip=0 pixel=255/0/0/255 draw=0/1 error=0x0 result=1' "$out/array-data.log"
grep -qxF '[host-egl-core33-depth-mip-target] matching=0 depths=0.750000/0.750000/0.750000/0.750000/0.750000 draw=0/5 rejected_3d=1 error=0x0 result=1' "$out/missing-draw.log"
grep -qxF '[host-egl-core33-depth-mip-target] invalid_3d_error=0x0 expected=0x502 result=1' "$out/valid-3d.log"
grep -qxF '[host-egl-core33-depth-mip-target] matching=5 depths=0.500000/0.500000/0.500000/0.500000/0.500000 draw=0/5 rejected_3d=0 error=0x0 result=1' "$out/valid-3d.log"
echo 'Depth targets host PASS: array mip/raw/shadow pixels, five depth mip targets, invalid 3D rejected; four faults rejected'
