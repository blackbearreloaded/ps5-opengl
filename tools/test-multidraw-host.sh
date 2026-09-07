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
for variant in plain textured deferred; do
    defines=()
    [[ $variant == plain ]] || defines=(-DPS5_MULTIDRAW_TEXTURE_TEST=1)
    [[ $variant != deferred ]] || defines+=(-DPS5_DEFERRED_DRAW_TEST=1)
    clang-18 "${flags[@]}" "${defines[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/$variant"
    "$out/$variant" > "$out/$variant.log"
    grep -F '[ps5-multidraw] completed=4 cleanup=1 result=0' "$out/$variant.log"
done
grep -F '[ps5-deferred] state=uniform,scissor texture-upload=1 buffer-subdata=1 map-write=1 pending-fence=1 pixels=9216 PASS' "$out/deferred.log"
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
for fault in uniform scissor pending-texture; do
    if [[ $fault == uniform ]]; then
        change='s/green \&\& !(band % 2)/green/'
    elif [[ $fault == scissor ]]; then
        change='s/glScissor(band \* 8, 0, 8, HEIGHT - 4 \* (band % 3))/glScissor(band * 8, 0, 8, HEIGHT)/'
    else
        # Simulate the last staged draw incorrectly sampling the replacement.
        change='/GL_UNSIGNED_BYTE, replacement);/a\   glDrawArrays(GL_TRIANGLES, BANDS * 6, 6);'
    fi
    sed "$change" "$source" |
        clang-18 "${flags[@]}" -DPS5_MULTIDRAW_TEXTURE_TEST=1 -DPS5_DEFERRED_DRAW_TEST=1 \
            -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/fault-$fault"
    if "$out/fault-$fault" > "$out/fault-$fault.log"; then
        echo "Oracle missed $fault fault" >&2; exit 1
    fi
    grep -F '[ps5-multidraw] pixel mismatch' "$out/fault-$fault.log" >/dev/null
done
echo 'Draw host PASS: plain/textured/deferred pixels, upload hazards, sampler/upload/uniform/scissor/pending-texture fault rejection'
