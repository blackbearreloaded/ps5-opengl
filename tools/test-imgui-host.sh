#!/usr/bin/env bash
# Same renderer/oracle on host Mesa; only the EGL surface creation differs.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
imgui="$root/third_party/imgui"
test "$(git -C "$imgui" rev-parse HEAD)" = f5befd2d29e66809cd1110a152e375a7f1981f06
test -z "$(git -C "$imgui" status --porcelain)"
mkdir -p "$root/build/imgui-host"
demo_flags=()
case ${PS5_IMGUI_HOST_HEIGHT:-1080} in 1080|1440|2160) ;; *) echo 'Invalid host surface height' >&2; exit 2 ;; esac
main_source="$root/examples/core33-imgui/main.cpp"
case ${1:-} in
    '') ;;
    --tv-demo) demo_flags=(-DPS5_IMGUI_TV_DEMO) ;;
    --profile) demo_flags=(-DPS5_IMGUI_TV_DEMO -DPS5_IMGUI_PROFILE) ;;
    --window-benchmark)
        case ${PS5_IMGUI_WINDOW_TARGET:-60} in 30|60|90|120) ;; *) echo 'Invalid window target' >&2; exit 2 ;; esac
        demo_flags=(-DPS5_IMGUI_TV_DEMO -DPS5_IMGUI_PROFILE -DPS5_IMGUI_WINDOW_BENCHMARK
                    -DPS5_IMGUI_WINDOW_TARGET=${PS5_IMGUI_WINDOW_TARGET:-60}) ;;
    --benchmark) demo_flags=(-DPS5_IMGUI_TV_DEMO -DPS5_IMGUI_BENCHMARK
                             -DPS5_IMGUI_BENCHMARK_CASE=${PS5_IMGUI_BENCHMARK_CASE:--1}) ;;
    --lifecycle) main_source="$root/examples/core33-imgui/lifecycle.cpp" ;;
    *) echo 'usage: test-imgui-host.sh [--tv-demo|--profile|--window-benchmark|--benchmark|--lifecycle]' >&2; exit 2 ;;
esac
clang++-18 -std=c++11 -O2 -Wall -Wextra -Werror \
    "${demo_flags[@]}" \
    -DPS5_IMGUI_HOST_REFERENCE -DGL_GLEXT_PROTOTYPES=1 \
    -DPS5_IMGUI_HOST_HEIGHT=${PS5_IMGUI_HOST_HEIGHT:-1080} \
    -DIMGUI_IMPL_OPENGL_LOADER_CUSTOM -include GL/gl.h \
    -I"$root/build/sdk/ps5-opengl-core33/include" \
    -I"$imgui" -I"$imgui/backends" \
    "$main_source" \
    "$imgui/imgui.cpp" "$imgui/imgui_draw.cpp" "$imgui/imgui_tables.cpp" \
    "$imgui/imgui_widgets.cpp" "$imgui/backends/imgui_impl_opengl3.cpp" \
    -l:libEGL.so.1 -l:libGL.so.1 -o "$root/build/imgui-host/check"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 \
    MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 \
    "$root/build/imgui-host/check" | tee "$root/build/imgui-host/check.log"
if [[ ${1:-} == --lifecycle ]]; then
    test "$(grep -cE '^\[ps5-imgui-lifecycle\] session=[012] PASS$' "$root/build/imgui-host/check.log")" = 3
    test "$(grep -cE '^\[ps5-imgui\] frame=.* PASS$' "$root/build/imgui-host/check.log")" = 18
fi
if [[ ${1:-} == --benchmark ]]; then
    python3 "$root/tools/summarize-imgui-benchmark.py" "$root/build/imgui-host/check.log" --host \
        --case "${PS5_IMGUI_BENCHMARK_CASE:--1}"
fi
if [[ ${1:-} == --window-benchmark ]]; then
    python3 "$root/tools/summarize-imgui-profile.py" "$root/build/imgui-host/check.log" \
        --host --window-target "${PS5_IMGUI_WINDOW_TARGET:-60}" --window-height "${PS5_IMGUI_HOST_HEIGHT:-1080}"
fi
if [[ ${1:-} == --tv-demo ]]; then
    python3 - "$root/build/imgui-host/check.log" <<'PY'
import re
import sys
from pathlib import Path
probes = re.findall(r"\[ps5-imgui-tv\] readback frame=(\d+) rgba=[0-9,]+ (\w+)",
                    Path(sys.argv[1]).read_text())
assert probes == [(str(frame), "PASS") for frame in (0, 2, 3, 4, 5, 6, 8, 9, 10, 11)], probes
print("imgui-tv: periodic readbacks PASS (simulated elapsed 0..275 seconds)")
PY
fi
