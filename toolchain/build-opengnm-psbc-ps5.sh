#!/usr/bin/env bash
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir="$project_dir/third_party/opengnm-psbc"
makefile="$project_dir/toolchain/Makefile.opengnm-psbc-ps5"
python3 "$project_dir/tools/fetch-sources.py" --verify-psbc

# This standalone Makefile has no generated-header dependency graph.  Force the
# target objects so the PS5 archive cannot silently retain an older NIR/ACO ABI
# or lowering after compiler sources change.
make -C "$source_dir" -f "$makefile" -B -j"${PSBC_JOBS:-8}" libpsbc
