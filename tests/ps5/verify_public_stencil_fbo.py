#!/usr/bin/env python3
"""Guard the candidate-only public GL_STENCIL_INDEX8 FBO contract."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


screen = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
probe = (ROOT / "tests/ps5/mesa_context_probe.c").read_text()
makefile = (ROOT / "tests/ps5/Makefile").read_text()
st_format = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_format.c"
).read_text()
st_extensions = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_extensions.c"
).read_text()
tracked_patch = (ROOT / "toolchain/mesa-ps5.patch").read_text()
native = (
    ROOT.parents[1] / "native_probe/vdec_hello/agc_clean_triangle.c"
).read_text()

require("#define PS5_ENABLE_PACKED_DEPTH_STENCIL 0" in screen,
        "ordinary packed-format capability gate is not off")
candidate = re.search(
    r"ps5_screen_public_stencil\.o:.*?\n\n", makefile, re.S
)
require(candidate is not None and
        "-DPS5_ENABLE_PACKED_DEPTH_STENCIL=1" in candidate.group(0) and
        "-DPS5_PUBLIC_STENCIL_TEST=1" in candidate.group(0),
        "candidate-only packed public-stencil screen build is missing")
require("mesa_context_public_stencil_probe.elf" in makefile and
        "mesa_context_public_stencil_probe.o" in makefile,
        "dedicated public-stencil executable is missing")
require("public-stencil-offscreen-color bytes=%zu buffers=1" in screen and
        "templ->bind & PIPE_BIND_DISPLAY_TARGET" in screen and
        "PS5_RENDER_TARGET_BYTES" in screen,
        "candidate does not distinguish one-buffer offscreen color storage")
require("runtime_framebuffer_size < FRAMEBUFFER_BYTES" in native and
        "framebuffer_alias_second = 1" in native and
        "framebuffer_buffer_count = 1" not in native and
        "framebuffer_alias_second ? 0 : FRAMEBUFFER_BYTES" in native and
        "framebuffer-pool bytes=%zu buffers=%d alias=%d" in native and
        "framebuffer_pool_bytes" in native,
        "native bridge does not alias two registrations over bounded offscreen storage")

stencil = re.search(
    r"\{\s*\{ GL_STENCIL_INDEX,.*?\},\s*\{(.*?)\}\s*\},",
    st_format,
    re.S,
)
require(stencil is not None, "Mesa stencil format row is missing")
stencil_formats = re.findall(r"PIPE_FORMAT_[A-Z0-9_]+", stencil.group(1))
require(stencil_formats[-1] == "PIPE_FORMAT_Z32_FLOAT_S8X24_UINT",
        "Z32F+S8 must be the final stencil-only implementation fallback")
require(stencil_formats.count("PIPE_FORMAT_Z32_FLOAT_S8X24_UINT") == 1,
        "stencil-only fallback is duplicated")
packed_depth = re.search(
    r"\{\s*\{[^}]*GL_DEPTH_STENCIL_EXT[^}]*\},\s*\{(.*?)\}\s*\},",
    st_format,
    re.S,
)
depth32f_stencil8 = re.search(
    r"\{\s*\{ GL_DEPTH32F_STENCIL8, 0 \},\s*\{(.*?)\}\s*\},",
    st_format,
    re.S,
)
require(packed_depth is not None and
        "PIPE_FORMAT_Z32_FLOAT_S8X24_UINT" not in packed_depth.group(1),
        "GL_DEPTH_STENCIL mapping changed")
require(depth32f_stencil8 is not None and
        re.findall(r"PIPE_FORMAT_[A-Z0-9_]+",
                   depth32f_stencil8.group(1)) ==
        ["PIPE_FORMAT_Z32_FLOAT_S8X24_UINT"] and
        "GL_DEPTH32F_STENCIL8" not in tracked_patch,
        "existing GL_DEPTH32F_STENCIL8 row changed")
require("PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_SAMPLER_VIEW" in st_extensions,
        "ARB_depth_buffer_float completeness gate changed")
require("PIPE_FORMAT_Z32_FLOAT_S8X24_UINT, 0" in tracked_patch,
        "tracked Mesa patch lacks the public stencil fallback")

require("_mesa_RenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8," in
        probe and "GL_STENCIL_ATTACHMENT" in probe,
        "public stencil renderbuffer request/attachment is missing")
sync = probe.index("stage=depth-detach-sync-before-stencil")
release = probe.index("stage=depth-release-before-stencil")
allocate = probe.index("_mesa_GenRenderbuffers(1, &stencil_renderbuffer)")
require("_mesa_Clear(0);" in probe and sync < release < allocate and
        "depth_renderbuffer = 0;" in probe[:allocate],
        "detached depth state/resource is not synchronized and released before stencil allocation")
require("depth_detach_sync_error == GL_NO_ERROR" in probe,
        "depth detach synchronization is not an acceptance predicate")
require("stencil_internal_format == GL_STENCIL_INDEX8" in probe and
        "stencil_size == 8" in probe and
        "stencil_setup_error == GL_NO_ERROR" in probe,
        "public stencil query/setup oracle is incomplete")
require(probe.count("_mesa_StencilFunc(") == 4,
        "prototype plus exactly three stencil-function calls are required")
for expression in (
    "_mesa_StencilFunc(GL_ALWAYS, 0x5a, UINT32_C(0xff))",
    "_mesa_StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE)",
    "_mesa_StencilFunc(GL_EQUAL, 0x5a, UINT32_C(0xff))",
    "_mesa_StencilOp(GL_KEEP, GL_KEEP, GL_KEEP)",
    "_mesa_StencilFunc(GL_EQUAL, 0x33, UINT32_C(0xff))",
):
    require(expression in probe, f"public discriminator missing: {expression}")
require("_mesa_Clear(GL_STENCIL_BUFFER_BIT)" not in probe,
        "fresh public stencil storage must not be cleared")
require("stencil_write_draw_calls == 15" in probe and
        "stencil_pass_draw_calls == 16" in probe and
        "stencil_reject_draw_calls == 17" in probe and
        "draw_status == 0 && draw_calls == 18" in probe,
        "public stencil/final draw order changed")
require("stencil_readback_nonzero == 16320" in probe and
        "stencil_readback_hash == UINT32_C(0x29ca9ec5)" in probe and
        "stencil_readback_error == GL_NO_ERROR" in probe,
        "final public green color oracle is incomplete")

require("context->draw_calls == 17" in screen and
        "ps5_record_public_stencil(depth)" in screen,
        "post-completion candidate evidence hook is missing")
require("public-stencil-resource-failure stage=depth-allocate" in screen and
        "public-stencil-resource-failure stage=depth-map" in screen and
        "public-stencil-resource-failure stage=stencil-allocate" in screen and
        "public-stencil-resource-failure stage=stencil-map" in screen and
        "[ps5-gallium] public-stencil-resource depth=" in screen,
        "candidate allocation/map evidence records are incomplete")
order = screen.index("public-stencil-allocation-order stencil-first")
stencil_allocate = screen.index(
    "&resource->stencil_direct_start);", order)
depth_allocate = screen.index("&resource->direct_start);", stencil_allocate)
require(order < stencil_allocate < depth_allocate,
        "candidate does not reserve S8 before D32F")
require("public-stencil-prior-depth-destroy format=%u" in screen and
        "record_prior_depth" in screen and
        "unmap=%08x release=%08x" in screen,
        "candidate does not prove prior public-depth resource destruction")
require("opengl33-public-stencil-depth.raw" in screen and
        "opengl33-public-stencil.raw" in screen,
        "full D32F/S8 evidence dump paths are missing")
require("depth_nonzero == 0" in screen and
        "stencil_matches == 16320" in screen and
        "stencil_unexpected == 0" in screen,
        "exact candidate plane oracles are incomplete")
require("resource->base.format != PIPE_FORMAT_Z32_FLOAT" in screen and
        "buffers != PIPE_CLEAR_DEPTH" in screen,
        "packed/stencil clear must remain outside this slice")

require("draw_status == 0 && draw_calls == 15" in probe,
        "ordinary public-depth regression draw contract changed")
require("GL_DEPTH32F_STENCIL8" not in probe and
        "_mesa_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_STENCIL" not in
        probe,
        "probe accidentally requests a public packed depth/stencil format")

print("public-stencil-fbo: PASS candidate-only api=STENCIL_INDEX8 "
      "pipe=Z32F_S8 bits=8 draws=15,16,17 final=18 "
      "dsa=00000701/30/00ffff5a,00000201/0/00ffff5a,"
      "00000201/0/00ffff33 color=16320/29ca9ec5 "
      "planes=depth-zero,stencil-16320x5a clear=unsupported")
