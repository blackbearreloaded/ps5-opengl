#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Pre-hardware guard for the PS5 U8/U16/U32 element-index contract."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT.parents[1] / "native_probe/vdec_hello/agc_clean_triangle.c"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def float_array(source: str, name: str) -> list[float]:
    match = re.search(
        rf"static const float {name}\[\d+\] = \{{(.*?)\}};", source, re.S
    )
    require(match is not None, f"probe array {name} missing")
    return [
        float(token.removesuffix("f"))
        for token in re.findall(r"[-+]?\d+(?:\.\d+)?f?", match.group(1))
    ]


probe = (ROOT / "tests/ps5/mesa_context_probe.c").read_text()
screen = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
pipe_state = (
    ROOT / "third_party/mesa-26.2.0/src/gallium/include/pipe/p_state.h"
).read_text()
primconvert = (
    ROOT
    / "third_party/mesa-26.2.0/src/gallium/auxiliary/indices/u_primconvert.c"
).read_text()
mesa_draw = (ROOT / "third_party/mesa-26.2.0/src/mesa/main/draw.c").read_text()
threaded = (
    ROOT
    / "third_party/mesa-26.2.0/src/gallium/auxiliary/util/u_threaded_context.c"
).read_text()
tracked_patch = (ROOT / "toolchain/mesa-ps5.patch").read_text()
native = NATIVE.read_text()

require("was_index_ubyte:1" in pipe_state, "U8 provenance bit missing")
provenance = (
    "new_info->was_index_ubyte = "
    "info->was_index_ubyte || info->index_size == 1;"
)
require(provenance in primconvert, "U8 provenance propagation missing")
require(provenance in tracked_patch, "tracked Mesa patch lacks U8 provenance")
require("draw->info.was_primitive_restart = false;" in mesa_draw and
        "draw->info.was_index_ubyte = false;" in mesa_draw,
        "display-list draw provenance is not initialized")
require("info->_pad" not in threaded,
        "threaded draw simplification still writes removed padding")
require("src/mesa/main/draw.c" in tracked_patch and
        "src/gallium/auxiliary/util/u_threaded_context.c" in tracked_patch,
        "tracked Mesa patch lacks packed-field initializer updates")
require("info->index_size != 2 && info->index_size != 4" in screen,
        "driver does not accept exactly U16/U32")
require("ps5_agc_gate2_set_index_buffer_typed" in screen,
        "typed native bridge call missing")
require("lowered-index-ubyte" in screen, "U8 evidence log missing")
require("native-index-u16" in screen, "U16 selection evidence log missing")
require("native-index-u32" in screen, "U32 evidence log missing")
require("max_index == UINT32_MAX" in screen, "U32 vertex-count overflow guard missing")

require("int ps5_agc_gate2_set_index_buffer_typed" in native,
        "typed native bridge entry missing")
require("runtime_index_size = index_size;" in native,
        "native bridge does not retain index width")
require("runtime_index_size == sizeof(uint32_t) ?" in native and
        "AGC_INDEX_SIZE_32 : AGC_INDEX_SIZE_16" in native,
        "native bridge does not select AGC U32 mode")
require("agc_index_size=%u" in native,
        "native evidence does not expose encoded AGC width")
require("sizeof(uint16_t));" in native,
        "legacy bridge is not an explicit U16 wrapper")

public_calls = {
    "u16": "_mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,",
    "u8": "_mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_BYTE,",
    "u32": "_mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT,",
}
for name, call in public_calls.items():
    require(call in probe, f"public {name} discriminator missing")

for x in (112, 712, 1312):
    require(f"_mesa_ReadPixels({x}, 870, 264, 150" in probe,
            f"index-width crop {x} changed")

vertices = [
    float_array(probe, "index_u16_vertices"),
    float_array(probe, "index_u8_vertices"),
    float_array(probe, "index_u32_vertices"),
]
require(all(len(values) == 12 for values in vertices),
        "each index-width case must contain three vec4 vertices")
for case in (1, 2):
    for offset in range(0, 12, 4):
        previous = vertices[case - 1][offset:offset + 4]
        current = vertices[case][offset:offset + 4]
        require(abs((current[0] - previous[0]) - 0.625) < 1e-6,
                "adjacent index-width cases must translate +0.625 NDC")
        require(current[1:] == previous[1:],
                "index-width Y/UV coordinates differ")

pixel_x = [
    [(values[i] + 1.0) * 960.0 for i in range(0, 12, 4)]
    for values in vertices
]
crops = [(112, 376), (712, 976), (1312, 1576)]
for points, (left, right) in zip(pixel_x, crops):
    require(all(left <= point < right for point in points),
            "index-width geometry escapes its crop")
for left, right in zip(pixel_x, pixel_x[1:]):
    require(all(abs((b - a) - 600.0) < 1e-4 for a, b in zip(left, right)),
            "adjacent raster translations must be exactly 600 pixels")

require("index_u16_draw_calls == 4" in probe, "U16 draw count changed")
require("index_u8_draw_calls == 5" in probe, "U8 draw count changed")
require("index_u32_draw_calls == 6" in probe, "U32 draw count changed")
require("draw_calls == 15" in probe, "final cumulative draw count changed")
require("index_u16_hash == index_u8_hash" in probe and
        "index_u16_hash == index_u32_hash" in probe,
        "three-width pixel equivalence oracle missing")

print("index-width: PASS public=U16,U8,U32 offsets=2,1,4 "
      "u8-output=U16 u32-native=1 spatial-shift=600px")
