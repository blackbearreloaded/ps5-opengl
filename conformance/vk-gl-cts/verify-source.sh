#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Compare tracked source against the pinned base plus only the prescribed patches.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cts=$(realpath -e -- "${1:-$root/third_party/VK-GL-CTS}")
revision=$(git -C "$cts" rev-parse HEAD)
case "$revision" in
    cf7edb26d3be2d8763595ed08fdc41f3c1b1966f|067e8832315e79817ede1c4863804e440f5d1c80) ;;
    *) printf 'Unsupported CTS source revision: %s\n' "$revision" >&2; exit 2 ;;
esac
overlay="$root/conformance/vk-gl-cts"
git -C "$cts" diff --cached --quiet HEAD -- || {
    printf 'CTS audit requires an unstaged upstream checkout.\n' >&2
    exit 2
}
audit_dir=$(mktemp -d)
trap 'rm -f -- "$audit_dir/index"; rmdir -- "$audit_dir"' EXIT
# Retain Git's stat cache for untouched files; never alter the real index.
cp -- "$(git -C "$cts" rev-parse --absolute-git-dir)/index" "$audit_dir/index"
export GIT_INDEX_FILE="$audit_dir/index"
for patch in "$overlay"/patches/*.patch; do
    if [[ $revision == 067e8832315e79817ede1c4863804e440f5d1c80 && $(basename "$patch") == 0005-* ]]; then
        continue
    fi
    git -C "$cts" apply --cached "$patch"
done
git -C "$cts" diff --exit-code --
for path in targets/ps5/ps5.cmake targets/ps5/ps5-toolchain.cmake \
    framework/platform/ps5/tcuPS5Platform.cpp framework/platform/ps5/tcuPS5Platform.hpp \
    external/openglcts/modules/ps5/glcPS5GL33PackageEntry.cpp \
    external/openglcts/modules/ps5/glcPS5GL33PackageEntry.hpp \
    external/openglcts/modules/ps5/glcPS5Main.cpp; do
    cmp -- "$overlay/$path" "$cts/$path"
done
printf 'CTS source audit PASS revision=%s\n' "$revision"
