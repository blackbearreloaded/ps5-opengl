#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise the exact location-to-AGC-semantic helper added to PSBC."""

import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "toolchain/opengnm-psbc-ps5.patch"


def added_function(source: str, signature: str) -> str:
    lines = source.splitlines()
    start = next(
        index for index, line in enumerate(lines)
        if line.startswith(f"+static bool {signature}(")
    )
    function = []
    for line in lines[start:]:
        if not line.startswith("+"):
            break
        function.append(line[1:])
        if line == "+}":
            return "\n".join(function)
    raise AssertionError(f"unterminated added function: {signature}")


def added_multiline_function(source: str, name: str) -> str:
    lines = source.splitlines()
    name_line = next(
        index for index, line in enumerate(lines)
        if line.startswith(f"+{name}(")
    )
    start = name_line - 1
    assert lines[start] == "+static bool"
    function = []
    for line in lines[start:]:
        if not line.startswith("+"):
            break
        function.append(line[1:])
        if line == "+}":
            return "\n".join(function)
    raise AssertionError(f"unterminated function: {name}")


def main() -> None:
    patch = PATCH.read_text()
    subprocess.run(["git", "apply", "--stat", str(PATCH)], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
    assert "ps5_varying_semantic(io.location + slot, &semantic)" in patch
    assert "ps5_varying_semantic(io.location, &semantic)" in patch
    assert "semantic | ((uint32_t)parameter << 8)" in patch
    assert "point_coord ? PSBC_SEMANTIC_POINT_COORD" in patch
    assert "(idx >= VARYING_SLOT_COL0 && idx <= VARYING_SLOT_TEX7)" in patch
    assert "idx == VARYING_SLOT_BFC0 || idx == VARYING_SLOT_BFC1" in patch

    helper = added_function(patch, "ps5_varying_semantic")
    harness = r"""
#include <assert.h>
#include <stdbool.h>
#define PSBC_MAX_SEMANTICS 32u
enum {
    VARYING_SLOT_POS = 0,
    VARYING_SLOT_COL0 = 1,
    VARYING_SLOT_COL1 = 2,
    VARYING_SLOT_FOGC = 3,
    VARYING_SLOT_TEX0 = 4,
    VARYING_SLOT_TEX1 = 5,
    VARYING_SLOT_TEX2 = 6,
    VARYING_SLOT_TEX3 = 7,
    VARYING_SLOT_TEX4 = 8,
    VARYING_SLOT_TEX5 = 9,
    VARYING_SLOT_TEX6 = 10,
    VARYING_SLOT_TEX7 = 11,
    VARYING_SLOT_PSIZ = 12,
    VARYING_SLOT_BFC0 = 13,
    VARYING_SLOT_BFC1 = 14,
    VARYING_SLOT_EDGE = 15,
    VARYING_SLOT_CLIP_VERTEX = 16,
    VARYING_SLOT_VAR0 = 32,
};
""" + helper + r"""
static void expect(unsigned location, unsigned wanted) {
    unsigned semantic = 99;
    assert(ps5_varying_semantic(location, &semantic));
    assert(semantic == wanted);
}
int main(void) {
    expect(VARYING_SLOT_COL0, 0);
    expect(VARYING_SLOT_COL1, 1);
    expect(VARYING_SLOT_FOGC, 2);
    expect(VARYING_SLOT_TEX0, 3);
    expect(VARYING_SLOT_TEX1, 4);
    expect(VARYING_SLOT_TEX2, 5);
    expect(VARYING_SLOT_TEX3, 6);
    expect(VARYING_SLOT_TEX4, 7);
    expect(VARYING_SLOT_TEX5, 8);
    expect(VARYING_SLOT_TEX6, 9);
    expect(VARYING_SLOT_TEX7, 10);
    expect(VARYING_SLOT_BFC0, 12);
    expect(VARYING_SLOT_BFC1, 13);
    expect(VARYING_SLOT_VAR0, 15);
    expect(VARYING_SLOT_VAR0 + PSBC_MAX_SEMANTICS - 1, 46);
    const unsigned unsupported[] = {
        VARYING_SLOT_POS, VARYING_SLOT_PSIZ, VARYING_SLOT_EDGE,
        VARYING_SLOT_CLIP_VERTEX, VARYING_SLOT_VAR0 + PSBC_MAX_SEMANTICS,
    };
    for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        unsigned semantic = 99;
        assert(!ps5_varying_semantic(unsupported[i], &semantic));
        assert(semantic == 99);
    }
    return 0;
}
"""
    with tempfile.TemporaryDirectory(prefix="ps5-varying-map-") as directory:
        binary = Path(directory) / "varying-map"
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c",
             "-o", str(binary), "-"],
            input=harness, text=True, check=True,
        )
        subprocess.run([str(binary)], check=True)

    radv_helper = added_multiline_function(patch, "radv_map_io_legacy_varying")
    radv_harness = r"""
#include <assert.h>
#include <stdbool.h>
enum {
    VARYING_SLOT_POS = 0,
    VARYING_SLOT_COL0 = 1,
    VARYING_SLOT_COL1 = 2,
    VARYING_SLOT_FOGC = 3,
    VARYING_SLOT_TEX0 = 4,
    VARYING_SLOT_TEX7 = 11,
    VARYING_SLOT_BFC0 = 13,
    VARYING_SLOT_BFC1 = 14,
    RADV_IO_SLOT_VAR0 = 4,
    RADV_IO_SLOT_LEGACY0 = RADV_IO_SLOT_VAR0 + 32,
};
""" + radv_helper + r"""
int main(void) {
    const unsigned semantics[] = {
        VARYING_SLOT_COL0, VARYING_SLOT_COL1, VARYING_SLOT_BFC0,
        VARYING_SLOT_BFC1, VARYING_SLOT_FOGC,
        VARYING_SLOT_TEX0, VARYING_SLOT_TEX0 + 1, VARYING_SLOT_TEX0 + 2,
        VARYING_SLOT_TEX0 + 3, VARYING_SLOT_TEX0 + 4, VARYING_SLOT_TEX0 + 5,
        VARYING_SLOT_TEX0 + 6, VARYING_SLOT_TEX7,
    };
    bool used[13] = { false };
    for (unsigned i = 0; i < sizeof(semantics) / sizeof(semantics[0]); ++i) {
        unsigned driver_location = 0;
        assert(radv_map_io_legacy_varying(semantics[i], &driver_location));
        assert(driver_location >= RADV_IO_SLOT_LEGACY0);
        assert(driver_location < RADV_IO_SLOT_LEGACY0 + 13);
        assert(!used[driver_location - RADV_IO_SLOT_LEGACY0]);
        used[driver_location - RADV_IO_SLOT_LEGACY0] = true;
    }
    unsigned driver_location = 999;
    assert(!radv_map_io_legacy_varying(VARYING_SLOT_POS, &driver_location));
    assert(driver_location == 999);
    return 0;
}
"""
    assert "radv_map_io_legacy_varying(semantic, &legacy_driver_location)" in patch
    with tempfile.TemporaryDirectory(prefix="radv-legacy-varying-map-") as directory:
        binary = Path(directory) / "legacy-driver-map"
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c",
             "-o", str(binary), "-"],
            input=radv_harness, text=True, check=True,
        )
        subprocess.run([str(binary)], check=True)
    print("PASS: legacy/generic PSBC semantics and RADV unlinked IO slots map consistently")


if __name__ == "__main__":
    main()
