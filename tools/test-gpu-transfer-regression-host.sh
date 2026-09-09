#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sources=${PS5_TEST_SOURCE_ROOT:-$root}
out="$root/build/gpu-transfer-regression-host"
mkdir -p "$out"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -I"${PS5_TEST_INCLUDE:-$root/third_party/mesa-26.2.0/include}")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
files=(gpu_blit_extended gpu_clear_extended native_color_formats)
macros=(PS5_GPU_BLIT_EXTENDED_HOST_REFERENCE PS5_GPU_CLEAR_EXTENDED_HOST_REFERENCE PS5_NATIVE_COLOR_FORMATS_HOST_REFERENCE)
names=(blit clear formats)
for i in 0 1 2; do
    "${CC:-cc}" "${flags[@]}" -D"${macros[i]}" -Dmain="ps5_transfer_${names[i]}_main" \
        -c "$sources/tests/ps5/egl_public_core33_${files[i]}.c" -o "$out/${names[i]}.o"
done
wrapper="$root/tests/ps5/egl_public_core33_gpu_transfer_regression.c"
"${CC:-cc}" "${flags[@]}" "$wrapper" "$out/blit.o" "$out/clear.o" "$out/formats.o" \
    -l:libEGL.so.1 -l:libGL.so.1 -o "$out/reference"
timeout 120s "$out/reference" > "$out/reference.log"
grep -q '^\[gpu-transfer-regression\] completed=3/3 result=0$' "$out/reference.log"
printf 'int ps5_transfer_blit_main(void){return 1;} int ps5_transfer_clear_main(void){return 99;} int ps5_transfer_formats_main(void){return 99;}\n' |
    "${CC:-cc}" "${flags[@]}" "$wrapper" -x c - -o "$out/failure"
status=0
"$out/failure" > "$out/failure.log" || status=$?
[[ $status == 1 && $(grep -c 'begin=' "$out/failure.log") == 1 ]]
grep -q '^\[gpu-transfer-regression\] completed=0/3 result=1$' "$out/failure.log"
echo 'PASS: three real GL oracles in one process; first failure stops the batch'
