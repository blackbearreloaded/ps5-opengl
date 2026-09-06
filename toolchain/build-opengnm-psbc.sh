#!/usr/bin/env bash
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
third_party_dir="$project_dir/third_party"
psbc_dir="$third_party_dir/opengnm-psbc"
config_file="../../toolchain/opengnm-psbc-host.mak"

check_revision() {
    repo_dir=$1
    expected=$2
    actual=$(git -C "$repo_dir" rev-parse HEAD)
    if [ "$actual" != "$expected" ]; then
        echo "revision mismatch: $repo_dir" >&2
        echo "expected $expected" >&2
        echo "actual   $actual" >&2
        exit 1
    fi
}

python3 "$project_dir/tools/fetch-sources.py" --verify-psbc
check_revision "$third_party_dir/opengnm" 4b295ca54c82c83acf308d1c646a2dfa9ae57350
check_revision "$third_party_dir/SPIRV-Headers" 0d25db97cb9b8f725e4c95e4553001710e7fc39d
check_revision "$third_party_dir/Vulkan-Headers" b51f6b865c18fc5b33990d12f75e8dfd672cede6

cd "$psbc_dir"

# These generated files are required by the vendored Mesa sources but omitted
# from the upstream standalone Makefile's GENERATED list.
python3 src/util/format/u_format_table.py src/util/format/u_format.yaml --enums > src/util/format/u_format_gen.h
python3 src/util/format/u_format_table.py src/util/format/u_format.yaml --header > src/util/format/u_format_pack.h
python3 src/util/format/u_format_table.py src/util/format/u_format.yaml > src/util/format/u_format_table.c
python3 src/util/format_srgb.py > src/util/format_srgb.c
python3 src/compiler/builtin_types_h.py src/compiler/builtin_types.h
python3 src/compiler/builtin_types_c.py src/compiler/builtin_types.c
python3 src/util/process_shader_stats.py src/util/shader_stats.rnc src/util/shader_stats.xml > src/util/shader_stats.h
python3 src/vulkan/util/vk_struct_type_cast_gen.py \
    --xml src/vulkan/registry/vk.xml \
    --out src/vulkan/util/vk_struct_type_cast.h \
    --beta false
python3 src/amd/packets/parse_cp_pm4_table_data_json.py \
    src/amd/packets/cp_pm4_table_data_gfx11.json \
    src/amd/packets/pm4_it_opcodes_gfx11.h \
    src/amd/packets/cp_pm4_table_data_gfx12.json \
    src/amd/packets/pm4_it_opcodes_gfx12.h \
    gfx11 packets_h > src/amd/common/amd_cp_packets_gfx11.h
python3 src/amd/packets/parse_cp_pm4_table_data_json.py \
    src/amd/packets/cp_pm4_table_data_gfx11.json \
    src/amd/packets/pm4_it_opcodes_gfx11.h \
    src/amd/packets/cp_pm4_table_data_gfx12.json \
    src/amd/packets/pm4_it_opcodes_gfx12.h \
    gfx12 packets_h > src/amd/common/amd_cp_packets_gfx12.h
python3 src/amd/common/gfx10_format_table.py \
    src/util/format/u_format.yaml \
    src/amd/registers/gfx10-rsrc.json \
    src/amd/registers/gfx11-rsrc.json > src/amd/common/gfx10_format_table.c

# Upstream enumerates C/C++ sources with wildcard at Makefile parse time.
# Materialize generated sources before that enumeration on a fresh checkout.
make -j"${PSBC_JOBS:-8}" CONFIG="$config_file" generated
make -B -j"${PSBC_JOBS:-8}" CONFIG="$config_file"

echo "Host compiler built; run make test-compiler for the current PS5 NIR/ACO contracts."
