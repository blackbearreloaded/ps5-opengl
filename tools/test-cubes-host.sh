#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/cubes-host"
mkdir -p "$out"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_CUBES_HOST_REFERENCE
    -I"$root/build/sdk/ps5-opengl-core33/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1
export MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
clang-18 "${flags[@]}" "$root/examples/core33-cubes/main.c" -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$out/check"
"$out/check" > "$out/control.log"
python3 "$root/tools/summarize-cubes.py" --host "$out/control.log"
# These host-only mutations must fail the numerical oracle, not just look wrong.
for fault in depth texture instances; do
    if [[ $fault == depth ]]; then
        change='s/glEnable(GL_DEPTH_TEST)/glDisable(GL_DEPTH_TEST)/'
    elif [[ $fault == texture ]]; then
        change='s/GL_UNSIGNED_BYTE,texels\[i\]/GL_UNSIGNED_BYTE,texels[0]/'
    else
        change='s/glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count)/glDrawArraysInstanced(GL_TRIANGLES, 0, 36, 1)/'
    fi
    sed "$change" "$root/examples/core33-cubes/main.c" |
        clang-18 "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$out/fault-$fault"
    if "$out/fault-$fault" > "$out/fault-$fault.log"; then
        echo "Oracle missed $fault fault" >&2; exit 1
    fi
    grep -F '[ps5-cubes] pixel=' "$out/fault-$fault.log" >/dev/null
done
echo 'Cubes host PASS: depth/texture/instance probes and deliberate fault rejection'
clang-18 "${flags[@]}" "$root/tests/ps5/egl_public_core33_cubes_uv.c" -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$out/check-uv"
"$out/check-uv" > "$out/uv.log"
python3 "$root/tools/summarize-cubes.py" --host --uv "$out/uv.log"
sed 's/texcoord=uv/texcoord=vec2(0)/' "$root/examples/core33-cubes/main.c" |
    clang-18 "${flags[@]}" -DPS5_CUBES_UV_DIAGNOSTIC -x c - -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$out/fault-uv"
if "$out/fault-uv" > "$out/fault-uv.log"; then
    echo 'Oracle missed UV fault' >&2; exit 1
fi
grep -F '[ps5-cubes] pixel=' "$out/fault-uv.log" >/dev/null
echo 'UV diagnostic host PASS: independent coordinate oracle and fault rejection'
