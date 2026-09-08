#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/cubes-host"
mkdir -p "$out"
for completion in 0 1; do
    clang-18 -std=c11 -O2 -Wall -Wextra -Werror -DPS5_CUBES_SWAP_COMPLETED="$completion" \
        "$root/tools/test_cubes_profile_accounting.c" -o "$out/accounting-$completion"
    "$out/accounting-$completion" > "$out/accounting-$completion.log"
done
echo 'Native accounting mock PASS: both completion modes, CPU/GPU clear, every-frame faults'
prefix=${PS5_OPENGL_PREFIX:-"$root/build/sdk/ps5-opengl-core33"}
flags=(-std=c11 -O2 -Wall -Wextra -Werror -DPS5_CUBES_HOST_REFERENCE
    -I"$prefix/include" -I"$root/examples/core33-cubes")
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

# Software EGL validates the workload and parser, never console performance.
profile_flags=(-DPS5_CUBES_PROFILE=1 -DPS5_CUBES_SECONDS=1)
for config in 0:128 1:128 1:512; do
    swap=${config%:*}; objects=${config#*:}
    name="profile-$swap-$objects"
    clang-18 "${flags[@]}" "${profile_flags[@]}" -DPS5_CUBES_SWAP_COMPLETED="$swap" \
        -DPS5_CUBES_OBJECTS="$objects" "$root/examples/core33-cubes/main.c" \
        -l:libEGL.so.1 -l:libGL.so.1 -lm -o "$out/$name"
    "$out/$name" > "$out/$name.log"
    python3 "$root/tools/summarize-cubes-profile.py" --host --seconds 1 \
        --objects "$objects" --swap-completed "$swap" "$out/$name.log"
done
for fault in depth texture instances; do
    if [[ $fault == depth ]]; then
        change='s/glEnable(GL_DEPTH_TEST)/glDisable(GL_DEPTH_TEST)/'
    elif [[ $fault == texture ]]; then
        change='s/GL_UNSIGNED_BYTE,texels\[i\]/GL_UNSIGNED_BYTE,texels[0]/'
    else
        change='s/glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count)/glDrawArraysInstanced(GL_TRIANGLES, 0, 36, 1)/'
    fi
    sed "$change" "$root/examples/core33-cubes/main.c" |
        clang-18 "${flags[@]}" "${profile_flags[@]}" -DPS5_CUBES_OBJECTS=128 \
            -DPS5_CUBES_SWAP_COMPLETED=1 -x c - -l:libEGL.so.1 -l:libGL.so.1 -lm \
            -o "$out/profile-fault-$fault"
    if "$out/profile-fault-$fault" > "$out/profile-fault-$fault.log"; then
        echo "Profile oracle missed $fault fault" >&2; exit 1
    fi
    grep -F '[ps5-cubes] pixel=' "$out/profile-fault-$fault.log" >/dev/null
done
echo 'Cubes profile host PASS: both completion modes, 512 objects, depth/texture/instance fault rejection (host timings only)'
