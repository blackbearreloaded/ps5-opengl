#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/gpu-blit-extended-host"
mkdir -p "$out"
source="$root/tests/ps5/egl_public_core33_gpu_blit_extended.c"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_GPU_BLIT_EXTENDED_HOST_REFERENCE
       -I"$root/third_party/mesa-26.2.0/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
"${CC:-cc}" "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/reference"
timeout 60s "$out/reference" | tee "$out/reference.log"
"${CC:-cc}" "${flags[@]}" -UPS5_GPU_BLIT_EXTENDED_HOST_REFERENCE -fsyntax-only "$source"

# Mutate the actual GL operation, leaving the CPU oracle untouched. Both failures
# must be pixel mismatches with successful cleanup, not crashes or timeouts.
sed 's/glBlitFramebuffer(t->src\[0\],/glBlitFramebuffer(t->src[0]+1,/' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong-coordinates"
sed 's/glSampleMaski(0,1u << s);/glSampleMaski(0,1u);/' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong-samples"
for mutation in coordinates samples; do
    status=0
    timeout 60s "$out/wrong-$mutation" > "$out/wrong-$mutation.log" 2>&1 || status=$?
    if [[ $status != 1 ]]; then
        echo "FAIL: injected $mutation returned $status, expected pixel failure (1)" >&2
        exit 1
    fi
    expected=tiny-nearest
    [[ $mutation != samples ]] || expected=msaa4-average
    grep -q "\[gpu-blit-extended\] mismatch case=$expected stage=blit " "$out/wrong-$mutation.log"
    grep -q '\[gpu-blit-extended\] cleanup=1 result=1' "$out/wrong-$mutation.log"
    echo "PASS: injected $mutation rejected in $expected with clean teardown"
done
echo 'PASS: software GL scaled/flipped/scissored/state/4-sample-average oracles and native syntax'
