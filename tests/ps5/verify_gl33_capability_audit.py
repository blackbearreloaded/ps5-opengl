#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Pin the Mesa-derived PS5 GL ceiling and its non-override policy."""

import re
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VERSION = (ROOT / "third_party/mesa-26.2.0/src/mesa/main/version.c").read_text()
EXTENSIONS = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/main/extensions.c"
).read_text()
MANAGER = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_manager.c"
).read_text()
SCREEN = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
LINEAR_SAMPLED_LAYOUT = SCREEN[
    SCREEN.index("ps5_linear_sampled_layout"):
    SCREEN.index("ps5_color_render_target")
]
EGL = (ROOT / "src/egl/ps5_egl.c").read_text()
PSBC_H = (
    ROOT / "third_party/opengnm-psbc/libpsbc/psbc_compile.h"
).read_text()
PSBC_C = (
    ROOT / "third_party/opengnm-psbc/libpsbc/psbc_compile.c"
).read_text()
NGG_NIR = (
    ROOT / "third_party/opengnm-psbc/src/amd/common/nir/ac_nir_lower_ngg.c"
).read_text()
NGG_STREAMOUT = (
    ROOT / "third_party/opengnm-psbc/src/amd/common/nir/"
           "ac_nir_prerast_utils.c"
).read_text()
RADV_SHADER = (
    ROOT / "third_party/opengnm-psbc/src/amd/vulkan/radv_shader.c"
).read_text()
RADV_POSTPROCESS = (
    ROOT / "third_party/opengnm-psbc/src/amd/vulkan/"
           "radv_postprocess_nir_standalone.c"
).read_text()
ACO_INTRINSICS = (
    ROOT / "third_party/opengnm-psbc/src/amd/compiler/"
           "instruction_selection/aco_select_nir_intrinsics.cpp"
).read_text()
PACKAGE = (ROOT / "src/platform/ps5_agc_package.c").read_text()
BACKEND = (ROOT / "src/platform/ps5_agc_runtime_backend.c").read_text()
NATIVE_RUNTIME = (
    ROOT / "src/platform/ps5_agc_native_runtime.c"
).read_text()
MAKEFILE = (ROOT / "tests/ps5/Makefile").read_text()
CORE33_MK = (ROOT / "toolchain/ps5-opengl-core33.mk").read_text()
INSTALLED_MK = (
    ROOT / "toolchain/ps5-opengl-core33-installed.mk"
).read_text()
SDK_INSTALLER = (
    ROOT / "toolchain/install-ps5-opengl-core33.sh"
).read_text()
SDK_VERIFY = (ROOT / "tools/verify-installed-sdk.sh").read_text()
SDK_CONSUMERS = (ROOT / "tools/check-sdk-consumers.py").read_text()
PSBC_HOST_CONFIG = (ROOT / "toolchain/opengnm-psbc-host.mak").read_text()
PSBC_PS5_BUILD = (
    ROOT / "toolchain/build-opengnm-psbc-ps5.sh"
).read_text()
PSBC_PS5_CONFIG = (
    ROOT / "toolchain/opengnm-psbc-ps5.mak"
).read_text()
ACO_ISEL_HELPERS = (
    ROOT / "third_party/opengnm-psbc/src/amd/compiler/"
           "instruction_selection/aco_isel_helpers.cpp"
).read_text()
ACO_NIR = (
    ROOT / "third_party/opengnm-psbc/src/amd/compiler/"
           "instruction_selection/aco_select_nir.cpp"
).read_text()
MULTI_TEXTURE = (ROOT / "tests/ps5/egl_public_multi_texture.c").read_text()
MIPMAP = (ROOT / "tests/ps5/egl_public_texture_mipmap.c").read_text()
CUBE = (ROOT / "tests/ps5/egl_public_texture_cube.c").read_text()
ARRAY = (ROOT / "tests/ps5/egl_public_texture_array.c").read_text()
FRAMEBUFFER_LAYER = (
    ROOT / "tests/ps5/egl_public_core33_framebuffer_texture_layer.c"
).read_text()
RASTER_SEMANTICS = (
    ROOT / "tests/ps5/egl_public_core33_raster_semantics.c"
).read_text()
CLIP_DISTANCE = (
    ROOT / "tests/ps5/egl_public_core33_clip_distance.c"
).read_text()
DEPTH_ONLY = (
    ROOT / "tests/ps5/egl_public_core33_depth_only_fbo.c"
).read_text()
LAYERED_MIP_FBO = (
    ROOT / "tests/ps5/egl_public_core33_layered_mip_fbo.c"
).read_text()
THREED = (ROOT / "tests/ps5/egl_public_texture_3d.c").read_text()
ONED = (ROOT / "tests/ps5/egl_public_core33_texture_1d.c").read_text()
SAMPLER_BORDER = (
    ROOT / "tests/ps5/egl_public_core33_sampler_border.c"
).read_text()
RG = (ROOT / "tests/ps5/egl_public_texture_rg.c").read_text()
UBO = (ROOT / "tests/ps5/egl_public_uniform_buffer.c").read_text()
GEOMETRY_TEXTURE = (
    ROOT / "tests/ps5/egl_public_core33_geometry_texture.c"
).read_text()
UBO_VERT = (ROOT / "tests/ps5/uniform_buffer.vert").read_text()
MRT = (ROOT / "tests/ps5/egl_public_multiple_render_targets.c").read_text()
EIGHT_MRT = (
    ROOT / "tests/ps5/egl_public_core33_eight_draw_buffers.c"
).read_text()
RTT = (ROOT / "tests/ps5/egl_public_render_to_texture.c").read_text()
SCALED_LINEAR_BLIT = (
    ROOT / "tests/ps5/egl_public_core33_scaled_linear_blit.c"
).read_text()
DEPTH_STENCIL_BLIT = (
    ROOT / "tests/ps5/egl_public_core33_depth_stencil_blit.c"
).read_text()
DEPTH_TEXTURE = (ROOT / "tests/ps5/egl_public_depth_texture.c").read_text()
PACKED_FLOAT = (ROOT / "tests/ps5/egl_public_packed_float.c").read_text()
TEXTURE_INTEGER = (
    ROOT / "tests/ps5/egl_public_texture_integer.c"
).read_text()
TEXTURE_INTEGER_NARROW = (
    ROOT / "tests/ps5/egl_public_texture_integer_narrow.c"
).read_text()
RGB10_A2UI = (ROOT / "tests/ps5/egl_public_rgb10_a2ui.c").read_text()
INTEGER_VERTEX = (
    ROOT / "tests/ps5/egl_public_core33_integer_vertex.c"
).read_text()
VISIBLE_ANIMATION = (
    ROOT / "tests/ps5/egl_public_core33_visible_animation.c"
).read_text()
RECTANGLE = (ROOT / "tests/ps5/egl_public_texture_rectangle.c").read_text()
XFB = (ROOT / "tests/ps5/egl_public_transform_feedback.c").read_text()
XFB_QUERY = (
    ROOT / "tests/ps5/egl_public_transform_feedback_query.c"
).read_text()
XFB_INSTANCED = (
    ROOT / "tests/ps5/egl_public_transform_feedback_instanced.c"
).read_text()
XFB_OVERFLOW = (
    ROOT / "tests/ps5/egl_public_transform_feedback_overflow.c"
).read_text()
XFB_TOPOLOGIES = (
    ROOT / "tests/ps5/egl_public_transform_feedback_topologies.c"
).read_text()
TRIANGLE = (ROOT / "tests/ps5/egl_public_triangle.c").read_text()
BASE_VERTEX = (ROOT / "tests/ps5/egl_public_base_vertex.c").read_text()
GLSL_SUITE = (
    ROOT / "tests/ps5/egl_public_core33_glsl_suite.c"
).read_text()
MSAA4 = (ROOT / "tests/ps5/egl_public_core33_msaa4.c").read_text()
MSAA4_DEPTH = (
    ROOT / "tests/ps5/egl_public_core33_msaa4_depth.c"
).read_text()
MSAA4_DEPTH_TEXTURE = (
    ROOT / "tests/ps5/egl_public_core33_msaa4_depth_texture.c"
).read_text()
MSAA_ARRAY = (
    ROOT / "tests/ps5/egl_public_core33_msaa_array.c"
).read_text()
TEXTURE_BUFFER = (
    ROOT / "tests/ps5/egl_public_core33_texture_buffer.c"
).read_text()
MSAA_ALPHA = (
    ROOT / "tests/ps5/egl_public_core33_msaa_alpha.c"
).read_text()
SAMPLE_COVERAGE = (
    ROOT / "tests/ps5/egl_public_core33_sample_coverage.c"
).read_text()
FORMAT_MATRIX = (
    ROOT / "tests/ps5/egl_public_core33_texture_format_matrix.c"
).read_text()
RENDER_FORMAT_FLOAT = (
    ROOT / "tests/ps5/egl_public_core33_render_format_float.c"
).read_text()
RENDER_FORMAT_INTEGER = (
    ROOT / "tests/ps5/egl_public_core33_render_format_integer.c"
).read_text()
RENDER_FORMAT_BLIT = (
    ROOT / "tests/ps5/egl_public_core33_render_format_blit.c"
).read_text()
ENTRYPOINT_TEST = (
    ROOT / "tests/ps5/egl_public_core33_entrypoints.c"
).read_text()
ENTRYPOINT_NAMES = (
    ROOT / "tests/ps5/egl_public_core33_entrypoints.inc"
).read_text()
ENTRYPOINT_GENERATOR = (
    ROOT / "tools/generate-gl33-entrypoints.py"
).read_text()
BUFFER_COPY = (
    ROOT / "tests/ps5/egl_public_core33_buffer_copy.c"
).read_text()
TEXTURE_COPY = (
    ROOT / "tests/ps5/egl_public_core33_texture_copy.c"
).read_text()
LAYERED_RENDER = (
    ROOT / "tests/ps5/egl_public_core33_layered_render.c"
).read_text()
CURRENT_VERTEX_ATTRIB = (
    ROOT / "tests/ps5/egl_public_core33_current_vertex_attrib.c"
).read_text()
PIXEL_BUFFER = (
    ROOT / "tests/ps5/egl_public_core33_pixel_buffer.c"
).read_text()
DEPTH_TRANSFER = (
    ROOT / "tests/ps5/egl_public_core33_depth_transfer.c"
).read_text()
DEPTH_ARRAY = (
    ROOT / "tests/ps5/egl_public_core33_depth_array.c"
).read_text()
LAYERED_DEPTH = (
    ROOT / "tests/ps5/egl_public_core33_layered_depth.c"
).read_text()
DEPTH_TARGETS = (
    ROOT / "tests/ps5/egl_public_core33_depth_targets.c"
).read_text()
DEPTH_MIP_TARGET = (
    ROOT / "tests/ps5/egl_public_core33_depth_mip_target.c"
).read_text()
SCISSORED_CLEAR = (
    ROOT / "tests/ps5/egl_public_core33_scissored_clear.c"
).read_text()
CORE_LIMITS = (
    ROOT / "tests/ps5/egl_public_core33_limits.c"
).read_text()
CONTEXT_SHARE = (
    ROOT / "tests/ps5/egl_public_core33_context_share.c"
).read_text()
PBUFFER = (
    ROOT / "tests/ps5/egl_public_core33_pbuffer.c"
).read_text()
SURFACELESS = (
    ROOT / "tests/ps5/egl_public_core33_surfaceless.c"
).read_text()
RGTC_TRANSFER = (
    ROOT / "tests/ps5/egl_public_core33_rgtc_transfer.c"
).read_text()
DEPTH_FORMATS = (
    ROOT / "tests/ps5/egl_public_core33_depth_format_fallback.c"
).read_text()
SMOOTH_RASTER = (
    ROOT / "tests/ps5/egl_public_core33_smooth_raster.c"
).read_text()
MESA_PATCH = (ROOT / "toolchain/mesa-ps5.patch").read_text()
MESA_ST_FORMAT = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_format.c"
).read_text()
MESA_ST_TEXTURE = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_cb_texture.c"
).read_text()
MESA_ST_EXTENSIONS = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_extensions.c"
).read_text()
MESA_RENDERBUFFER = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/main/renderbuffer.c"
).read_text()
MESA_FBO = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/main/fbobject.c"
).read_text()
MESA_TEXPARAM = (
    ROOT / "third_party/mesa-26.2.0/src/mesa/main/texparam.c"
).read_text()
SWIZZLE_DERIVATION = (
    ROOT / "tests/ps5/derive_gfx10_swizzle27.py"
).read_text()
LOGIC_OP = (ROOT / "tests/ps5/egl_public_core33_logic_op.c").read_text()
DRAW_MATRIX = (
    ROOT / "tests/ps5/egl_public_core33_draw_matrix.c"
).read_text()
STATE_MATRIX = (
    ROOT / "tests/ps5/egl_public_core33_state_matrix.c"
).read_text()
PROGRAM_API = (
    ROOT / "tests/ps5/egl_public_core33_program_api.c"
).read_text()
OBJECT_API = (
    ROOT / "tests/ps5/egl_public_core33_object_api.c"
).read_text()
VERTEX_ATTRIB_API = (
    ROOT / "tests/ps5/egl_public_core33_vertex_attrib_api.c"
).read_text()
COMPRESSED_DIMENSIONS = (
    ROOT / "tests/ps5/egl_public_core33_compressed_dimensions.c"
).read_text()
AUDIT = (ROOT / "docs/development/capability-audit.md").read_text()
CTS_MAIN = (
    ROOT / "conformance/vk-gl-cts/external/openglcts/modules/ps5/glcPS5Main.cpp"
).read_text()
CTS_PLATFORM = (
    ROOT / "conformance/vk-gl-cts/framework/platform/ps5/tcuPS5Platform.cpp"
).read_text()
CTS_RUNNER = (ROOT / "tools/Run-NativeOpenGLCTS.ps1").read_text()
CTS_CAMPAIGN = (ROOT / "tools/Run-NativeOpenGLCTSCampaign.ps1").read_text()
CTS_NEGATIVE_GUARD = (
    ROOT / "conformance/vk-gl-cts/patches/0005-gl33-negative-compute-guard.patch"
).read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


render_limits = (ROOT / "src/gallium/ps5/ps5_screen.h").read_text()
require("#define PS5_MAX_RENDER_SIZE 8192u" in render_limits,
        "dynamic target and sampled-texture limits must share the 8192 bound")
for axis in ("WIDTH", "HEIGHT"):
    for target in ("COLOR", "DEPTH"):
        require(f"#define PS5_AGC_MAX_{target}_{axis} PS5_MAX_RENDER_SIZE" in BACKEND,
                "backend and public renderbuffer bounds diverged")
require("context->st->ctx->Const.MaxRenderbufferSize =" not in EGL,
        "renderbuffer limits must derive from the matching screen capability")


texture_formats_build = MAKEFILE.split("ps5_screen_texture_formats.o:", 1)[1]
texture_formats_build = texture_formats_build.split("\n\n", 1)[0]
require(all(flag in texture_formats_build for flag in (
            "PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE",
            "PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE",
            "PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE")),
        "aggregate texture build lost proven depth/shared-arena gates")

core33_build = MAKEFILE.split("ps5_screen_core33.o:", 1)[1]
core33_build = core33_build.split("\n\n", 1)[0]
require(all(flag in core33_build for flag in (
            "PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE",
            "PS5_ENABLE_PACKED_FLOAT_CANDIDATE",
            "PS5_ENABLE_RGB10_A2UI_CANDIDATE",
            "PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE",
            "PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE",
            "PS5_ENABLE_DEPTH_CLAMP_CANDIDATE",
            "PS5_ENABLE_GLSL_330_CANDIDATE",
            "PS5_ENABLE_PACKED_VERTEX_CANDIDATE",
            "PS5_ENABLE_PACKED_DEPTH_STENCIL",
            "PS5_ENABLE_PADDED_FBO_CANDIDATE",
            "PS5_ENABLE_SOFTWARE_BLIT_CANDIDATE",
            "PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE",
            "PS5_ENABLE_MSAA4_CANDIDATE",
            "PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE",
            "PS5_ENABLE_GEOMETRY_CANDIDATE",
            "PS5_ENABLE_MRT_CANDIDATE",
            "PS5_ENABLE_INDEPENDENT_BLEND_CANDIDATE",
            "PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE",
            "PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE",
            "PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE",
            "PS5_ENABLE_POINT_COORD_CANDIDATE",
        )), "Core 3.3 integration build lost a Mesa ladder predicate")
require('return "PS5 AGC";' in SCREEN and
        "caps->accelerated = 1;" in SCREEN,
        "installable Core driver lost its production renderer identity")

core33_runtime_defines = CORE33_MK.split(
    "PS5_OPENGL_CORE33_DEFINES :=", 1)[1].split(
        "PS5_OPENGL_RUNTIME_OBJECTS :=", 1)[0]
core33_test_flags = {
    word
    for word in core33_build.split()
    if word.startswith("-DPS5_")
}
core33_runtime_flags = {
    word
    for word in core33_runtime_defines.split()
    if word.startswith("-DPS5_")
}
# The legacy standalone gate reserves a fixed 1080p pool. Native scanout
# reserves the same arena after two resolution-sized buffers; test_scanout_config
# compiles all three layouts. Compare every other define, including its value.
require(core33_test_flags - {"-DPS5_RENDER_POOL_BYTES=0x4000000u"} ==
        core33_runtime_flags - {"-DPS5_RENDER_ARENA_BYTES=0x2c00000u"},
        "standalone Core 3.3 driver flags differ from reusable runtime: "
        f"test-only={sorted(core33_test_flags - core33_runtime_flags)} "
        f"runtime-only={sorted(core33_runtime_flags - core33_test_flags)}")
require("PS5_ENABLE_MSAA4_CANDIDATE" in core33_runtime_defines and
        "PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE" in core33_runtime_defines and
        "PS5_ENABLE_POINT_COORD_CANDIDATE" in core33_runtime_defines and
        "-DPS5_RENDER_POOL_BYTES=0x4000000u" in core33_test_flags and
        "-DPS5_RENDER_ARENA_BYTES=0x2c00000u" in core33_runtime_flags and
        "#define PS5_RENDER_POOL_BYTES (PS5_SCANOUT_POOL_BYTES + PS5_RENDER_ARENA_BYTES)" in SCREEN and
        "PS5_ENABLE_FAKE_SW_MSAA_CANDIDATE" not in core33_runtime_defines and
        "$(ps5_opengl_mk_self)" in CORE33_MK,
        "reusable Core 3.3 runtime lost pool capacity, native MSAA, or config invalidation")

require("PS5_OPENGL_IMPORT_STUBS" in CORE33_MK and
        "libSceAgcDriver.so" in CORE33_MK and
        "-lSceVideoOut -lkernel_web" in CORE33_MK and
        "libSceAgcDriver.so" in SDK_INSTALLER and
        "'!<thin>'" in SDK_INSTALLER and "ar -M" in SDK_INSTALLER and
        "manifest.sha256" in SDK_INSTALLER and
        "-lPS5OpenGLCore33" in INSTALLED_MK and
        "-lSceAgcDriver" in INSTALLED_MK and
        'python3 "$root/tools/check-sdk-consumers.py"' in SDK_VERIFY and
        '--sdk "$prefix" --payload-sdk "$sdk"' in SDK_VERIFY and
        '--example-dir "$root/examples/core33-triangle"' in SDK_VERIFY and
        '--registry "$root/third_party/mesa-26.2.0/src/mesa/glapi/glapi/registry/gl.xml"' in SDK_VERIFY,
        "relocatable Core 3.3 SDK lost archives, imports, or consumer proof")
require("summary['manifest'] = verify_manifest(sdk)" in SDK_CONSUMERS and
        "libraries = [sdk / 'lib' / name for name in ('libglapi_bridge.a', 'libglapi.a')]" in SDK_CONSUMERS and
        "run(['nm', '-g', '--defined-only', *libraries]" in SDK_CONSUMERS and
        "if len(commands) != 344 or commands - symbols:" in SDK_CONSUMERS and
        "run(['make', '--no-print-directory', '-f', 'Makefile.installed'" in SDK_CONSUMERS and
        "run(['pkg-config', '--cflags', 'ps5-opengl-core33']" in SDK_CONSUMERS and
        "run(['pkg-config', '--libs', 'ps5-opengl-core33']" in SDK_CONSUMERS and
        "find_package(PS5OpenGLCore33 CONFIG REQUIRED)" in SDK_CONSUMERS and
        "target_link_libraries(triangle PRIVATE PS5OpenGLCore33::OpenGL)" in SDK_CONSUMERS and
        "run(['cmake', '--build'" in SDK_CONSUMERS,
        "delegated SDK checker lost installed integrity, exports, or consumer builds")

geometry_build = MAKEFILE.split("egl_public_geometry_shader.elf:", 1)[1]
geometry_build = geometry_build.split("\n\n", 1)[0]
require("ps5_egl_core33.o" in geometry_build and
        "ps5_screen_core33.o" in geometry_build,
        "geometry proof is not linked through the Core 3.3 envelope")
geometry_minimal_build = MAKEFILE.split(
    "egl_public_core33_geometry_minimal.elf:", 1)[1]
geometry_minimal_build = geometry_minimal_build.split("\n\n", 1)[0]
require("ps5_egl_core33.o" in geometry_minimal_build and
        "ps5_screen_core33.o" in geometry_minimal_build and
        "PS5_GEOMETRY_MINIMAL_TEST" in MAKEFILE and
        "glAttachShader(program, geometry_shader)" in TRIANGLE and
        "gl_in[i].gl_Position" in TRIANGLE,
        "resource-free geometry isolation proof regressed")
require("egl_public_core33_geometry_adjacency.o:" in MAKEFILE and
        "PS5_GEOMETRY_ADJACENCY_TEST" in MAKEFILE and
        "MESA_PRIM_LINES_ADJACENCY" in SCREEN and
        "MESA_PRIM_LINE_STRIP_ADJACENCY" in SCREEN and
        "MESA_PRIM_TRIANGLES_ADJACENCY" in SCREEN and
        "MESA_PRIM_TRIANGLE_STRIP_ADJACENCY" in SCREEN and
        "geometry_primitive_type == primitive_type" in SCREEN and
        "V_008958_DI_PT_LINELIST_ADJ" in PSBC_C and
        "V_008958_DI_PT_TRISTRIP_ADJ" in PSBC_C and
        "layout(lines_adjacency) in" in TRIANGLE and
        "layout(triangles_adjacency) in" in TRIANGLE and
        "GL_LINES_ADJACENCY, GL_LINE_STRIP_ADJACENCY" in TRIANGLE and
        "GL_TRIANGLES_ADJACENCY, GL_TRIANGLE_STRIP_ADJACENCY" in TRIANGLE and
        "adjacency_draw_status == 0 && adjacency_draw_calls == 4" in TRIANGLE,
        "native geometry adjacency topology path regressed")
require("egl_public_core33_geometry_texture.o:" in MAKEFILE and
        "PS5_GEOMETRY_TEXTURE_SLOT" in SCREEN and
        "PS5_MAX_TEXTURE_UNITS + binding" in SCREEN and
        "ps5_offset_geometry_texture" in SCREEN and
        "PS5_MERGED_TEXTURE_UNITS <= PSBC_GALLIUM_UBO_BINDING_BASE" in SCREEN and
        "gs_caps->max_texture_samplers = PS5_MAX_TEXTURE_UNITS" in SCREEN and
        "gs_caps->max_sampler_views = PS5_MAX_TEXTURE_UNITS" in SCREEN and
        "GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS" in GEOMETRY_TEXTURE and
        "GL_GEOMETRY_SHADER" in GEOMETRY_TEXTURE and
        'CHECK_TEXTURES("u_texture", "u_expected")' in GEOMETRY_TEXTURE and
        "max_geometry_units >= 16" in GEOMETRY_TEXTURE and
        "ps5_egl_current_draw_status(&draw_calls)" in GEOMETRY_TEXTURE,
        "geometry texture descriptor path or public proof regressed")
require("egl_public_core33_geometry_varying.o:" in MAKEFILE and
        "PS5_GEOMETRY_VARYING_TEST" in MAKEFILE and
        "out vec4 g_color" in TRIANGLE and
        "color = g_color" in TRIANGLE and
        "gl_in[i].gl_Position.w" in TRIANGLE and
        "geometry-varying exact=rgba1011" in TRIANGLE,
        "geometry-to-fragment varying proof regressed")
require("egl_public_core33_geometry_varying_limit.o:" in MAKEFILE and
        "PS5_GEOMETRY_VARYING_LIMIT_TEST" in MAKEFILE and
        "gs_caps->max_outputs = 32" in SCREEN and
        "fs_caps->max_inputs = 32" in SCREEN and
        "out vec4 g_varyings[32]" in TRIANGLE and
        "in vec4 g_varyings[32]" in TRIANGLE and
        "geometry-varying-limit slots=32 components=128" in TRIANGLE,
        "full 128-component geometry-to-fragment interface regressed")
require("egl_public_core33_default_uniform_limit.o:" in MAKEFILE and
        "PS5_DEFAULT_UNIFORM_LIMIT_TEST" in MAKEFILE and
        "uniform vec4 vertex_uniforms[256]" in TRIANGLE and
        "uniform vec4 geometry_uniforms[256]" in TRIANGLE and
        "uniform vec4 fragment_uniforms[256]" in TRIANGLE and
        "glUniform4fv(uniform_locations[i], 256" in TRIANGLE and
        "default-uniform-limit stages=3 components=1024" in TRIANGLE,
        "full 1024-component default-uniform path regressed")


for version, predicates in {
    "ver_2_1": ("EXT_texture_sRGB",),
    "ver_3_0": (
        "MaxColorAttachments >= 4",
        "MaxSamples >= 4",
        "ARB_depth_buffer_float",
        "ARB_map_buffer_range",
        "EXT_transform_feedback",
        "NV_conditional_render",
    ),
    "ver_3_1": ("ARB_draw_instanced", "ARB_uniform_buffer_object"),
    "ver_3_2": ("GLSLVersion >= 150", "ARB_texture_multisample"),
    "ver_3_3": ("GLSLVersion >= 330", "ARB_instanced_arrays", "ARB_timer_query"),
}.items():
    require(f"const bool {version}" in VERSION, f"Mesa {version} ladder missing")
    for predicate in predicates:
        require(predicate in VERSION, f"Mesa predicate missing: {predicate}")

require("api == API_OPENGL_CORE && version < 31" in VERSION,
        "Core-below-3.1 zero rule changed")
for call in ("st_init_limits", "st_init_extensions", "_mesa_get_version"):
    require(call in MANAGER, f"state-tracker version derivation missing: {call}")

for default_extension in (
    "ARB_draw_elements_base_vertex", "ARB_explicit_attrib_location",
    "ARB_fragment_coord_conventions", "ARB_half_float_vertex",
    "ARB_map_buffer_range", "ARB_sync", "EXT_provoking_vertex",
):
    require(f"extensions->{default_extension} = GL_TRUE" in EXTENSIONS,
            f"Mesa default extension changed: {default_extension}")

for cap in (
    "caps->glsl_feature_level = PS5_ENABLE_GLSL_330_CANDIDATE ? 330 :",
    "caps->fs_coord_origin_upper_left = true",
    "caps->fs_coord_pixel_center_half_integer = true",
    "caps->fs_coord_pixel_center_integer = true",
    "caps->fs_position_is_sysval = true",
    "caps->max_viewports = 1",
    "caps->max_vertex_buffers = 16",
    "caps->primitive_restart = true",
    "caps->vs_instanceid = true",
    "caps->vertex_element_instance_divisor = true",
    "caps->gl_begin_end_buffer_size = 512 * 1024",
    "caps->min_map_buffer_alignment = 64",
):
    require(cap in SCREEN, f"audited PS5 cap changed: {cap}")
require("caps->max_render_targets = PS5_ENABLE_MRT_CANDIDATE" in SCREEN and
        "? PS5_MAX_RENDER_TARGETS : 1" in SCREEN,
        "render-target limit lost its candidate gate")
require("caps->indep_blend_enable = PS5_ENABLE_INDEPENDENT_BLEND_CANDIDATE" in SCREEN and
        "caps->indep_blend_func = PS5_ENABLE_INDEPENDENT_BLEND_CANDIDATE" in SCREEN,
        "indexed blend capability lost its candidate gate")
require("#define PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE 0" in SCREEN and
        "caps->max_dual_source_render_targets =" in SCREEN and
        "PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE ? 1 : 0" in SCREEN,
        "dual-source capability is not conservatively gated")
require("caps->glsl_feature_level_compatibility =" in SCREEN and
        "PS5_ENABLE_GLSL_330_CANDIDATE ? 330 :" in SCREEN and
        "PS5_ENABLE_GEOMETRY_CANDIDATE ? 150 : 140" in SCREEN,
        "GLSL compatibility ceiling lost its geometry gate")

for pipe_format in (
    "PIPE_FORMAT_R32_FLOAT", "PIPE_FORMAT_R32G32_FLOAT",
    "PIPE_FORMAT_R32G32B32_FLOAT", "PIPE_FORMAT_R32G32B32A32_FLOAT",
):
    require(pipe_format in SCREEN, f"PS5 float vertex format missing: {pipe_format}")
for pipe_format in (
    "PIPE_FORMAT_R32_SINT", "PIPE_FORMAT_R32G32_SINT",
    "PIPE_FORMAT_R32G32B32_SINT", "PIPE_FORMAT_R32G32B32A32_SINT",
    "PIPE_FORMAT_R32_UINT", "PIPE_FORMAT_R32G32_UINT",
    "PIPE_FORMAT_R32G32B32_UINT", "PIPE_FORMAT_R32G32B32A32_UINT",
):
    require(pipe_format in SCREEN, f"PS5 integer vertex format missing: {pipe_format}")
require("PS5_ENABLE_INTEGER_VERTEX_CANDIDATE=1" in CORE33_MK and
        "glVertexAttribIPointer(0, 2, GL_INT" in INTEGER_VERTEX and
        "glVertexAttribIPointer(1, 4, GL_UNSIGNED_INT" in INTEGER_VERTEX and
        "EXPECTED_HASH UINT32_C(0x64e31dc5)" in INTEGER_VERTEX,
        "Core 3.3 integer vertex path regressed")
require("target == PIPE_BUFFER" in SCREEN and
        "!(bindings & ~PIPE_BIND_VERTEX_BUFFER)" in SCREEN,
        "PS5 vertex format gate changed")
require("format == PIPE_FORMAT_R8G8B8A8_UNORM" in SCREEN and
        "format == PIPE_FORMAT_Z32_FLOAT" in SCREEN,
        "audited PS5 texture/depth formats changed")
for gate in ("PS5_ENABLE_TEXTURE_SNORM_CANDIDATE",
             "PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE",
             "PS5_ENABLE_SHARED_EXPONENT_CANDIDATE",
             "PS5_ENABLE_SHADER_TEXTURE_LOD_CANDIDATE",
             "PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE",
             "PS5_ENABLE_TEXTURE_SWIZZLE_CANDIDATE"):
    require(f"#define {gate} 1" in SCREEN,
            f"hardware-proven texture gate is not enabled by default: {gate}")
require("PS5_ENABLE_PADDED_FBO_CANDIDATE" in SCREEN and
        "caps->mixed_framebuffer_sizes = PS5_ENABLE_PADDED_FBO_CANDIDATE" in
        SCREEN and
        "ps5_surface_width(surface) >= framebuffer->width" in SCREEN and
        "ps5_surface_height(surface) >= framebuffer->height" in SCREEN and
        "resource->stride = PS5_RENDER_WIDTH * bytes_per_pixel" in SCREEN,
        "padded mixed-size framebuffer candidate regressed")
require("PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE 1" in SCREEN and
        "PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW" in SCREEN and
        "UINT32_C(0x91b00000)" in SCREEN and
        "ps5_agc_gate2_set_color_to_texture_barrier" in SCREEN and
        "UINT32_C(0xc0064900)" in BACKEND and
        "UINT32_C(0x0070f52d)" in BACKEND and
        "glFramebufferTexture2D" in RTT and
        'has_extension(extensions, "GL_EXT_framebuffer_object")' in RTT and
        "EXPECTED_HASH UINT32_C(0xc40abdc5)" in RTT,
        "render-to-texture candidate regressed")
require("#define PS5_ENABLE_SOFTWARE_BLIT_CANDIDATE 0" in SCREEN and
        "context->base.blit = ps5_blit" in SCREEN and
        "info->mask & PIPE_MASK_ZS" in SCREEN and
        "PIPE_MAP_READ, &src_box" in SCREEN and
        "info->scissor_enable ? PIPE_MAP_READ : 0" in SCREEN and
        '"[ps5-gallium] software-blit color=%ux%u->%dx%d filter=%u\\n"' in
        SCREEN,
        "bounded software color-blit candidate regressed")
require("egl_public_color_blit.elf" in MAKEFILE and
        "PS5_ENABLE_SOFTWARE_BLIT_CANDIDATE=1" in MAKEFILE and
        "PS5_COLOR_BLIT_TEST=1" in MAKEFILE and
        'glBlitFramebuffer(0, 0, TARGET_WIDTH, TARGET_HEIGHT,' in RTT and
        'glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);' in RTT and
        'has_extension(extensions, "GL_EXT_framebuffer_blit")' in RTT,
        "public scaled-nearest color-blit candidate regressed")
require("egl_public_core33_scaled_linear_blit.o:" in MAKEFILE and
        "PIPE_TEX_FILTER_LINEAR" in SCREEN and
        "util_format_unpack_rgba" in SCREEN and
        "util_format_pack_rgba" in SCREEN and
        "glBlitFramebuffer(0, 0, 2, 2, 0, 0, 4, 4" in
        SCALED_LINEAR_BLIT and
        "GL_COLOR_BUFFER_BIT, GL_LINEAR" in SCALED_LINEAR_BLIT and
        "memcmp(pixels, expected, sizeof(expected)) == 0" in
        SCALED_LINEAR_BLIT,
        "Core 3.3 scaled-linear RGBA8 blit route is incomplete")
require("egl_public_core33_depth_stencil_blit.o:" in MAKEFILE and
        "egl_public_core33_depth_stencil_blit_scaled.o:" in MAKEFILE and
        "PS5_DEPTH_STENCIL_SCALED_TEST=1" in MAKEFILE and
        "ps5_tiled_stencil_surface_size" in SCREEN and
        "ps5_tiled_depth_offset" in SCREEN and
        "ps5_tiled_stencil_offset" in SCREEN and
        "source->data + src_offset, sizeof(float)" in SCREEN and
        "source->stencil_data[src_offset]" in SCREEN and
        'software-blit depth-stencil=%ux%u->%dx%d mask=%x' in SCREEN and
        "GL_DEPTH32F_STENCIL8" in DEPTH_STENCIL_BLIT and
        "GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT" in
        DEPTH_STENCIL_BLIT and
        "glDepthFunc(GL_LESS)" in DEPTH_STENCIL_BLIT and
        "glStencilFunc(GL_EQUAL, 0x5a, 0xff)" in DEPTH_STENCIL_BLIT and
        "depth_hash == GREEN_HASH" in DEPTH_STENCIL_BLIT and
        "stencil_hash == MAGENTA_HASH" in DEPTH_STENCIL_BLIT and
        "SRC_WIDTH 256" in DEPTH_STENCIL_BLIT and
        "READ_WIDTH 128" in DEPTH_STENCIL_BLIT and
        "glScissor(SRC_X, SRC_Y, SRC_WIDTH / 2, SRC_HEIGHT)" in
        DEPTH_STENCIL_BLIT and
        "glStencilFunc(GL_ALWAYS, 0x2a, 0xff)" in DEPTH_STENCIL_BLIT and
        "DST_X + READ_WIDTH, DST_Y + READ_HEIGHT" in DEPTH_STENCIL_BLIT and
        "depth_hash == expected_depth_hash" in DEPTH_STENCIL_BLIT and
        "stencil_hash == expected_stencil_hash" in DEPTH_STENCIL_BLIT,
        "Core 3.3 depth/stencil blit routes are incomplete")
require("PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE 1" in SCREEN and
        "Mesa's mutable-texture proxy" in SCREEN and
        "proxy.nr_samples > 1 && proxy.nr_samples <= 4" in SCREEN and
        "proxy.nr_samples == proxy.nr_storage_samples" in SCREEN and
        "proxy.nr_samples = 4;" in SCREEN and
        "proxy.nr_storage_samples = 4;" in SCREEN and
        "proxy.last_level = 0" in SCREEN and
        "ps5_agc_gate2_set_color_target_extents" in SCREEN and
        "ps5_agc_mrt_attrib2" in BACKEND and
        "(heights[target] - 1u) | ((widths[target] - 1u) << 14)" in
        BACKEND and
        "ps5_tiled_rgba8_width(resource)" in SCREEN and
        "TARGET_WIDTH 128" in RTT and "TARGET_HEIGHT 96" in RTT and
        "UPLOAD_HASH UINT32_C(0xc38d1dc5)" in RTT,
        "dynamic color-target extent or tiled-upload support regressed")
require("templ->bind & PIPE_BIND_DISPLAY_TARGET" in SCREEN and
        "? PS5_RENDER_POOL_BYTES" in SCREEN and
        "tiled_size + PS5_RENDER_ALIGNMENT - 1u" in SCREEN and
        "~(size_t)(PS5_RENDER_ALIGNMENT - 1u)" in SCREEN and
        "PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE 1" in SCREEN and
        "ps5_render_arena_allocate" in SCREEN and
        "render_arena_bitmap" in SCREEN and
        "ps5_render_arena_allocate(ps5, resource, allocation_size" in SCREEN and
        "allocation_alignment" in SCREEN and
        "render_pool_owner" in SCREEN and
        "PS5_RENDER_ARENA_OFFSET" in SCREEN and
        "ps5_agc_gate2_set_scanout" in SCREEN and
        "scanout->data, scanout->allocation_size" in SCREEN and
        "ps5_agc_scanout_target" in BACKEND and
        "ps5_agc_gate2_set_scanout" in BACKEND and
        "sizes[0] >= PS5_AGC_FRAMEBUFFER_POOL_BYTES" in BACKEND and
        "records[color_base].value = (uint32_t)(address >> 8)" in BACKEND and
        "ps5_agc_mrt_sizes[target] < required" in BACKEND,
        "dynamic color allocation or scanout decoupling regressed")
require("PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE" in SCREEN and
        "PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE" in SCREEN and
        "PIPE_BIND_DEPTH_STENCIL | PIPE_BIND_SAMPLER_VIEW" in SCREEN and
        "UINT32_C(0x01600000)" in SCREEN and
        "UINT32_C(0x91800000)" in SCREEN and
        "sampler->compare_func << 12" in SCREEN and
        "ps5_agc_gate2_set_depth_to_texture_barrier" in SCREEN and
        "ps5_agc_gate2_set_depth_target_extents" in SCREEN and
        "UINT32_C(0x0070f52b)" in BACKEND and
        "((ps5_agc_depth_height - 1u) << 16)" in BACKEND and
        "GL_ARB_depth_buffer_float" in DEPTH_TEXTURE and
        "uniform sampler2DShadow u_depth_texture" in DEPTH_TEXTURE and
        "GREEN_HASH UINT32_C(0xc38d1dc5)" in DEPTH_TEXTURE and
        "nir_tex_src_comparator" in ACO_NIR and
        "aco_opcode::image_sample_c" in ACO_NIR,
        "depth-buffer-float render/sample candidate regressed")
require("PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE" in SCREEN and
        "caps->dest_surface_srgb_control" in SCREEN and
        "PIPE_FORMAT_R8G8B8A8_SRGB" in SCREEN and
        "ps5_agc_gate2_set_color_target_info" in SCREEN and
        "UINT32_C(0x00008628)" in SCREEN and
        "number_type <= 1u || number_type == 6u" in BACKEND and
        "DISABLED_EXPECTED_HASH UINT32_C(0x06bfddc5)" in RTT and
        "ENABLED_EXPECTED_HASH UINT32_C(0xcec31dc5)" in RTT,
        "framebuffer-sRGB candidate regressed")
require("caps->texture_swizzle = PS5_ENABLE_TEXTURE_SWIZZLE_CANDIDATE" in SCREEN and
        "ps5_texture_descriptor_swizzle(view->swizzle_r" in SCREEN and
        "ps5_texture_descriptor_swizzle(view->swizzle_a" in SCREEN,
        "texture-swizzle candidate lacks cap gate or descriptor lowering")
require("PS5_MAX_TEXTURE_2D_SIZE PS5_MAX_RENDER_SIZE" in SCREEN and
        "(((texture->base.width0 - 1u) & 3u) << 30)" in SCREEN and
        "((texture->base.height0 - 1u) << 14)" in SCREEN and
        "(UINT32_C(1) << 31); /* GFX10 RESOURCE_LEVEL. */" in SCREEN and
        "texture->base.target == PIPE_TEXTURE_2D &&\n          !texture->base.last_level &&" in SCREEN and
        "descriptor[4] = pitch - 1u; /* GFX10.3 custom linear pitch. */" in SCREEN and
        "return MAX2((extent + BITFIELD_BIT(level) - 1u) >> level, 1u);" in SCREEN and
        SCREEN.count("ps5_linear_mip_storage_extent(") >= 9 and
        "row = (row + 255u) & ~(size_t)255u" in SCREEN and
        "util_format_get_nblocksy(templ->format, height)" in SCREEN and
        "(unsigned)box->x / block_width" in SCREEN and
        "(unsigned)box->y / block_height" in SCREEN and
        "for (level = templ->last_level + 1; level-- > 0;)" in SCREEN,
        "sampled texture extents or format stride regressed")
require("PIPE_TEX_WRAP_REPEAT: *clamp = 0" in SCREEN and
        "PIPE_TEX_WRAP_MIRROR_REPEAT: *clamp = 1" in SCREEN and
        "PIPE_TEX_WRAP_CLAMP_TO_EDGE: *clamp = 2" in SCREEN and
        "PIPE_TEX_FILTER_LINEAR: *native = 1" in SCREEN and
        "(filter[1] << 20) | (filter[0] << 22)" in SCREEN,
        "sampled texture wrap/filter descriptor lowering regressed")
require("PS5_ENABLE_TEXTURE_SNORM_CANDIDATE" in SCREEN and
        "PIPE_FORMAT_R8_SNORM" in SCREEN and
        "PIPE_FORMAT_R8G8_SNORM" in SCREEN and
        "PIPE_FORMAT_R8G8B8A8_SNORM" in SCREEN and
        "*word1 = UINT32_C(0x03900000)" in SCREEN,
        "texture-SNORM candidate format route regressed")
require("PS5_ENABLE_COMPRESSED_FALLBACK_CANDIDATE" in EGL and
        "options.allow_compressed_fallback" in EGL and
        "GL_COMPRESSED_RED_RGTC1" in RG and
        "GL_COMPRESSED_SIGNED_RG_RGTC2" in RG and
        "GL_ARB_texture_compression_rgtc" in RG,
        "Mesa RGTC CPU fallback candidate regressed")
require("egl_public_core33_texture_rgtc.elf:" in MAKEFILE and
        "EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR" in RG and
        '"#version 330\\n"' in RG and "glGenVertexArrays" in RG,
        "Core 3.3 RGTC oracle is incomplete")
require("PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE" in SCREEN and
        "PIPE_FORMAT_R16G16B16A16_FLOAT" in SCREEN and
        "PIPE_FORMAT_R32G32B32A32_FLOAT" in SCREEN and
        "*word1 = UINT32_C(0x04700000)" in SCREEN and
        "*word1 = UINT32_C(0x04d00000)" in SCREEN,
        "texture-float candidate format route regressed")
require("PS5_ENABLE_SHARED_EXPONENT_CANDIDATE" in SCREEN and
        "PIPE_FORMAT_R9G9B9E5_FLOAT" in SCREEN and
        "*word1 = UINT32_C(0x08400000)" in SCREEN,
        "shared-exponent texture candidate format route regressed")
require("#define PS5_ENABLE_PACKED_FLOAT_CANDIDATE 0" in SCREEN and
        "PIPE_FORMAT_R11G11B10_FLOAT" in SCREEN and
        "UINT32_C(0x02400000)" in SCREEN and
        "UINT32_C(0x00060718)" in SCREEN and
        "PS5_ENABLE_PACKED_FLOAT_CANDIDATE=1" in MAKEFILE and
        "GL_EXT_packed_float" in PACKED_FLOAT and
        "UPLOAD_HASH UINT32_C(0x64e31dc5)" in PACKED_FLOAT and
        "RENDER_HASH UINT32_C(0xc38d1dc5)" in PACKED_FLOAT,
        "packed-float format, target, or public oracle regressed")
require("egl_public_core33_packed_float.elf:" in MAKEFILE and
        "EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR" in PACKED_FLOAT and
        '"#version 330\\n"' in PACKED_FLOAT and
        "glGenVertexArrays" in PACKED_FLOAT,
        "Core 3.3 packed-float oracle is incomplete")
require("#define PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE 1" in SCREEN and
        "PIPE_FORMAT_R32G32B32A32_UINT" in SCREEN and
        "PIPE_FORMAT_R32G32B32A32_SINT" in SCREEN and
        "UINT32_C(0x04b00000)" in SCREEN and
        "UINT32_C(0x04c00000)" in SCREEN and
        "UINT32_C(0x00070438)" in SCREEN and
        "UINT32_C(0x00070538)" in SCREEN and
        "PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE=1" in MAKEFILE and
        "GL_EXT_texture_integer" in TEXTURE_INTEGER and
        "GL_EXT_gpu_shader4" in TEXTURE_INTEGER and
        "#extension GL_EXT_gpu_shader4 : require" in TEXTURE_INTEGER and
        "varying out uvec4 frag_value" in TEXTURE_INTEGER and
        "varying out ivec4 frag_value" in TEXTURE_INTEGER and
        "texture2D(u_texture, vec2(0.5))" in TEXTURE_INTEGER and
        "uniform usampler2D u_texture" in TEXTURE_INTEGER and
        "uniform isampler2D u_texture" in TEXTURE_INTEGER and
        "UINT_HASH UINT32_C(0x64e31dc5)" in TEXTURE_INTEGER and
        "SINT_HASH UINT32_C(0xc38d1dc5)" in TEXTURE_INTEGER and
        "run_upload_case" in TEXTURE_INTEGER and
        "uint_upload_ok && sint_upload_ok" in TEXTURE_INTEGER,
        "integer-texture format, target, or public oracle regressed")
require("#define PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE 0" in SCREEN and
        "PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE=1" in MAKEFILE and
        "PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE=1" in CORE33_MK and
        all(token in TEXTURE_INTEGER_NARROW for token in (
            "GL_RGBA8UI", "GL_RGBA8I", "GL_RGBA16UI", "GL_RGBA16I",
            "UINT32_C(0x64e31dc5)", "UINT32_C(0xc38d1dc5)",
            "UINT32_C(0xf1461dc5)", "UINT32_C(0x5f3a1dc5)")) and
        "egl_public_texture_integer_narrow_rgba16i.o:" in MAKEFILE and
        "egl_public_texture_integer_narrow_uploads.o:" in MAKEFILE,
        "narrow integer texture candidate or oracle regressed")
require("#define PS5_ENABLE_RGB10_A2UI_CANDIDATE 0" in SCREEN and
        "PIPE_FORMAT_R10G10B10A2_UINT" in SCREEN and
        "UINT32_C(0x03600000)" in SCREEN and
        "UINT32_C(0x00070424)" in SCREEN and
        "PS5_ENABLE_RGB10_A2UI_CANDIDATE=1" in MAKEFILE and
        "GL_ARB_texture_rgb10_a2ui" in RGB10_A2UI and
        "#extension GL_EXT_gpu_shader4 : require" in RGB10_A2UI and
        "varying out uvec4 frag_value" in RGB10_A2UI and
        "texture2D(u_texture, vec2(0.5))" in RGB10_A2UI and
        "uniform usampler2D u_texture" in RGB10_A2UI and
        "uvec4(1023u, 341u, 17u, 2u)" in RGB10_A2UI and
        "EXPECTED_HASH UINT32_C(0xf1461dc5)" in RGB10_A2UI,
        "RGB10_A2UI format, target, or public oracle regressed")
require("egl_public_core33_rgb10_a2ui.elf:" in MAKEFILE and
        "EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR" in RGB10_A2UI and
        '"#version 330\\n"' in RGB10_A2UI and
        "glGenVertexArrays" in RGB10_A2UI,
        "Core 3.3 RGB10_A2UI oracle is incomplete")
require("#define PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE 0" in SCREEN and
        "target == PIPE_TEXTURE_RECT" in SCREEN and
        "PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE=1" in MAKEFILE and
        "GL_ARB_texture_rectangle" in RECTANGLE and
        "uniform sampler2DRect u_texture" in RECTANGLE and
        "texture2DRect(u_texture, vec2(16.5, 0.5))" in RECTANGLE and
        "EXPECTED_HASH UINT32_C(0x64e31dc5)" in RECTANGLE,
        "texture-rectangle resource or public oracle regressed")
require("egl_public_core33_texture_rectangle.elf:" in MAKEFILE and
        "EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR" in RECTANGLE and
        '"#version 330\\n"' in RECTANGLE and
        "glGenVertexArrays" in RECTANGLE,
        "Core 3.3 texture-rectangle oracle is incomplete")
require("egl_public_core33_depth_texture.elf:" in MAKEFILE and
        "EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR" in DEPTH_TEXTURE and
        '"#version 330\\n"' in DEPTH_TEXTURE and
        "glGenVertexArrays" in DEPTH_TEXTURE and
        'has_core_extension("GL_ARB_depth_buffer_float")' in DEPTH_TEXTURE,
        "Core 3.3 D32F raw/shadow oracle is incomplete")
require("caps->fragment_shader_texture_lod = "
        "PS5_ENABLE_SHADER_TEXTURE_LOD_CANDIDATE" in SCREEN,
        "shader texture-LOD candidate cap gate regressed")
require("PS5_MAX_TEXTURE_UNITS 16u" in SCREEN and
        "samplers[PS5_TEXTURE_STAGE_COUNT][PS5_MAX_TEXTURE_UNITS]" in SCREEN and
        "*sampler_views[PS5_TEXTURE_STAGE_COUNT][PS5_MAX_TEXTURE_UNITS]" in SCREEN and
        "!ps5_sampled_texture_target(texture->target)" in SCREEN and
        "texture->target != PIPE_TEXTURE_2D" not in SCREEN and
        "descriptor->binding = binding" in SCREEN and
        "binding->offset + binding->stride > table->size" in SCREEN and
        "context->sampler_views[slot][start + index]" in SCREEN and
        "context->samplers[slot][start + index]" in SCREEN and
        "ps5_prepare_texture(context, context->vs, 0" in SCREEN,
        "multi-texture descriptor or state-slot route regressed")
require("uniform sampler2D u_first" in MULTI_TEXTURE and
        "uniform sampler2D u_second" in MULTI_TEXTURE and
        "draw_oracle(first, second, 0, 1" in MULTI_TEXTURE and
        "draw_oracle(first, second, 1, 0" in MULTI_TEXTURE,
        "public multi-texture discriminator regressed")
require("egl_public_core33_sampler_array.o:" in MAKEFILE and
        "uniform sampler2D u_textures[2]" in MULTI_TEXTURE and
        "texture(u_textures[0]" in MULTI_TEXTURE and
        "texture(u_textures[1]" in MULTI_TEXTURE and
        'glGetUniformLocation(program, "u_textures[0]")' in MULTI_TEXTURE and
        'glGetUniformLocation(program, "u_textures[1]")' in MULTI_TEXTURE,
        "Core 3.3 constant sampler-array route regressed")
require("PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE" in SCREEN and
        "(view->u.tex.first_level << 12)" in SCREEN and
        "(view->u.tex.last_level << 16)" in SCREEN and
        "((multisampled ? 2u : texture->base.last_level) << 4)" in SCREEN and
        "(mip_filter << 26)" in SCREEN and
        "descriptor[9] = min_lod | (max_lod << 12)" in SCREEN and
        "descriptor[10] = lod_bias |" in SCREEN and
        "PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE ? 16.0f : 0.0f" in SCREEN and
        "context->base.generate_mipmap = ps5_generate_mipmap" in SCREEN and
        "caps->generate_mipmap = true" in SCREEN and
        "ps5_linear_sampled_layout" in SCREEN and
        "(size_t)box->width * format_size" in SCREEN,
        "mipmap descriptor, layout, or upload route regressed")
require("GL_NEAREST_MIPMAP_NEAREST" in MIPMAP and
        "glTexImage2D(GL_TEXTURE_2D, 1" in MIPMAP and
        "texture2DLod(u_texture, vec2(0.5), u_lod)" in MIPMAP and
        "GL_TEXTURE_MIN_LOD, 1.0f" in MIPMAP and
        "GL_TEXTURE_MAX_LOD, 1.0f" in MIPMAP and
        "glGenerateMipmap(GL_TEXTURE_2D)" in MIPMAP and
        "GENERATED_HASH UINT32_C(0xdac31dc5)" in MIPMAP and
        "LEVEL0_HASH UINT32_C(0xab831dc5)" in MIPMAP and
        "LEVEL1_HASH UINT32_C(0xcec31dc5)" in MIPMAP,
        "public mipmap discriminator regressed")
require("PS5_ENABLE_TEXTURE_CUBE_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_TEXTURE_CUBE_CANDIDATE 1" in SCREEN and
        "PS5_MAX_TEXTURE_CUBE_LEVELS 12u" in SCREEN and
        "target == PIPE_TEXTURE_CUBE" in SCREEN and
        "ps5_mutable_sampled_resource_bind" in SCREEN and
        "resource->target != PIPE_TEXTURE_2D" in SCREEN and
        "proxy.bind = PIPE_BIND_SAMPLER_VIEW" in SCREEN and
        "UINT32_C(0xb0000000)" in SCREEN and
        "texture->base.target == PIPE_TEXTURE_CUBE" in SCREEN and
        "? 0" in SCREEN and
        "resource->layer_stride" in SCREEN and
        "(size_t)(unsigned)box->z * resource->layer_stride" in SCREEN,
        "cube resource, descriptor, or transfer route regressed")
require("uniform samplerCube u_cube" in CUBE and
        "textureCube(u_cube, u_direction)" in CUBE and
        "GL_TEXTURE_CUBE_MAP_POSITIVE_X" in CUBE and
        "GL_TEXTURE_CUBE_MAP_NEGATIVE_Z" in CUBE and
        "UINT32_C(0x0ec31dc5)" in CUBE and
        "UINT32_C(0x3ec31dc5)" in CUBE,
        "public cube-face discriminator regressed")
require("PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE" in SCREEN and
        "caps->seamless_cube_map = PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE" in SCREEN and
        "caps->seamless_cube_map_per_texture" in SCREEN and
        "!sampler->seamless_cube_map) << 28" in SCREEN and
        "glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS)" in CUBE and
        "glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS)" in CUBE and
        "glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS)" in CUBE and
        "SEAM_PIXEL UINT32_C(0xff400040)" in CUBE and
        "SEAM_HASH UINT32_C(0xdec31dc5)" in CUBE,
        "seamless cube-map candidate regressed")
require("PS5_ENABLE_TEXTURE_ARRAY_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_TEXTURE_ARRAY_CANDIDATE 1" in SCREEN and
        "PS5_MAX_TEXTURE_ARRAY_LAYERS 256u" in SCREEN and
        "target == PIPE_TEXTURE_2D_ARRAY" in SCREEN and
        "UINT32_C(0xd0000000)" in SCREEN and
        "caps->max_texture_array_layers" in SCREEN,
        "2D-array resource, descriptor, or cap route regressed")
require("GL_EXT_texture_array : require" in ARRAY and
        "uniform sampler2DArray u_array" in ARRAY and
        "texture2DArray(u_array, vec3(0.5, 0.5, u_layer))" in ARRAY and
        "glTexImage3D(GL_TEXTURE_2D_ARRAY_EXT" in ARRAY and
        "max_layers >= 256" in ARRAY,
        "public 2D-array discriminator regressed")
require("egl_public_core33_framebuffer_texture_layer.o:" in MAKEFILE and
        "PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE=1" in MAKEFILE and
        "PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE=1" in CORE33_MK and
        "UINT32_C(0xd1b00000)" in SCREEN and
        "surface->first_layer == surface->last_layer" in SCREEN and
        "target->data + layer_offset" in SCREEN and
        "PS5_AGC_COLOR_TARGET_ALIGNMENT" in BACKEND and
        "glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0" in
        FRAMEBUFFER_LAYER and
        "GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER" in FRAMEBUFFER_LAYER and
        "uniform sampler2DArray layers" in FRAMEBUFFER_LAYER and
        "sample_matches == READ_SIZE * READ_SIZE" in FRAMEBUFFER_LAYER,
        "Core 3.3 framebuffer texture-layer route is incomplete")
require("PS5_ENABLE_TEXTURE_3D_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_TEXTURE_3D_CANDIDATE 1" in SCREEN and
        "PS5_MAX_TEXTURE_3D_SIZE" in SCREEN and
        "PS5_MAX_TEXTURE_3D_LEVELS 9u" in SCREEN and
        "target == PIPE_TEXTURE_3D" in SCREEN and
        "UINT32_C(0xa0000000)" in SCREEN and
        "texture->base.depth0 - 1" in SCREEN and
        "caps->max_texture_3d_levels" in SCREEN,
        "3D resource, descriptor, or cap route regressed")
require("uniform sampler3D u_texture" in THREED and
        "texture3D(u_texture, vec3(0.5, 0.5, u_z))" in THREED and
        "glTexImage3D(GL_TEXTURE_3D" in THREED and
        "max_size >= 256" in THREED and
        "UINT32_C(0x0ec31dc5)" in THREED and
        "UINT32_C(0x8ec31dc5)" in THREED,
        "public 3D-texture discriminator regressed")
require("PS5_ENABLE_TEXTURE_1D_CANDIDATE" in SCREEN and
        "PS5_ENABLE_TEXTURE_1D_CANDIDATE=1" in CORE33_MK and
        "target == PIPE_TEXTURE_1D" in SCREEN and
        "target == PIPE_TEXTURE_1D_ARRAY" in SCREEN and
        "UINT32_C(0x80000000)" in SCREEN and
        "UINT32_C(0xc0000000)" in SCREEN and
        "uniform sampler1D line_texture" in ONED and
        "uniform sampler1DArray array_texture" in ONED and
        "glGenerateMipmap(GL_TEXTURE_1D_ARRAY)" in ONED and
        "EXPECTED_HASH UINT32_C(0x64e31dc5)" in ONED,
        "Core 3.3 1D texture routes regressed")
require("PS5_ENABLE_BORDER_COLOR_CANDIDATE=1" in CORE33_MK and
        "PS5_BORDER_COLOR_COUNT 4096u" in SCREEN and
        "PIPE_TEX_WRAP_CLAMP_TO_BORDER" in SCREEN and
        "descriptor[11] = sampler_state->border_color_ptr" in SCREEN and
        "ps5_agc_gate2_set_border_color_table" in SCREEN and
        "0x0020u" in BACKEND and "0x0021u" in BACKEND and
        "glSamplerParameterfv" in SAMPLER_BORDER and
        "custom=%.3f/%.3f/%.3f/%.3f" in SAMPLER_BORDER and
        "EXPECTED_HASH UINT32_C(0x64e31dc5)" in SAMPLER_BORDER,
        "Core 3.3 sampler border-color routes regressed")
require("PS5_ENABLE_UBO_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_UBO_CANDIDATE 1" in SCREEN and
        "PS5_MAX_CONSTANT_BUFFERS 13u" in SCREEN and
        "PS5_MAX_CONSTANT_BUFFER_SIZE 0x4000u" in SCREEN and
        "PS5_MAX_DEFAULT_CONSTANT_BUFFER_SIZE 0x1080u" in SCREEN and
        "vs_caps->max_const_buffer0_size =" in SCREEN and
        "fs_caps->max_const_buffer0_size =" in SCREEN and
        "caps->max_constant_buffer_size" in SCREEN and
        "caps->constant_buffer_offset_alignment" in SCREEN and
        "PSBC_GALLIUM_UBO_BINDING_BASE + binding" in SCREEN and
        "descriptor->offset = PS5_TEXTURE_DESCRIPTOR_BYTES + binding * 16u" in SCREEN and
        "context->constants[state_slot][state_binding]" in SCREEN and
        "ps5_flush_gpu_data((void *)data_address, state->size)" in SCREEN,
        "uniform-buffer limits, descriptors, or live-buffer route regressed")
require("GL_ARB_uniform_buffer_object : require" in UBO and
        "glGetUniformBlockIndex(program, \"VertexBlock\")" in UBO and
        "glGetUniformBlockIndex(program, \"FragmentBlock\")" in UBO and
        "glBindBufferRange(GL_UNIFORM_BUFFER, 3" in UBO and
        "glBindBufferRange(GL_UNIFORM_BUFFER, 5" in UBO and
        "uniform sampler2D u_texture" in UBO and
        "texture2D(u_texture, vec2(0.5))" in UBO and
        "uniform sampler2D u_vertex_texture" in UBO and
        "texture2DLod(u_vertex_texture" in UBO and
        "glBufferSubData(GL_UNIFORM_BUFFER, RANGE_OFFSET, sizeof(blue)" in UBO and
        "max_vertex_blocks >= 12" in UBO and
        "max_fragment_blocks >= 12" in UBO,
        "public uniform-buffer discriminator regressed")
require("binding = 0) uniform sampler2D source_texture" in UBO_VERT and
        "binding = 16, std140) uniform Defaults" in UBO_VERT and
        "binding = 17, std140) uniform Params" in UBO_VERT and
        "textureLod(source_texture" in UBO_VERT,
        "vertex texture/UBO compiler proof regressed")
require("PS5_ENABLE_TIMER_QUERY_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_TIMER_QUERY_CANDIDATE 1" in SCREEN and
        "caps->query_timestamp = PS5_ENABLE_TIMER_QUERY_CANDIDATE" in SCREEN and
        "caps->query_timestamp_bits = PS5_ENABLE_TIMER_QUERY_CANDIDATE ? 64 : 0" in SCREEN and
        "os_time_get_nano()" in SCREEN and
        "_mesa_glapi_get_proc_address(name)" in EGL and
        "GL_ARB_timer_query" in UBO and
        "glBeginQuery(GL_TIME_ELAPSED" in UBO and
        "eglGetProcAddress(\"glQueryCounter\")" in UBO and
        "eglGetProcAddress(\"glGetQueryObjectui64v\")" in UBO,
        "timer-query candidate regressed")
require("PS5_ENABLE_MSAA4_CANDIDATE" in SCREEN and
        "caps->texture_multisample = PS5_ENABLE_MSAA4_CANDIDATE" in SCREEN and
        "caps->sample_shading = PS5_ENABLE_MSAA4_CANDIDATE" in SCREEN and
        "GL_ARB_texture_multisample" in UBO and
        "GL_EXT_framebuffer_multisample" in UBO and
        "max_samples >= 4" in MSAA4,
        "native 4x-MSAA candidate regressed")
require("compiler_info.spirv_caps.Geometry = true;" in PSBC_C and
        "ctx->stage == MESA_SHADER_GEOMETRY" in PSBC_C and
        "PSBC_HW_STAGE_UNKNOWN" in PSBC_C and
        "PSBC_UNRESOLVED_AGC_LINKAGE" in PSBC_C,
        "standalone geometry compiler proof is no longer marked diagnostic")
require("psbc_compile_nir_geometry_pipeline" in PSBC_H and
        "#define PSBC_MAX_DESCRIPTOR_BINDINGS 64" in PSBC_H and
        "shader_count = 2" in PSBC_C and
        "stage.info.force_indirect_descriptors = false;" in PSBC_C and
        "S_028B54_ES_EN(V_028B54_ES_STAGE_REAL)" in PSBC_C and
        "S_028B54_GS_EN(has_geometry)" in PSBC_C and
        "S_028B54_MAX_PRIMGRP_IN_WAVE(2)" in PSBC_C and
        "ctx->rinfo->is_ngg_passthrough" in PSBC_C and
        "PSBC_CX_OFFSET(R_028A6C_VGT_GS_OUT_PRIM_TYPE)" in PSBC_C and
        "ctx->rinfo->gs.output_prim == MESA_PRIM_LINE_STRIP" in PSBC_C and
        "case MESA_PRIM_LINE_STRIP:" in PSBC_C and
        "case MESA_PRIM_TRIANGLE_STRIP:" in PSBC_C and
        "S_028A6C_OUTPRIM_TYPE(output_primitive)" in PSBC_C and
        "nir_lower_gs_intrinsics_count_primitives" in PSBC_C and
        "const unsigned linked_slots = util_bitcount64(nir->info.inputs_read)" in PSBC_C and
        "previous.info.vs.num_linked_outputs = linked_slots" in PSBC_C and
        "previous.info.outputs_linked = true" in PSBC_C and
        "stage.info.gs.num_linked_inputs = linked_slots" in PSBC_C and
        "stage.info.inputs_linked = true" in PSBC_C and
        "PSBC_SH_OFFSET(R_00B21C_SPI_SHADER_PGM_RSRC3_GS)" in PSBC_C and
        "S_00B21C_CU_EN(0xffff) | S_00B21C_WAVE_LIMIT(0x3f)" in PSBC_C and
        "PSBC_SH_OFFSET(R_00B204_SPI_SHADER_PGM_RSRC4_GS)" in PSBC_C and
        "S_00B204_SPI_SHADER_LATE_ALLOC_GS_GFX10(0)" in PSBC_C and
        "info->gs.has_pipeline_stat_query" in RADV_SHADER and
        "options.has_ms_gs_invocations_query =" in RADV_SHADER and
        "PS5_ENABLE_GEOMETRY_CANDIDATE" in SCREEN and
        "ps5_select_geometry_pipeline" in SCREEN and
        "*itemsize = 1u;" in SCREEN and
        "scaled once by VGT and again by the generated shader" in SCREEN and
        "ps5_agc_gate2_set_ngg_control" in SCREEN and
        "UINT32_C(0x000007fe)" in SCREEN and
        "runtime_ngg_ge_pc_alloc_valid" in NATIVE_RUNTIME and
        "0x0260, 0, runtime_ngg_ge_pc_alloc" in NATIVE_RUNTIME and
        'resource-prepare reject=vertex-constants' in SCREEN and
        'resource-prepare reject=vertex-textures' in SCREEN and
        'resource-prepare reject=fragment-constants' in SCREEN and
        'resource-prepare reject=fragment-textures' in SCREEN and
        'geometry-select reject=vertex-options' in SCREEN and
        'geometry-select reject=descriptors' in SCREEN and
        'geometry-select reject=ubo-remap' in SCREEN and
        "context->base.create_gs_state = ps5_create_gs_state" in SCREEN and
        'create-geometry state=%s' in SCREEN and
        'bind-geometry state=%s' in SCREEN and
        "caps->max_geometry_output_vertices" in SCREEN and
        "metadata->source_stage == PSBC_STAGE_GEOMETRY" in PACKAGE and
        "linkage_primitive_override = find_register(" in PACKAGE and
        "UINT16_C(0x29b)" in PACKAGE and
        "write_register(header, linkage_at + 32u" in PACKAGE and
        "PS5_GEOMETRY_TEST" in UBO and
        "GL_GEOMETRY_SHADER" in UBO and
        "GeometryBlock" in UBO and
        "GL_MAX_GEOMETRY_UNIFORM_BLOCKS" in UBO and
        "EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR" in UBO and
        '"#version 330 core\\n"' in UBO and
        "glBindVertexArray(vao)" in UBO and
        "ps5_egl_current_draw_status(&draw_calls)" in UBO and
        'draw-status=%d calls=%u expected=%u' in UBO and
        "PS5_GEOMETRY_CONSTANT_SLOT" in SCREEN and
        "ps5_offset_ubo_index" in SCREEN and
        "state->source_count != 1" in SCREEN and
        "nir_imm_int(builder, state->first)" in SCREEN and
        "ps5_append_ubo_descriptors(&options, vertex_ubos, geometry_ubos)" in SCREEN and
        "case MESA_SHADER_GEOMETRY:" in SCREEN and
        "gs_caps->max_const_buffers = 0" not in SCREEN,
        "merged NGG geometry candidate regressed")
require("-DHAVE_FUNC_ATTRIBUTE_PACKED=1" in PSBC_HOST_CONFIG,
        "host PSBC build lost the shared packed-NIR ABI")
require('--verify-psbc' in PSBC_PS5_BUILD and
        json.loads((ROOT / 'dependencies.json').read_text())['psbc_patch']['patched_tree'] ==
        '595822fc23fd4895e25f83f65acb40d615d813b0',
        "PS5 compiler archive is not pinned to the expected source tree")
require("-DOPENGNM_PSBC_ORBIS=1" in PSBC_PS5_CONFIG and
        "defined(OPENGNM_PSBC_ORBIS)" in ACO_ISEL_HELPERS and
        "nir_print_instr(instr, stderr);" in ACO_ISEL_HELPERS,
        "PS5 ACO diagnostics can regress to the unsafe memstream route")
require("CXXFLAGS =" in PSBC_PS5_CONFIG and
        "CXXFLAGS = -std=c++17 -O2 -g -Wall -fPIC -DNDEBUG" in
        PSBC_PS5_CONFIG,
        "target PSBC archive can regress to per-compile ACO validation")
require("NIR_PASS(_, nir, nir_normalize_sin_cos);" in PSBC_C and
        "nir_fcos(&b, vertex_float)" in
        (ROOT / "tests/ps5/psbc_nir_runtime.c").read_text() and
        "nir_fsin(&b, vertex_float)" in
        (ROOT / "tests/ps5/psbc_nir_runtime.c").read_text(),
        "Gallium NIR can reach ACO with unsupported raw sin/cos operations")
require("NIR_PASS(_, nir, nir_lower_undef_to_zero, NULL);" in PSBC_C and
        ".limit = 10" in PSBC_C and
        PSBC_C.index("NIR_PASS(_, nir, nir_lower_undef_to_zero, NULL);") <
        PSBC_C.index("NIR_PASS(_, nir, nir_opt_peephole_select,") <
        PSBC_C.index("radv_optimize_nir(nir, false);"),
        "small-array if-tree optimization can discard the first dynamic write")
require("free(binary);\n    if (previous_nir)\n        ralloc_free(previous_nir);\n    ralloc_free(nir);\n    psbc_shutdown();" in PSBC_C,
        "successful compiler path leaks cloned NIR shaders")
require("UINT32_C(0x99999999)" in PSBC_C and
        "PS5_ENABLE_MRT_CANDIDATE" in SCREEN and
        "-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1" in MAKEFILE and
        "ps5_agc_gate2_set_framebuffers" in SCREEN and
        "ps5_agc_gate2_set_graphics_state_mrt" in SCREEN and
        "ps5_agc_set_cx_mrt" in BACKEND and
        "0x01e0u + target" in BACKEND and
        '"[ps5-gallium] clear-mrt-color targets=%u' in SCREEN and
        "The matching framebuffer list is installed later in the same draw" in BACKEND and
        "#define WIDTH 128" in MRT and "#define HEIGHT 96" in MRT and
        "glDrawBuffers(4, attachments)" in MRT and
        "gl_FragData[3]" in MRT and
        "draw_status == 0 && draw_calls == 1" in MRT,
        "four-target MRT candidate regressed")
require("#define PS5_MAX_RENDER_TARGETS 8u" in SCREEN and
        "#define PS5_AGC_MRT_TARGETS 8u" in BACKEND and
        "egl_public_core33_eight_draw_buffers.o:" in MAKEFILE and
        "#define TARGETS 8" in EIGHT_MRT and
        "glDrawBuffers(TARGETS, attachments)" in EIGHT_MRT and
        '"layout(location=7) out vec4 c7;\\n"' in EIGHT_MRT and
        "GL_MAX_DRAW_BUFFERS" in EIGHT_MRT and
        "GL_MAX_COLOR_ATTACHMENTS" in EIGHT_MRT and
        "UINT32_C(0xff0000ff)" in EIGHT_MRT and
        "UINT32_C(0xff000000)" in EIGHT_MRT,
        "eight-draw-buffer candidate regressed")
require("BITFIELD64_BIT(FRAG_RESULT_DUAL_SRC_BLEND)" in PSBC_C and
        "gfx_state.ps.epilog.mrt0_is_dual_src = true" in PSBC_C and
        "spi_shader_col_format = format * 0x11" in PSBC_C and
        "opts->spi_shader_col_format & 0xf : V_028714_SPI_SHADER_FP16_ABGR" in PSBC_C and
        "(opts->color_is_int8 & 1) * 3" in PSBC_C and
        "(opts->color_is_int10 & 1) * 3" in PSBC_C and
        "records, &count, 0x01d8u, 0" in BACKEND and
        "case PIPE_BLENDFACTOR_SRC1_COLOR:" in SCREEN and
        "case PIPE_BLENDFACTOR_INV_SRC1_COLOR:" in SCREEN and
        "case PIPE_BLENDFACTOR_SRC1_ALPHA:" in SCREEN and
        "case PIPE_BLENDFACTOR_INV_SRC1_ALPHA:" in SCREEN and
        "target_count != 1" in SCREEN and
        "rt->rgb_func == PIPE_BLEND_MIN" in SCREEN and
        "ps5_agc_gate2_set_dual_source_blend" in SCREEN and
        "0x01e1u, UINT32_C(1) << 30" in BACKEND and
        'glBindFragDataLocationIndexed(program, 0, 1, "secondary")' in TRIANGLE and
        "glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR)" in TRIANGLE and
        "egl_public_dual_source_blend.elf" in MAKEFILE and
        "egl_public_core33_dual_source.elf" in MAKEFILE and
        "ps5_screen_core33.o" in MAKEFILE and
        "ps5_egl_core33.o" in MAKEFILE and
        "-DPS5_ENABLE_CORE_CONTEXT_CANDIDATE=1" in MAKEFILE and
        "EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR" in TRIANGLE and
        "API_OPENGL_CORE" in EGL and
        '"[ps5-egl] versions core=%d compat=%d requested=%d.%d '
        'profile=%04x\\n"' in EGL,
        "dual-source blend candidate route is incomplete")
require("ps5_report_core_predicates" not in EGL and
        "core-predicates" not in EGL,
        "one-shot Core predicate diagnostic was not removed")

for psbc_format in (
    "PSBC_VERTEX_FORMAT_R32_FLOAT",
    "PSBC_VERTEX_FORMAT_R32G32_FLOAT",
    "PSBC_VERTEX_FORMAT_R32G32B32_FLOAT",
    "PSBC_VERTEX_FORMAT_R32G32B32A32_FLOAT",
    "PSBC_VERTEX_FORMAT_B8G8R8A8_UNORM",
    "PSBC_VERTEX_FORMAT_R10G10B10A2_UNORM",
    "PSBC_VERTEX_FORMAT_B10G10R10A2_UNORM",
    "PSBC_VERTEX_FORMAT_R10G10B10A2_SNORM",
    "PSBC_VERTEX_FORMAT_B10G10R10A2_SNORM",
    "PSBC_VERTEX_FORMAT_R10G10B10A2_USCALED",
    "PSBC_VERTEX_FORMAT_B10G10R10A2_USCALED",
    "PSBC_VERTEX_FORMAT_R10G10B10A2_SSCALED",
    "PSBC_VERTEX_FORMAT_B10G10R10A2_SSCALED",
):
    require(psbc_format in PSBC_H and psbc_format in PSBC_C,
            f"PSBC vertex ABI changed: {psbc_format}")
require("PS5_ENABLE_PACKED_VERTEX_CANDIDATE" in SCREEN and
        "ps5_packed_vertex_format" in SCREEN and
        "PIPE_FORMAT_B8G8R8A8_UNORM" in SCREEN and
        "PIPE_FORMAT_R10G10B10A2_SNORM" in SCREEN and
        "PIPE_FORMAT_B10G10R10A2_SSCALED" in SCREEN and
        "GL_INT_2_10_10_10_REV" in TRIANGLE and
        "GL_BGRA" in TRIANGLE and
        "GL_ARB_vertex_type_2_10_10_10_rev" in TRIANGLE and
        "GL_EXT_vertex_array_bgra" in TRIANGLE,
        "packed/BGRA vertex candidate route is incomplete")

require("bool                 streamout_valid;" in PSBC_H and
        "streamout_buffer_table_user_data_dword" in PSBC_H and
        "streamout_enabled_stream_buffers_mask" in PSBC_H and
        "streamout_strides_dwords[4]" in PSBC_H and
        "streamout_config_sgpr" in PSBC_H and
        "streamout_write_index_sgpr" in PSBC_H and
        "streamout_offset_sgprs[4]" in PSBC_H and
        "compiler_info.spirv_caps.TransformFeedback = true;" in PSBC_C,
        "PSBC legacy transform-feedback ABI is incomplete")
require("PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE" in SCREEN and
        "util_upload_index_buffer(base, info, &draws[0]" in SCREEN and
        "uploaded_info.has_user_indices = false" in SCREEN and
        "caps->max_stream_output_buffers =" in SCREEN and
        "ps5_create_stream_output_target" in SCREEN and
        "ps5_set_stream_output_targets" in SCREEN and
        "shader->stream_output = templ->stream_output" in SCREEN and
        "ps5_agc_package_build(&variant->streamout_output, 4" in SCREEN and
        "vertex_package = context->vs->active->streamout_package" in SCREEN and
        "ps5_stream_output_metadata_matches" in SCREEN and
        "variant->streamout_output" in SCREEN,
        "transform-feedback Gallium/compiler candidate route is incomplete")
require("PSBC_HW_STAGE_NGG" in PACKAGE and "agc_stage = 2" in PACKAGE and
        "pgm_lo = 0x0c8" in PACKAGE and "has_linkage" in PACKAGE and
        "ps5_agc_gate2_set_streamout" in SCREEN and
        "UINT32_C(0x31016fac)" in SCREEN and
        "uint32_t    primitive_type;" in PSBC_H and
        "switch (opts->primitive_type)" in PSBC_C and
        "uint32_t primitive_type;" in SCREEN and
        "variant->primitive_type == primitive_type" in SCREEN and
        "options->primitive_type = primitive_type" in SCREEN and
        "use_primitive_id_streamout" in NGG_NIR and
        "nir_def *primitive_id = nir_load_primitive_id(b);" in NGG_NIR and
        "buffer_offsets[buffer] = nir_imul(b, primitive_id, primitive_stride);" in NGG_NIR and
        "remaining = nir_isub(b, buffer_size, buffer_offsets[buffer]);" in NGG_NIR and
        "ac_nir_ngg_build_streamout_vertex" in NGG_NIR and
        "nir_scoped_memory_barrier(b, SCOPE_DEVICE, NIR_MEMORY_RELEASE" in NGG_NIR and
        "ngg_stage->info.uses_prim_id = true;" in RADV_SHADER and
        "SYSTEM_VALUE_PRIMITIVE_ID" in RADV_SHADER and
        "options.use_primitive_id_streamout = use_primitive_id_streamout;" in RADV_SHADER and
        "address = (uintptr_t)resource->data + begin;" in SCREEN and
        "descriptors[index * 4u + 2u] = (uint32_t)(end - begin)" in SCREEN and
        "offset_dwords[index] = 0;" in SCREEN and
        "instance_count != 1 ||" in SCREEN and
        "PS5_AGC_PKT3(0x50, 5)" not in BACKEND and
        "UINT32_C(0x40100000)" not in BACKEND and
        "PS5_AGC_PKT3(0x34, 4)" not in BACKEND and
        "PS5_AGC_PKT3(0x46, 0)" in BACKEND and
        "PS5_AGC_PKT3(0x3c, 5)" in BACKEND and
        "glTransformFeedbackVaryings" in XFB and
        "glBeginTransformFeedback" in XFB and
        "glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER" in XFB and
        "CAPTURE_OFFSET % sizeof(GLuint) == 0" in XFB and
        "prefix_ok && values_ok && suffix_ok" in XFB and
        "glBindVertexArray(vao)" in XFB and
        "glMapBufferRange" in XFB and
        "EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR" in XFB and
        "#version 330" in XFB and
        "ps5_egl_core33.o" in MAKEFILE and
        "-0.25f, 0.0f, 0.75f, 0.0f, 0.25f, 1.0f" in XFB and
        "draw_status == 0 && draw_calls == 1" in XFB,
        "transform-feedback package, command, or public proof route is incomplete")

require("PIPE_QUERY_PRIMITIVES_GENERATED" in SCREEN and
        "PIPE_QUERY_PRIMITIVES_EMITTED" in SCREEN and
        "active_primitives_generated_query->value +=" in SCREEN and
        "active_primitives_emitted_query->value +=" in SCREEN and
        "GL_SEPARATE_ATTRIBS" in XFB_QUERY and
        "glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 1" in XFB_QUERY and
        "glBeginQuery(GL_PRIMITIVES_GENERATED" in XFB_QUERY and
        "glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN" in XFB_QUERY and
        XFB_QUERY.count("glDrawArrays(GL_TRIANGLES, 0, 3)") == 2 and
        "query_result[0] == 2" in XFB_QUERY and
        "query_result[1] == 2" in XFB_QUERY and
        "const unsigned expected_draw_calls = 2;" in XFB_QUERY and
        "draw_calls == expected_draw_calls" in XFB_QUERY and
        "egl_public_transform_feedback_query.elf:" in MAKEFILE,
        "transform-feedback separate-buffer, append, or query proof is incomplete")

require("PS5_GEOMETRY_XFB_TEST" in XFB_QUERY and
        "GL_GEOMETRY_SHADER" in XFB_QUERY and
        "layout(triangle_strip, max_vertices = 6) out" in XFB_QUERY and
        "const unsigned expected_draw_calls = 1;" in XFB_QUERY and
        "geometry_streamout_package" in SCREEN and
        "ps5_collect_geometry_streamout" in SCREEN and
        "PS5_STREAMOUT_CONTROL_DESCRIPTOR 4u" in SCREEN and
        "options.ps5_global_streamout = true;" in SCREEN and
        "use_ps5_global_streamout" in NGG_STREAMOUT and
        "nir_global_atomic_amd(" in NGG_STREAMOUT and
        "nir_atomic_op_iadd" in NGG_STREAMOUT and
        "SCOPE_DEVICE, NIR_MEMORY_ACQ_REL" in NGG_STREAMOUT and
        "nir_var_mem_global" in NGG_STREAMOUT and
        "nir_ps5_ordered_xfb_lock_amd" not in NGG_STREAMOUT and
        "nir_ps5_ordered_xfb_unlock_amd" not in NGG_STREAMOUT and
        "nir_intrinsic_ps5_ordered_xfb_lock_amd" not in ACO_INTRINSICS and
        "nir_intrinsic_ps5_ordered_xfb_unlock_amd" not in ACO_INTRINSICS and
        "-DPS5_GEOMETRY_XFB_TEST=1" in MAKEFILE and
        "egl_public_geometry_transform_feedback.elf:" in MAKEFILE,
        "geometry transform-feedback no-GDS global-counter route is incomplete")

require("streamout-instanced-split" in SCREEN and
        "single.instance_count = 1" in SCREEN and
        "single.start_instance = info->start_instance + instance" in SCREEN and
        "ps5_stream_output_lower_instance_id" in SCREEN and
        "nir_load_base_instance(builder)" in SCREEN and
        "nir_iadd(builder, &intrinsic->def, base_instance)" in SCREEN and
        "glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 3)" in XFB_INSTANCED and
        "float(gl_InstanceID)" in XFB_INSTANCED and
        "query_result[0] == 3" in XFB_INSTANCED and
        "query_result[1] == 3" in XFB_INSTANCED and
        "draw_status == 0 && draw_calls == 3" in XFB_INSTANCED and
        "egl_public_transform_feedback_instanced.elf:" in MAKEFILE,
        "transform-feedback instancing split or public proof is incomplete")

require("capacity = (end - begin) /" in SCREEN and
        "primitives = MIN2(primitives, capacity)" in SCREEN and
        "streamout_written_vertices / vertices_per_primitive" in SCREEN and
        "CAPTURE_BYTES == 72" in XFB_OVERFLOW and
        "glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, output, CAPTURE_OFFSET," in XFB_OVERFLOW and
        "glDrawArrays(GL_TRIANGLES, 0, 9)" in XFB_OVERFLOW and
        "query_result[0] == 3" in XFB_OVERFLOW and
        "query_result[1] == 2" in XFB_OVERFLOW and
        "prefix_ok && values_ok && suffix_ok" in XFB_OVERFLOW and
        "draw_status == 0 && draw_calls == 1" in XFB_OVERFLOW and
        "egl_public_transform_feedback_overflow.elf:" in MAKEFILE,
        "transform-feedback whole-primitive overflow proof is incomplete")

require(all(mode in XFB_TOPOLOGIES for mode in (
            "GL_POINTS", "GL_LINES", "GL_LINE_STRIP", "GL_LINE_LOOP",
            "GL_TRIANGLES", "GL_TRIANGLE_STRIP", "GL_TRIANGLE_FAN")) and
        "CASE_COUNT 7" in XFB_TOPOLOGIES and
        "captured_id = float(gl_VertexID)" in XFB_TOPOLOGIES and
        "GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN" in XFB_TOPOLOGIES and
        "query_result[i][0] == cases[i].expected_primitives" in XFB_TOPOLOGIES and
        "query_result[i][1] == cases[i].expected_primitives" in XFB_TOPOLOGIES and
        "prefix_ok && values_ok && suffix_ok" in XFB_TOPOLOGIES and
        "draw_calls == CASE_COUNT" in XFB_TOPOLOGIES and
        "egl_public_transform_feedback_topologies.elf:" in MAKEFILE,
        "transform-feedback topology matrix proof is incomplete")

require("PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE" in SCREEN and
        "caps->occlusion_query = PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE" in SCREEN and
        "caps->conditional_render = PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE" in SCREEN and
        "PS5_OCCLUSION_MAX_RBS 16u" in SCREEN and
        "PS5_OCCLUSION_VALID_BIT" in SCREEN and
        "value += end - start" in SCREEN and
        "ps5_render_condition_passes" in SCREEN and
        "ps5_agc_gate2_set_occlusion_query" in SCREEN and
        "PS5_AGC_PKT3(0x46, 2)" in BACKEND and
        "UINT32_C(0x115)" in BACKEND and
        "0x0001u" in BACKEND and
        "UINT32_C(0xff000f06)" in BACKEND and
        "UINT32_C(0xff000f02)" in BACKEND and
        "glBeginQuery(GL_SAMPLES_PASSED" in TRIANGLE and
        "glBeginQuery(GL_ANY_SAMPLES_PASSED" in TRIANGLE and
        "glBeginConditionalRender(queries[1], GL_QUERY_WAIT)" in TRIANGLE and
        "GL_QUERY_WAIT_INVERTED" in TRIANGLE and
        "egl_public_core33_occlusion_query.elf:" in MAKEFILE,
        "occlusion-query or conditional-render candidate route is incomplete")

require("PS5_ENABLE_DEPTH_CLAMP_CANDIDATE" in SCREEN and
        "caps->depth_clip_disable = PS5_ENABLE_DEPTH_CLAMP_CANDIDATE" in SCREEN and
        "!context->rasterizer->depth_clip_near ? 1u << 26" in SCREEN and
        "!context->rasterizer->depth_clip_far ? 1u << 27" in SCREEN and
        "context->rasterizer->rasterizer_discard ? 1u << 22" in SCREEN and
        "ps5_agc_gate2_set_clip_control" in SCREEN and
        "0x0204u, ps5_agc_clip_control" in BACKEND and
        "glEnable(GL_DEPTH_CLAMP)" in TRIANGLE and
        "glEnable(GL_RASTERIZER_DISCARD)" in TRIANGLE and
        "glDisable(GL_DEPTH_CLAMP)" in TRIANGLE,
        "depth-clamp or rasterizer-discard candidate route is incomplete")
require("egl_public_core33_depth_clamp.elf:" in MAKEFILE and
        'has_core_extension("GL_ARB_depth_clamp")' in TRIANGLE,
        "Core 3.3 depth-clamp oracle is incomplete")

require("PS5_ENABLE_GLSL_330_CANDIDATE" in SCREEN and
        "#version 330" in TRIANGLE and
        "layout(location = 0) in vec2 a_position" in TRIANGLE and
        "layout(location = 0) out vec4 color" in TRIANGLE and
        "floatBitsToUint(1.0)" in TRIANGLE and
        "uintBitsToFloat(one)" in TRIANGLE and
        'has_core_extension("GL_ARB_explicit_attrib_location")' in TRIANGLE and
        'has_core_extension("GL_ARB_shader_bit_encoding")' in TRIANGLE and
        "GL_ARB_explicit_attrib_location" in TRIANGLE and
        "GL_ARB_shader_bit_encoding" in TRIANGLE and
        "egl_public_core33_glsl330.o:" in MAKEFILE,
        "GLSL 3.30 language candidate route is incomplete")

require("egl_public_core33_glsl_suite.o:" in MAKEFILE and
        "flat out uvec4 v_bits" in GLSL_SUITE and
        "noperspective out vec2 v_uv" in GLSL_SUITE and
        "roundEven(u_math.x)" in GLSL_SUITE and
        "dFdx(gl_FragCoord.x)" in GLSL_SUITE and
        "textureSize(u_texture, 0)" in GLSL_SUITE and
        "texelFetch(u_texture, ivec2(1, 0), 0)" in GLSL_SUITE,
        "broader GLSL 3.30 execution suite regressed")

require("egl_public_core33_raster_semantics.o:" in MAKEFILE and
        "caps->fs_face_is_integer_sysval = true" in SCREEN and
        "!state->flatshade_first ? 1u << 19" in SCREEN and
        "bool        provoking_vtx_last" in PSBC_H and
        "options.provoking_vtx_last = gfx_state->rs.provoking_vtx_last" in
        RADV_SHADER and
        "word |= BITFIELD_BIT(22)" in PSBC_C and
        "return nir_load_packed_passthrough_primitive_amd(b);" in NGG_NIR and
        "glProvokingVertex(GL_FIRST_VERTEX_CONVENTION)" in
        RASTER_SEMANTICS and
        "glProvokingVertex(GL_LAST_VERTEX_CONVENTION)" in
        RASTER_SEMANTICS and
        "fract(gl_FragCoord.x)" in RASTER_SEMANTICS and
        "coordinate_matches == PIXELS" in RASTER_SEMANTICS and
        "PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE" in SCREEN and
        "ps5_agc_gate2_set_point_line_state" in SCREEN and
        "PS5_ENABLE_POINT_COORD_CANDIDATE" in SCREEN and
        "ps5_agc_gate2_set_interp_control" in SCREEN and
        "ps5_agc_gate2_set_point_coord_input" in SCREEN and
        "runtime_point_coord_input" in NATIVE_RUNTIME and
        "UINT32_C(1) << 17" in NATIVE_RUNTIME and
        "gl_PointCoord" in RASTER_SEMANTICS and
        "gl_FrontFacing" in RASTER_SEMANTICS and
        "S_0286E0_FRONT_FACE_ALL_BITS(0)" in PSBC_C and
        "glFrontFace(winding ? GL_CW : GL_CCW)" in RASTER_SEMANTICS and
        "GL_POINT_SPRITE_COORD_ORIGIN" in RASTER_SEMANTICS and
        "glEnable(GL_PROGRAM_POINT_SIZE)" in RASTER_SEMANTICS and
        "fixed_point_pixels == 16" in RASTER_SEMANTICS and
        "line4_pixels == 192" in RASTER_SEMANTICS and
        "front_face_pixels[0] == 3456" in RASTER_SEMANTICS and
        "front_face_pixels[1] == 3456" in RASTER_SEMANTICS,
        "Core 3.3 raster semantics gate regressed")

require("info->instance_count != 1" not in SCREEN and
        "#define PSBC_SHADER_METADATA_VERSION 8u" in PSBC_H and
        "uint32_t         instance_divisor;" in PSBC_H and
        "bool                 start_instance_valid;" in PSBC_H and
        "gfx_state.vi.instance_rate_inputs" in PSBC_C and
        "gfx_state.vi.instance_rate_divisors" in PSBC_C and
        "attribute->instance_divisor = element->instance_divisor;" in SCREEN and
        "binding_records[element->vertex_buffer_index]" in SCREEN and
        "vertex_metadata->start_instance_user_data_dword" in SCREEN and
        "ps5_agc_gate2_set_instance_count(info->instance_count)" in SCREEN,
        "instanced arrays capability lacks its compiler or backend route")

require("uint32_t             clip_distance_mask;" in PSBC_H and
        "uint32_t             cull_distance_mask;" in PSBC_H and
        "metadata->clip_distance_mask = ctx->rinfo->outinfo.clip_dist_mask;"
        in PSBC_C and
        "ps5_agc_gate2_set_vs_out_control" in SCREEN and
        "0x0207u, ps5_agc_vs_out_control" in BACKEND and
        "egl_public_core33_clip_distance.o:" in MAKEFILE and
        "gl_ClipDistance[0] = position.x" in CLIP_DISTANCE and
        "gl_ClipDistance[1] = position.x" in CLIP_DISTANCE and
        "white[1] == PIXELS / 2" in CLIP_DISTANCE and
        "white[3] == PIXELS / 2" in CLIP_DISTANCE,
        "Core 3.3 dynamic clip-distance route or sparse export oracle missing")

require("bool has_depth = framebuffer && framebuffer->zsbuf.texture;" in
        SCREEN and
        "(has_color || has_depth)" in SCREEN and
        "color_target_count = MAX2(context->framebuffer.nr_cbufs, 1);" in
        SCREEN and
        "egl_public_core33_depth_only_fbo.o:" in MAKEFILE and
        "glDrawBuffer(GL_NONE)" in DEPTH_ONLY and
        "glReadBuffer(GL_NONE)" in DEPTH_ONLY and
        "depth_matches == PIXELS" in DEPTH_ONLY,
        "Core 3.3 depth-only framebuffer route or exact depth oracle missing")

require("render_staging_offset" in SCREEN and
        "PIPE_TEXTURE_2D_ARRAY" not in LINEAR_SAMPLED_LAYOUT and
        "resource->bind & PIPE_BIND_SAMPLER_VIEW" in LINEAR_SAMPLED_LAYOUT and
        "resource->target == PIPE_TEXTURE_RECT" in
        LINEAR_SAMPLED_LAYOUT and
        "ps5_stage_color_surface(surface, true)" in SCREEN and
        "ps5_stage_color_surface(surface, false)" in SCREEN and
        "surface->texture->target == PIPE_TEXTURE_CUBE" in SCREEN and
        "surface->texture->target == PIPE_TEXTURE_3D" in SCREEN and
        "egl_public_core33_layered_mip_fbo.o:" in MAKEFILE and
        "GL_TEXTURE_CUBE_MAP_POSITIVE_Y, cube, 1" in LAYERED_MIP_FBO and
        "glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0"
        in LAYERED_MIP_FBO and
        "volume, 1, 1" in LAYERED_MIP_FBO and
        "sample_matches == READ_SIZE * READ_SIZE" in LAYERED_MIP_FBO,
        "Core 3.3 cube/3D mip framebuffer route or exact oracle missing")

require('#include "util/u_draw.h"' in SCREEN and
        "util_draw_multi(base, info, drawid_offset, indirect, draws, num_draws)" in SCREEN and
        "u_trim_pipe_prim(info->mode, &draw.count)" in SCREEN and
        "egl_public_core33_multi_draw.elf" in MAKEFILE and
        "glMultiDrawArrays(GL_TRIANGLES, first, counts, 2)" in TRIANGLE and
        "glMultiDrawElements(GL_TRIANGLES, counts, GL_UNSIGNED_SHORT" in TRIANGLE and
        "draw_status == 0 && draw_calls == 4" in TRIANGLE,
        "direct Core 3.3 multi-draw lowering or public discriminator regressed")

require("egl_public_draw_range.o:" in MAKEFILE and
        "glDrawRangeElements(GL_TRIANGLES, 0, 2, 3" in BASE_VERTEX and
        "glDrawRangeElementsBaseVertex" in BASE_VERTEX and
        "range_error == GL_INVALID_VALUE" in BASE_VERTEX,
        "draw-range API or boundary discriminator regressed")

require("egl_public_core33_draw_matrix.o:" in MAKEFILE and
        "glPrimitiveRestartIndex(UINT16_MAX)" in DRAW_MATRIX and
        "glDrawElementsInstanced" in DRAW_MATRIX and
        "glDrawElementsInstancedBaseVertex" in DRAW_MATRIX and
        "glMultiDrawElementsBaseVertex" in DRAW_MATRIX and
        "instance_error == GL_INVALID_VALUE" in DRAW_MATRIX and
        "multi_error == GL_INVALID_VALUE" in DRAW_MATRIX and
        "glDrawArrays(GL_LINES, 0, 3)" in DRAW_MATRIX and
        "glDrawArrays(GL_TRIANGLES, 0, 4)" in DRAW_MATRIX and
        "draw_status == 0 && draw_calls == 7" in DRAW_MATRIX,
        "combined Core 3.3 draw-semantics matrix regressed")

require("egl_public_core33_state_matrix.o:" in MAKEFILE and
        "glBlendEquationSeparate" in STATE_MATRIX and
        "glBlendFuncSeparate" in STATE_MATRIX and
        "glStencilFuncSeparate" in STATE_MATRIX and
        "glStencilOpSeparate" in STATE_MATRIX and
        "glCullFace" in STATE_MATRIX and
        "glPolygonMode" in STATE_MATRIX and
        "glPolygonOffset" in STATE_MATRIX and
        "draw_status == 0" in STATE_MATRIX,
        "combined Core 3.3 fixed-state matrix regressed")

require("egl_public_core33_program_api.o:" in MAKEFILE and
        "glBindFragDataLocation" in PROGRAM_API and
        "glGetActiveUniformBlockiv" in PROGRAM_API and
        "glGetActiveUniformName" in PROGRAM_API and
        "glGetAttachedShaders" in PROGRAM_API and
        "glGetTransformFeedbackVarying" in PROGRAM_API and
        "glGetShaderSource" in PROGRAM_API and
        "glUniformMatrix4x3fv" in PROGRAM_API and
        "glValidateProgram" in PROGRAM_API and
        "reflection_ok" in PROGRAM_API,
        "Core 3.3 program/reflection API matrix regressed")

require("egl_public_core33_object_api.o:" in MAKEFILE and
        "context->base.get_sample_position = "
        "u_default_get_sample_position" in SCREEN and
        "screen->base.get_timestamp = ps5_get_timestamp" in SCREEN and
        "glMapBuffer" in OBJECT_API and
        "glGetBufferPointerv" in OBJECT_API and
        "glGetMultisamplefv" in OBJECT_API and
        "expected_sample_positions" in OBJECT_API and
        "glGetQueryObjectui64v" in OBJECT_API and
        "glQueryCounter" in OBJECT_API and
        "glGetSamplerParameterIuiv" in OBJECT_API and
        "glGetTexParameterIuiv" in OBJECT_API and
        "identities" in OBJECT_API and "queries_ok" in OBJECT_API,
        "Core 3.3 object/query/state API matrix regressed")

require("egl_public_core33_vertex_attrib_api.o:" in MAKEFILE and
        "glVertexAttrib1d" in VERTEX_ATTRIB_API and
        "glVertexAttrib4Nusv" in VERTEX_ATTRIB_API and
        "glVertexAttribI4usv" in VERTEX_ATTRIB_API and
        "glVertexAttribP4uiv" in VERTEX_ATTRIB_API and
        "glGetVertexAttribdv" in VERTEX_ATTRIB_API and
        "glGetVertexAttribIiv" in VERTEX_ATTRIB_API,
        "Core 3.3 generic vertex-attribute API matrix regressed")

flush_body = SCREEN.split("static void\nps5_flush(", 1)[1].split("\n}\n", 1)[0]
require(flush_body.index("ps5_draw_batch_drain();") <
        flush_body.index("fence = calloc(") and
        "egl_public_core33_sync.elf" in MAKEFILE and
        "glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)" in TRIANGLE and
        "glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1)" in TRIANGLE and
        "glWaitSync(sync, 0, GL_TIMEOUT_IGNORED)" in TRIANGLE and
        "sync_wait == GL_ALREADY_SIGNALED" in TRIANGLE and
        "sync_status == GL_SIGNALED" in TRIANGLE and
        "sync_deleted" in TRIANGLE,
        "Core 3.3 fence retirement boundary or public discriminator regressed")

require("caps->texture_multisample = PS5_ENABLE_MSAA4_CANDIDATE" in SCREEN and
        "ps5_tiled_color_msaa4_offset" in SCREEN and
        "ps5_tiled_color_msaa4_surface_size" in SCREEN and
        "ps5_resolve_color_msaa4" in SCREEN and
        "GL_MAX_INTEGER_SAMPLES" in MSAA4 and
        "glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8UI" in
        MSAA4 and
        "glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8UI" in
        MSAA4 and
        "ps5_agc_gate2_set_multisample_state" in SCREEN and
        "UINT32_C(0x00132202)" in BACKEND and
        "UINT32_C(0x00132222)" in BACKEND and
        "ps5_agc_sample_shading" in BACKEND and
        "0x0293u" in BACKEND and
        "0x01b8u" in BACKEND and
        "shader->nir->info.fs.uses_sample_shading" in SCREEN and
        "radv_nir_lower_opt_fs_frag_pos" in RADV_POSTPROCESS and
        "stage->nir->info.fs.uses_sample_shading" in RADV_POSTPROCESS and
        "UINT32_C(0x00000023)" in BACKEND and
        "UINT32_C(0x00000022)" in BACKEND and
        "UINT32_C(0x0020c002)" in BACKEND and
        "UINT32_C(0xe62a62ae)" in BACKEND and
        "egl_public_core33_msaa4.elf" in MAKEFILE and
        "egl_public_core33_msaa4_mask.elf" in MAKEFILE and
        "egl_public_core33_msaa4_texture.elf" in MAKEFILE and
        "egl_public_core33_msaa4_texture_fetch.elf" in MAKEFILE and
        "glRenderbufferStorageMultisample" in MSAA4 and
        "glTexImage2DMultisample" in MSAA4 and
        "GL_TEXTURE_FIXED_SAMPLE_LOCATIONS" in MSAA4 and
        "uniform sampler2DMS source" in MSAA4 and
        "texelFetch(source, p, 3)" in MSAA4 and
        "glBlitFramebuffer" in MSAA4 and
        "glSampleMaski(0, 1)" in MSAA4 and
        "partial >= 64" in MSAA4,
        "Core 3.3 4x color-MSAA render/resolve route is incomplete")

require("const uint16_t low_offsets[] = {0x031fu, 0x0321u};" in BACKEND and
        "ps5_agc_mrt_samples == 4 && (i == 5 || i == 6)" in BACKEND,
        "4x MSAA CMASK/FMASK addresses are not paired with their base registers")

require("tiled_depth_target = PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE &&\n"
        "                           depth_texture &&" in SCREEN,
        "multisampled color textures can be misclassified as tiled depth")

require("variant->alpha_to_one == alpha_to_one" in SCREEN and
        "nir_lower_alpha_to_one(variant_nir)" in SCREEN and
        "graphics.alpha_to_coverage" in SCREEN and
        "ps5_agc_alpha_to_coverage" in BACKEND and
        "0x02dcu" in BACKEND and
        "UINT32_C(0x00018700)" in BACKEND and
        "egl_public_core33_msaa_alpha.o:" in MAKEFILE and
        "GL_SAMPLE_ALPHA_TO_COVERAGE" in MSAA_ALPHA and
        "GL_SAMPLE_ALPHA_TO_ONE" in MSAA_ALPHA and
        "draw_calls == 6" in MSAA_ALPHA,
        "Core 3.3 multisample alpha state route is incomplete")

require("st->ctx->Multisample.SampleCoverageValue" in
        (ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_atom_msaa.c").read_text() and
        "->sample_mask = sample_mask" in SCREEN and
        "ps5_agc_sample_mask = sample_mask" in BACKEND and
        "egl_public_core33_sample_coverage.o:" in MAKEFILE and
        "glSampleCoverage(0.25f, GL_FALSE)" in SAMPLE_COVERAGE and
        "glSampleCoverage(0.25f, GL_TRUE)" in SAMPLE_COVERAGE and
        "glSampleMaski(0, 0x5u)" in SAMPLE_COVERAGE and
        "draw_calls == 5" in SAMPLE_COVERAGE,
        "Core 3.3 sample coverage/mask composition regressed")

require("PS5_ENABLE_SMOOTH_RASTER_CANDIDATE" in SCREEN and
        "nir_lower_poly_line_smooth(variant_nir, 4)" in SCREEN and
        "options.rasterization_samples = 4" in SCREEN and
        "ps5_agc_poly_line_smooth" in BACKEND and
        "UINT32_C(0x02130000)" in BACKEND and
        "0x02f7u" in BACKEND and
        "PS5_ENABLE_SMOOTH_RASTER_CANDIDATE=1" in
        (ROOT / "toolchain/ps5-opengl-core33.mk").read_text() and
        "egl_public_core33_smooth_raster.o:" in MAKEFILE and
        "GL_LINE_SMOOTH" in SMOOTH_RASTER and
        "GL_POLYGON_SMOOTH" in SMOOTH_RASTER and
        "line_smooth.partial > 0" in SMOOTH_RASTER and
        "poly_smooth.partial > 0" in SMOOTH_RASTER and
        "draw_calls == 4" in SMOOTH_RASTER,
        "Core 3.3 line/polygon smoothing route regressed")

require("PS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE" in SCREEN and
        "gfx10_format_table[format]" in SCREEN and
        "PS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE=1" in
        (ROOT / "toolchain/ps5-opengl-core33.mk").read_text() and
        "egl_public_core33_texture_format_matrix.o:" in MAKEFILE and
        "GL_RGBA16_SNORM" in FORMAT_MATRIX and
        "GL_RG32F" in FORMAT_MATRIX and
        "GL_R32UI" in FORMAT_MATRIX and
        "GL_RG32I" in FORMAT_MATRIX and
        "GL_RGB10_A2" in FORMAT_MATRIX and
        "matching == 17" in FORMAT_MATRIX,
        "Core 3.3 exact sampled-format matrix regressed")

require("PS5_ENABLE_CORE_RENDER_FORMATS_CANDIDATE" in SCREEN and
        "ac_get_cb_format(GFX10, format)" in SCREEN and
        "switch (util_format_get_blocksize(format))" in SCREEN and
        "ps5_render_target_format(surface->format)" in SCREEN and
        "PS5_ENABLE_CORE_RENDER_FORMATS_CANDIDATE=1" in CORE33_MK and
        "egl_public_core33_render_format_float.o:" in MAKEFILE and
        "GL_RGBA4" in RENDER_FORMAT_FLOAT and
        "GL_RGB5_A1" in RENDER_FORMAT_FLOAT and
        "GL_RGB565" in RENDER_FORMAT_FLOAT and
        "GL_RGBA8_SNORM" in RENDER_FORMAT_FLOAT and
        "GL_RGBA16" in RENDER_FORMAT_FLOAT and
        "GL_RGBA16F" in RENDER_FORMAT_FLOAT and
        "GL_RGBA32F" in RENDER_FORMAT_FLOAT and
        "GL_R11F_G11F_B10F" in RENDER_FORMAT_FLOAT and
        "check_pixel(clear_pixel" in RENDER_FORMAT_FLOAT and
        "check_pixel(draw_pixel" in RENDER_FORMAT_FLOAT,
        "Core 3.3 float/normalized render-format candidate regressed")

require("ps5_blit_scissor_bounds" in SCREEN and
        "dst_x < min_x || dst_x >= max_x" in SCREEN and
        "dst_y < min_y || dst_y >= max_y" in SCREEN and
        "info->sample0_only || info->scissor_enable" not in SCREEN,
        "Core 3.3 scissored framebuffer blit route regressed")

require("ps5_color_view_format_compatible" in SCREEN and
        "util_format_linear(storage) == util_format_linear(view)" in SCREEN and
        SCREEN.count("ps5_color_view_format_compatible(") >= 5 and
        "egl_public_core33_msaa4_srgb.o:" in MAKEFILE and
        "PS5_MSAA_SRGB_TEST" in
        (ROOT / "tests/ps5/egl_public_core33_msaa4.c").read_text(),
        "Core 3.3 sRGB framebuffer blit reinterpretation regressed")

require("egl_public_core33_render_format_integer.o:" in MAKEFILE and
        "GL_RGBA8UI" in RENDER_FORMAT_INTEGER and
        "GL_RGBA8I" in RENDER_FORMAT_INTEGER and
        "GL_RGBA16UI" in RENDER_FORMAT_INTEGER and
        "GL_RGBA16I" in RENDER_FORMAT_INTEGER and
        "GL_RGBA32UI" in RENDER_FORMAT_INTEGER and
        "GL_RGBA32I" in RENDER_FORMAT_INTEGER and
        "GL_RGB10_A2UI" in RENDER_FORMAT_INTEGER and
        "glClearBufferuiv" in RENDER_FORMAT_INTEGER and
        "glClearBufferiv" in RENDER_FORMAT_INTEGER and
        "GL_RGBA_INTEGER" in RENDER_FORMAT_INTEGER,
        "Core 3.3 integer render-format candidate regressed")

require("egl_public_core33_render_format_blit.o:" in MAKEFILE and
        "util_format_is_pure_uint(info->src.format)" in SCREEN and
        "util_format_is_pure_sint(info->src.format)" in SCREEN and
        "info->src.format == info->dst.format" in SCREEN and
        "GL_RGBA8_SNORM" in RENDER_FORMAT_BLIT and
        "GL_RGBA16F" in RENDER_FORMAT_BLIT and
        "GL_RGBA32F" in RENDER_FORMAT_BLIT and
        "GL_R11F_G11F_B10F" in RENDER_FORMAT_BLIT and
        "GL_COLOR_BUFFER_BIT, GL_LINEAR" in RENDER_FORMAT_BLIT and
        "conversion=R16F-RGBA32F" in RENDER_FORMAT_BLIT,
        "Core 3.3 render-format blit candidate regressed")

require("egl_public_core33_entrypoints.o:" in MAKEFILE and
        "egl_public_core33_entrypoints.inc" in MAKEFILE and
        'root.findall("feature")' in ENTRYPOINT_GENERATOR and
        'feature.get("api") != "gl"' in ENTRYPOINT_GENERATOR and
        "> (3, 3)" in ENTRYPOINT_GENERATOR and
        'requirement.get("profile") == "compatibility"' in
        ENTRYPOINT_GENERATOR and
        'removal.get("profile") != "core"' in ENTRYPOINT_GENERATOR and
        ENTRYPOINT_NAMES.count('   "gl') == 344 and
        '"glBegin",' not in ENTRYPOINT_NAMES and
        '"glActiveTexture",' in ENTRYPOINT_NAMES and
        '"glVertexAttribDivisor",' in ENTRYPOINT_NAMES and
        "eglGetProcAddress(core33_entrypoints[i])" in ENTRYPOINT_TEST and
        "glDefinitelyNotARealEntrypointPS5" in ENTRYPOINT_TEST and
        "resolved == sizeof(core33_entrypoints)" in ENTRYPOINT_TEST,
        "OpenGL 3.3 Core entry-point resolver coverage regressed")

require("util/u_surface.h" in SCREEN and
        "resource_copy_region = util_resource_copy_region" in SCREEN and
        "egl_public_core33_buffer_copy.o:" in MAKEFILE and
        "glCopyBufferSubData" in BUFFER_COPY and
        "glGetBufferParameteri64v" in BUFFER_COPY and
        "glGetBufferSubData" in BUFFER_COPY and
        "error == GL_INVALID_VALUE" in BUFFER_COPY,
        "Core buffer copy/readback fallback regressed")

require("egl_public_core33_texture_copy.o:" in MAKEFILE and
        "glCopyTexImage1D" in TEXTURE_COPY and
        "glCopyTexImage2D" in TEXTURE_COPY and
        "glCopyTexSubImage1D" in TEXTURE_COPY and
        "glCopyTexSubImage2D" in TEXTURE_COPY and
        "glCopyTexSubImage3D" in TEXTURE_COPY and
        "glTexSubImage1D" in TEXTURE_COPY and
        "glTexSubImage3D" in TEXTURE_COPY and
        "glFramebufferTexture3D" in TEXTURE_COPY and
        TEXTURE_COPY.count("glGetTexImage") == 4,
        "Core framebuffer-to-texture copy/readback matrix regressed")

require("egl_public_core33_compressed_dimensions.o:" in MAKEFILE and
        "glCompressedTexImage1D" in COMPRESSED_DIMENSIONS and
        "glCompressedTexSubImage1D" in COMPRESSED_DIMENSIONS and
        "glCompressedTexImage3D" in COMPRESSED_DIMENSIONS and
        "glCompressedTexSubImage3D" in COMPRESSED_DIMENSIONS and
        "!memcmp(read_1d, expected_1d" in COMPRESSED_DIMENSIONS and
        "!memcmp(read_3d, expected_3d" in COMPRESSED_DIMENSIONS,
        "Core RGTC 1D/3D compressed transfer matrix regressed")

require("ps5_agc_gate2_set_color_target_views" in SCREEN and
        "render_staging_size *= layers" in SCREEN and
        "layer - surface->first_layer" in SCREEN and
        "surface->last_layer > surface->first_layer" in SCREEN and
        "surface->first_layer <= surface->last_layer" in SCREEN and
        "ps5_agc_mrt_views" in BACKEND and
        "0x031bu" in BACKEND and
        "UINT32_C(0x7ff) << 13" in BACKEND and
        "egl_public_core33_layered_render.o:" in MAKEFILE and
        "glFramebufferTexture(GL_FRAMEBUFFER" in LAYERED_RENDER and
        "GL_TEXTURE_MAX_LEVEL, 1" in LAYERED_RENDER and
        "gl_Layer = layer" in LAYERED_RENDER and
        "GL_FRAMEBUFFER_ATTACHMENT_LAYERED" in LAYERED_RENDER and
        "matching[2] == READ_SIZE * READ_SIZE" in LAYERED_RENDER,
        "Core whole-layer framebuffer rendering candidate regressed")

require("element->src_stride ? vertex_count : 1u" in SCREEN and
        "ps5_vertex_buffer_descriptor(" in SCREEN and
        "(stride << 16)" in SCREEN and
        "stride ? records : (uint32_t)available" in SCREEN and
        "S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW)" in SCREEN and
        "vertex_resource->size - vertex_buffer->buffer_offset" in SCREEN and
        "egl_public_core33_current_vertex_attrib.o:" in MAKEFILE and
        "glDisableVertexAttribArray" in CURRENT_VERTEX_ATTRIB and
        "glVertexAttrib4f" in CURRENT_VERTEX_ATTRIB and
        "glVertexAttribI4ui" in CURRENT_VERTEX_ATTRIB and
        "glGetVertexAttribiv" in CURRENT_VERTEX_ATTRIB and
        "glGetVertexAttribfv" in CURRENT_VERTEX_ATTRIB and
        "glGetVertexAttribIuiv" in CURRENT_VERTEX_ATTRIB and
        "glGetVertexAttribPointerv" in CURRENT_VERTEX_ATTRIB and
        "matching == READ_SIZE * READ_SIZE" in CURRENT_VERTEX_ATTRIB,
        "Core current generic vertex-attribute path regressed")

require("egl_public_core33_pixel_buffer.o:" in MAKEFILE and
        "GL_PIXEL_UNPACK_BUFFER" in PIXEL_BUFFER and
        "GL_PIXEL_PACK_BUFFER" in PIXEL_BUFFER and
        "GL_UNPACK_ROW_LENGTH" in PIXEL_BUFFER and
        "GL_UNPACK_SKIP_PIXELS" in PIXEL_BUFFER and
        "GL_UNPACK_SKIP_ROWS" in PIXEL_BUFFER and
        "GL_PACK_ROW_LENGTH" in PIXEL_BUFFER and
        "GL_PACK_SKIP_PIXELS" in PIXEL_BUFFER and
        "GL_PACK_SKIP_ROWS" in PIXEL_BUFFER and
        "glGetTexImage" in PIXEL_BUFFER and
        "glReadPixels" in PIXEL_BUFFER and
        "glMapBufferRange" in PIXEL_BUFFER and
        "pack_matches" in PIXEL_BUFFER,
        "Core pixel pack/unpack buffer and PixelStore matrix regressed")

require("resource->base.bind & PIPE_BIND_DEPTH_STENCIL" in SCREEN and
        "ps5_tiled_depth_offset" in SCREEN and
        "ps5_tiled_stencil_offset" in SCREEN and
        "pixel[4] = resource->stencil_data[stencil_offset]" in SCREEN and
        "resource->stencil_data[stencil_offset] = pixel[4]" in SCREEN and
        "egl_public_core33_depth_transfer.o:" in MAKEFILE and
        "glTexSubImage2D" in DEPTH_TRANSFER and
        DEPTH_TRANSFER.count("glGetTexImage") == 2 and
        "GL_DEPTH_COMPONENT32F" in DEPTH_TRANSFER and
        "GL_DEPTH32F_STENCIL8" in DEPTH_TRANSFER and
        "GL_FLOAT_32_UNSIGNED_INT_24_8_REV" in DEPTH_TRANSFER and
        DEPTH_TRANSFER.count("glReadPixels") == 3 and
        "packed_stencil_ok" in DEPTH_TRANSFER,
        "Core tiled depth/stencil CPU transfer path regressed")

require("format == PIPE_FORMAT_Z32_FLOAT" in SCREEN and
        "ps5_sampled_texture_target(target)" in SCREEN and
        "!depth_texture || sampler->compare_func > PIPE_FUNC_ALWAYS" in
        SCREEN and
        "egl_public_core33_depth_array.o:" in MAKEFILE and
        DEPTH_ARRAY.count("glTexImage3D") == 2 and
        "glGenerateMipmap(GL_TEXTURE_2D_ARRAY)" in DEPTH_ARRAY and
        "glGetTexImage(GL_TEXTURE_2D_ARRAY, 1" in DEPTH_ARRAY and
        "sampler2DArrayShadow" in DEPTH_ARRAY and
        "GL_TEXTURE_COMPARE_MODE" in DEPTH_ARRAY and
        "pixel[2] == 255" in DEPTH_ARRAY,
        "Core sampled depth-array mipmap/shadow path regressed")

require("ps5_depth_render_target" in SCREEN and
        "UINT32_C(0xd1800000)" in SCREEN and
        "ps5_agc_gate2_set_depth_target_view" in SCREEN and
        "ps5_agc_gate2_set_depth_target_view" in NATIVE_RUNTIME and
        "records[11].value = runtime_depth_view" in NATIVE_RUNTIME and
        "egl_public_core33_layered_depth.o:" in MAKEFILE and
        "glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT" in
        LAYERED_DEPTH and
        "glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT" in
        LAYERED_DEPTH and
        "GL_FRAMEBUFFER_ATTACHMENT_LAYERED" in LAYERED_DEPTH and
        "gl_Layer=layer" in LAYERED_DEPTH and
        "draw_matches[2] == LAYER_PIXELS" in LAYERED_DEPTH,
        "Core layered Z32 depth-target path regressed")

require("target == PIPE_TEXTURE_1D_ARRAY" in SCREEN and
        "target == PIPE_TEXTURE_CUBE" in SCREEN and
        "target == PIPE_TEXTURE_2D_ARRAY" in SCREEN and
        "ps5_texture_level_layers" in SCREEN and
        "UINT32_C(0xb1800000)" in SCREEN and
        "UINT32_C(0xd1800000)" in SCREEN and
        "egl_public_core33_depth_targets.o:" in MAKEFILE and
        "glFramebufferTexture1D" in DEPTH_TARGETS and
        "GL_TEXTURE_CUBE_MAP_NEGATIVE_Y" in DEPTH_TARGETS and
        "glFramebufferTextureLayer" in DEPTH_TARGETS and
        "GL_TEXTURE_CUBE_MAP, GL_TEXTURE_2D_ARRAY," in DEPTH_TARGETS and
        "GL_TEXTURE_3D" not in DEPTH_TARGETS and
        "GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER" in DEPTH_TARGETS and
        "target == GL_TEXTURE_1D || target == GL_TEXTURE_1D_ARRAY" in DEPTH_TARGETS and
        "? 1 : SIZE;" in DEPTH_TARGETS and
        "glViewport(0, 0, SIZE, height)" in DEPTH_TARGETS and
        "glReadPixels(SIZE / 2, height / 2, 1, 1" in DEPTH_TARGETS and
        "layers[4] = {0, 2, 0, 2}" in DEPTH_TARGETS and
        "attached_layer == (GLint)layer" in DEPTH_TARGETS and
        "glGetTexImage(target, 0, GL_DEPTH_COMPONENT, GL_FLOAT, array_pixels)" in DEPTH_TARGETS and
        "index / SIZE == layer ? 0.5f : 0.25f" in DEPTH_TARGETS and
        "routing_ok &= array_pixels[index] == expected" in DEPTH_TARGETS and
        "routing_ok && fabsf(*depth - 0.5f)" in DEPTH_TARGETS and
        "query_discrepancy=%d" in DEPTH_TARGETS and
        "matching == 4" in DEPTH_TARGETS and
        "draw_calls == 4" in DEPTH_TARGETS,
        "Core depth-target nonzero layers, strict routing, or explicit query limitation regressed")

require("#ifdef PS5_DEPTH_TARGETS_HOST_REFERENCE" in DEPTH_TARGETS and
        '#define TAG "[host-egl-core33-depth-targets]"' in DEPTH_TARGETS and
        '#define TAG "[ps5-egl-core33-depth-targets]"' in DEPTH_TARGETS and
        "#define SURFACE_TYPE EGL_PBUFFER_BIT" in DEPTH_TARGETS and
        "#define SURFACE_TYPE EGL_WINDOW_BIT" in DEPTH_TARGETS and
        "eglCreatePbufferSurface" in DEPTH_TARGETS and
        "eglCreateWindowSurface" in DEPTH_TARGETS and
        "++host_draw_calls;" in DEPTH_TARGETS and
        "draw_counter=host-issued" in DEPTH_TARGETS,
        "Depth-target host reference lost separation from native draw-status proof")

require("ps5_depth_staging_required" in SCREEN and
        "ps5_stage_depth_surface" in SCREEN and
        "depth->depth_staging_offset" in SCREEN and
        "egl_public_core33_depth_mip_target.o:" in MAKEFILE and
        "glFramebufferTexture1D" in DEPTH_MIP_TARGET and
        "glFramebufferTexture2D" in DEPTH_MIP_TARGET and
        "glFramebufferTextureLayer" in DEPTH_MIP_TARGET and
        "glGetTexImage" in DEPTH_MIP_TARGET and
        "#define TARGET_COUNT 5" in DEPTH_MIP_TARGET and
        "rejected_3d = test_invalid_3d_depth();" in DEPTH_MIP_TARGET and
        "rejected = error == GL_INVALID_OPERATION;" in DEPTH_MIP_TARGET and
        "matching == TARGET_COUNT" in DEPTH_MIP_TARGET and
        "draw_calls == TARGET_COUNT && rejected_3d" in DEPTH_MIP_TARGET,
        "Core five legal mipmapped Z32 depth targets or mandatory invalid 3D rejection regressed")

require("egl_public_core33_scissored_clear.o:" in MAKEFILE and
        "ps5_clear_bounds" in SCREEN and
        "ps5_clear_msaa4_color" in SCREEN and
        "ps5_tiled_depth_msaa4_offset" in SCREEN and
        "ps5_tiled_stencil_msaa4_offset" in SCREEN and
        "glEnable(GL_SCISSOR_TEST)" in SCISSORED_CLEAR and
        "glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE)" in
        SCISSORED_CLEAR and
        "glStencilMask(0x0f)" in SCISSORED_CLEAR and
        "GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |" in
        SCISSORED_CLEAR and
        "stencils[index] == 0x1a" in SCISSORED_CLEAR and
        "outside_ok == SIZE * SIZE - inside_ok" in SCISSORED_CLEAR,
        "Core scissored masked color/depth/stencil clear proof regressed")

require("egl_public_core33_limits.o:" in MAKEFILE and
        'GL_MAX_CUBE_MAP_TEXTURE_SIZE, 2048, "texture-cube"' in CORE_LIMITS and
        "GL_SUBPIXEL_BITS, 4" in CORE_LIMITS and
        "GL_MAX_RENDERBUFFER_SIZE, 1024" in CORE_LIMITS and
        "GL_MAX_RECTANGLE_TEXTURE_SIZE, 1024" in CORE_LIMITS and
        "GL_MAX_VERTEX_UNIFORM_COMPONENTS, 1024" in CORE_LIMITS and
        "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, 1024" in CORE_LIMITS and
        "GL_MAX_GEOMETRY_UNIFORM_COMPONENTS, 1024" in CORE_LIMITS and
        "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS" in CORE_LIMITS and
        "GL_MAX_COMBINED_UNIFORM_BLOCKS" in CORE_LIMITS and
        "GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS, 50176" in CORE_LIMITS and
        "GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS, 50176" in CORE_LIMITS and
        "GL_MAX_COMBINED_GEOMETRY_UNIFORM_COMPONENTS, 50176" in CORE_LIMITS and
        "GL_MAX_VERTEX_OUTPUT_COMPONENTS" in CORE_LIMITS and
        "GL_MAX_FRAGMENT_INPUT_COMPONENTS, 128" in CORE_LIMITS and
        "GL_MAX_GEOMETRY_INPUT_COMPONENTS" in CORE_LIMITS and
        "GL_MAX_GEOMETRY_OUTPUT_COMPONENTS, 128" in CORE_LIMITS and
        "GL_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS" in CORE_LIMITS and
        "GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS" in CORE_LIMITS and
        "GL_MIN_PROGRAM_TEXEL_OFFSET" in CORE_LIMITS and
        "GL_MAX_PROGRAM_TEXEL_OFFSET" in CORE_LIMITS and
        "GL_MAX_SAMPLE_MASK_WORDS, 1" in CORE_LIMITS and
        "GL_MAX_COLOR_TEXTURE_SAMPLES, 4" in CORE_LIMITS and
        "GL_MAX_DEPTH_TEXTURE_SAMPLES, 4" in CORE_LIMITS and
        "GL_MAX_INTEGER_SAMPLES, 4" in CORE_LIMITS and
        "caps->fake_sw_msaa = !PS5_ENABLE_MSAA4_CANDIDATE &&" in SCREEN and
        "GL_MAX_UNIFORM_BUFFER_BINDINGS, 36" in CORE_LIMITS and
        "query_errors == 0" in CORE_LIMITS and
        "caps->rasterizer_subpixel_bits = 8" in SCREEN and
        "caps->min_texel_offset = -8" in SCREEN and
        "caps->max_texel_offset = 7" in SCREEN and
        "GL_CONTEXT_CORE_PROFILE_BIT" in CORE_LIMITS and
        'strcmp((const char *)renderer, "PS5 AGC")' in CORE_LIMITS and
        "glGetStringi(GL_EXTENSIONS, extension_count)" in CORE_LIMITS and
        "invalid_index_error == GL_INVALID_VALUE" in CORE_LIMITS,
        "Core identity, extension enumeration, or minimum-limit gate regressed")

require("context->base.set_active_query_state = ps5_set_active_query_state" in SCREEN and
        "context->queries_enabled" in SCREEN and
        "glBeginQuery(GL_SAMPLES_PASSED, queries[0])" in OBJECT_API and
        "glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE)" in OBJECT_API and
        "glClear(GL_COLOR_BUFFER_BIT)" in OBJECT_API,
        "internal draws are no longer excluded from active queries")

require("struct ps5_egl_context *next" in EGL and
        "shared ? shared->st : NULL" in EGL and
        "context->next = ps5_contexts" in EGL and
        "EGL_MAX_SWAP_INTERVAL: *value = 1" in EGL and
        "interval < 0 || interval > 1" in EGL and
        "egl_public_core33_context_share.o:" in MAKEFILE and
        "eglCreateContext(display, config, contexts[0]" in CONTEXT_SHARE and
        "glIsTexture(texture) == GL_TRUE" in CONTEXT_SHARE and
        "eglSwapInterval(display, 1)" in CONTEXT_SHARE and
        "pixel[1] == 255" in CONTEXT_SHARE,
        "Core EGL context-sharing or swap-interval candidate regressed")

require("struct ps5_egl_surface *next" in EGL and
        "EGL_WINDOW_BIT | EGL_PBUFFER_BIT" in EGL and
        "ps5_display.scanout[0]" in EGL and
        "ps5_init_surface(surface, width, height, false)" in EGL and
        "if (!surface->window)" in EGL and
        "egl_public_core33_pbuffer.o:" in MAKEFILE and
        "eglCreatePbufferSurface(display, config, pbuffer_attributes)" in
        PBUFFER and
        "eglMakeCurrent(display, pbuffer, pbuffer, context)" in PBUFFER and
        "eglSwapBuffers(display, pbuffer)" in PBUFFER and
        "pixel[2] == 255" in PBUFFER,
        "Core EGL pbuffer candidate regressed")

require("EGL_KHR_surfaceless_context" in EGL and
        "EGL_KHR_no_config_context" in EGL and
        "config == EGL_NO_CONFIG_KHR" in EGL and
        "context->config_id" in EGL and
        "st_api_make_current(context->st, NULL, NULL)" in EGL and
        "egl_public_core33_surfaceless.o:" in MAKEFILE and
        "eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)" in
        SURFACELESS and
        "eglGetCurrentSurface(EGL_DRAW) == EGL_NO_SURFACE" in SURFACELESS and
        "eglCreateContext(display, EGL_NO_CONFIG_KHR" in SURFACELESS and
        "config_id == 0" in SURFACELESS and
        "EGL_BAD_MATCH" in SURFACELESS and
        "EGL_BAD_SURFACE" in SURFACELESS and
        "GL_FRAMEBUFFER_COMPLETE" in SURFACELESS and
        "pixel[2] == 255" in SURFACELESS,
        "Core EGL surfaceless context or user-FBO proof regressed")

require("egl_public_core33_rgtc_transfer.o:" in MAKEFILE and
        "glCompressedTexImage2D" in RGTC_TRANSFER and
        "glCompressedTexSubImage2D" in RGTC_TRANSFER and
        "glGetCompressedTexImage" in RGTC_TRANSFER and
        "GL_TEXTURE_COMPRESSED_IMAGE_SIZE" in RGTC_TRANSFER and
        "glGetTexImage" in RGTC_TRANSFER and
        "bytes_ok" in RGTC_TRANSFER and "texels_ok" in RGTC_TRANSFER,
        "Core RGTC compressed subimage/readback path regressed")

require("GL_DEPTH24_STENCIL8_EXT" in
        (ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_format.c").read_text() and
        "PIPE_FORMAT_Z32_FLOAT_S8X24_UINT" in
        (ROOT / "third_party/mesa-26.2.0/src/mesa/state_tracker/st_format.c").read_text() and
        "DEFAULT_DEPTH_FORMATS" in MESA_PATCH and
        "GL_DEPTH24_STENCIL8_EXT" in MESA_PATCH and
        "rb->InternalFormat == GL_DEPTH24_STENCIL8_EXT" in MESA_FBO and
        "texImage->InternalFormat == GL_DEPTH24_STENCIL8_EXT" in MESA_FBO and
        "img->InternalFormat == GL_DEPTH24_STENCIL8_EXT" in MESA_TEXPARAM and
        "rb->InternalFormat == GL_DEPTH24_STENCIL8_EXT" in MESA_PATCH and
        "texImage->InternalFormat == GL_DEPTH24_STENCIL8_EXT" in MESA_PATCH and
        "img->InternalFormat == GL_DEPTH24_STENCIL8_EXT" in MESA_PATCH and
        "egl_public_core33_depth_format_fallback.o:" in MAKEFILE and
        "GL_DEPTH_COMPONENT16" in DEPTH_FORMATS and
        "GL_DEPTH_COMPONENT24" in DEPTH_FORMATS and
        "GL_DEPTH_COMPONENT32" in DEPTH_FORMATS and
        "GL_DEPTH24_STENCIL8" in DEPTH_FORMATS and
        "GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE" in DEPTH_FORMATS and
        "GL_UNSIGNED_NORMALIZED" in DEPTH_FORMATS and
        "GL_TEXTURE_DEPTH_SIZE" in DEPTH_FORMATS and
        "GL_TEXTURE_DEPTH_TYPE" in DEPTH_FORMATS and
        "matching == 8" in DEPTH_FORMATS and
        "draw_calls == 8" in DEPTH_FORMATS,
        "Core depth-format fallback matrix regressed")

require("state->logicop_func | (state->logicop_func << 4)" in SCREEN and
        "native->color_control_valid = 1;" in SCREEN and
        "egl_public_core33_logic_op.o:" in MAKEFILE and
        "static const GLenum operations[16]" in LOGIC_OP and
        "GL_COLOR_LOGIC_OP" in LOGIC_OP and
        "matching == 16" in LOGIC_OP and
        "draw_calls == 16" in LOGIC_OP,
        "Core 3.3 color logic-operation matrix regressed")

require("ps5_tiled_depth_surface_size" in SCREEN and
        "format == PIPE_FORMAT_Z32_FLOAT" in SCREEN and
        "runtime_depth_samples = samples" in BACKEND and
        "NUM_SAMPLES = log2(4)" in NATIVE_RUNTIME and
        "egl_public_core33_msaa4_depth.elf" in MAKEFILE and
        "glRenderbufferStorageMultisample" in MSAA4_DEPTH and
        "GL_DEPTH_COMPONENT32F" in MSAA4_DEPTH and
        "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE" in MSAA4_DEPTH and
        "glDepthFunc(GL_LESS)" in MSAA4_DEPTH and
        "mixed >= 64" in MSAA4_DEPTH,
        "Core 3.3 4x depth-MSAA candidate route is incomplete")

require("ps5_msaa4_depth_support" in SCREEN and
        "caps->clear_scissored = true" in SCREEN and
        "UINT32_C(0xe1820000)" in SCREEN and
        "(multisampled && !depth_texture)" in SCREEN and
        "GL_MAX_DEPTH_TEXTURE_SAMPLES" in MSAA4_DEPTH_TEXTURE and
        "GL_MAX_INTEGER_SAMPLES" in MSAA4_DEPTH_TEXTURE and
        "glTexImage2DMultisample" in MSAA4_DEPTH_TEXTURE and
        "GL_DEPTH_COMPONENT32F" in MSAA4_DEPTH_TEXTURE and
        "uniform sampler2DMS source" in MSAA4_DEPTH_TEXTURE and
        "texelFetch(source, p, sample)" in MSAA4_DEPTH_TEXTURE and
        "glBlitFramebuffer" in MSAA4_DEPTH_TEXTURE and
        "resolved_left == SIZE * SIZE / 2" in MSAA4_DEPTH_TEXTURE and
        "sampled == SIZE * SIZE" in MSAA4_DEPTH_TEXTURE and
        "egl_public_core33_msaa4_depth_texture.o:" in MAKEFILE,
        "Core 3.3 4x sampled depth-texture route regressed")

require("ps5_msaa4_color_format" in SCREEN and
        "return ps5_render_target_format(format) &&\n"
        "          ps5_tiled_color_msaa4_tile" in SCREEN and
        "ps5_tiled_color_msaa4_tile" in SCREEN and
        "case 1: *width = 128; *height = 128; return true" in SCREEN and
        "case 16: *width = 32; *height = 32; return true" in SCREEN and
        "ps5_resolve_color_msaa4" in SCREEN and
        "const bool sample0_only = info->sample0_only || integer" in SCREEN and
        "sample < (sample0_only ? 1u : 4u)" in SCREEN and
        "resolved = value" in SCREEN and
        "util_format_apply_color_swizzle(&swizzled, &resolved" in SCREEN and
        "info->swizzle, integer" in SCREEN and
        "info->src.box.height < 0 ? src_height - 1u - y : y" in SCREEN and
        "util_format_unpack_rgba(info->src.format" in SCREEN and
        "util_format_pack_rgba(info->dst.format" in SCREEN and
        "ps5_resolve_depth_stencil_msaa4" in SCREEN and
        "ps5_replicate_depth_stencil_msaa4" in SCREEN and
        "destination->base.nr_samples != 4" in SCREEN and
        "for (unsigned sample = 0; sample < 4; ++sample)" in SCREEN and
        "ps5_tiled_depth_msaa4_offset" in SCREEN and
        "static const uint16_t sample_masks[2] = {0x0004, 0x0008}" in
        SCREEN and
        "ps5_tiled_stencil_msaa4_offset" in SCREEN and
        "static const uint16_t sample_masks[2] = {0x0001, 0x0002}" in
        SCREEN and
        'parse_patinfo(text, "GFX10_SW_64K_Z_X_4xaa_PATINFO")' in
        SWIZZLE_DERIVATION and
        'parse_patinfo(text, "GFX10_SW_64K_R_X_4xaa_PATINFO")' in
        SWIZZLE_DERIVATION and
        "color{bpe}_4x_tile={width}x{height}x4/complete" in
        SWIZZLE_DERIVATION and
        "driver_color_4x=1,2,4,8,16-byte/exact" in
        SWIZZLE_DERIVATION and
        "depth_stencil_4x_tiles=d32:64x64x4,s8:128x128x4/complete" in
        SWIZZLE_DERIVATION,
        "Core 3.3 multi-format color/depth/stencil resolve route regressed")

require("#define WHITE UINT32_C(0xffffffff)" in MSAA_ARRAY and
        "#define WHITE_HASH UINT32_C(0x4847ddc5)" in MSAA_ARRAY and
        "matching == SIZE * SIZE && hash == WHITE_HASH" in MSAA_ARRAY and
        "resolved[0] == SIZE * SIZE && resolved[1] == SIZE * SIZE" in
        MSAA_ARRAY,
        "Core multisample-array receipt has a mismatched image oracle")
require("#define WHITE UINT32_C(0xffffffff)" in TEXTURE_BUFFER and
        "#define WHITE_HASH UINT32_C(0x4847ddc5)" in TEXTURE_BUFFER and
        "hash == WHITE_HASH" in TEXTURE_BUFFER,
        "Core texture-buffer receipt has a mismatched image oracle")

require("unsigned sample_count" in MESA_ST_FORMAT and
        "ctx->Const.MaxIntegerSamples" in MESA_ST_FORMAT and
        "ctx->Const.MaxDepthTextureSamples" in MESA_ST_FORMAT and
        "ctx->Const.MaxColorTextureSamples" in MESA_ST_FORMAT and
        "target, sample_count, sample_count" in MESA_ST_FORMAT and
        "MAX2(1u, get_max_samples_for_formats" in MESA_ST_EXTENSIONS and
        "!util_format_is_pure_integer(fmt)" in MESA_ST_TEXTURE and
        "!_mesa_is_enum_format_integer(internalFormat)" in
        MESA_RENDERBUFFER and
        "GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY" in MESA_PATCH and
        "!util_format_is_pure_integer(fmt)" in MESA_PATCH and
        "!_mesa_is_enum_format_integer(internalFormat)" in MESA_PATCH,
        "Core 3.3 multisample backing-format/sample-one contract regressed")

require("ps5_tiled_stencil_surface_size_samples" in SCREEN and
        "PIPE_FORMAT_Z32_FLOAT_S8X24_UINT" in SCREEN and
        "(!sampled || depth_target ||\n              (target != PIPE_BUFFER &&" in SCREEN and
        "PIPE_CLEAR_STENCIL" in SCREEN and
        "ps5_agc_native_set_depth_stencil_buffer" in BACKEND and
        "egl_public_core33_msaa4_stencil.elf" in MAKEFILE and
        "PS5_MSAA4_STENCIL_TEST" in MSAA4_DEPTH and
        "GL_DEPTH32F_STENCIL8" in MSAA4_DEPTH and
        "glStencilFunc(GL_NOTEQUAL, 0x5a, 0xff)" in MSAA4_DEPTH and
        "GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |" in MSAA4_DEPTH,
        "Core 3.3 4x packed depth/stencil candidate route is incomplete")

require("[pss-opengl-cts] finished" in CTS_MAIN and
        "sceKernelDebugOutText" in CTS_MAIN and
        "std::fflush(stdout)" in CTS_MAIN and
        'return finish("argument_error", 2)' in CTS_MAIN and
        'return finish("fatal", 3)' in CTS_MAIN and
        'return finish(passed ? "passed" : "failed"' in CTS_MAIN and
        "ObservationStopText = '[pss-opengl-cts] finished'" in CTS_RUNNER,
        "native CTS completion-marker observation contract regressed")

require("$cycleArguments.UploadRelativePaths = @(" in CTS_RUNNER and
        "'eboot.bin'" in CTS_RUNNER and
        "'sce_module/libc.prx'" in CTS_RUNNER and
        "'cts-args.txt'" in CTS_RUNNER and
        "$cycleArguments.UploadRelativePaths += 'cts-shard.txt'" in
        CTS_RUNNER,
        "incremental CTS deployment can execute a stale native runtime")

require("[int]$MaximumNotSupported = 1000" in CTS_CAMPAIGN and
        "[Math]::Min($MaximumNotSupported, $count)" in CTS_CAMPAIGN,
        "full CTS campaign no longer accepts CTS-defined inapplicable cases")

require("GLSL_VERSION_430" in CTS_NEGATIVE_GUARD and
        "GLSL_VERSION_310_ES" in CTS_NEGATIVE_GUARD,
        "GL33 CTS can incorrectly create an unavailable compute shader")

require("struct pipe_resource *depth_stencil" in EGL and
        "ST_ATTACHMENT_DEPTH_STENCIL_MASK" in EGL and
        "PIPE_FORMAT_Z32_FLOAT_S8X24_UINT" in EGL and
        "resource = surface->depth_stencil" in EGL and
        "PIPE_BIND_DEPTH_STENCIL" in EGL and
        "case EGL_DEPTH_SIZE: *value = 32" in EGL and
        "case EGL_STENCIL_SIZE: *value = 8" in EGL and
        "PixelFormat(8, 8, 8, 8), 32, 8, 0" in CTS_PLATFORM and
        "EGL_DEPTH_SIZE" in CTS_PLATFORM and
        "EGL_STENCIL_SIZE" in CTS_PLATFORM,
        "native CTS/default EGL surface lost depth-stencil backing")

require("case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:" in MANAGER and
        "rb->InternalFormat = GL_DEPTH32F_STENCIL8" in MANAGER and
        "src/mesa/state_tracker/st_manager.c" in MESA_PATCH and
        "PIPE_FORMAT_Z32_FLOAT_S8X24_UINT" in MESA_PATCH,
        "Mesa window framebuffer rejects the PS5 packed depth format")

require("PS5_AGC_BIND_NATIVE_API(agc, sceAgcDcbSetNumInstances)" in
        NATIVE_RUNTIME and
        "VIDEO_OUT_PIXEL_FORMAT UINT64_C(0x8000000000000000)" in
        NATIVE_RUNTIME and
        "runtime_video_registered" in NATIVE_RUNTIME and
        "sceVideoOutSubmitFlip" in NATIVE_RUNTIME and
        "runtime_video_prepare_draw" in NATIVE_RUNTIME and
        "completion_marker" in NATIVE_RUNTIME and
        "release_mem(&command, 40, 0x30c" in NATIVE_RUNTIME and
        "sceSystemServiceHideSplashScreen" in EGL and
        "ps5_display_target_alias" in SCREEN and
        "surface->targets[surface->buffer_index]" in EGL and
        "ps5_agc_gate2_present(surface->buffer_index)" in EGL and
        "surface->buffer_index ^= 1u" in EGL and
        "p_atomic_inc(&surface->drawable.stamp)" in EGL and
        "runtime_video_handle, (int)buffer_index" in NATIVE_RUNTIME and
        "scanout_target" in BACKEND and
        "ps5_agc_gate2_shutdown_present" in EGL and
        "egl_public_core33_visible_animation.o:" in MAKEFILE and
        "#version 330 core" in VISIBLE_ANIMATION and
        "eglSwapBuffers(display, surface)" in VISIBLE_ANIMATION and
        "ps5_agc_runtime_bind_native_api" in BACKEND and
        "*wrapped_draw_auto = ps5_agc_draw_auto_instanced" in BACKEND and
        "*wrapped_draw_index = ps5_agc_draw_index_instanced" in BACKEND and
        "*wrapped_set_cx = ps5_agc_set_cx_mrt" in BACKEND,
        "direct-linked native runtime bypasses state or presentation hooks")

require("defined(AGC_RUNTIME_PACKAGES) && !defined(AGC_RUNTIME_DIAGNOSTICS)" in
        NATIVE_RUNTIME and
        "Avoid multi-megabyte command/framebuffer dumps on every GL draw" in
        NATIVE_RUNTIME and
        "#ifdef AGC_RUNTIME_DIAGNOSTICS" in BACKEND,
        "Gallium draw submission regained per-draw diagnostic dumps")

clear_path = SCREEN.split("ps5_clear(struct pipe_context", 1)[1].split(
    "static bool\nps5_remove_point_size", 1)[0]
require(clear_path.index("PS5_ENABLE_MSAA4_CANDIDATE") <
        clear_path.index("PS5_ENABLE_MRT_CANDIDATE"),
        "combined Core 3.3 runtime lets MRT reject multisample clears")

for override in ("MESA_GL_VERSION_OVERRIDE", "MESA_EXTENSION_OVERRIDE"):
    require(override not in EGL and override not in SCREEN,
            f"speculative GL override present: {override}")

for heading in (
    "How Mesa computes the version", "Current PS5 Gallium envelope",
    "Mesa version predicates and present blockers",
    "Gate 7 feature-family inventory", "Immediate acceptance contract",
):
    require(heading in AUDIT, f"capability audit section missing: {heading}")

required_entrypoints = set(re.findall(r'"(gl[A-Za-z0-9_]+)"',
                                      ENTRYPOINT_NAMES))
called_entrypoints = set()
for public_test in (ROOT / "tests/ps5").glob("egl_public*.c"):
    called_entrypoints.update(re.findall(
        r"\b(gl[A-Z][A-Za-z0-9_]*)\s*\(", public_test.read_text()))
missing_behavior_calls = required_entrypoints - called_entrypoints
require(len(required_entrypoints) == 344 and not missing_behavior_calls,
        "required Core commands without a public behavioral invocation: "
        f"{sorted(missing_behavior_calls)}")

print("gl33-capability-audit: PASS derived=core33,compat33 "
      "behavioral-api=344/344 "
      "direct-vertex=float32+integer32 packed-vertex=hardware instanced-arrays=hardware "
      "multi-texture=hardware mipmap=hardware cube=hardware array=hardware "
      "1d=hardware 3d=hardware rectangle=hardware rgtc=hardware-cpu-fallback packed-float=hardware texture-integer=hardware-rgba8-16-32 rgb10-a2ui=hardware ubo=hardware timer=hardware geometry=hardware-minimal mrt=hardware xfb=hardware "
      "snorm=hardware float-texture=hardware rgb9-e5=hardware rg-render=hardware "
      "mixed-fbo=hardware occlusion=hardware conditional-render=hardware depth-clamp=hardware "
      "glsl330=hardware core33-context=hardware msaa4=hardware-color-pass "
      "dual-source-blend=hardware-proven")
