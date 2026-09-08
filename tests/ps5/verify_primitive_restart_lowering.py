#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Pre-hardware guard for the PS5 primitive-restart fallback contract."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def triangle_strip(indices: list[int]) -> list[int]:
    output: list[int] = []
    for i in range(len(indices) - 2):
        if i & 1:
            output.extend((indices[i + 1], indices[i], indices[i + 2]))
        else:
            output.extend(indices[i : i + 3])
    return output


def assembled_adjacency(mode: str, indices: list[int]) -> list[int]:
    output: list[int] = []
    if mode == "lines":
        for i in range(0, len(indices) - 3, 4):
            output.extend((indices[i + 1], indices[i + 2]))
    elif mode == "line_strip":
        for i in range(len(indices) - 3):
            output.extend((indices[i + 1], indices[i + 2]))
    elif mode == "triangles":
        for i in range(0, len(indices) - 5, 6):
            output.extend((indices[i], indices[i + 2], indices[i + 4]))
    elif mode == "triangle_strip":
        for primitive, i in enumerate(range(0, len(indices) - 5, 2)):
            output.extend((indices[i + 2], indices[i], indices[i + 4])
                          if primitive & 1 else
                          (indices[i], indices[i + 2], indices[i + 4]))
    return output


segments = ([0, 1, 2], [3, 4, 5])
expected = [0, 1, 2, 3, 4, 5]
lowered = [index for segment in segments for index in triangle_strip(segment)]
incorrect_joined = triangle_strip([index for segment in segments for index in segment])

require(lowered == expected, "per-segment triangle-strip lowering changed")
require(len(lowered) == 6, "per-segment output count must be six")
require(len(incorrect_joined) == 12, "joined-strip discriminator lost its bridge")
require(incorrect_joined != expected, "joined strips no longer discriminate failure")
require(assembled_adjacency("lines", [9, 0, 1, 8]) == [0, 1],
        "line adjacency principals changed")
require(assembled_adjacency("line_strip", [9, 0, 1, 2, 8]) ==
        [0, 1, 1, 2], "line-strip adjacency principals changed")
require(assembled_adjacency("triangles", [0, 9, 1, 9, 2, 9]) ==
        [0, 1, 2], "triangle adjacency principals changed")
require(assembled_adjacency("triangle_strip", [0, 9, 1, 9, 2, 9, 3, 9]) ==
        [0, 1, 2, 2, 1, 3], "triangle-strip adjacency winding changed")

primconvert = (ROOT / "third_party/mesa-26.2.0/src/gallium/auxiliary/indices/u_primconvert.c").read_text()
pipe_state = (ROOT / "third_party/mesa-26.2.0/src/gallium/include/pipe/p_state.h").read_text()
screen = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
runtime = (ROOT / "src/platform/ps5_agc_native_runtime.c").read_text()
probe = (ROOT / "tests/ps5/mesa_context_probe.c").read_text()
tracked_patch = (ROOT / "toolchain/mesa-ps5.patch").read_text()


def float_array(name: str) -> list[float]:
    match = re.search(
        rf"static const float {name}\[\d+\] = \{{(.*?)\}};", probe, re.S
    )
    require(match is not None, f"probe array {name} missing")
    return [
        float(token.removesuffix("f"))
        for token in re.findall(r"[-+]?\d+(?:\.\d+)?f?", match.group(1))
    ]

checks = {
    "force-independent-mode": "primtypes_mask &= ~BITFIELD_BIT(info->mode);",
    "sum-per-range": "primtypes_mask, true, info->mode, direct_draws[i].count",
    "restart-provenance": "new_info->was_primitive_restart = true;",
}
for name, needle in checks.items():
    require(needle in primconvert, f"Mesa fallback missing {name}")
    require(needle in tracked_patch, f"tracked Mesa patch missing {name}")

require("was_primitive_restart:1" in pipe_state, "pipe draw provenance bit missing")
require("caps->primitive_restart = true;" in screen, "emulated capability missing")
require("caps->supported_prim_modes_with_restart = caps->supported_prim_modes;" in screen,
        "restart modes must route to the driver fallback")
require("util_draw_vbo_without_prim_restart(" in screen,
        "driver restart splitter missing")
require("ps5_draw_vbo_without_adjacency(" in screen,
        "driver no-GS adjacency reassembly missing")
require("!((struct ps5_context *)base)->gs" in screen,
        "geometry-shader adjacency must remain native")
require("lowered-primitive-restart" in screen, "driver evidence log missing")
for primitive in ("LINELIST_ADJ", "LINESTRIP_ADJ", "TRILIST_ADJ", "TRISTRIP_ADJ"):
    require(f"/* {primitive} */" in runtime,
            f"native runtime rejects {primitive}")
require("_mesa_EnableClientState(GL_PRIMITIVE_RESTART_NV);" in probe,
        "public NV restart enable path missing")
require("_mesa_DrawElements(GL_TRIANGLE_STRIP, 7" in probe,
        "public restart discriminator missing")
require("restart_baseline_hash == restart_draw_hash" in probe,
        "pixel equivalence oracle missing")
require("restart_renderbuffers" not in probe,
        "successor must not allocate extra restart renderbuffers")
require("_mesa_ReadPixels(90, 260, 780, 560" in probe,
        "left baseline crop changed")
require("_mesa_ReadPixels(1050, 260, 780, 560" in probe,
        "right restart crop changed")
require(probe.index("stage=primitive-restart-baseline") <
        probe.index("stage=line-loop-arrays"),
        "restart crops must be read before line-loop contamination")
require("restart_baseline_draw_calls == 7" in probe,
        "baseline cumulative draw count changed")
require("restart_draw_calls == 8" in probe,
        "restart cumulative draw count changed")

baseline_vertices = float_array("restart_baseline_vertices")
restart_vertices = float_array("restart_vertices")
require(len(baseline_vertices) == len(restart_vertices) == 24,
        "restart vertex arrays must contain six vec4 vertices")
for offset in range(0, 24, 4):
    baseline = baseline_vertices[offset:offset + 4]
    restart = restart_vertices[offset:offset + 4]
    require(abs((restart[0] - baseline[0]) - 1.0) < 1e-6,
            "restart X coordinates must translate exactly +1.0 NDC")
    require(restart[1:] == baseline[1:],
            "restart Y/UV coordinates must equal the baseline")

left_x = [(baseline_vertices[i] + 1.0) * 960.0 for i in range(0, 24, 4)]
right_x = [(restart_vertices[i] + 1.0) * 960.0 for i in range(0, 24, 4)]
require(all(90 <= x < 870 for x in left_x), "baseline escapes left crop")
require(all(1050 <= x < 1830 for x in right_x), "restart escapes right crop")
require(all(abs((right - left) - 960.0) < 1e-4
            for left, right in zip(left_x, right_x)),
        "raster translation must be exactly 960 pixels")
require(max(left_x) < min(right_x), "baseline and restart crops overlap")

print("primitive-restart-lowering: PASS segments=2 output-mode=triangles "
      "count=6 indices=0,1,2,3,4,5 spatial-shift=960px")
