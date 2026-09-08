#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Verify that the tested Mesa tree is exactly the release archive plus our patch.
set -euo pipefail
export LC_ALL=C
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
archive="$root/third_party/mesa-26.2.0.tar.xz"
patch_file="$root/toolchain/mesa-ps5.patch"
printf '%s  %s\n' efd4bb08cdb7c365a812cd4e6c9202ab55b2f22cdcd13c7d6c4f9647b799a4ef "$archive" | sha256sum --check --status
mapfile -t patched < <(sed -n 's|^+++ b/||p' "$patch_file" | sort -u)
differences=$(tar --compare -Jf "$archive" -C "$root/third_party" 2>&1) || test "$?" = 1
while IFS= read -r line; do
    case "$line" in
        ''|*': Mod time differs'|*': Mode differs'|*': Uid differs'|*': Gid differs') continue ;;
        'mesa-26.2.0/'*': Size differs'|'mesa-26.2.0/'*': Contents differ')
            path=${line%%: *}
            path=${path#mesa-26.2.0/}
            printf '%s\n' "${patched[@]}" | grep -qxF "$path" || {
                printf 'Unrecorded Mesa change: %s\n' "$path" >&2
                exit 1
            }
            ;;
        *) printf 'Unexpected Mesa tree difference: %s\n' "$line" >&2; exit 1 ;;
    esac
done <<< "$differences"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
members=()
for path in "${patched[@]}"; do members+=("mesa-26.2.0/$path"); done
tar -xJf "$archive" -C "$work" "${members[@]}"
patch --batch --forward -p1 -d "$work/mesa-26.2.0" < "$patch_file" >/dev/null
for path in "${patched[@]}"; do
    cmp "$work/mesa-26.2.0/$path" "$root/third_party/mesa-26.2.0/$path"
done
# Intrinsic numbering must match the separately built shader compiler.
cmp "$root/third_party/mesa-26.2.0/src/compiler/nir/nir_intrinsics.py" \
    "$root/third_party/opengnm-psbc/src/compiler/nir/nir_intrinsics.py"
printf 'mesa-patch-sources: PASS (%u patched files; other archived sources unchanged)\n' "${#patched[@]}"
