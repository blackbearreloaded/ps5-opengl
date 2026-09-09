#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/gpu-mipmap-host"
mkdir -p "$out"
source="$root/tests/ps5/egl_public_core33_gpu_mipmap.c"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_GPU_MIPMAP_HOST_REFERENCE
       -I"${PS5_TEST_INCLUDE:-$root/third_party/mesa-26.2.0/include}")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
"${CC:-cc}" "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/reference"
timeout 60s "$out/reference" | tee "$out/reference.log"
"${CC:-cc}" "${flags[@]}" -UPS5_GPU_MIPMAP_HOST_REFERENCE -pedantic -fsyntax-only "$source"
# Corrupt mip 2 after generation: mip-1 sampling must still pass, and the
# independent all-level readback oracle must reject the changed texel.
sed '/^   glGenerateMipmap(target);$/a\   if (target==GL_TEXTURE_2D) { const uint8_t bad[4]={0}; glTexSubImage2D(target,2,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,bad); }' "$source" |
    "${CC:-cc}" "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong"
status=0
timeout 60s "$out/wrong" > "$out/wrong.log" 2>&1 || status=$?
if [[ $status != 1 ]]; then
    echo "FAIL: injected corruption returned $status, expected pixel failure (1)" >&2
    exit 1
fi
grep -q '\[gpu-mipmap\] oracle case=chain-RGBA8 stage=sample-all .* result=0' "$out/wrong.log"
grep -q '\[gpu-mipmap\] mismatch case=chain-RGBA8 stage=chain level=2 layer=0 ' "$out/wrong.log"
grep -q '\[gpu-mipmap\] cleanup=1 result=1' "$out/wrong.log"
echo 'PASS: software GL mip chains/layer preservation/sampling/state, native syntax, injected corruption rejected'
