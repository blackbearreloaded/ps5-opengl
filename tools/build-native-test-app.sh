#!/usr/bin/env bash
# Build one OpenGL gate as the installed native title PPSA99005.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
template=${PS5_NATIVE_APP_TEMPLATE:-"$root/../ps5-native-app-boilerplate"}
requested_test=${1:-core33-texture-rectangle}

if [[ $requested_test == --list ]]; then
    printf 'egl_public_core33_imgui.o\negl_public_core33_imgui_tv.o\negl_public_core33_imgui_benchmark.o\negl_public_core33_imgui_lifecycle.o\negl_public_core33_nanovg.o\negl_public_core33_sokol.o\negl_public_core33_sokol_cube.o\n'
    grep -oE '^egl_public_[A-Za-z0-9_]+\.o' "$root/tests/ps5/Makefile" |
        sort -u
    exit 0
fi

case "$requested_test" in
    core33-texture-rectangle)
        gate_object=egl_public_core33_texture_rectangle.o
        ;;
    core33-texture-rgtc)
        gate_object=egl_public_core33_texture_rgtc.o
        ;;
    core33-depth-texture)
        gate_object=egl_public_core33_depth_texture.o
        ;;
    egl_public_*.o)
        gate_object=$requested_test
        ;;
    egl_public_*)
        gate_object=$requested_test.o
        ;;
    *)
        printf 'usage: %s {--list|core33-texture-rectangle|core33-texture-rgtc|core33-depth-texture|egl_public_<gate>[.o]}\n' "$0" >&2
        exit 2
        ;;
esac

[[ $gate_object =~ ^egl_public_core33_(imgui(_tv|_lifecycle|_benchmark)?|nanovg|sokol(_cube)?)\.o$ ]] || grep -qxF "${gate_object}:" < <(
    grep -oE '^egl_public_[A-Za-z0-9_]+\.o:' "$root/tests/ps5/Makefile"
) || {
    printf 'unknown public OpenGL test object: %s\n' "$gate_object" >&2
    exit 2
}
test_name=${gate_object%.o}
case ${PS5_IMGUI_WINDOW_TARGET:-60} in 30|60|90|120) ;; *) echo 'Invalid window target' >&2; exit 2 ;; esac
if [[ ${PS5_IMGUI_WINDOW_TARGET:-60} -gt 60 ]]; then
    [[ $gate_object == egl_public_core33_imgui_tv.o && ${PS5_IMGUI_PROFILE:-0} == 1 &&
       ${PS5_IMGUI_WINDOW_BENCHMARK:-0} == 1 ]] || {
        echo 'High-refresh metadata is restricted to the profiled window benchmark' >&2; exit 2;
    }
fi
if [[ $test_name == egl_public_core33_submit_batch ]]; then
    [[ ${PS5_DRAW_BATCH_PROBE:-0} == 1 ]] || { echo 'Batch gate requires PS5_DRAW_BATCH_PROBE=1' >&2; exit 2; }
elif [[ ${PS5_DRAW_BATCH_PROBE:-0} == 1 ]]; then
    echo 'Batch probe is restricted to the opaque submit-batch gate' >&2
    exit 2
fi

[[ -f "$template/Makefile" && -d "$template/.deps/native" ]] || {
    printf 'native-app boilerplate or its dependencies are missing: %s\n' "$template" >&2
    exit 2
}
boilerplate_commit=$(git -c safe.directory="$template" -C "$template" \
    rev-parse HEAD)

sdk="$template/.deps/native/ps5-payload-sdk"
if [[ $gate_object =~ ^egl_public_core33_(imgui(_tv|_lifecycle|_benchmark)?|nanovg|sokol(_cube)?)\.o$ ]]; then
    renderer=${gate_object#egl_public_core33_}
    renderer=${renderer%.o}
    renderer=${renderer%_tv}
    renderer=${renderer%_lifecycle}
    renderer=${renderer%_benchmark}
    renderer=${renderer//_/-}
    prefix=$(realpath -m -- "${PS5_OPENGL_PREFIX:-$root/build/sdk/ps5-opengl-core33}")
    (cd "$prefix" && sha256sum --check --strict manifest.sha256 >/dev/null)
    make -B -C "$root/examples/core33-$renderer" --no-print-directory -j8 \
        PS5_PAYLOAD_SDK="$sdk" PS5_OPENGL_PREFIX="$prefix" \
        CC="env PS5_PAYLOAD_SDK=$sdk sh $template/tooling/prospero-clang18" \
        CXX="env PS5_PAYLOAD_SDK=$sdk sh $template/tooling/prospero-clang18" \
        check-source "$gate_object"
    gate_object_path="$root/examples/core33-$renderer/$gate_object"
    static_libraries=("$prefix/lib/libPS5OpenGLCore33.a")
    public_headers="$prefix/include"
    if [[ $gate_object == egl_public_core33_imgui_tv.o ]]; then
        static_libraries+=("$sdk/target/lib/libScePad.so" "$sdk/target/lib/libSceUserService.so")
    fi
else
    PS5_PAYLOAD_SDK="$sdk" bash "$root/toolchain/build-opengnm-psbc-ps5.sh" --if-needed
    make -C "$root/tests/ps5" --no-print-directory -j8 \
        PS5_PAYLOAD_SDK="$sdk" "$gate_object"
    gate_object_path="$root/tests/ps5/$gate_object"
    make -C "$root/tests/ps5" --no-print-directory -f native-app.mk -j8 \
        PS5_PAYLOAD_SDK="$sdk" runtime
    mapfile -t static_libraries < <(
        make -C "$root/tests/ps5" --no-print-directory -s -f native-app.mk \
            PS5_PAYLOAD_SDK="$sdk" print-static-libs
    )
    public_headers="$root/third_party/mesa-26.2.0/include"
fi
test -s "$gate_object_path"
oracle=$(strings "$gate_object_path" | grep -m1 -E '^\[ps5-' || true)
compiler=${PS5_CLANG:-clang-18}
compiler_runtime=$(
    "$compiler" --print-resource-dir
)/lib/linux/libclang_rt.builtins-x86_64.a
static_libraries+=(
    "$sdk/target/lib/libunwind.a"
    "$sdk/target/lib/libc++abi.a"
    "$sdk/target/lib/libc++.a"
    "$compiler_runtime"
)
for library in "${static_libraries[@]}"; do
    test -s "$library"
done

app="$root/build/native-app/PPSA99005"
[[ "$app" == "$root/build/native-app/PPSA99005" && -n "$test_name" ]] || exit 2
mkdir -p "$app"
cp "$template/Makefile" "$app/Makefile"
for directory in assets runtime sce_sys tooling tools; do
    mkdir -p "$app/$directory"
    cp -a "$template/$directory/." "$app/$directory/"
done
heap_source="$app/tooling/native/sce_module_writer.cpp"
heap_default='write_u64(result.data, result.heap_size, std::numeric_limits<std::uint64_t>::max());'
test "$(grep -Fc "$heap_default" "$heap_source")" = 1
sed -i "s/$heap_default/write_u64(result.data, result.heap_size, 0x10000000ULL);/" \
    "$heap_source"
link_script="$app/tools/build.sh"
link_marker='--eh-frame-hdr \'
test "$(grep -Fc -- "$link_marker" "$link_script")" = 1
sed -i 's/--eh-frame-hdr \\/--eh-frame-hdr --wrap=malloc --wrap=calloc --wrap=realloc --wrap=free --wrap=posix_memalign --wrap=malloc_usable_size \\/' \
    "$link_script"
cp "$template/tooling/native/ps5-pie.ld" \
    "$app/tooling/native/ps5-pie-base.ld"
cp "$root/native-app/ps5-pie.ld" "$app/tooling/native/ps5-pie.ld"
cp "$root/native-app/app-symbols.map" "$app/tooling/native/app-symbols.map"
rm -rf -- "$app/src" "$app/include" "$app/vendor"
mkdir -p "$app/src" "$app/include" "$app/vendor"
cp "$root/native-app/runtime_shims.c" "$app/src/runtime_shims.c"
cp "$root/native-app/app_heap.c" "$app/src/app_heap.c"
for headers in EGL GL KHR; do
    cp -a "$public_headers/$headers" "$app/include/"
done
cp "$root/native-app/param.json" "$app/sce_sys/param.json"
if [[ ${PS5_IMGUI_WINDOW_TARGET:-60} -gt 60 ]]; then
    # Ordinary high-resolution/HFR title metadata, matching the native VideoOut request.
    python3 - "$app/sce_sys/param.json" <<'HFR_METADATA'
import json
import sys
from pathlib import Path
path = Path(sys.argv[1])
metadata = json.loads(path.read_text())
assert metadata["titleId"] == "PPSA99005" and metadata["attribute3"] == 0
metadata["attribute3"] = 0x80040
path.write_text(json.dumps(metadata, indent=2) + "\n")
HFR_METADATA
fi

group="$app/vendor/libps5_opengl_group.a"
{
    printf 'SEARCH_DIR("%s")\n' "$sdk/target/lib"
    if [[ $gate_object == egl_public_core33_imgui*.o ]]; then
        printf 'SEARCH_DIR("%s")\n' "$prefix/lib"
    fi
    printf 'EXTERN(ps5_agc_gate2_run)\n'
    printf 'GROUP (\n'
    printf '  "%s"\n' "$gate_object_path"
    printf '  "%s"\n' "${static_libraries[@]}"
    printf ')\n'
} > "$group"
printf 'APP_INCLUDE_PATHS = include\nAPP_STATIC_ARCHIVES = vendor/libps5_opengl_group.a\n' \
    > "$app/.env"
printf '%s\n' "$gate_object" > "$app/selected-test.txt"

if [[ ! -x "$app/.deps/native/ps5-payload-sdk/bin/prospero-lld" ]]; then
    mkdir -p "$app/.deps"
    cp -a "$template/.deps/native" "$app/.deps/native"
fi

app_sdk="$app/.deps/native/ps5-payload-sdk"
mkdir -p "$app/build/native-imports"
PS5_PAYLOAD_SDK="$app_sdk" sh "$app/tooling/prospero-clang18" \
    -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections \
    -c "$root/native-app/agc_link_stub.c" \
    -o "$app/build/native-imports/agc_link_stub.o"
"$app_sdk/bin/prospero-lld" --shared -soname libSceAgc.prx \
    -o "$app_sdk/target/lib/libSceAgc.so" \
    "$app/build/native-imports/agc_link_stub.o"
PS5_PAYLOAD_SDK="$app_sdk" sh "$app/tooling/prospero-clang18" \
    -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections \
    -c "$root/native-app/agc_driver_link_stub.c" \
    -o "$app/build/native-imports/agc_driver_link_stub.o"
"$app_sdk/bin/prospero-lld" --shared -soname libSceAgcDriver.prx \
    -o "$app_sdk/target/lib/libSceAgcDriver.so" \
    "$app/build/native-imports/agc_driver_link_stub.o"

make -C "$app" --no-print-directory -j8 app
[[ -z $oracle ]] || grep -aFq "$oracle" "$app/build/eboot.elf"

dist="$app/dist/PPSA99005"
test -s "$dist/eboot.bin"
test -s "$dist/sce_module/libc.prx"
test -s "$dist/sce_sys/param.json"
printf 'Native app: %s\n' "$dist"
printf 'Selected test: %s\n' "$gate_object"
printf 'Native boilerplate commit: %s\n' "$boilerplate_commit"
"$root/tools/verify-native-test-app.sh" "$app"
