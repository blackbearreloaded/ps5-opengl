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
(cd "$prefix" && sha256sum --check --strict manifest.sha256 >/dev/null)

for archive in "$prefix"/lib/*.a; do
    [[ $(basename -- "$archive") == libPS5OpenGLCore33.a ]] && continue
    [[ $(head -c 7 "$archive") == '!<arch>' ]] || {
        printf 'non-portable archive: %s\n' "$archive" >&2
        exit 1
    }
done
for import in libSceAgc.so libSceAgcDriver.so; do
    test -s "$prefix/lib/$import"
done

make -C "$root/examples/core33-triangle" --no-print-directory \
    -f Makefile.installed clean \
    PS5_PAYLOAD_SDK="$sdk" PS5_OPENGL_PREFIX="$prefix"
make -C "$root/examples/core33-triangle" --no-print-directory \
    -f Makefile.installed -j8 \
    PS5_PAYLOAD_SDK="$sdk" PS5_OPENGL_PREFIX="$prefix"

pkg_flags=$(PKG_CONFIG_PATH="$prefix/lib/pkgconfig" \
    pkg-config --cflags --libs ps5-opengl-core33)
[[ $pkg_flags == *'-lPS5OpenGLCore33'* &&
   $pkg_flags == *'-lSceAgcDriver'* &&
   $pkg_flags == *'-lSceVideoOut'* ]] || {
    printf 'incomplete pkg-config contract: %s\n' "$pkg_flags" >&2
    exit 1
}

cmake_work=$(mktemp -d)
trap 'rm -rf -- "$cmake_work"' EXIT
cp "$root/examples/core33-triangle/main.c" "$cmake_work/main.c"
PKG_CONFIG_PATH="$prefix/lib/pkgconfig" python3 - "$sdk" "$cmake_work" "$pkg_flags" <<'PY'
import shlex
import subprocess
import sys
from pathlib import Path

sdk, work = map(Path, sys.argv[1:3])
cflags = shlex.split(subprocess.check_output(
    ["pkg-config", "--cflags", "ps5-opengl-core33"], text=True))
subprocess.run([str(sdk / "bin/prospero-clang"), *cflags, "-c",
                str(work / "main.c"), "-o", str(work / "pkgconfig.o")], check=True)
subprocess.run([str(sdk / "bin/prospero-clang++"), str(work / "pkgconfig.o"),
                *shlex.split(sys.argv[3]), "-Wl,--gc-sections", "-Wl,--build-id=sha1",
                "-o", str(work / "pkgconfig.elf")], check=True)
print("pkg-config-consumer: PASS (compiled C; linked C++; installed flags only)")
PY
cat > "$cmake_work/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(ps5_opengl_package LANGUAGES C CXX)
find_package(PS5OpenGLCore33 CONFIG REQUIRED)
get_target_property(includes PS5OpenGLCore33::OpenGL INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(libraries PS5OpenGLCore33::OpenGL INTERFACE_LINK_LIBRARIES)
if(NOT includes MATCHES "/include" OR NOT libraries MATCHES "libSceAgcDriver")
  message(FATAL_ERROR "incomplete PS5OpenGLCore33 imported target")
endif()
add_executable(core33-triangle main.c)
set_property(TARGET core33-triangle PROPERTY LINKER_LANGUAGE CXX)
target_link_libraries(core33-triangle PRIVATE PS5OpenGLCore33::OpenGL)
target_link_options(core33-triangle PRIVATE
  "LINKER:--gc-sections" "LINKER:--build-id=sha1")
EOF
cmake -S "$cmake_work" -B "$cmake_work/build" \
    -DCMAKE_C_COMPILER="$sdk/bin/prospero-clang" \
    -DCMAKE_CXX_COMPILER="$sdk/bin/prospero-clang++" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DPS5OpenGLCore33_DIR="$prefix/lib/cmake/PS5OpenGLCore33" >/dev/null
cmake --build "$cmake_work/build" -j8 >/dev/null

python3 "$root/tests/ps5/verify_gl33_link_surface.py"
printf 'installed-sdk: PASS prefix=%s consumer=core33-triangle commands=344\n' \
    "$prefix"
