#!/usr/bin/env bash
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
compiler="$project_dir/third_party/opengnm-psbc/opengnm-psbc"
source_dir="$project_dir/shaders/gate3"
output_dir="${1:-$project_dir/build/gate3-texture}"
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
    "$source_dir/texture.vert" -o "$output_dir/texture.vert.spv"
glslangValidator -V --target-env vulkan1.2 -S frag \
    "$source_dir/texture.frag" -o "$output_dir/texture.frag.spv"

"$compiler" -g -s vertex --ngg --raw --address32-hi 2 \
    --vertex-attribute 0:r32g32_float:0:0:16:4 \
    --vertex-attribute 1:r32g32_float:0:8:16:4 \
    -f "$output_dir/texture.vert.spv" \
    -o "$output_dir/texture.vert.ngg.bin" \
    --metadata "$output_dir/texture.vert.ngg.hw.json"
"$compiler" -g -s fragment --raw --address32-hi 2 \
    --descriptor-binding 0:0:combined_image_sampler:1:0:48 \
    -f "$output_dir/texture.frag.spv" \
    -o "$output_dir/texture.frag.raw.bin" \
    --metadata "$output_dir/texture.frag.hw.json"

python3 "$writer" \
    "$output_dir/texture.vert.ngg.bin" \
    "$output_dir/texture.vert.ngg.hw.json" \
    -o "$output_dir/texture.vert.ngg.agc.sb" \
    --esgs-ring-itemsize 4 \
    --allow-unresolved
python3 "$writer" \
    "$output_dir/texture.frag.raw.bin" \
    "$output_dir/texture.frag.hw.json" \
    -o "$output_dir/texture.frag.agc.sb" \
    --allow-unresolved

sha256sum \
    "$source_dir/texture.vert" \
    "$source_dir/texture.frag" \
    "$output_dir/texture.vert.spv" \
    "$output_dir/texture.frag.spv" \
    "$output_dir/texture.vert.ngg.bin" \
    "$output_dir/texture.frag.raw.bin" \
    "$output_dir/texture.vert.ngg.hw.json" \
    "$output_dir/texture.frag.hw.json" \
    "$output_dir/texture.vert.ngg.agc.sb" \
    "$output_dir/texture.frag.agc.sb" \
    > "$output_dir/SHA256SUMS"

echo "Gate 3 texture shader intermediates: $output_dir"
