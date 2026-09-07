#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
python3 "$root/examples/core33-sokol-cube/prepare.py"
mkdir -p "$root/build/sokol-cube-host"
flags=()
case ${1:-} in
    '') ;;
    --mapped) flags=(-DPS5_SOKOL_MAPPED_READBACK) ;;
    --heap) flags=(-DPS5_SOKOL_HEAP_READBACK) ;;
    *) echo 'usage: test-sokol-cube-host.sh [--mapped|--heap]' >&2; exit 2 ;;
esac
clang-18 -std=gnu11 -O2 -Wall -Werror=implicit-function-declaration \
    "${flags[@]}" \
    -DPS5_SOKOL_HOST_REFERENCE -I"$root/build/sdk/ps5-opengl-core33/include" \
    -I"$root/examples/core33-sokol-cube" -I"$root/build/sokol-cube-source" \
    -I"$root/third_party/sokol" -I"$root/third_party/sokol-samples/libs/vecmath" \
    "$root/examples/core33-sokol-cube/main.c" -l:libEGL.so.1 -l:libGL.so.1 -lm \
    -o "$root/build/sokol-cube-host/check"
extra=$(comm -23 \
    <(nm -u "$root/build/sokol-cube-host/check" | sed 's/@.*//' | sed -n 's/.* U \(gl[A-Za-z0-9_]*\)$/\1/p' | sort -u) \
    <(sed -n 's/^ *"\(gl[A-Za-z0-9_]*\)".*/\1/p' "$root/tests/ps5/egl_public_core33_entrypoints.inc" | sort -u))
test -z "$extra" || { printf 'Non-Core-3.3 imports: %s\n' "$extra" >&2; exit 1; }
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330
"$root/build/sokol-cube-host/check" | tee "$root/build/sokol-cube-host/check.log"
test "$(grep -c 'mismatches=0 PASS' "$root/build/sokol-cube-host/check.log")" = 5
if [[ ${1:-} == --heap ]]; then
    grep -Fq 'readback=heap bytes=8294400' "$root/build/sokol-cube-host/check.log"
fi
if PS5_CUBE_CORRUPT=1 "$root/build/sokol-cube-host/check" > "$root/build/sokol-cube-host/negative.log" 2>&1; then
    echo 'Erased-cube negative control unexpectedly passed' >&2
    exit 1
fi
grep -q 'FAIL rotation readback' "$root/build/sokol-cube-host/negative.log"
if [[ ${1:-} == --mapped ]]; then
    if PS5_CUBE_MAP_FAIL=1 "$root/build/sokol-cube-host/check" > "$root/build/sokol-cube-host/map-fail.log" 2>&1; then
        echo 'Failed allocation control unexpectedly passed' >&2
        exit 1
    fi
    grep -q 'FAIL readback allocation' "$root/build/sokol-cube-host/map-fail.log"
    ! grep -q 'FAIL EGL cleanup' "$root/build/sokol-cube-host/map-fail.log"
    grep -q 'finished frames=0 probes=0 face_mask=0x0 status=1' "$root/build/sokol-cube-host/map-fail.log"
fi
echo 'Sokol cube: five host rotation checks pass; erased-cube control fails as required'
