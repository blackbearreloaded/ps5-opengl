#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later


set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cts=${1:-"$root/third_party/VK-GL-CTS"}
overlay="$root/conformance/vk-gl-cts"
expected=cf7edb26d3be2d8763595ed08fdc41f3c1b1966f

test -d "$cts/.git"
actual=$(git -C "$cts" rev-parse HEAD)
if [[ $actual != "$expected" ]]; then
    printf 'VK-GL-CTS revision mismatch: expected %s, found %s\n' \
        "$expected" "$actual" >&2
    exit 2
fi

install -d \
    "$cts/targets/ps5" \
    "$cts/framework/platform/ps5" \
    "$cts/external/openglcts/modules/ps5"

sync_file() {
    local source=$1
    local destination=$2
    if [[ ! -f $destination ]] || ! cmp -s "$source" "$destination"; then
        install -m 0644 "$source" "$destination"
    fi
}

sync_file "$overlay/targets/ps5/ps5.cmake" \
    "$cts/targets/ps5/ps5.cmake"
sync_file "$overlay/targets/ps5/ps5-toolchain.cmake" \
    "$cts/targets/ps5/ps5-toolchain.cmake"
sync_file "$overlay/framework/platform/ps5/tcuPS5Platform.cpp" \
    "$cts/framework/platform/ps5/tcuPS5Platform.cpp"
sync_file "$overlay/framework/platform/ps5/tcuPS5Platform.hpp" \
    "$cts/framework/platform/ps5/tcuPS5Platform.hpp"
sync_file "$overlay/external/openglcts/modules/ps5/glcPS5GL33PackageEntry.cpp" \
    "$cts/external/openglcts/modules/ps5/glcPS5GL33PackageEntry.cpp"
sync_file "$overlay/external/openglcts/modules/ps5/glcPS5GL33PackageEntry.hpp" \
    "$cts/external/openglcts/modules/ps5/glcPS5GL33PackageEntry.hpp"
sync_file "$overlay/external/openglcts/modules/ps5/glcPS5Main.cpp" \
    "$cts/external/openglcts/modules/ps5/glcPS5Main.cpp"

for patch in "$overlay"/patches/*.patch; do
    case "$(basename "$patch")" in
        0001-*) marker='add_library(ps5-gl33-package STATIC' ;;
        0002-*) marker='add_library(ps5-gl33-runner STATIC' ;;
        0003-*) marker='NOT DEQP_TARGET_NAME STREQUAL "PS5 OpenGL"' ;;
        0004-*) marker='add_library(ps5-gl33-support STATIC' ;;
        *) marker= ;;
    esac
    if [[ -n $marker ]] && git -C "$cts" grep -Fq "$marker" -- \
        external/openglcts/modules/CMakeLists.txt \
        framework/platform/CMakeLists.txt; then
        continue
    fi
    if git -C "$cts" apply --reverse --check "$patch" >/dev/null 2>&1; then
        continue
    fi
    if ! git -C "$cts" apply --check "$patch"; then
        printf 'VK-GL-CTS patch does not apply cleanly: %s\n' \
            "$(basename "$patch")" >&2
        exit 3
    fi
    git -C "$cts" apply "$patch"
done

printf 'Prepared VK-GL-CTS %s with the PS5 overlay.\n' "$actual"
