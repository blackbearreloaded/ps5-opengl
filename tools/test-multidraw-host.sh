#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/multidraw-host"
mkdir -p "$out"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DGL_GLEXT_PROTOTYPES=1
    -DPS5_MULTIDRAW_HOST_REFERENCE -I"$root/build/sdk/ps5-opengl-core33/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1
export MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
source="$root/tests/ps5/egl_public_core33_multidraw_batch.c"
for variant in plain textured; do
    defines=()
    [[ $variant != textured ]] || defines=(-DPS5_MULTIDRAW_TEXTURE_TEST=1)
    clang-18 "${flags[@]}" "${defines[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/$variant"
    "$out/$variant" > "$out/$variant.log"
    grep -F '[ps5-multidraw] completed=4 cleanup=1 result=0' "$out/$variant.log"
done
grep -F '[ps5-multidraw-texture] sampled=2 uploads=1 pixels=32256 PASS' "$out/textured.log"
for fault in sampler upload; do
    if [[ $fault == sampler ]]; then
        change='s/glUniform1i(image1, 7)/glUniform1i(image1, 0)/'
    else
        change='/^[[:space:]]*glTexSubImage2D(/d'
    fi
    sed "$change" "$source" |
        clang-18 "${flags[@]}" -DPS5_MULTIDRAW_TEXTURE_TEST=1 -x c - \
            -l:libEGL.so.1 -l:libGL.so.1 -o "$out/fault-$fault"
    if "$out/fault-$fault" > "$out/fault-$fault.log"; then
        echo "Oracle missed $fault fault" >&2; exit 1
    fi
    grep -F '[ps5-multidraw] pixel mismatch' "$out/fault-$fault.log" >/dev/null
done
echo 'Multidraw host PASS: plain/textured pixels, sparse units, post-batch upload, sampler/upload fault rejection'
