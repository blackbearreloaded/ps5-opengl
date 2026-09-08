#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler="$project_dir/third_party/opengnm-psbc/opengnm-psbc"
source_dir="$project_dir/shaders/gate3"
output_dir="${1:-$project_dir/build/gate3-varying}"
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
    "$source_dir/varying.vert" -o "$output_dir/varying.vert.spv"
glslangValidator -V --target-env vulkan1.2 -S frag \
    "$source_dir/varying.frag" -o "$output_dir/varying.frag.spv"

"$compiler" -g -s vertex --ngg --raw \
    -f "$output_dir/varying.vert.spv" \
    -o "$output_dir/varying.vert.ngg.bin" \
    --metadata "$output_dir/varying.vert.ngg.hw.json"
"$compiler" -g -s fragment --raw \
    -f "$output_dir/varying.frag.spv" \
    -o "$output_dir/varying.frag.raw.bin" \
    --metadata "$output_dir/varying.frag.hw.json"

python3 "$writer" \
    "$output_dir/varying.vert.ngg.bin" \
    "$output_dir/varying.vert.ngg.hw.json" \
    -o "$output_dir/varying.vert.ngg.agc.sb" \
    --esgs-ring-itemsize 4 \
    --allow-unresolved
python3 "$writer" \
    "$output_dir/varying.frag.raw.bin" \
    "$output_dir/varying.frag.hw.json" \
    -o "$output_dir/varying.frag.agc.sb" \
    --allow-unresolved

sha256sum \
    "$source_dir/varying.vert" \
    "$source_dir/varying.frag" \
    "$output_dir/varying.vert.spv" \
    "$output_dir/varying.frag.spv" \
    "$output_dir/varying.vert.ngg.bin" \
    "$output_dir/varying.frag.raw.bin" \
    "$output_dir/varying.vert.ngg.hw.json" \
    "$output_dir/varying.frag.hw.json" \
    "$output_dir/varying.vert.ngg.agc.sb" \
    "$output_dir/varying.frag.agc.sb" \
    > "$output_dir/SHA256SUMS"

echo "Gate 3 varying shader intermediates: $output_dir"
