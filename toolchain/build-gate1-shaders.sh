#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler="$project_dir/third_party/opengnm-psbc/opengnm-psbc"
source_dir="$project_dir/shaders/gate1"
output_dir="${1:-$project_dir/build/gate1}"
manifest="$project_dir/tools/gnm_shader_manifest.py"
agc_writer="$project_dir/tools/agc_shader_package_writer.py"

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
    "$source_dir/passthrough.vert" -o "$output_dir/passthrough.vert.spv"
glslangValidator -V --target-env vulkan1.2 -S frag \
    "$source_dir/constant.frag" -o "$output_dir/constant.frag.spv"

"$compiler" -g -s vertex \
    -f "$output_dir/passthrough.vert.spv" \
    -o "$output_dir/passthrough.vert.gnm.sb"
"$compiler" -g -s fragment \
    -f "$output_dir/constant.frag.spv" \
    -o "$output_dir/constant.frag.gnm.sb"

"$compiler" -g -s fragment --raw \
    -f "$output_dir/constant.frag.spv" \
    -o "$output_dir/constant.frag.raw.bin" \
    --metadata "$output_dir/constant.frag.hw.json"
"$compiler" -g -s vertex --ngg \
    -f "$output_dir/passthrough.vert.spv" \
    -o "$output_dir/passthrough.vert.ngg.bin" \
    --metadata "$output_dir/passthrough.vert.ngg.hw.json"

python3 "$agc_writer" \
    "$output_dir/constant.frag.raw.bin" \
    "$output_dir/constant.frag.hw.json" \
    -o "$output_dir/constant.frag.agc.sb" \
    --allow-unresolved
python3 "$agc_writer" \
    "$output_dir/passthrough.vert.ngg.bin" \
    "$output_dir/passthrough.vert.ngg.hw.json" \
    -o "$output_dir/passthrough.vert.ngg.agc.sb" \
    --esgs-ring-itemsize 4 \
    --allow-unresolved

python3 "$manifest" "$output_dir/passthrough.vert.gnm.sb" \
    --output "$output_dir/passthrough.vert.manifest.json"
python3 "$manifest" "$output_dir/constant.frag.gnm.sb" \
    --output "$output_dir/constant.frag.manifest.json"

sha256sum \
    "$source_dir/passthrough.vert" \
    "$source_dir/constant.frag" \
    "$output_dir/passthrough.vert.spv" \
    "$output_dir/constant.frag.spv" \
    "$output_dir/passthrough.vert.gnm.sb" \
    "$output_dir/constant.frag.gnm.sb" \
    "$output_dir/passthrough.vert.ngg.bin" \
    "$output_dir/constant.frag.raw.bin" \
    "$output_dir/passthrough.vert.ngg.hw.json" \
    "$output_dir/constant.frag.hw.json" \
    "$output_dir/passthrough.vert.ngg.agc.sb" \
    "$output_dir/constant.frag.agc.sb" \
    "$output_dir/passthrough.vert.manifest.json" \
    "$output_dir/constant.frag.manifest.json" \
    > "$output_dir/SHA256SUMS"

echo "Gate 1 clean shader intermediates: $output_dir"
