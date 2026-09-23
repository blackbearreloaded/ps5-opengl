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
start = source.index("   /* A full-surface scissor is the same uniform fill")
normalize = source[start:source.index("   clear_bits =", start)]
helper_start = source.index("static void\nps5_clear_words(")
helper = source[helper_start:source.index("static bool\nps5_clear_depth_stencil(", helper_start)]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "util/u_memset.h"
#include <emmintrin.h>
#define PS5_NATIVE_TITLE_RUNTIME 1
''' + helper + r'''
struct scissor { unsigned minx,miny,maxx,maxy; };
static void fill_layer(uint8_t *layer_data, size_t depth_layer_size, uint32_t clear_bits,
                       const struct scissor *scissor_state)
{
    const struct { struct { unsigned width0,height0; } base; } target = {{8,8}}, *resource = &target;
''' + normalize + fill + r'''
}
int main(void)
{
    const size_t counts[] = {0, 1, 15, 16, 17, 4096, 16383, 16384, 16385, 32768, 8388608};
    const uint32_t values[] = {0, 0x80000000, 0x3f800000, 0x3eaaaaab};
    for (size_t n = 0; n < sizeof(counts) / sizeof(counts[0]); ++n) {
        size_t count = counts[n], total = (count + 64 + 15) & ~(size_t)15;
        uint32_t *p = aligned_alloc(64, total * sizeof(*p));
        assert(p);
        for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); ++v) {
            for (unsigned offset = 16; offset <= 17; ++offset) {
                memset(p, 0xa5, total * sizeof(*p));
                fill_layer((uint8_t *)(p + offset), count * sizeof(*p), values[v], NULL);
                for (size_t i = 0; i < total; ++i)
                    assert(p[i] == (i >= offset && i < count + offset ? values[v] : 0xa5a5a5a5));
            }
        }
        free(p);
    }
    const struct scissor rectangles[] = {{0,0,8,8},{0,0,32,32},{1,0,8,8},{0,1,8,8},{0,0,7,8},{0,0,8,7}};
    uint32_t words[66];
    for (unsigned r=0;r<6;++r) {
        memset(words,0xa5,sizeof(words));
        fill_layer((uint8_t *)(words+1),64*4,0x3f800000,&rectangles[r]);
        for(unsigned i=0;i<66;++i)
            assert(words[i]==(r<2 && i>0 && i<65 ? 0x3f800000 : 0xa5a5a5a5));
    }
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "depth-clear-fill")
    subprocess.run(["cc", "-std=c11", "-O3", "-Wall", "-Wextra", "-Werror",
                    "-DHAVE_ENDIAN_H=1", "-I" + str(root / "third_party/mesa-26.2.0/src"),
                    "-x", "c", "-o", executable, "-"], input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: exact depth words, padded 32 MiB layers, full/oversized scissors, partial rejection and canaries")
