#!/usr/bin/env bash
# Build the bounded Khronos GL33 CTS runner as native title PPSA99005.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
template=${PS5_NATIVE_APP_TEMPLATE:-"$root/../ps5-native-app-boilerplate"}
cts=${VK_GL_CTS_ROOT:-"$root/third_party/VK-GL-CTS"}
cts_build=${VK_GL_CTS_BUILD:-"$cts/build-ps5-gl33"}

[[ -f "$template/Makefile" && -d "$template/.deps/native" ]] || {
    printf 'native-app boilerplate or dependencies are missing: %s\n' \
        "$template" >&2
    exit 2
}

sdk="$template/.deps/native/ps5-payload-sdk"
export PS5_PAYLOAD_SDK="$sdk"
psbc="$root/third_party/opengnm-psbc"
psbc_revision=$(git -C "$psbc" rev-parse HEAD)
psbc_stamp="$root/build/core33-native-runtime/psbc-revision"
psbc_stamped_revision=
[[ ! -f "$psbc_stamp" ]] || read -r psbc_stamped_revision < "$psbc_stamp"
if [[ ! -s "$psbc/libpsbc.ps5.a" ||
      "$psbc_stamped_revision" != "$psbc_revision" ]]; then
    "$root/toolchain/build-opengnm-psbc-ps5.sh"
    mkdir -p "$(dirname -- "$psbc_stamp")"
    printf '%s\n' "$psbc_revision" > "$psbc_stamp"
fi
"$root/conformance/vk-gl-cts/prepare.sh" "$cts"
if [[ ! -f "$cts_build/build.ninja" ]]; then
    cmake -S "$cts" -B "$cts_build" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DDEQP_TARGET=ps5 \
        -DDEQP_TARGET_TOOLCHAIN=ps5-toolchain \
        -DSELECTED_BUILD_TARGETS=ps5-gl33-runner \
        -DDEQP_DISABLE_VK_VIDEO_TESTS=ON
fi
cmake --build "$cts_build" --target ps5-gl33-runner -j8

runner="$cts_build/external/openglcts/modules/libps5-gl33-runner.a"
test -s "$runner"

make -C "$root/tests/ps5" --no-print-directory -f native-app.mk -j8 \
    PS5_PAYLOAD_SDK="$sdk" \
    PS5_OPENGL_RUNTIME_DEFINES='-DPS5_NATIVE_TITLE_RUNTIME=1' \
    runtime
mapfile -t opengl_libraries < <(
    make -C "$root/tests/ps5" --no-print-directory -s -f native-app.mk \
        PS5_PAYLOAD_SDK="$sdk" print-static-libs
)
mapfile -d '' -t cts_libraries < <(
    find "$cts_build" -type f -name '*.a' -print0 | sort -z
)

compiler=${PS5_CLANG:-clang-18}
compiler_runtime=$(
    "$compiler" --print-resource-dir
)/lib/linux/libclang_rt.builtins-x86_64.a
static_libraries=(
    "${cts_libraries[@]}"
    "${opengl_libraries[@]}"
    "$sdk/target/lib/libunwind.a"
    "$sdk/target/lib/libc++abi.a"
    "$sdk/target/lib/libc++.a"
    "$compiler_runtime"
)
for library in "${static_libraries[@]}"; do
    test -s "$library"
done

app="$root/build/native-app/PPSA99005-cts"
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
cp "$root/native-app/cts_runtime_shims.c" "$app/src/cts_runtime_shims.c"
for headers in EGL GL KHR; do
    cp -a "$root/third_party/mesa-26.2.0/include/$headers" "$app/include/"
done
cp "$root/native-app/param.json" "$app/sce_sys/param.json"

group="$app/vendor/libps5_opengl_cts_group.a"
{
    printf 'SEARCH_DIR("%s")\n' "$sdk/target/lib"
    printf 'EXTERN(main)\n'
    printf 'EXTERN(ps5_agc_gate2_run)\n'
    printf 'GROUP (\n'
    printf '  "%s"\n' "${static_libraries[@]}"
    printf ')\n'
} > "$group"
printf 'APP_INCLUDE_PATHS = include\nAPP_STATIC_ARCHIVES = vendor/libps5_opengl_cts_group.a\n' \
    > "$app/.env"

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

dist="$app/dist/PPSA99005"
cp "$root/conformance/vk-gl-cts/native/cts-args.txt" "$dist/cts-args.txt"
cp -a "$cts/external/openglcts/data/gl_cts" "$dist/gl_cts"

printf 'Native CTS app: %s\n' "$dist"
printf 'VK-GL-CTS commit: %s\n' "$(git -C "$cts" rev-parse HEAD)"
printf 'Native boilerplate commit: %s\n' \
    "$(git -c safe.directory="$template" -C "$template" rev-parse HEAD)"
"$root/tools/verify-native-cts-app.sh" "$app"
