#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/native-color-formats-host"
mkdir -p "$out"
source="$root/tests/ps5/egl_public_core33_native_color_formats.c"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_NATIVE_COLOR_FORMATS_HOST_REFERENCE
       -I"$root/third_party/mesa-26.2.0/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
"${CC:-cc}" "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/reference"
timeout 60s "$out/reference" | tee "$out/reference.log"
"${CC:-cc}" "${flags[@]}" -UPS5_NATIVE_COLOR_FORMATS_HOST_REFERENCE -pedantic -fsyntax-only "$source"

# Change shader output, never CPU expectations. Each mutation first fails in a
# different format: R8 missing alpha, RG8 missing blue, RGBA16F negative/HDR.
sed 's/color=texture(image,gl_FragCoord.xy\/extent);/color=texture(image,gl_FragCoord.xy\/extent); color.a=0.0;/' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong-alpha"
sed 's/color=texture(image,gl_FragCoord.xy\/extent);/color=texture(image,gl_FragCoord.xy\/extent); color.b=color.g;/' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong-blue"
sed 's/color=texture(image,gl_FragCoord.xy\/extent);/color=clamp(texture(image,gl_FragCoord.xy\/extent),0.0,1.0);/' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong-hdr"
for mutation in alpha blue hdr; do
    status=0
    timeout 60s "$out/wrong-$mutation" > "$out/wrong-$mutation.log" 2>&1 || status=$?
    if [[ $status != 1 ]]; then
        echo "FAIL: injected $mutation returned $status, expected pixel failure (1)" >&2
        exit 1
    fi
    expected=R8
    [[ $mutation != blue ]] || expected=RG8
    [[ $mutation != hdr ]] || expected=RGBA16F
    grep -q "\[native-color-formats\] mismatch format=$expected size=513x259 stage=draw-sample " "$out/wrong-$mutation.log"
    grep -q '\[native-color-formats\] cleanup=1 result=1' "$out/wrong-$mutation.log"
    echo "PASS: injected $mutation rejected in $expected with clean teardown"
done
echo 'PASS: software GL native color format upload/draw/sample/clear/state oracles and native syntax'
