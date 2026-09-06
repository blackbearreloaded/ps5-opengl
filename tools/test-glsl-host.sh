#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/glsl-host"
mkdir -p "$out"
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_GLSL_HOST_REFERENCE
    -I"$root/third_party/mesa-26.2.0/include")
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1
export MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
# Installed direct llvmpipe/softpipe fail mixed PrimitiveID/user inputs; the
# unchanged oracle and its mutations pass Zink over software Vulkan instead.
export GALLIUM_DRIVER=${GALLIUM_DRIVER:-zink}
source="$root/tests/ps5/egl_public_core33_glsl_suite.c"
clang-18 "${flags[@]}" "$source" -l:libEGL.so.1 -l:libGL.so.1 -o "$out/check"
"$out/check" > "$out/control.log"
grep -F 'math=1 texture=1 primitive=1 cleanup=0/3000 result=0' "$out/control.log"
# Prove the oracle rejects a missing built-in, bad user varying, or missing draw.
for fault in id tag uv draw alpha; do
    case "$fault" in
        id) change='s/float(gl_PrimitiveID)/float(0)/'; channels=4096/0/0/0;;
        tag) change='s/glUniform1i(tag, 7)/glUniform1i(tag, 6)/'; channels=0/4096/0/0;;
        uv) change='s/uv=a_position/uv=vec2(0)/'; channels=0/0/4096/0;;
        draw) change='s/cases\[i\].count);/0);/'; channels=0/4096/4096/0;;
        alpha) change='s/float(interp),1)/float(interp),0)/'; channels=0/0/0/4096;;
    esac
    sed "$change" "$source" |
        clang-18 "${flags[@]}" -x c - -l:libEGL.so.1 -l:libGL.so.1 -o "$out/fault-$fault"
    if "$out/fault-$fault" > "$out/fault-$fault.log"; then
        echo "Oracle missed $fault fault" >&2; exit 1
    fi
    grep -E 'primitive-id-.* matching=0 ' "$out/fault-$fault.log" >/dev/null
    grep -F "different-rgba=$channels " "$out/fault-$fault.log" >/dev/null
done
echo 'GLSL host PASS: math/texture/PrimitiveID/program restoration; five faults rejected and localized'
