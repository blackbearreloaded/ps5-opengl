#!/usr/bin/env python3
"""Check the real point/line encoder against Mesa's zero-initialized meta state."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
start = source.index("   native->point_line[0] = 0;")
block = source[start:source.index("   native->interp_control = 0;", start)]
helpers = []
for result, name in (("uint32_t", "ps5_float_bits"), ("bool", "ps5_float_is_finite"),
                     ("uint32_t", "ps5_pack_float_12p4")):
    start = source.index("static " + result + "\n" + name + "(")
    helpers.append(source[start:source.index("\n}", start) + 2])
limits = "\n".join(re.findall(r"^#define PS5_(?:MIN|MAX)_POINT_LINE_SIZE .+$", source, re.M))
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#define PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE 1
struct rasterizer { float point_size, line_width; bool point_size_per_vertex; };
struct context { struct rasterizer *rasterizer; };
struct native { uint32_t point_line[3], point_line_valid; };
''' + limits + "\n" + "\n".join(helpers) + r'''
static bool encode(struct context *context, struct native *native) {
    float point_min, point_max;
    uint32_t fixed_size;
''' + block + r'''
    return true;
}
int main(void) {
    struct rasterizer raster = {0};
    struct context context = {&raster};
    struct native native = {0};
    assert(encode(&context, &native));
    assert(native.point_line_valid == 1 && native.point_line[0] == 0x00080008);
    assert(native.point_line[1] == 0x00080008 && native.point_line[2] == 8);
    raster.point_size = 4; raster.line_width = 2;
    assert(encode(&context, &native));
    assert(native.point_line[0] == 0x00200020 && native.point_line[2] == 16);
    const float invalid[] = {-1, 0.5f, PS5_MAX_POINT_LINE_SIZE + 1, INFINITY, NAN};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        raster.point_size = invalid[i]; raster.line_width = 1;
        assert(!encode(&context, &native));
        raster.point_size = 1; raster.line_width = invalid[i];
        assert(!encode(&context, &native));
    }
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "meta-rasterizer")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: zero-initialized meta rasterizer; explicit sizes; invalid sizes still rejected")
