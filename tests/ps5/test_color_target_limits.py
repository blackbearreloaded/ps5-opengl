#!/usr/bin/env python3
"""Host check of the native color batch's bounds and pixel oracle, not GPU emulation."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "tests/ps5/egl_public_core33_msaa4.c").read_text()
start = source.index("static const struct color_limit_case")
cases = source[start:source.index("\n};", start) + 3]
start = source.index("static int\ncolor_limit_pixel_matches(")
oracle = source[start:source.index("\n}", start) + 2]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <GL/gl.h>
''' + cases + oracle + r'''
int main(void) {
    assert(sizeof(color_limit_cases) / sizeof(color_limit_cases[0]) == 10);
    for (unsigned i = 0; i < 10; ++i) {
        const struct color_limit_case *t = &color_limit_cases[i];
        unsigned bytes = t->format == GL_R8 ? 1 : t->format == GL_RG8 ? 2 :
            t->format == GL_RGBA8 ? 4 : t->format == GL_RGBA16F ? 8 :
            t->format == GL_RGBA32F ? 16 : 0;
        assert(bytes && bytes == t->bytes_per_pixel);
        assert(t->width && t->height && t->width <= 8192 && t->height <= 8192);
        assert(t->samples == 0 || t->samples == 4);
        assert((uint64_t)t->width * t->height * bytes *
               (t->samples ? t->samples : 1) <= 64u * 1024u * 1024u);
    }
    float p[4] = {1, 0, 0, 1};
    assert(color_limit_pixel_matches(p, 0, 0));
    assert(!color_limit_pixel_matches(p, 0, 4));
    assert(!color_limit_pixel_matches(p, 4, 0));
    p[0] = 0.25f;
    assert(color_limit_pixel_matches(p, 3, 4));
    assert(!color_limit_pixel_matches(p, 4, 4));
    p[0] = 64.0f / 255.0f;
    assert(color_limit_pixel_matches(p, 0, 4));
    p[0] = 0;
    assert(color_limit_pixel_matches(p, 4, 4));
    assert(!color_limit_pixel_matches(p, 0, 4));
    p[3] = 0;
    assert(!color_limit_pixel_matches(p, 4, 4));
    p[3] = 1;
    p[0] = NAN;
    assert(!color_limit_pixel_matches(p, 0, 4));
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "color-limits")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "third_party/mesa-26.2.0/include"),
                    "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: 20-case color batch bounds; single-sample, masked-resolve and clear oracles")
