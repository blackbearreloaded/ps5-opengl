#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Assemble and verify the relocatable PS5 OpenGL 3.3 consumer package.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
template=${PS5_NATIVE_APP_TEMPLATE:-"$root/../ps5-native-app-boilerplate"}
sdk=${PS5_PAYLOAD_SDK:-"$template/.deps/native/ps5-payload-sdk"}
prefix=$(realpath -m -- "${1:-$root/build/sdk/ps5-opengl-core33}")

PS5_PAYLOAD_SDK="$sdk" \
    bash "$root/toolchain/install-ps5-opengl-core33.sh" "$prefix"

# Retain diagnostics and verify the installed exports, not the build-tree archives.
work=$(mktemp -d "$root/build/installed-sdk-checks.XXXXXX")
python3 "$root/tools/check-sdk-consumers.py" \
    --sdk "$prefix" --payload-sdk "$sdk" \
    --example-dir "$root/examples/core33-triangle" \
    --registry "$root/third_party/mesa-26.2.0/src/mesa/glapi/glapi/registry/gl.xml" \
    --output "$work/consumers"
printf 'installed-sdk: PASS prefix=%s consumer=core33-triangle commands=344\n' \
    "$prefix"
