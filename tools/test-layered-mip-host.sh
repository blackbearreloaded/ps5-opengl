#!/usr/bin/env bash
# The native mip/layer oracle on software Mesa, plus the original filter mistake.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/layered-mip-host"
mkdir -p "$out"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_LAYERED_MIP_HOST_REFERENCE
    -I"$root/build/sdk/ps5-opengl-core33/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1
export MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
source="$root/tests/ps5/egl_public_core33_layered_mip_fbo.c"
clang-18 "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/check"
"$out/check" > "$out/control.log"
grep -F 'match=1024/1024/1024 error=0x0 result=0' "$out/control.log"
grep -F '[ps5-egl-layered-mip-fbo] cleanup=1 result=0' "$out/control.log"
sed 's/GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST/GL_TEXTURE_MIN_FILTER, GL_NEAREST/g' "$source" |
    clang-18 "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/wrong-filter"
if "$out/wrong-filter" > "$out/wrong-filter.log"; then
    echo 'Mip oracle incorrectly accepted non-mipmapped filters' >&2; exit 1
fi
grep -F 'match=1024/1024/0 error=0x0 result=1' "$out/wrong-filter.log"
grep -F '[ps5-egl-layered-mip-fbo] cleanup=1 result=1' "$out/wrong-filter.log"
echo 'Layered mip host PASS: cube/volume mip pixels, original filter mistake rejected'
