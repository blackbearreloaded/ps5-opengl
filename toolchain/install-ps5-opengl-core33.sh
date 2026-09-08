#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Build and install a relocatable PS5 OpenGL 3.3 developer package.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
prefix=$(realpath -m -- "${1:?usage: $0 <install-prefix>}")
template=${PS5_NATIVE_APP_TEMPLATE:-"$root/../ps5-native-app-boilerplate"}
sdk=${PS5_PAYLOAD_SDK:-"$template/.deps/native/ps5-payload-sdk"}

[[ $prefix != / && $prefix != "$root" && -x $sdk/bin/prospero-clang ]] || {
    printf 'unsafe prefix or missing PS5 SDK: %s / %s\n' "$prefix" "$sdk" >&2
    exit 2
}

make -C "$root/tests/ps5" --no-print-directory -f native-app.mk -j8 \
    PS5_PAYLOAD_SDK="$sdk" runtime

runtime="$root/build/core33-native-runtime"
mesa="$root/build/mesa-ps5-probe"
psbc="$root/third_party/opengnm-psbc"
libraries=(
    "$runtime/libps5_opengl_core33.a"
    "$mesa/src/mesa/glapi/glapi/libglapi_bridge.a"
    "$psbc/libpsbc.ps5.a"
    "$mesa/src/mesa/libmesa.a"
    "$mesa/src/mesa/libmesa_sse41.a"
    "$mesa/src/gallium/auxiliary/libgallium.a"
    "$mesa/src/mesa/glapi/shared-glapi/libglapi.a"
    "$mesa/src/compiler/glsl/libglsl.a"
    "$mesa/src/compiler/glsl/glcpp/libglcpp.a"
    "$mesa/src/compiler/glsl/libglsl_util.a"
    "$mesa/src/compiler/spirv/libvtn.a"
    "$mesa/src/compiler/nir/libnir.a"
    "$mesa/src/compiler/libcompiler.a"
    "$mesa/src/util/libmesa_util.a"
    "$mesa/src/util/libmesa_util_simd.a"
    "$mesa/src/util/blake3/libblake3.a"
    "$mesa/src/c11/impl/libmesa_util_c11.a"
)
for library in "${libraries[@]}"; do
    test -s "$library"
done
imports=(
    "$runtime/libSceAgc.so"
    "$runtime/libSceAgcDriver.so"
)
for import in "${imports[@]}"; do
    test -s "$import"
done
archive_work=$(mktemp -d)
trap 'rm -rf -- "$archive_work"' EXIT

install -d "$prefix/include" "$prefix/lib/pkgconfig" \
    "$prefix/lib/cmake/PS5OpenGLCore33" \
    "$prefix/share/ps5-opengl-core33"
for headers in EGL GL KHR; do
    cp -a "$root/third_party/mesa-26.2.0/include/$headers" \
        "$prefix/include/"
done
for library in "${libraries[@]}"; do
    name=$(basename -- "$library")
    if [[ $(head -c 7 "$library") == '!<thin>' ]]; then
        materialized="$archive_work/$name"
        printf 'CREATE %s\nADDLIB %s\nSAVE\nEND\n' \
            "$materialized" "$library" | ar -M
        install -m 0644 "$materialized" "$prefix/lib/$name"
    else
        install -m 0644 "$library" "$prefix/lib/$name"
    fi
done
for import in "${imports[@]}"; do
    install -m 0644 "$import" "$prefix/lib/$(basename -- "$import")"
done
install -m 0644 "$root/toolchain/libPS5OpenGLCore33.ld" \
    "$prefix/lib/libPS5OpenGLCore33.a"
install -m 0644 "$root/toolchain/ps5-opengl-core33.pc" \
    "$prefix/lib/pkgconfig/ps5-opengl-core33.pc"
install -m 0644 "$root/toolchain/PS5OpenGLCore33Config.cmake" \
    "$prefix/lib/cmake/PS5OpenGLCore33/PS5OpenGLCore33Config.cmake"
install -m 0644 "$root/toolchain/ps5-opengl-core33-installed.mk" \
    "$prefix/share/ps5-opengl-core33/ps5-opengl-core33.mk"

(cd "$prefix" && find include lib share -type f -print0 | sort -z | \
    xargs -0 sha256sum) > "$prefix/manifest.sha256"
printf 'PS5 OpenGL 3.3 package: %s\n' "$prefix"
printf 'Archives: %u, Imports: %u, Core commands: 344\n' \
    "${#libraries[@]}" "${#imports[@]}"
