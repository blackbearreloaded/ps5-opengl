#!/usr/bin/env python3
"""Guard the public unsized-depth to Gallium Z32F framebuffer contract."""

from pathlib import Path
import math
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


def crop(color: bytes) -> tuple[int, int]:
    data = bytearray()
    coverage = 0
    for y in range(60, 210):
        row = y - 67
        left = math.ceil(120 + row * 8 / 9 - 0.5)
        right = math.ceil(360 - row * 8 / 9 - 0.5) - 1
        for x in range(112, 376):
            covered = 0 <= row < 135 and left <= x <= right
            data += color if covered else bytes(4)
            coverage += covered
    hash32 = 2166136261
    for byte in data:
        hash32 = ((hash32 ^ byte) * 16777619) & 0xFFFFFFFF
    return coverage, hash32


screen = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
probe = (ROOT / "tests/ps5/mesa_context_probe.c").read_text()
st_format = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_format.c"
).read_text()
st_extensions = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_extensions.c"
).read_text()
tracked_patch = (ROOT / "toolchain/mesa-ps5.patch").read_text()
native = NATIVE.read_text()

require("context->base.clear = ps5_clear;" in screen,
        "Gallium clear callback is not installed")
require("buffers != PIPE_CLEAR_DEPTH || scissor_state" in screen,
        "clear does not reject requests outside the frozen depth envelope")
require("resource->base.format != PIPE_FORMAT_Z32_FLOAT" in screen,
        "clear does not require D32F")
require("!(depth >= 0.0 && depth <= 1.0)" in screen,
        "clear depth range validation is missing")
require("words[index] = clear_bits;" in screen and
        "ps5_flush_gpu_data(resource->data, resource->allocation_size);" in screen,
        "full-plane fill or flush is missing")
require("[ps5-gallium] clear-depth" in screen,
        "successful clear evidence record is missing")
require("[ps5-gallium] reject-clear" in screen,
        "unsupported clear evidence record is missing")
require("[ps5-gallium] depth-state" in screen and
        "native.depth_control" in screen,
        "exact depth-control evidence record is missing")

unsized_depth = re.search(
    r"\{\s*\{ GL_DEPTH_COMPONENT, 0 \},\s*\{(.*?)\}\s*\},",
    st_format,
    re.S,
)
require(unsized_depth is not None and
        "PIPE_FORMAT_Z32_FLOAT" in unsized_depth.group(1),
        "Mesa unsized depth does not have a Z32F fallback")
unsized_formats = re.findall(r"PIPE_FORMAT_[A-Z0-9_]+",
                             unsized_depth.group(1))
require(unsized_formats[-1] == "PIPE_FORMAT_Z32_FLOAT",
        "Z32F must remain the final unsized-depth fallback")
depth32 = re.search(
    r"\{\s*\{ GL_DEPTH_COMPONENT32, 0 \},\s*\{(.*?)\}\s*\},",
    st_format,
    re.S,
)
require(depth32 is not None and
        "PIPE_FORMAT_Z32_FLOAT" not in depth32.group(1),
        "Z32F must not masquerade as explicit fixed-point depth32")
require("{ PIPE_FORMAT_Z32_FLOAT," in st_extensions and
        "PIPE_FORMAT_Z32_FLOAT_S8X24_UINT }" in st_extensions and
        "PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_SAMPLER_VIEW" in st_extensions,
        "ARB_depth_buffer_float completeness gate changed")
require("src/mesa/state_tracker/st_format.c" in tracked_patch and
        "PIPE_FORMAT_S8_UINT_Z24_UNORM, PIPE_FORMAT_Z32_FLOAT, 0" in
        tracked_patch,
        "tracked Mesa patch lacks the unsized-depth Z32F fallback")
require("_mesa_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT," in
        probe and "GL_DEPTH_ATTACHMENT" in probe,
        "public unsized depth renderbuffer attachment is missing")
require("depth_internal_format == GL_DEPTH_COMPONENT" in probe and
        "depth_size == 32" in probe and "!depth_float_extension" in probe and
        "arb-depth-float=%u" in probe,
        "public format/size or extension-overclaim guard is missing")
require(probe.count("_mesa_Clear(GL_DEPTH_BUFFER_BIT);") == 2,
        "the discriminator must issue exactly two public depth clears")
require("_mesa_Enable(GL_DEPTH_TEST);" in probe and
        "_mesa_DepthFunc(GL_LESS);" in probe and
        "_mesa_DepthMask(GL_FALSE);" in probe,
        "public depth compare/write-mask sequence is incomplete")
require(probe.count("_mesa_ReadPixels(112, 60, 264, 150") == 3,
        "two depth plus one candidate stencil readback are required")

baseline = float_array(probe, "index_u16_vertices")
near = float_array(probe, "depth_near_vertices")
far = float_array(probe, "depth_far_vertices")
require(len(baseline) == len(near) == len(far) == 12,
        "depth discriminator must use three vec4 vertices")
for offset in range(0, 12, 4):
    require(near[offset] == baseline[offset], "depth triangle X changed")
    require(abs((near[offset + 1] - baseline[offset + 1]) + 1.5) < 1e-6,
            "depth triangle is not the exact -810-pixel Y translation")
    require(far[offset:offset + 2] == near[offset:offset + 2],
            "near/far geometry differs")
    require(near[offset + 2:offset + 4] == [0.25, 0.25],
            "near phase must sample uniform red")
    require(far[offset + 2:offset + 4] == [0.75, 0.25],
            "far phase must sample uniform green")

require("depth_near_draw_calls == 11" in probe and
        "depth_far_draw_calls == 12" in probe and
        "depth_mask_near_draw_calls == 13" in probe and
        "depth_mask_far_draw_calls == 14" in probe,
        "depth draw order/count changed")
require("depth_write_nonzero == 16320" in probe and
        "depth_mask_nonzero == 16320" in probe,
        "translated known-good coverage oracle missing")
require("0x21c32545" in probe and "0x29ca9ec5" in probe,
        "frozen red/green crop hashes changed")
require("draw_calls == 15" in probe,
        "final cumulative draw count changed")

runtime_result = re.search(
    r"#ifdef AGC_RUNTIME_PACKAGES\s*/\*.*?\*/\s*"
    r"result = (.*?);\s*#else\s*#ifdef AGC_EXPECT_EMPTY_TARGET",
    native,
    re.S,
)
require(runtime_result is not None,
        "native runtime execution-status branch is missing")
runtime_expression = runtime_result.group(1)
require(all(term in runtime_expression for term in
            ("submit_rc == 0", "suspend_rc == 0", "waits < 120")),
        "native runtime status lacks submit/retirement checks")
require("nonzero" not in runtime_expression and
        "corner" not in runtime_expression,
        "native runtime status still depends on framebuffer content")
require("nonzero == 0 && corner == 0" in native and
        "nonzero != 0 && corner == 0" in native,
        "standalone native content oracles changed")

red = crop(bytes.fromhex("ff0000ff"))
green = crop(bytes.fromhex("00ff00ff"))
require(red == (16320, 0x21C32545),
        f"independent red oracle changed: {red}")
require(green == (16320, 0x29CA9EC5),
        f"independent green oracle changed: {green}")

print("public-depth-fbo: PASS api=DEPTH_COMPONENT pipe=Z32F bits=32 "
      "arb-depth-float=false clear=1.0 compare=LESS "
      "write-mask=true,false crop=112,60,264,150 coverage=16320 "
      "hashes=21c32545,29ca9ec5 draws=15 native-status=execution")
