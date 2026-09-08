#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Verify the reusable PPSA99005 folder and its converted native ELF.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
stage=${1:-"$root/build/native-app/PPSA99005"}
dist="$stage/dist/PPSA99005"
linked="$stage/build/llvm-pie.elf"
converted="$stage/build/eboot.elf"
selected="$stage/selected-test.txt"

for artifact in "$dist/eboot.bin" "$dist/sce_module/libc.prx" \
    "$dist/sce_sys/param.json" "$linked" "$converted" "$selected"; do
    test -s "$artifact" || {
        printf 'missing native-title artifact: %s\n' "$artifact" >&2
        exit 1
    }
done

python3 - "$dist/sce_sys/param.json" "$converted" <<'PY'
import json
import struct
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    metadata = json.load(stream)
assert metadata["titleId"] == "PPSA99005"
assert metadata["conceptId"] == "99005"
assert metadata["contentId"].startswith("UP9000-PPSA99005_00-")

with open(sys.argv[2], "rb") as stream:
    elf = stream.read()
assert elf[:4] == b"\x7fELF" and elf[4] == 2 and elf[5] == 1
program_offset = struct.unpack_from("<Q", elf, 32)[0]
program_size = struct.unpack_from("<H", elf, 54)[0]
program_count = struct.unpack_from("<H", elf, 56)[0]
for index in range(program_count):
    header = program_offset + index * program_size
    kind, _flags, offset, address, _physical, _file_size, _memory_size, alignment = \
        struct.unpack_from("<IIQQQQQQ", elf, header)
    if kind == 1 and alignment > 1:
        assert offset % alignment == address % alignment, \
            f"LOAD segment {index} is not congruently aligned"
PY

gate=$(tr -d '\r\n' < "$selected")
[[ $gate =~ ^egl_public_[A-Za-z0-9_]+\.o$ ]] || {
    printf 'invalid selected gate: %s\n' "$gate" >&2
    exit 1
}
grep -aFq '[pss-opengl-native] gate completed status=%d' "$linked"
grep -aFq '/download0/pss-opengl.log' "$linked"
# Drain producers under pipefail: grep -q can otherwise make nm/readelf SIGPIPE.
nm -u "$linked" | grep -F 'sceAgcDcbSetNumInstances' >/dev/null
for wrapper in __wrap_malloc __wrap_free; do
    nm --defined-only "$linked" | grep -E " [Tt] $wrapper\$" >/dev/null || {
        printf 'native app is missing its shared heap wrapper: %s\n' "$wrapper" >&2
        exit 1
    }
done

for module in libSceAgc.prx libSceAgcDriver.prx libSceLibcInternal.prx \
    libScePosixForWebKit.prx libSceVideoOut.prx libkernel.prx; do
    readelf -d "$converted" | grep -F "Shared library: [$module]" >/dev/null || {
        printf 'missing required native import: %s\n' "$module" >&2
        exit 1
    }
done

if [[ -f "$stage/src/gpu_memory.c" ]]; then
    for symbol in sceKernelAllocateDirectMemory sceKernelMapDirectMemory sceKernelReleaseDirectMemory munmap; do
        nm --defined-only "$linked" | grep -E " [Tt] __wrap_${symbol}\$" >/dev/null || {
            printf 'missing GPU diagnostic wrapper: %s\n' "$symbol" >&2; exit 1;
        }
    done
fi

if nm -u "$linked" | grep -E \
    'ps5_agc_gate2|_ZTH23_mesa_glapi_tls_Context|__dl|kernel_mprotect' >/dev/null; then
    printf 'native ELF retains a forbidden unresolved symbol\n' >&2
    exit 1
fi

if readelf --dyn-syms --wide "$linked" | tail -n +4 | grep -v ' UND ' >/dev/null; then
    printf 'native ELF unexpectedly publishes application exports\n' >&2
    exit 1
fi

printf 'Native app verified: PPSA99005 gate=%s\n' "$gate"
sha256sum "$dist/eboot.bin" "$dist/sce_module/libc.prx" \
    "$dist/sce_sys/param.json"
