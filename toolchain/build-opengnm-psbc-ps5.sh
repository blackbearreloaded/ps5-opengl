#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir="$project_dir/third_party/opengnm-psbc"
makefile="$project_dir/toolchain/Makefile.opengnm-psbc-ps5"
mode=${1:-}
case "$mode" in ''|--if-needed) ;; *) echo 'usage: build-opengnm-psbc-ps5.sh [--if-needed]' >&2; exit 2;; esac
python3 "$project_dir/tools/fetch-sources.py" --verify-psbc
sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
stamp="$source_dir/libpsbc.ps5.identity"
fingerprint() {
    git -C "$source_dir" write-tree || return
    "$sdk/bin/prospero-clang" --version || return
    sha256sum "$project_dir/dependencies.json" "$makefile" \
        "$project_dir/toolchain/opengnm-psbc-ps5.mak" "$0" \
        "$project_dir/src/platform/ps5_mesa_shims.c" \
        "$sdk/bin/prospero-clang" "$sdk/bin/prospero-clang++" \
        "$source_dir/libpsbc.ps5.a"
}
if [ "$mode" = --if-needed ] && [ -s "$stamp" ] &&
   fingerprint | cmp -s "$stamp" -; then
    echo 'PS5 compiler: matching source/toolchain/archive identity; reused.'
    exit 0
fi

# This standalone Makefile has no generated-header dependency graph.  Force the
# target objects so the PS5 archive cannot silently retain an older NIR/ACO ABI
# or lowering after compiler sources change.
make -C "$source_dir" -f "$makefile" -B -j"${PSBC_JOBS:-8}" libpsbc
fingerprint > "$stamp.tmp"
mv "$stamp.tmp" "$stamp"
