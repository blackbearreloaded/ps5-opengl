#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check the actual unscissored depth fill, including padded layers and canaries.

This is a host memory check, not a GPU coherence or tiled-addressing test.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()
start = source.index("static bool\nps5_clear_depth_stencil(")
start = source.index("         if (!scissor_state) {", start)
fill = source[start:source.index("         } else {", start)] + "         }\n"
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "util/u_memset.h"
static void fill_layer(uint8_t *layer_data, size_t depth_layer_size, uint32_t clear_bits)
{
    const void *scissor_state = NULL;
''' + fill + r'''
}
int main(void)
{
    const size_t counts[] = {0, 1, 15, 16, 17, 4096, 8388608};
    const uint32_t values[] = {0, 0x80000000, 0x3f800000, 0x3eaaaaab};
    for (size_t n = 0; n < sizeof(counts) / sizeof(counts[0]); ++n) {
        size_t count = counts[n], total = count + 32;
        uint32_t *p = malloc(total * sizeof(*p));
        assert(p);
        for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
            memset(p, 0xa5, total * sizeof(*p));
            fill_layer((uint8_t *)(p + 16), count * sizeof(*p), values[v]);
            for (size_t i = 0; i < total; ++i)
                assert(p[i] == (i >= 16 && i < count + 16 ? values[v] : 0xa5a5a5a5));
        }
        free(p);
    }
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "depth-clear-fill")
    subprocess.run(["cc", "-std=c11", "-Os", "-Wall", "-Wextra", "-Werror",
                    "-DHAVE_ENDIAN_H=1", "-I" + str(root / "third_party/mesa-26.2.0/src"),
                    "-x", "c", "-o", executable, "-"], input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: exact depth words, short/padded 32 MiB layer fills, adjacent-memory canaries")
