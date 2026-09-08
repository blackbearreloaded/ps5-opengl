#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Guard the candidate-only split D32F+S8 stencil contract."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT.parents[1] / "native_probe/vdec_hello/agc_clean_triangle.c"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


screen = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
probe = (ROOT / "tests/ps5/gallium_gate2_triangle.c").read_text()
makefile = (ROOT / "tests/ps5/Makefile").read_text()
native = NATIVE.read_text()

require("#define PS5_ENABLE_PACKED_DEPTH_STENCIL 0" in screen,
        "ordinary packed-format capability gate is not off")
require("-DPS5_ENABLE_PACKED_DEPTH_STENCIL=1" in makefile and
        "gallium_packed_stencil_candidate.elf" in makefile,
        "candidate-only packed screen build is missing")
packed_link = re.search(
    r"gallium_packed_stencil_candidate\.elf:.*?\n\n", makefile, re.S
)
require(packed_link is not None and
        "$(MESA_LIBGALLIUM)" in packed_link.group(0),
        "candidate link is missing Mesa's uploader implementation")
require("PIPE_FORMAT_Z32_FLOAT_S8X24_UINT" in screen and
        "PIPE_BIND_DEPTH_STENCIL" in screen,
        "packed resource format/binding path is missing")
require("PS5_DEPTH_TARGET_BYTES 0xa00000u" in screen and
        "PS5_STENCIL_TARGET_BYTES 0x280000u" in screen and
        "PS5_STENCIL_ALIGNMENT 0x10000u" in screen,
        "split-plane size/alignment contract changed")
require("[ps5-gallium] stencil-state" in screen and
        "native.stencil_control" in screen and
        "native.stencil_refmask_bf" in screen,
        "exact packed DSA evidence record is missing")

require("records[1].value = UINT32_C(0x20000181)" in native,
        "native S8 descriptor value changed")
require("ps5_agc_gate2_set_depth_stencil_buffer" in native and
        "stencil_size < 0x280000u" in native and
        "((uintptr_t)stencil & 0xffffu)" in native,
        "native stencil setter validation changed")

for expression in (
    "depth_state.stencil[0].func = PIPE_FUNC_ALWAYS",
    "depth_state.stencil[0].fail_op = PIPE_STENCIL_OP_KEEP",
    "depth_state.stencil[0].zpass_op = PIPE_STENCIL_OP_REPLACE",
    "depth_state.stencil[0].zfail_op = PIPE_STENCIL_OP_KEEP",
    "depth_state.stencil[0].valuemask = 0xff",
    "depth_state.stencil[0].writemask = 0xff",
    "stencil_ref.ref_value[0] = 0x5a",
):
    require(expression in probe, f"candidate state missing: {expression}")

require("bytes[i] == UINT8_C(0x5a)" in probe and
        "stencil_matches == 259200" in probe and
        "stencil_unexpected == 0" in probe,
        "exact S8 byte oracle is missing")
require("words[i] == UINT32_C(0x3f000000)" in probe and
        "depth_matches == 259200" in probe and
        "depth_unexpected == 0" in probe,
        "exact D32F oracle is missing")
require("opengl33-gallium-packed-depth.raw" in probe and
        "packed_depth_dump_status == 0" in probe and
        "packed_depth_first" in probe and "packed_depth_min" in probe and
        "packed_depth_max" in probe,
        "packed D32F diagnostic dump or extrema are missing")
require("target_nonzero == 259200" in probe and
        "stencil_dump_status == 0" in probe and
        "opengl33-gallium-packed-stencil.raw" in probe,
        "coverage or full S8 dump oracle is missing")

encoder = re.search(
    r"ps5_encode_depth_stencil_state\(.*?^\}", screen, re.S | re.M
)
require(encoder is not None and
        "native->depth_control |= 1u | (front->func << 8)" in encoder.group(0)
        and "stencil_op[front->zpass_op] << 4" in encoder.group(0)
        and "front->writemask << 16" in encoder.group(0)
        and "ps5_stencil_uses_unit_op_value(front)" in encoder.group(0)
        and "ps5_stencil_uses_unit_op_value(back)" in encoder.group(0),
        "front stencil encoder mapping changed")
require("PIPE_STENCIL_OP_INCR_WRAP" in screen and
        "PIPE_STENCIL_OP_DECR_WRAP" in screen and
        "UINT32_C(1) << 24" in screen,
        "increment/decrement stencil operation value is missing")

require("buffers != PIPE_CLEAR_DEPTH" in screen and
        "resource->base.format != PIPE_FORMAT_Z32_FLOAT" in screen,
        "packed/stencil clear must remain unsupported in this slice")

print("packed-stencil: PASS candidate-only format=Z32F_S8 planes=a00000,280000 "
      "dsa=00000777,00000030,00ffff5a,00ffff5a "
      "coverage=259200 value=3f000000/5a dumps=depth,stencil "
      "clear=unsupported")
