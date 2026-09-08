#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler="$project_dir/third_party/opengnm-psbc/opengnm-psbc"
source_dir="$project_dir/shaders/gate4"
output_dir="${1:-$project_dir/build/gate4-depth}"
writer="$project_dir/tools/agc_shader_package_writer.py"

test -x "$compiler" || {
    echo "missing compiler: run toolchain/build-opengnm-psbc.sh first" >&2
    exit 1
}
command -v glslangValidator >/dev/null || {
    echo "missing glslangValidator" >&2
    exit 1
}

mkdir -p "$output_dir"

glslangValidator -V --target-env vulkan1.2 -S vert \
    "$source_dir/depth.vert" -o "$output_dir/depth.vert.spv"
glslangValidator -V --target-env vulkan1.2 -S frag \
    "$source_dir/depth.frag" -o "$output_dir/depth.frag.spv"

"$compiler" -g -s vertex --ngg --raw --address32-hi 2 \
    --vertex-attribute 0:r32g32b32_float:0:0:28:4 \
    --vertex-attribute 1:r32g32b32a32_float:0:12:28:4 \
    -f "$output_dir/depth.vert.spv" \
    -o "$output_dir/depth.vert.ngg.bin" \
    --metadata "$output_dir/depth.vert.ngg.hw.json"
"$compiler" -g -s fragment --raw --address32-hi 2 \
    -f "$output_dir/depth.frag.spv" \
    -o "$output_dir/depth.frag.raw.bin" \
    --metadata "$output_dir/depth.frag.hw.json"

python3 "$writer" \
    "$output_dir/depth.vert.ngg.bin" \
    "$output_dir/depth.vert.ngg.hw.json" \
    -o "$output_dir/depth.vert.ngg.agc.sb" \
    --esgs-ring-itemsize 4 \
    --allow-unresolved
python3 "$writer" \
    "$output_dir/depth.frag.raw.bin" \
    "$output_dir/depth.frag.hw.json" \
    -o "$output_dir/depth.frag.agc.sb" \
    --allow-unresolved

sha256sum \
    "$source_dir/depth.vert" \
    "$source_dir/depth.frag" \
    "$output_dir/depth.vert.spv" \
    "$output_dir/depth.frag.spv" \
    "$output_dir/depth.vert.ngg.bin" \
    "$output_dir/depth.frag.raw.bin" \
    "$output_dir/depth.vert.ngg.hw.json" \
    "$output_dir/depth.frag.hw.json" \
    "$output_dir/depth.vert.ngg.agc.sb" \
    "$output_dir/depth.frag.agc.sb" \
    > "$output_dir/SHA256SUMS"

echo "Gate 4 depth shader intermediates: $output_dir"
