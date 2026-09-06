#!/usr/bin/env bash
# Same renderer/oracle on host Mesa; only the EGL surface creation differs.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
imgui="$root/third_party/imgui"
test "$(git -C "$imgui" rev-parse HEAD)" = f5befd2d29e66809cd1110a152e375a7f1981f06
test -z "$(git -C "$imgui" status --porcelain)"
mkdir -p "$root/build/imgui-host"
demo_flags=()
case ${1:-} in
    '') ;;
    --tv-demo) demo_flags=(-DPS5_IMGUI_TV_DEMO) ;;
    --profile) demo_flags=(-DPS5_IMGUI_TV_DEMO -DPS5_IMGUI_PROFILE) ;;
    *) echo 'usage: test-imgui-host.sh [--tv-demo|--profile]' >&2; exit 2 ;;
esac
clang++-18 -std=c++11 -O2 -Wall -Wextra -Werror \
    "${demo_flags[@]}" \
    -DPS5_IMGUI_HOST_REFERENCE -DGL_GLEXT_PROTOTYPES=1 \
    -DIMGUI_IMPL_OPENGL_LOADER_CUSTOM -include GL/gl.h \
    -I"$root/build/sdk/ps5-opengl-core33/include" \
    -I"$imgui" -I"$imgui/backends" \
    "$root/examples/core33-imgui/main.cpp" \
    "$imgui/imgui.cpp" "$imgui/imgui_draw.cpp" "$imgui/imgui_tables.cpp" \
    "$imgui/imgui_widgets.cpp" "$imgui/backends/imgui_impl_opengl3.cpp" \
    -l:libEGL.so.1 -l:libGL.so.1 -o "$root/build/imgui-host/check"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 \
    MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 \
    "$root/build/imgui-host/check"
