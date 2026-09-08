#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Compile every public OpenGL gate and verify that the native runtime can resolve it.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
template=${PS5_NATIVE_APP_TEMPLATE:-"$root/../ps5-native-app-boilerplate"}
sdk="$template/.deps/native/ps5-payload-sdk"
audit="$root/build/native-app/catalog-audit"

mapfile -t gates < <("$root/tools/build-native-test-app.sh" --list)
(( ${#gates[@]} > 0 )) || {
    printf 'native test catalog is empty\n' >&2
    exit 2
}

make -C "$root/tests/ps5" --no-print-directory -j8 \
    PS5_PAYLOAD_SDK="$sdk" "${gates[@]}"
make -C "$root/tests/ps5" --no-print-directory -s -f native-app.mk -j8 \
    PS5_PAYLOAD_SDK="$sdk" runtime
mapfile -t providers < <(
    make -C "$root/tests/ps5" --no-print-directory -s -f native-app.mk \
        PS5_PAYLOAD_SDK="$sdk" print-static-libs
)

compiler=${PS5_CLANG:-clang-18}
compiler_runtime=$(
    "$compiler" --print-resource-dir
)/lib/linux/libclang_rt.builtins-x86_64.a
providers+=(
    "$sdk/target/lib/libunwind.a"
    "$sdk/target/lib/libc++abi.a"
    "$sdk/target/lib/libc++.a"
    "$compiler_runtime"
)

mkdir -p "$audit"
PS5_PAYLOAD_SDK="$sdk" sh "$template/tooling/prospero-clang18" \
    -std=c11 -O2 -c "$root/native-app/runtime_shims.c" \
    -o "$audit/runtime_shims.o"
providers+=("$audit/runtime_shims.o")

for provider in "${providers[@]}"; do
    test -s "$provider"
done

{
    nm -g --defined-only "${providers[@]}"
    nm -D -g --defined-only "$sdk"/target/lib/*.so
} 2>/dev/null | awk 'NF { print $NF }' | sort -u > "$audit/providers.txt"

for gate in "${gates[@]}"; do
    nm -u "$root/tests/ps5/$gate"
done | awk 'NF { print $NF }' | sort -u > "$audit/required.txt"

comm -23 "$audit/required.txt" "$audit/providers.txt" > "$audit/missing.txt"
if [[ -s "$audit/missing.txt" ]]; then
    printf 'native catalog has unresolved symbols:\n' >&2
    sed 's/^/  /' "$audit/missing.txt" >&2
    exit 1
fi

printf 'Native catalog audit: %d public gates, 0 unresolved symbols\n' \
    "${#gates[@]}"
