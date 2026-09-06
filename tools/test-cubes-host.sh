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
for fault in depth texture; do
    if [[ $fault == depth ]]; then
        change='s/glEnable(GL_DEPTH_TEST)/glDisable(GL_DEPTH_TEST)/'
    else
        change='s/GL_UNSIGNED_BYTE,texels\[i\]/GL_UNSIGNED_BYTE,texels[0]/'
    fi
    sed "$change" "$root/examples/core33-cubes/main.c" |
        clang-18 "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$out/fault-$fault"
    if "$out/fault-$fault" > "$out/fault-$fault.log"; then
        echo "Oracle missed $fault fault" >&2; exit 1
    fi
    grep -F '[ps5-cubes] pixel=' "$out/fault-$fault.log" >/dev/null
done
echo 'Cubes host PASS: depth/texture probes and deliberate fault rejection'
