#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Verify the PPSA99005 Khronos GL33 CTS native-title payload.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
stage=${1:-"$root/build/native-app/PPSA99005-cts"}
dist="$stage/dist/PPSA99005"
linked="$stage/build/llvm-pie.elf"
converted="$stage/build/eboot.elf"

for artifact in "$dist/eboot.bin" "$dist/sce_module/libc.prx" \
    "$dist/sce_sys/param.json" "$dist/cts-args.txt" \
    "$dist/gl_cts/data/mustpass/gl/khronos_mustpass/main/gl33-main.txt" \
    "$linked" "$converted"; do
    test -s "$artifact" || {
        printf 'missing native CTS artifact: %s\n' "$artifact" >&2
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

with open(sys.argv[2], "rb") as stream:
    elf = stream.read()
assert elf[:6] == b"\x7fELF\x02\x01"
program_offset = struct.unpack_from("<Q", elf, 32)[0]
program_size = struct.unpack_from("<H", elf, 54)[0]
program_count = struct.unpack_from("<H", elf, 56)[0]
for index in range(program_count):
    header = program_offset + index * program_size
    kind, _flags, offset, address, _physical, _file_size, _memory_size, alignment = \
        struct.unpack_from("<IIQQQQQQ", elf, header)
    if kind == 1 and alignment > 1:
        assert offset % alignment == address % alignment
PY

grep -aFq '[ps5-opengl-cts] starting GL33 CTS runner' "$linked"
grep -aFq '[ps5-opengl-cts] finished' "$linked"
grep -aFq '/download0/ps5-opengl-cts.status' "$linked"
grep -aFq 'KHR-GL33' "$linked"
grep -Fqx -- '--deqp-terminate-on-device-lost=disable' "$dist/cts-args.txt"
if grep -Fq -- '--deqp-case=KHR-GL33.info.*' "$dist/cts-args.txt"; then
    :
elif grep -Fq -- '--deqp-caselist-file=/app0/cts-shard.txt' \
        "$dist/cts-args.txt" && test -s "$dist/cts-shard.txt" &&
        ! grep -Ev '^KHR-GL33\.[^[:space:]]+$' "$dist/cts-shard.txt"; then
    :
else
    printf 'native CTS arguments select no valid GL33 cases\n' >&2
    exit 1
fi

dynamic=$(readelf -d "$converted")
for module in libSceAgc.prx libSceAgcDriver.prx libSceLibcInternal.prx \
    libScePosixForWebKit.prx libSceVideoOut.prx libkernel.prx; do
    grep -Fq "Shared library: [$module]" <<< "$dynamic" || {
        printf 'missing required native import: %s\n' "$module" >&2
        exit 1
    }
done

undefined=$(nm -u "$linked")
if grep -Eq \
    'ps5_agc_gate2|_ZTH23_mesa_glapi_tls_Context|__dl|kernel_mprotect' \
    <<< "$undefined"; then
    printf 'native CTS ELF retains a forbidden unresolved symbol\n' >&2
    exit 1
fi

printf 'Native CTS app verified: PPSA99005\n'
sha256sum "$dist/eboot.bin" "$dist/cts-args.txt" \
    "$dist/sce_module/libc.prx"
