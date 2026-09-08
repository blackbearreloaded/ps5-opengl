#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise the shared blend encoder's format-independent export invariant."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
start = source.index("static bool\nps5_dual_source_blend_factor(")
encoder = source[start:source.index("static bool\nps5_polygon_offset_for_fill", start)]
code = r'''
#include <assert.h>
#include <string.h>
#include "pipe/p_state.h"
#define PS5_MAX_RENDER_TARGETS 8u
#define PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE 1
''' + encoder + r'''
int main(void) {
    uint32_t control[8], mask, color, dual;
    struct pipe_blend_state state = {0};
    state.rt[0].colormask = 15;
    for (unsigned count = 1; count <= 8; ++count) {
        assert(ps5_encode_blend_state(NULL, count, control, &mask, &color, &dual));
        assert(color == 0xcc0011 && dual == 0);
        assert(mask == (UINT32_MAX >> (32 - 4 * count)));
        for (unsigned i = 0; i < 8; ++i) assert(control[i] == 0);
        assert(ps5_encode_blend_state(&state, count, control, &mask, &color, &dual));
        assert(color == 0xcc0011 && dual == 0);
    }
    state.rt[0].blend_enable = true;
    state.rt[0].rgb_src_factor = PIPE_BLENDFACTOR_SRC_ALPHA;
    state.rt[0].rgb_dst_factor = PIPE_BLENDFACTOR_INV_SRC_ALPHA;
    state.rt[0].alpha_src_factor = PIPE_BLENDFACTOR_ONE;
    state.rt[0].alpha_dst_factor = PIPE_BLENDFACTOR_INV_SRC_ALPHA;
    assert(ps5_encode_blend_state(&state, 1, control, &mask, &color, &dual));
    assert(color == 0xcc0011 && dual == 0 && control[0] == 0x65010504);
    state.rt[0].rgb_src_factor = PIPE_BLENDFACTOR_SRC1_COLOR;
    assert(ps5_encode_blend_state(&state, 1, control, &mask, &color, &dual));
    assert(color == 0xcc0011 && dual == 1 && (control[0] & 31) == 15);
    assert(!ps5_encode_blend_state(&state, 2, control, &mask, &color, &dual));
    state.rt[0].blend_enable = false;
    state.logicop_enable = true;
    state.logicop_func = PIPE_LOGICOP_XOR;
    assert(ps5_encode_blend_state(&state, 1, control, &mask, &color, &dual));
    assert(color == 0x660011 && dual == 0 && control[0] == 0);
    state.rt[0].colormask = 0;
    assert(ps5_encode_blend_state(&state, 1, control, &mask, &color, &dual));
    assert(mask == 0 && !(color & 0x10));
    assert(ps5_encode_blend_state(&state, 0, control, &mask, &color, &dual));
    assert(mask == 0 && !(color & 0x10));
    assert(!ps5_encode_blend_state(&state, 9, control, &mask, &color, &dual));
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "color-export-state")
    mesa = ROOT / "third_party/mesa-26.2.0"
    subprocess.run(["clang-18", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-DHAVE_ENDIAN_H=1", "-D_POSIX_C_SOURCE=200809L",
                    "-I", str(mesa / "include"),
                    "-I", str(ROOT / "build/mesa-ps5-probe/src"),
                    "-I", str(mesa / "src"), "-I", str(mesa / "src/gallium/include"),
                    "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], check=True)
assert "native->color_control_valid = 1;" in source
print("PASS: explicit non-RB+ color state; opaque/alpha/dual-source/MRT/ROP/masked paths")
