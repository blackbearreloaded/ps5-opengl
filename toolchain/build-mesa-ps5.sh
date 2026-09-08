#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir="$project_dir/third_party/mesa-26.2.0"
archive="$project_dir/third_party/mesa-26.2.0.tar.xz"
patch_file="$project_dir/toolchain/mesa-ps5.patch"
signature="$archive.sig"
keyring="$project_dir/third_party/mesa-release-keyring.gpg"
build_dir="$project_dir/build/mesa-ps5-probe"
cross_file=${PS5_MESA_CROSS_FILE:-/opt/ps5-payload-sdk/toolchain/prospero.ini}
expected_sha=efd4bb08cdb7c365a812cd4e6c9202ab55b2f22cdcd13c7d6c4f9647b799a4ef

if [ ! -f "$source_dir/VERSION" ] || [ "$(cat "$source_dir/VERSION")" != 26.2.0 ]; then
    echo "expected extracted Mesa 26.2.0 at $source_dir" >&2
    exit 1
fi

if [ -f "$archive" ]; then
    printf '%s  %s\n' "$expected_sha" "$archive" | sha256sum --check --status || {
        echo "Mesa archive SHA-256 mismatch" >&2
        exit 1
    }
fi

if [ -f "$signature" ] && [ -f "$keyring" ]; then
    gpgv --keyring "$keyring" "$signature" "$archive"
fi

if patch --dry-run --silent --forward -p1 -d "$source_dir" < "$patch_file"; then
    patch --batch --forward -p1 -d "$source_dir" < "$patch_file"
elif ! patch --dry-run --silent --reverse -p1 -d "$source_dir" < "$patch_file"; then
    echo "Mesa source is neither pristine nor patched as expected" >&2
    exit 1
fi

meson_args="
--cross-file=$cross_file
-Dplatforms=
-Dc_args=-Wno-thread-safety-analysis
-Dcpp_args=-Wno-thread-safety-analysis
-Dgallium-drivers=softpipe
-Dvulkan-drivers=
-Dopengl=true
-Degl=disabled
-Dgbm=disabled
-Dgles1=disabled
-Dgles2=disabled
-Dglx=disabled
-Dllvm=disabled
-Dzlib=disabled
-Dzstd=disabled
-Dshader-cache=disabled
-Dxmlconfig=disabled
-Dexpat=disabled
-Dlibunwind=disabled
-Dvalgrind=disabled
-Dvideo-codecs=
-Dbuild-tests=false
-Dtools=
-Dgallium-va=disabled
-Dgallium-rusticl=false
"

if [ -f "$build_dir/build.ninja" ]; then
    # Word splitting here is intentional: every line is one Meson argument.
    # shellcheck disable=SC2086
    meson setup --reconfigure "$build_dir" "$source_dir" $meson_args
else
    # shellcheck disable=SC2086
    meson setup "$build_dir" "$source_dir" $meson_args
fi

ninja -C "$build_dir" \
    src/util/blake3/libblake3.a \
    src/c11/impl/libmesa_util_c11.a \
    src/util/libmesa_util_simd.a \
    src/util/libmesa_util.a \
    src/compiler/libcompiler.a \
    src/compiler/nir/libnir.a \
    src/compiler/spirv/libvtn.a \
    src/compiler/glsl/libglsl_util.a \
    src/compiler/glsl/glcpp/libglcpp.a \
    src/compiler/glsl/libglsl.a \
    src/mesa/libmesa.a \
    src/mesa/glapi/shared-glapi/libglapi.a \
    src/mesa/glapi/glapi/libglapi_bridge.a \
    src/gallium/auxiliary/libgallium.a \
    src/gallium/drivers/softpipe/libsoftpipe.a
