#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler="$project_dir/third_party/opengnm-psbc/opengnm-psbc"
source_dir="$project_dir/shaders/gate3"
output_dir="${1:-$project_dir/build/gate3-vertex-buffer}"
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
    "$source_dir/vertex-buffer.vert" -o "$output_dir/vertex-buffer.vert.spv"
glslangValidator -V --target-env vulkan1.2 -S frag \
    "$source_dir/vertex-buffer.frag" -o "$output_dir/vertex-buffer.frag.spv"

"$compiler" -g -s vertex --ngg --raw --address32-hi 2 \
    --vertex-attribute 0:r32g32_float:0:0:24:4 \
    --vertex-attribute 1:r32g32b32a32_float:0:8:24:4 \
    -f "$output_dir/vertex-buffer.vert.spv" \
    -o "$output_dir/vertex-buffer.vert.ngg.bin" \
    --metadata "$output_dir/vertex-buffer.vert.ngg.hw.json"
"$compiler" -g -s fragment --raw --address32-hi 2 \
    -f "$output_dir/vertex-buffer.frag.spv" \
    -o "$output_dir/vertex-buffer.frag.raw.bin" \
    --metadata "$output_dir/vertex-buffer.frag.hw.json"

python3 "$writer" \
    "$output_dir/vertex-buffer.vert.ngg.bin" \
    "$output_dir/vertex-buffer.vert.ngg.hw.json" \
    -o "$output_dir/vertex-buffer.vert.ngg.agc.sb" \
    --esgs-ring-itemsize 4 \
    --allow-unresolved
python3 "$writer" \
    "$output_dir/vertex-buffer.frag.raw.bin" \
    "$output_dir/vertex-buffer.frag.hw.json" \
    -o "$output_dir/vertex-buffer.frag.agc.sb" \
    --allow-unresolved

sha256sum \
    "$source_dir/vertex-buffer.vert" \
    "$source_dir/vertex-buffer.frag" \
    "$output_dir/vertex-buffer.vert.spv" \
    "$output_dir/vertex-buffer.frag.spv" \
    "$output_dir/vertex-buffer.vert.ngg.bin" \
    "$output_dir/vertex-buffer.frag.raw.bin" \
    "$output_dir/vertex-buffer.vert.ngg.hw.json" \
    "$output_dir/vertex-buffer.frag.hw.json" \
    "$output_dir/vertex-buffer.vert.ngg.agc.sb" \
    "$output_dir/vertex-buffer.frag.agc.sb" \
    > "$output_dir/SHA256SUMS"

echo "Gate 3 vertex-buffer shader intermediates: $output_dir"
