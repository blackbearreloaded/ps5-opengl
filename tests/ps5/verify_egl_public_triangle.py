#!/usr/bin/env python3
"""Guard the first public-only EGL/OpenGL PS5 sample."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SAMPLE = (ROOT / "tests/ps5/egl_public_triangle.c").read_text()
DEPTH_SAMPLE = (ROOT / "tests/ps5/egl_public_texture_depth.c").read_text()
MAP_SAMPLE = (ROOT / "tests/ps5/egl_public_map_buffer_range.c").read_text()
HALF_SAMPLE = (ROOT / "tests/ps5/egl_public_half_float_vertex.c").read_text()
SNORM_SAMPLE = (ROOT / "tests/ps5/egl_public_normalized_short_vertex.c").read_text()
BASE_SAMPLE = (ROOT / "tests/ps5/egl_public_base_vertex.c").read_text()
FIRST_SAMPLE = (ROOT / "tests/ps5/egl_public_first_vertex.c").read_text()
INSTANCED_SAMPLE = (ROOT / "tests/ps5/egl_public_draw_instanced.c").read_text()
ARRAYS_SAMPLE = (ROOT / "tests/ps5/egl_public_instanced_arrays.c").read_text()
SRGB_SAMPLE = (ROOT / "tests/ps5/egl_public_srgb_texture.c").read_text()
RTT_SAMPLE = (ROOT / "tests/ps5/egl_public_render_to_texture.c").read_text()
DEPTH_TEXTURE_SAMPLE = (
    ROOT / "tests/ps5/egl_public_depth_texture.c"
).read_text()
RG_SAMPLE = (ROOT / "tests/ps5/egl_public_texture_rg.c").read_text()
RG_MULTITILE_SAMPLE = (
    ROOT / "tests/ps5/egl_public_texture_rg_multitile.c"
).read_text()
RG_TILE_SAMPLE = (
    ROOT / "tests/ps5/egl_public_texture_rg_tile.c"
).read_text()
RG_TILE_ANALYZER = (
    ROOT / "tests/ps5/analyze_texture_rg_tiles.py"
).read_text()
PACKED_FLOAT_SAMPLE = (
    ROOT / "tests/ps5/egl_public_packed_float.c"
).read_text()
TEXTURE_INTEGER_SAMPLE = (
    ROOT / "tests/ps5/egl_public_texture_integer.c"
).read_text()
TEXTURE_INTEGER_NARROW_SAMPLE = (
    ROOT / "tests/ps5/egl_public_texture_integer_narrow.c"
).read_text()
RGB10_A2UI_SAMPLE = (
    ROOT / "tests/ps5/egl_public_rgb10_a2ui.c"
).read_text()
RECTANGLE_SAMPLE = (
    ROOT / "tests/ps5/egl_public_texture_rectangle.c"
).read_text()
EGL = (ROOT / "src/egl/ps5_egl.c").read_text()
MAKEFILE = (ROOT / "tests/ps5/Makefile").read_text()
BUILD = (ROOT / "toolchain/build-mesa-ps5.sh").read_text()
MESA = ROOT / "third_party/mesa-26.2.0"
EXTENSIONS = (MESA / "src/mesa/main/extensions.c").read_text()
VARRAY = (MESA / "src/mesa/main/varray.c").read_text()
UVBUF = (MESA / "src/gallium/auxiliary/util/u_vbuf.c").read_text()
TRANSLATE = (MESA / "src/gallium/auxiliary/translate/translate_generic.c").read_text()
CSO = (MESA / "src/gallium/auxiliary/cso_cache/cso_context.c").read_text()
SCREEN = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
VERTEX_FORMAT = SCREEN.split(
    "ps5_vertex_format(enum pipe_format format, PsbcVertexFormat *out)\n{", 1
)[1].split("\nstatic unsigned\nps5_vertex_format_size", 1)[0]
BACKEND = (ROOT / "src/platform/ps5_agc_runtime_backend.c").read_text()
PACKAGE = (ROOT / "src/platform/ps5_agc_package.c").read_text()
PSBC = ROOT / "third_party/opengnm-psbc"
PSBC_HEADER = (PSBC / "libpsbc/psbc_compile.h").read_text()
PSBC_COMPILE = (PSBC / "libpsbc/psbc_compile.c").read_text()
RADV_INPUTS = (PSBC / "src/amd/vulkan/nir/radv_nir_lower_vs_inputs.c").read_text()
RADV_ARGS = (PSBC / "src/amd/vulkan/radv_shader_args.c").read_text()
AC_INTRINSICS = (PSBC / "src/amd/common/nir/ac_nir_lower_intrinsics_to_args.c").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def solid_rgba_hash(word: int) -> int:
    value = 2166136261
    pixel = word.to_bytes(4, "little")
    for byte in pixel * (64 * 64):
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


for forbidden in ("ps5_screen", "frontend/api", "state_tracker", "pipe_",
                  "_mesa_", "ps5_agc"):
    require(forbidden not in SAMPLE,
            f"public sample imports private backend token: {forbidden}")
    require(forbidden not in DEPTH_SAMPLE,
            f"public texture/depth sample imports private token: {forbidden}")
    require(forbidden not in MAP_SAMPLE,
            f"public map sample imports private token: {forbidden}")
    require(forbidden not in HALF_SAMPLE,
            f"public half-float sample imports private token: {forbidden}")
    require(forbidden not in SNORM_SAMPLE,
            f"public normalized-short sample imports private token: {forbidden}")
    require(forbidden not in BASE_SAMPLE,
            f"public base-vertex sample imports private token: {forbidden}")
    require(forbidden not in PACKED_FLOAT_SAMPLE,
            f"public packed-float sample imports private token: {forbidden}")
    require(forbidden not in TEXTURE_INTEGER_SAMPLE,
            f"public texture-integer sample imports private token: {forbidden}")
    require(forbidden not in TEXTURE_INTEGER_NARROW_SAMPLE,
            f"public narrow-integer sample imports private token: {forbidden}")
    require(forbidden not in RGB10_A2UI_SAMPLE,
            f"public RGB10_A2UI sample imports private token: {forbidden}")
    require(forbidden not in RECTANGLE_SAMPLE,
            f"public texture-rectangle sample imports private token: {forbidden}")
for call in ("eglGetDisplay", "eglInitialize", "eglBindAPI",
             "eglChooseConfig", "eglCreateWindowSurface",
             "eglCreateContext", "eglMakeCurrent", "eglSwapBuffers",
             "eglDestroyContext", "eglDestroySurface", "eglTerminate",
             "glCreateShader", "glCreateProgram", "glGenBuffers",
             "glDrawArrays", "glFinish", "glReadPixels", "glGetString"):
    require(f"{call}(" in SAMPLE, f"public sample call missing: {call}")
require("eglCreateContext(display, config, EGL_NO_CONTEXT, NULL)" in SAMPLE,
        "unextended EGL 1.4 context creation must use an empty attribute list")
require("EXPECTED_PIXEL UINT32_C(0xffff00ff)" in SAMPLE and
        "EXPECTED_HASH UINT32_C(0x64e31dc5)" in SAMPLE and
        "matching == CROP_WIDTH * CROP_HEIGHT" in SAMPLE and
        "pixel_hash == EXPECTED_HASH" in SAMPLE and "unexpected" in SAMPLE,
        "public sample lacks the exact readback oracle")
require("PS5_DUAL_SOURCE_BLEND_TEST" in SAMPLE and
        '"#version 130\\n"' in SAMPLE and
        "PS5_CORE_33_TEST" in SAMPLE and
        '"#version 330\\n"' in SAMPLE and
        "out vec4 primary" in SAMPLE and "out vec4 secondary" in SAMPLE and
        'glBindFragDataLocationIndexed(program, 0, 0, "primary")' in SAMPLE and
        'glBindFragDataLocationIndexed(program, 0, 1, "secondary")' in SAMPLE and
        "GL_ARB_blend_func_extended" in SAMPLE and
        "GL_MAX_DUAL_SOURCE_DRAW_BUFFERS" in SAMPLE and
        "blue_fragment_source" in SAMPLE and
        "glUseProgram(blue_program)" in SAMPLE and
        "glUseProgram(program)" in SAMPLE and
        "EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR" in SAMPLE and
        "glGenVertexArrays(1, &vertex_array)" in SAMPLE and
        "glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR)" in SAMPLE,
        "public dual-source sample lacks its indexed-output or exact blend oracle")
require('strncmp((const char *)gl_version, "2.1 ", 4)' in SAMPLE and
        'strncmp((const char *)glsl_version, "1.20", 4)' in SAMPLE,
        "public sample does not enforce conservative GL/GLSL identity")
require("cleanup_ok &= eglMakeCurrent" in SAMPLE and
        "cleanup_ok &= eglDestroyContext" in SAMPLE and
        "cleanup_ok &= eglDestroySurface" in SAMPLE and
        "cleanup_ok &= eglTerminate" in SAMPLE,
        "public sample does not make EGL teardown part of its result")

for call in ("eglGetDisplay", "eglCreateWindowSurface", "eglCreateContext",
             "eglMakeCurrent", "eglSwapBuffers", "glCreateShader",
             "glTexImage2D", "glGenFramebuffers", "glRenderbufferStorage",
             "glFramebufferRenderbuffer", "glCheckFramebufferStatus",
             "glClearDepth", "glDepthFunc", "glDepthMask", "glDrawArrays",
             "glReadPixels"):
    require(f"{call}(" in DEPTH_SAMPLE,
            f"public texture/depth call missing: {call}")
require("RED_HASH UINT32_C(0xc40abdc5)" in DEPTH_SAMPLE and
        "GREEN_HASH UINT32_C(0xc38d1dc5)" in DEPTH_SAMPLE and
        "depth_internal_format != GL_DEPTH_COMPONENT" in DEPTH_SAMPLE and
        "depth_bits != 32" in DEPTH_SAMPLE and
        "framebuffer_status != GL_FRAMEBUFFER_COMPLETE" in DEPTH_SAMPLE,
        "public texture/depth sample lacks exact FBO/depth oracles")
require(solid_rgba_hash(0xff0000ff) == 0xc40abdc5 and
        solid_rgba_hash(0xff00ff00) == 0xc38d1dc5,
        "public texture/depth solid-color FNV oracle is incorrect")
require("glDepthMask(GL_TRUE)" in DEPTH_SAMPLE and
        "glDepthMask(GL_FALSE)" in DEPTH_SAMPLE and
        "write_hash == RED_HASH" in DEPTH_SAMPLE and
        "mask_hash == GREEN_HASH" in DEPTH_SAMPLE,
        "public texture/depth sample lacks write-mask discriminator")
require("glBindFramebuffer(GL_FRAMEBUFFER, 0)" in DEPTH_SAMPLE and
        "glClear(0)" in DEPTH_SAMPLE and
        "detach_sync_error == GL_NO_ERROR" in DEPTH_SAMPLE and
        "cleanup_ok &= eglDestroyContext" in DEPTH_SAMPLE and
        "cleanup_ok &= eglDestroySurface" in DEPTH_SAMPLE,
        "public texture/depth sample lacks detach/teardown synchronization")

for call in ("eglGetDisplay", "eglCreateWindowSurface", "eglCreateContext",
             "eglMakeCurrent", "eglSwapBuffers", "glCreateShader",
             "glBufferData", "glMapBufferRange", "glGetBufferParameteriv",
             "glFlushMappedBufferRange", "glUnmapBuffer", "glDrawArrays",
             "glReadPixels"):
    require(f"{call}(" in MAP_SAMPLE,
            f"public map sample call missing: {call}")
require("BUFFER_OFFSET 64" in MAP_SAMPLE and
        "buffer_size = BUFFER_OFFSET + sizeof(vertices)" in MAP_SAMPLE and
        "buffer_size - 4, 8" in MAP_SAMPLE and
        "invalid_error != GL_INVALID_VALUE" in MAP_SAMPLE,
        "public map sample lacks exact range boundaries")
require("GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT" in MAP_SAMPLE and
        "GL_MAP_FLUSH_EXPLICIT_BIT" in MAP_SAMPLE and
        "map_access != WRITE_ACCESS" in MAP_SAMPLE and
        "read_access != GL_MAP_READ_BIT" in MAP_SAMPLE,
        "public map sample lacks exact access-state checks")
require("map_offset != BUFFER_OFFSET" in MAP_SAMPLE and
        "map_length != (GLint)sizeof(vertices)" in MAP_SAMPLE and
        "memcmp(mapped, vertices, sizeof(vertices)) == 0" in MAP_SAMPLE and
        "(const void *)(uintptr_t)BUFFER_OFFSET" in MAP_SAMPLE,
        "public map sample does not prove mapped bytes feed the VBO subrange")
require('has_extension(extensions, "GL_ARB_map_buffer_range")' in MAP_SAMPLE and
        'strncmp((const char *)gl_version, "2.1 ", 4)' in MAP_SAMPLE and
        'strncmp((const char *)glsl_version, "1.20", 4)' in MAP_SAMPLE,
        "public map sample lacks conservative extension/version identity")
require("EXPECTED_PIXEL UINT32_C(0xffff00ff)" in MAP_SAMPLE and
        "EXPECTED_HASH UINT32_C(0x64e31dc5)" in MAP_SAMPLE and
        solid_rgba_hash(0xffff00ff) == 0x64e31dc5 and
        "matching == CROP_WIDTH * CROP_HEIGHT" in MAP_SAMPLE and
        "pixel_hash == EXPECTED_HASH" in MAP_SAMPLE,
        "public map sample lacks the exact readback oracle")
require("cleanup_ok &= eglMakeCurrent" in MAP_SAMPLE and
        "cleanup_ok &= eglDestroyContext" in MAP_SAMPLE and
        "cleanup_ok &= eglDestroySurface" in MAP_SAMPLE and
        "cleanup_ok &= eglTerminate" in MAP_SAMPLE,
        "public map sample lacks complete EGL teardown")

for call in ("eglGetDisplay", "eglCreateWindowSurface", "eglCreateContext",
             "eglMakeCurrent", "eglSwapBuffers", "glCreateShader",
             "glBufferData", "glVertexAttribPointer", "glGetVertexAttribiv",
             "glDrawArrays", "glFinish", "glReadPixels"):
    require(f"{call}(" in HALF_SAMPLE,
            f"public half-float sample call missing: {call}")
require("UINT16_C(0xb800), UINT16_C(0xb800)" in HALF_SAMPLE and
        "UINT16_C(0x3800), UINT16_C(0xb800)" in HALF_SAMPLE and
        "UINT16_C(0x0000), UINT16_C(0x3800)" in HALF_SAMPLE,
        "public half-float sample lacks exact binary16 vertices")
require("glVertexAttribPointer(0, 0, GL_HALF_FLOAT" in HALF_SAMPLE and
        "invalid_error != GL_INVALID_VALUE" in HALF_SAMPLE and
        "glVertexAttribPointer(0, 2, GL_HALF_FLOAT" in HALF_SAMPLE,
        "public half-float sample lacks invalid and valid API discriminators")
require("attrib_type != GL_HALF_FLOAT" in HALF_SAMPLE and
        "attrib_size != 2" in HALF_SAMPLE and
        "attrib_stride != 2 * (GLint)sizeof(uint16_t)" in HALF_SAMPLE and
        "setup_error != GL_NO_ERROR" in HALF_SAMPLE,
        "public half-float sample lacks exact attribute-state checks")
require('has_extension(extensions, "GL_ARB_half_float_vertex")' in HALF_SAMPLE and
        'strncmp((const char *)gl_version, "2.1 ", 4)' in HALF_SAMPLE and
        'strncmp((const char *)glsl_version, "1.20", 4)' in HALF_SAMPLE,
        "public half-float sample lacks conservative extension/version identity")
require("EXPECTED_PIXEL UINT32_C(0xffff00ff)" in HALF_SAMPLE and
        "EXPECTED_HASH UINT32_C(0x64e31dc5)" in HALF_SAMPLE and
        "matching == CROP_WIDTH * CROP_HEIGHT" in HALF_SAMPLE and
        "pixel_hash == EXPECTED_HASH" in HALF_SAMPLE,
        "public half-float sample lacks the exact readback oracle")
require("cleanup_ok &= eglMakeCurrent" in HALF_SAMPLE and
        "cleanup_ok &= eglDestroyContext" in HALF_SAMPLE and
        "cleanup_ok &= eglDestroySurface" in HALF_SAMPLE and
        "cleanup_ok &= eglTerminate" in HALF_SAMPLE,
        "public half-float sample lacks complete EGL teardown")

for call in ("eglGetDisplay", "eglCreateWindowSurface", "eglCreateContext",
             "eglMakeCurrent", "eglSwapBuffers", "glCreateShader",
             "glBufferData", "glVertexAttribPointer", "glGetVertexAttribiv",
             "glDrawArrays", "glFinish", "glReadPixels"):
    require(f"{call}(" in SNORM_SAMPLE,
            f"public normalized-short sample call missing: {call}")
require("-16384, -16384" in SNORM_SAMPLE and
        "16384, -16384" in SNORM_SAMPLE and
        "0,  16384" in SNORM_SAMPLE,
        "public normalized-short sample lacks exact signed vertices")
require("glVertexAttribPointer(0, 2, GL_SHORT, GL_TRUE, -1" in SNORM_SAMPLE and
        "invalid_error != GL_INVALID_VALUE" in SNORM_SAMPLE and
        "glVertexAttribPointer(0, 2, GL_SHORT, GL_TRUE" in SNORM_SAMPLE,
        "public normalized-short sample lacks invalid and valid API checks")
require("GL_VERTEX_ATTRIB_ARRAY_NORMALIZED" in SNORM_SAMPLE and
        "attrib_type != GL_SHORT" in SNORM_SAMPLE and
        "attrib_size != 2" in SNORM_SAMPLE and
        "attrib_stride != 2 * (GLint)sizeof(int16_t)" in SNORM_SAMPLE and
        "!attrib_normalized" in SNORM_SAMPLE and
        "setup_error != GL_NO_ERROR" in SNORM_SAMPLE,
        "public normalized-short sample lacks exact attribute-state checks")
require('strncmp((const char *)gl_version, "2.1 ", 4)' in SNORM_SAMPLE and
        'strncmp((const char *)glsl_version, "1.20", 4)' in SNORM_SAMPLE,
        "public normalized-short sample lacks conservative version identity")
require("EXPECTED_PIXEL UINT32_C(0xffff00ff)" in SNORM_SAMPLE and
        "EXPECTED_HASH UINT32_C(0x64e31dc5)" in SNORM_SAMPLE and
        "matching == CROP_WIDTH * CROP_HEIGHT" in SNORM_SAMPLE and
        "pixel_hash == EXPECTED_HASH" in SNORM_SAMPLE,
        "public normalized-short sample lacks the exact readback oracle")
require("cleanup_ok &= eglMakeCurrent" in SNORM_SAMPLE and
        "cleanup_ok &= eglDestroyContext" in SNORM_SAMPLE and
        "cleanup_ok &= eglDestroySurface" in SNORM_SAMPLE and
        "cleanup_ok &= eglTerminate" in SNORM_SAMPLE,
        "public normalized-short sample lacks complete EGL teardown")

for call in ("eglGetDisplay", "eglCreateWindowSurface", "eglCreateContext",
             "eglMakeCurrent", "eglSwapBuffers", "glCreateShader",
             "glBufferData", "glVertexAttribPointer", "glDrawElements",
             "glDrawElementsBaseVertex", "glFinish", "glReadPixels"):
    require(f"{call}(" in BASE_SAMPLE,
            f"public base-vertex sample call missing: {call}")
require("RED_PIXEL UINT32_C(0xff0000ff)" in BASE_SAMPLE and
        "RED_HASH UINT32_C(0xc40abdc5)" in BASE_SAMPLE and
        "GREEN_PIXEL UINT32_C(0xff00ff00)" in BASE_SAMPLE and
        "GREEN_HASH UINT32_C(0xc38d1dc5)" in BASE_SAMPLE and
        "BLUE_PIXEL UINT32_C(0xffff0000)" in BASE_SAMPLE and
        "BLUE_HASH UINT32_C(0xbdf93dc5)" in BASE_SAMPLE and
        solid_rgba_hash(0xff0000ff) == 0xc40abdc5 and
        solid_rgba_hash(0xff00ff00) == 0xc38d1dc5 and
        solid_rgba_hash(0xffff0000) == 0xbdf93dc5,
        "public base-vertex sample lacks exact RGB overwrite oracles")
require('"control-base0-red"' in BASE_SAMPLE and
        "glDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL, 3)" in BASE_SAMPLE and
        "(const void *)(3 * sizeof(uint16_t)), -3" in BASE_SAMPLE and
        '"positive-base3-green"' in BASE_SAMPLE and
        '"negative-base3-blue"' in BASE_SAMPLE and
        "glClear(" not in BASE_SAMPLE,
        "public base-vertex sample lacks zero/positive/negative discriminators")
require('has_extension(extensions, "GL_ARB_draw_elements_base_vertex")' in BASE_SAMPLE and
        'strncmp((const char *)gl_version, "2.1 ", 4)' in BASE_SAMPLE and
        'strncmp((const char *)glsl_version, "1.20", 4)' in BASE_SAMPLE,
        "public base-vertex sample lacks conservative extension/version identity")
require("cleanup_ok &= eglMakeCurrent" in BASE_SAMPLE and
        "cleanup_ok &= eglDestroyContext" in BASE_SAMPLE and
        "cleanup_ok &= eglDestroySurface" in BASE_SAMPLE and
        "cleanup_ok &= eglTerminate" in BASE_SAMPLE,
        "public base-vertex sample lacks complete EGL teardown")

for call in ("eglGetDisplay", "eglCreateWindowSurface", "eglCreateContext",
             "eglMakeCurrent", "eglSwapBuffers", "glCreateShader",
             "glBufferData", "glVertexAttribPointer", "glDrawArrays",
             "glFinish", "glReadPixels"):
    require(f"{call}(" in FIRST_SAMPLE,
            f"public first-vertex sample call missing: {call}")
require("glDrawArrays(GL_TRIANGLES, 0, 3)" in FIRST_SAMPLE and
        "glDrawArrays(GL_TRIANGLES, 3, 3)" in FIRST_SAMPLE and
        '"control-first0"' in FIRST_SAMPLE and '"first3"' in FIRST_SAMPLE and
        "BLACK_HASH UINT32_C(0x1ec31dc5)" in FIRST_SAMPLE and
        "MAGENTA_HASH UINT32_C(0x64e31dc5)" in FIRST_SAMPLE,
        "public first-vertex sample lacks exact first=0/3 discriminators")
require('strncmp((const char *)gl_version, "2.1 ", 4)' in FIRST_SAMPLE and
        'strncmp((const char *)glsl_version, "1.20", 4)' in FIRST_SAMPLE,
        "public first-vertex sample lacks conservative version identity")
require("cleanup_ok &= eglMakeCurrent" in FIRST_SAMPLE and
        "cleanup_ok &= eglDestroyContext" in FIRST_SAMPLE and
        "cleanup_ok &= eglDestroySurface" in FIRST_SAMPLE and
        "cleanup_ok &= eglTerminate" in FIRST_SAMPLE,
        "public first-vertex sample lacks complete EGL teardown")

for call in ("eglGetDisplay", "eglCreateWindowSurface", "eglCreateContext",
             "eglMakeCurrent", "eglSwapBuffers", "glCreateShader",
             "glBufferData", "glVertexAttribPointer", "glDrawArraysInstanced",
             "glFinish", "glReadPixels"):
    require(f"{call}(" in INSTANCED_SAMPLE,
            f"public draw-instanced sample call missing: {call}")
require('"#extension GL_ARB_draw_instanced : require\\n"' in INSTANCED_SAMPLE and
        "gl_InstanceIDARB" in INSTANCED_SAMPLE and
        "glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1)" in INSTANCED_SAMPLE and
        "glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 2)" in INSTANCED_SAMPLE,
        "public draw-instanced sample lacks its count/InstanceID discriminator")
require("RED_HASH UINT32_C(0xc40abdc5)" in INSTANCED_SAMPLE and
        "GREEN_HASH UINT32_C(0xc38d1dc5)" in INSTANCED_SAMPLE and
        '"control-left-red"' in INSTANCED_SAMPLE and
        '"instance0-red"' in INSTANCED_SAMPLE and
        '"instance1-green"' in INSTANCED_SAMPLE,
        "public draw-instanced sample lacks exact spatial/color oracles")
require('has_extension(extensions, "GL_ARB_draw_instanced")' in INSTANCED_SAMPLE and
        'strncmp((const char *)gl_version, "2.1 ", 4)' in INSTANCED_SAMPLE and
        'strncmp((const char *)glsl_version, "1.20", 4)' in INSTANCED_SAMPLE,
        "public draw-instanced sample lacks conservative extension/version identity")
require("cleanup_ok &= eglMakeCurrent" in INSTANCED_SAMPLE and
        "cleanup_ok &= eglDestroyContext" in INSTANCED_SAMPLE and
        "cleanup_ok &= eglDestroySurface" in INSTANCED_SAMPLE and
        "cleanup_ok &= eglTerminate" in INSTANCED_SAMPLE,
        "public draw-instanced sample lacks complete EGL teardown")

for call in ("eglGetDisplay", "eglCreateWindowSurface", "eglCreateContext",
             "eglMakeCurrent", "eglSwapBuffers", "glCreateShader",
             "glBufferData", "glVertexAttribPointer",
             "glVertexAttribDivisorARB", "glDrawArraysInstanced",
             "glFinish", "glReadPixels"):
    require(f"{call}(" in ARRAYS_SAMPLE,
            f"public instanced-arrays sample call missing: {call}")
require('has_extension(extensions, "GL_ARB_instanced_arrays")' in ARRAYS_SAMPLE and
        "glVertexAttribDivisorARB(1, 1)" in ARRAYS_SAMPLE and
        "glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1)" in ARRAYS_SAMPLE and
        "glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 2)" in ARRAYS_SAMPLE,
        "public instanced-arrays sample lacks its divisor/count discriminator")
require("RED_HASH UINT32_C(0xc40abdc5)" in ARRAYS_SAMPLE and
        "GREEN_HASH UINT32_C(0xc38d1dc5)" in ARRAYS_SAMPLE and
        '"control-left-red"' in ARRAYS_SAMPLE and
        '"instance0-red"' in ARRAYS_SAMPLE and
        '"instance1-green"' in ARRAYS_SAMPLE,
        "public instanced-arrays sample lacks exact spatial/color oracles")
require("cleanup_ok &= eglMakeCurrent" in ARRAYS_SAMPLE and
        "cleanup_ok &= eglDestroyContext" in ARRAYS_SAMPLE and
        "cleanup_ok &= eglDestroySurface" in ARRAYS_SAMPLE and
        "cleanup_ok &= eglTerminate" in ARRAYS_SAMPLE,
        "public instanced-arrays sample lacks complete EGL teardown")

require("PS5_ENABLE_PADDED_FBO_CANDIDATE" in SCREEN and
        "caps->mixed_framebuffer_sizes = PS5_ENABLE_PADDED_FBO_CANDIDATE" in
        SCREEN and
        "PS5_RENDER_WIDTH * bytes_per_pixel" in SCREEN and
        "ps5_tiled_rgba8_offset" in SCREEN and
        "COLOR_WIDTH 128" in DEPTH_SAMPLE and
        "DEPTH_WIDTH 64" in DEPTH_SAMPLE and
        'has_extension(extensions, "GL_ARB_framebuffer_object")' in
        DEPTH_SAMPLE,
        "padded mixed-size FBO candidate lacks cap, storage, or public oracle")

require("PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE 1" in SCREEN and
        "tiled_render_target" in SCREEN and
        "UINT32_C(0x91b00000)" in SCREEN and
        "ps5_agc_gate2_set_color_to_texture_barrier" in SCREEN and
        "UINT32_C(0xc0064900)" in BACKEND and
        "UINT32_C(0x0070f52d)" in BACKEND and
        "UINT32_C(0x00010000)" in BACKEND,
        "render-to-texture descriptor or proven release barrier is missing")
for call in ("glTexImage2D", "glFramebufferTexture2D",
             "glCheckFramebufferStatus", "glDrawArrays", "glReadPixels"):
    require(f"{call}(" in RTT_SAMPLE,
            f"public render-to-texture sample call missing: {call}")
require("EXPECTED_PIXEL UINT32_C(0xff0000ff)" in RTT_SAMPLE and
        "EXPECTED_HASH UINT32_C(0xc40abdc5)" in RTT_SAMPLE and
        'has_extension(extensions, "GL_EXT_framebuffer_object")' in RTT_SAMPLE and
        "No API wait, CPU copy, or detile" in RTT_SAMPLE,
        "public render-to-texture sample lacks its two-draw transition oracle")
transition = RTT_SAMPLE.index("No API wait, CPU copy, or detile")
require(RTT_SAMPLE.rfind("glDrawArrays(GL_TRIANGLES, 0, 3)", 0,
                        transition) >= 0 and
        RTT_SAMPLE.find("glBindFramebuffer(GL_FRAMEBUFFER, 0)",
                        transition) <
        RTT_SAMPLE.find("glDrawArrays(GL_TRIANGLES, 0, 3)", transition),
        "public render-to-texture sample lost its producer/consumer order")
require("PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE 1" in SCREEN and
        "Mesa's mutable-texture proxy" in SCREEN and
        "proxy.last_level = 0" in SCREEN and
        "ps5_agc_gate2_set_color_target_extents" in SCREEN and
        "ps5_agc_mrt_attrib2" in BACKEND and
        "((widths[target] - 1u) << 14)" in BACKEND and
        "ps5_tiled_rgba8_width(resource)" in SCREEN and
        "PIPE_MAP_WRITE" in SCREEN and
        "PS5_DYNAMIC_COLOR_TARGET_TEST" in RTT_SAMPLE and
        "TARGET_WIDTH 128" in RTT_SAMPLE and "TARGET_HEIGHT 96" in RTT_SAMPLE and
        "UPLOAD_PIXEL UINT32_C(0xff00ff00)" in RTT_SAMPLE and
        "UPLOAD_HASH UINT32_C(0xc38d1dc5)" in RTT_SAMPLE and
        "egl_public_dynamic_color_target.elf" in MAKEFILE,
        "dynamic color-target dimensions or upload/RTT oracle is missing")
require("templ->bind & PIPE_BIND_DISPLAY_TARGET" in SCREEN and
        "#ifndef PS5_RENDER_POOL_BYTES" in SCREEN and
        "PS5_RENDER_POOL_BYTES - PS5_RENDER_ARENA_OFFSET" in SCREEN and
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
        "ps5_agc_mrt_sizes[target] < required" in BACKEND and
        "color-target actual=%zu runtime=%zu scanout=%u" in BACKEND,
        "dynamic color targets lost tiled allocation or scanout registration")
require("ps5_screen_large_pool.o:" in MAKEFILE and
        "-DPS5_RENDER_POOL_BYTES=0x4000000u" in MAKEFILE and
        "egl_public_large_pool.elf:" in MAKEFILE,
        "single-allocation large-pool discriminator is missing")
require(((1080 - 1) | ((1920 - 1) << 14)) == 0x01dfc437,
        "documented 1920x1080 CB_COLOR_ATTRIB2 encoding changed")
require("PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE" in SCREEN and
        "PS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE" in SCREEN and
        "((bindings & PIPE_BIND_DEPTH_STENCIL) ||" in SCREEN and
        "(bindings & PIPE_BIND_SAMPLER_VIEW)))" in SCREEN and
        "UINT32_C(0x01600000)" in SCREEN and
        "UINT32_C(0x91800000)" in SCREEN and
        "sampler->compare_func << 12" in SCREEN and
        "ps5_agc_gate2_set_depth_to_texture_barrier" in SCREEN and
        "ps5_agc_gate2_set_depth_target_extents" in SCREEN and
        "UINT32_C(0x0070f52b)" in BACKEND and
        "((ps5_agc_depth_height - 1u) << 16)" in BACKEND,
        "D32F render-to-sample descriptor, barrier, or extent route is missing")
require("GL_ARB_depth_buffer_float" in DEPTH_TEXTURE_SAMPLE and
        "GL_DEPTH_COMPONENT32F" in DEPTH_TEXTURE_SAMPLE and
        "uniform sampler2D u_depth_texture" in DEPTH_TEXTURE_SAMPLE and
        "uniform sampler2DShadow u_depth_texture" in DEPTH_TEXTURE_SAMPLE and
        "GL_COMPARE_R_TO_TEXTURE" in DEPTH_TEXTURE_SAMPLE and
        "GREEN_HASH UINT32_C(0xc38d1dc5)" in DEPTH_TEXTURE_SAMPLE and
        "without CPU staging" in DEPTH_TEXTURE_SAMPLE and
        "egl_public_depth_texture.elf" in MAKEFILE,
        "public D32F raw/shadow sampling oracle is incomplete")
require(((128 - 1) | ((96 - 1) << 16)) == 0x005f007f,
        "documented 128x96 DB_DEPTH_SIZE_XY encoding changed")

require("PS5_ENABLE_SRGB_CANDIDATE" in SCREEN and
        "PIPE_FORMAT_R8G8B8A8_SRGB" in SCREEN and
        "*word1 = UINT32_C(0x08200000)" in SCREEN,
        "sRGB format gate or GFX10 descriptor is missing")
require("GL_SRGB8_ALPHA8" in SRGB_SAMPLE and
        "INPUT_PIXEL UINT32_C(0xff808080)" in SRGB_SAMPLE and
        "EXPECTED_PIXEL UINT32_C(0xff373737)" in SRGB_SAMPLE and
        "EXPECTED_HASH UINT32_C(0x06bfddc5)" in SRGB_SAMPLE and
        'has_extension(extensions, "GL_EXT_texture_sRGB")' in SRGB_SAMPLE and
        'strncmp((const char *)version, "2.1 ", 4)' in SRGB_SAMPLE,
        "public sRGB sample lacks its decode/version discriminator")
require("#define PS5_ENABLE_SRGB_CANDIDATE 1" in SCREEN and
        "PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE" in SCREEN and
        "caps->dest_surface_srgb_control" in SCREEN and
        "ps5_agc_gate2_set_color_target_info" in SCREEN and
        "UINT32_C(0x00008628)" in SCREEN and
        "UINT32_C(0x00008628)" in BACKEND and
        "GL_FRAMEBUFFER_SRGB" in RTT_SAMPLE and
        "DISABLED_EXPECTED_PIXEL UINT32_C(0xff373737)" in RTT_SAMPLE and
        "DISABLED_EXPECTED_HASH UINT32_C(0x06bfddc5)" in RTT_SAMPLE and
        "ENABLED_EXPECTED_PIXEL UINT32_C(0xff808080)" in RTT_SAMPLE and
        "ENABLED_EXPECTED_HASH UINT32_C(0xcec31dc5)" in RTT_SAMPLE,
        "framebuffer-sRGB candidate lacks conversion control or exact oracle")
require("PS5_ENABLE_TEXTURE_RG_CANDIDATE" in SCREEN and
        "#define PS5_ENABLE_TEXTURE_RG_CANDIDATE 1" in SCREEN and
        "*word1 = UINT32_C(0x00100000)" in SCREEN and
        "*word1 = UINT32_C(0x00e00000)" in SCREEN and
        "ps5_texture_descriptor_swizzle" in SCREEN,
        "R/RG descriptor formats or swizzle lowering are missing")
for gate in ("PS5_ENABLE_TEXTURE_SNORM_CANDIDATE",
             "PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE",
             "PS5_ENABLE_SHARED_EXPONENT_CANDIDATE",
             "PS5_ENABLE_SHADER_TEXTURE_LOD_CANDIDATE",
             "PS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE",
             "PS5_ENABLE_TEXTURE_SWIZZLE_CANDIDATE"):
    require(f"#define {gate} 1" in SCREEN,
            f"hardware-proven texture gate is not enabled by default: {gate}")
require("GL_R8" in RG_SAMPLE and "GL_RG8" in RG_SAMPLE and
        "R_PIXEL UINT32_C(0xff000040)" in RG_SAMPLE and
        "R_HASH UINT32_C(0x4bc31dc5)" in RG_SAMPLE and
        "RG_PIXEL UINT32_C(0xff008020)" in RG_SAMPLE and
        "RG_HASH UINT32_C(0x11831dc5)" in RG_SAMPLE and
        'has_extension(extensions, "GL_ARB_texture_rg")' in RG_SAMPLE,
        "public texture-RG sample lacks exact format/swizzle oracles")
require("ps5_tiled_color_offset" in SCREEN and
        "0x0001, 0x0002, 0x0004, 0x0140" in SCREEN and
        "0x0010, 0x0008, 0x0020, 0x0100" in SCREEN and
        "0x0001, 0x0002, 0x0004, 0x00c0" in SCREEN and
        "0x0008, 0x0010, 0x0020, 0x0080" in SCREEN and
        "staging_stride = (size_t)(unsigned)box->width * format_size" in
        SCREEN and
        "opengl33-public-r8-x.raw" in SCREEN and
        "opengl33-public-rg8-xy.raw" in SCREEN and
        '"r8-x", 256, 256' in RG_TILE_SAMPLE and
        '"rg8-xy", 256, 128' in RG_TILE_SAMPLE and
        "derive_affine(mapping, 8, 8)" in RG_TILE_ANALYZER and
        "derive_affine(mapping, 8, 7)" in RG_TILE_ANALYZER and
        "egl_public_texture_rg_tile.elf" in MAKEFILE,
        "measured R/RG tiled transfer path or exhaustive oracle is missing")
require("R_WIDTH 300" in RG_MULTITILE_SAMPLE and
        "R_HEIGHT 300" in RG_MULTITILE_SAMPLE and
        "RG_WIDTH 300" in RG_MULTITILE_SAMPLE and
        "RG_HEIGHT 200" in RG_MULTITILE_SAMPLE and
        '"r8-q11"' in RG_MULTITILE_SAMPLE and
        '"rg8-q11"' in RG_MULTITILE_SAMPLE and
        "egl_public_texture_rg_multitile.elf" in MAKEFILE,
        "R/RG multi-tile block-order oracle is missing")
require("#define PS5_ENABLE_PACKED_FLOAT_CANDIDATE 0" in SCREEN and
        "PIPE_FORMAT_R11G11B10_FLOAT" in SCREEN and
        "UINT32_C(0x02400000)" in SCREEN and
        "UINT32_C(0x00060718)" in SCREEN and
        "UINT32_C(0x00060718)" in BACKEND and
        "PS5_ENABLE_PACKED_FLOAT_CANDIDATE=1" in MAKEFILE and
        "egl_public_packed_float.elf" in MAKEFILE,
        "packed-float candidate lacks its gated GFX10 descriptor or target")
require((36 << 20) == 0x02400000 and
        ((6 << 2) | (7 << 8) | (1 << 17) | (1 << 18)) == 0x00060718,
        "R11G11B10 GFX10 texture or color-target encoding changed")
require('has_extension(extensions, "GL_EXT_packed_float")' in
        PACKED_FLOAT_SAMPLE and
        "GL_R11F_G11F_B10F" in PACKED_FLOAT_SAMPLE and
        "GL_RGB, GL_FLOAT, upload" in PACKED_FLOAT_SAMPLE and
        "UPLOAD_PIXEL UINT32_C(0xffff00ff)" in PACKED_FLOAT_SAMPLE and
        "UPLOAD_HASH UINT32_C(0x64e31dc5)" in PACKED_FLOAT_SAMPLE and
        "RENDER_PIXEL UINT32_C(0xff00ff00)" in PACKED_FLOAT_SAMPLE and
        "RENDER_HASH UINT32_C(0xc38d1dc5)" in PACKED_FLOAT_SAMPLE and
        "bounds_error == GL_INVALID_VALUE" in PACKED_FLOAT_SAMPLE and
        "glFramebufferTexture2D" in PACKED_FLOAT_SAMPLE and
        "The native barrier makes the packed-float render immediately sampleable" in
        PACKED_FLOAT_SAMPLE,
        "public packed-float sample lacks upload/render/bounds oracles")
require(solid_rgba_hash(0xffff00ff) == 0x64e31dc5 and
        solid_rgba_hash(0xff00ff00) == 0xc38d1dc5,
        "public packed-float solid-color FNV oracle is incorrect")
require("#define PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE 1" in SCREEN and
        "PIPE_FORMAT_R32G32B32A32_UINT" in SCREEN and
        "PIPE_FORMAT_R32G32B32A32_SINT" in SCREEN and
        "0x0010, 0x0040, 0x2000, 0x0100, 0x8200, 0x0800, 0x0400" in
        SCREEN and
        "0x0020, 0x0080, 0x1000, 0x4100, 0x0200, 0x0400, 0x0800" in
        SCREEN and
        "rgba32_x_masks, 7" in SCREEN and
        "rgba32_y_masks, 7" in SCREEN and
        "UINT32_C(0x04b00000)" in SCREEN and
        "UINT32_C(0x04c00000)" in SCREEN and
        "UINT32_C(0x00070438)" in SCREEN and
        "UINT32_C(0x00070538)" in SCREEN and
        "UINT32_C(0x00070438)" in BACKEND and
        "UINT32_C(0x00070538)" in BACKEND and
        "PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE=1" in MAKEFILE and
        "egl_public_texture_integer.elf" in MAKEFILE,
        "integer-texture candidate lacks gated descriptors or color targets")
require((75 << 20) == 0x04b00000 and (76 << 20) == 0x04c00000 and
        ((14 << 2) | (4 << 8) | (1 << 16) | (1 << 17) | (1 << 18)) ==
        0x00070438 and
        ((14 << 2) | (5 << 8) | (1 << 16) | (1 << 17) | (1 << 18)) ==
        0x00070538,
        "RGBA32 integer GFX10 texture or color-target encoding changed")
require("#define PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE 0" in SCREEN and
        all(token in SCREEN for token in (
            "PIPE_FORMAT_R8G8B8A8_UINT", "PIPE_FORMAT_R8G8B8A8_SINT",
            "PIPE_FORMAT_R16G16B16A16_UINT", "PIPE_FORMAT_R16G16B16A16_SINT",
            "0x0008, 0x0020, 0x0040, 0x2100, 0x0200, 0x0800, 0x8400",
            "0x0010, 0x0080, 0x1000, 0x0100, 0x4200, 0x0400, 0x0800",
            "UINT32_C(0x03c00000)", "UINT32_C(0x03d00000)",
            "UINT32_C(0x04500000)", "UINT32_C(0x04600000)",
            "UINT32_C(0x00070428)", "UINT32_C(0x00070528)",
            "UINT32_C(0x00070430)", "UINT32_C(0x00070530)")) and
        "PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE=1" in MAKEFILE and
        "egl_public_texture_integer_narrow.elf" in MAKEFILE,
        "narrow integer texture candidate lacks gated native formats")
require((60 << 20) == 0x03c00000 and (61 << 20) == 0x03d00000 and
        (69 << 20) == 0x04500000 and (70 << 20) == 0x04600000 and
        ((10 << 2) | (4 << 8) | (1 << 16) | (1 << 17) | (1 << 18)) ==
        0x00070428 and
        ((10 << 2) | (5 << 8) | (1 << 16) | (1 << 17) | (1 << 18)) ==
        0x00070528 and
        ((12 << 2) | (4 << 8) | (1 << 16) | (1 << 17) | (1 << 18)) ==
        0x00070430 and
        ((12 << 2) | (5 << 8) | (1 << 16) | (1 << 17) | (1 << 18)) ==
        0x00070530,
        "narrow integer GFX10 texture or color-target encoding changed")
require(all(token in TEXTURE_INTEGER_NARROW_SAMPLE for token in (
             "GL_RGBA8UI", "GL_RGBA8I", "GL_RGBA16UI", "GL_RGBA16I",
             "#define UPLOAD_SIZE 128", "run_upload_case",
             "passed_uploads", "uploads=%u/%u",
            "uvec4(255u, 85u, 17u, 2u)", "ivec4(-128, 63, -7, 1)",
            "uvec4(65535u,21845u,4369u,2u)",
            "ivec4(-32768,16383,-257,1)",
            "UINT32_C(0x64e31dc5)", "UINT32_C(0xc38d1dc5)",
            "UINT32_C(0xf1461dc5)", "UINT32_C(0x5f3a1dc5)")) and
        "glFramebufferTexture2D" in TEXTURE_INTEGER_NARROW_SAMPLE and
        "result->bounds_error != GL_INVALID_VALUE" in
        TEXTURE_INTEGER_NARROW_SAMPLE and
        "egl_public_texture_integer_narrow_rgba16i.o:" in MAKEFILE and
        "egl_public_texture_integer_narrow_uploads.o:" in MAKEFILE,
        "public narrow integer sample lacks its four exact oracles")
require('has_extension(extensions, "GL_EXT_texture_integer")' in
        TEXTURE_INTEGER_SAMPLE and
        'has_extension(extensions, "GL_EXT_gpu_shader4")' in
        TEXTURE_INTEGER_SAMPLE and
        "#define UPLOAD_SIZE 128" in TEXTURE_INTEGER_SAMPLE and
        "vec2(0.25, 0.25)" in TEXTURE_INTEGER_SAMPLE and
        "vec2(0.75, 0.75)" in TEXTURE_INTEGER_SAMPLE and
        "GL_RGBA32UI, GL_UNSIGNED_INT" in TEXTURE_INTEGER_SAMPLE and
        "GL_RGBA32I, GL_INT" in TEXTURE_INTEGER_SAMPLE and
        '"#version 120\\n"' in TEXTURE_INTEGER_SAMPLE and
        '"#extension GL_EXT_gpu_shader4 : require\\n"' in
        TEXTURE_INTEGER_SAMPLE and
        "varying out uvec4 frag_value" in TEXTURE_INTEGER_SAMPLE and
        "varying out ivec4 frag_value" in TEXTURE_INTEGER_SAMPLE and
        "texture2D(u_texture, vec2(0.5))" in TEXTURE_INTEGER_SAMPLE and
        "uniform usampler2D u_texture" in TEXTURE_INTEGER_SAMPLE and
        "uniform isampler2D u_texture" in TEXTURE_INTEGER_SAMPLE and
        "uvec4(255u, 0u, 255u, 1u)" in TEXTURE_INTEGER_SAMPLE and
        "ivec4(-1, 7, -3, 1)" in TEXTURE_INTEGER_SAMPLE and
        "UINT_HASH UINT32_C(0x64e31dc5)" in TEXTURE_INTEGER_SAMPLE and
        "SINT_HASH UINT32_C(0xc38d1dc5)" in TEXTURE_INTEGER_SAMPLE and
        "run_upload_case" in TEXTURE_INTEGER_SAMPLE and
        "GL_RGBA32UI, GL_UNSIGNED_INT, uint_texels" in
        TEXTURE_INTEGER_SAMPLE and
        "GL_RGBA32I, GL_INT, sint_texels" in TEXTURE_INTEGER_SAMPLE and
        "uint_upload_ok && sint_upload_ok" in TEXTURE_INTEGER_SAMPLE and
        "result->bounds_error == GL_INVALID_VALUE" in
        TEXTURE_INTEGER_SAMPLE and
        "glFramebufferTexture2D" in TEXTURE_INTEGER_SAMPLE,
        "public integer-texture sample lacks render/upload/sample oracles")
require("#define PS5_ENABLE_RGB10_A2UI_CANDIDATE 0" in SCREEN and
        "PIPE_FORMAT_R10G10B10A2_UINT" in SCREEN and
        "UINT32_C(0x03600000)" in SCREEN and
        "UINT32_C(0x00070424)" in SCREEN and
        "UINT32_C(0x00070424)" in BACKEND and
        "PS5_ENABLE_RGB10_A2UI_CANDIDATE=1" in MAKEFILE and
        "egl_public_rgb10_a2ui.elf" in MAKEFILE,
        "RGB10_A2UI candidate lacks its gated descriptor or color target")
require((54 << 20) == 0x03600000 and
        ((9 << 2) | (4 << 8) | (1 << 16) | (1 << 17) | (1 << 18)) ==
        0x00070424,
        "RGB10_A2UI GFX10 texture or color-target encoding changed")
require('has_extension(extensions, "GL_ARB_texture_rgb10_a2ui")' in
        RGB10_A2UI_SAMPLE and
        'has_extension(extensions, "GL_EXT_gpu_shader4")' in
        RGB10_A2UI_SAMPLE and
        "GL_RGB10_A2UI" in RGB10_A2UI_SAMPLE and
        "GL_UNSIGNED_INT_2_10_10_10_REV" in RGB10_A2UI_SAMPLE and
        '"#version 120\\n"' in RGB10_A2UI_SAMPLE and
        '"#extension GL_EXT_gpu_shader4 : require\\n"' in
        RGB10_A2UI_SAMPLE and
        "varying out uvec4 frag_value" in RGB10_A2UI_SAMPLE and
        "texture2D(u_texture, vec2(0.5))" in RGB10_A2UI_SAMPLE and
        "uniform usampler2D u_texture" in RGB10_A2UI_SAMPLE and
        "uvec4(1023u, 341u, 17u, 2u)" in RGB10_A2UI_SAMPLE and
        "EXPECTED_PIXEL UINT32_C(0xffffff00)" in RGB10_A2UI_SAMPLE and
        "EXPECTED_HASH UINT32_C(0xf1461dc5)" in RGB10_A2UI_SAMPLE and
        "bounds_error == GL_INVALID_VALUE" in RGB10_A2UI_SAMPLE and
        "glFramebufferTexture2D" in RGB10_A2UI_SAMPLE,
        "public RGB10_A2UI sample lacks its exact render/sample oracle")
require(solid_rgba_hash(0xffffff00) == 0xf1461dc5,
        "public RGB10_A2UI solid-color FNV oracle is incorrect")
require("#define PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE 0" in SCREEN and
        "target == PIPE_TEXTURE_RECT" in SCREEN and
        "PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE=1" in MAKEFILE and
        "egl_public_texture_rectangle.elf" in MAKEFILE,
        "texture-rectangle candidate lacks its default-off resource route")
require('has_extension(extensions, "GL_ARB_texture_rectangle")' in
        RECTANGLE_SAMPLE and
        "uniform sampler2DRect u_texture" in RECTANGLE_SAMPLE and
        "texture2DRect(u_texture, vec2(16.5, 0.5))" in RECTANGLE_SAMPLE and
        "x == 16" in RECTANGLE_SAMPLE and
        "EXPECTED_PIXEL UINT32_C(0xffff00ff)" in RECTANGLE_SAMPLE and
        "EXPECTED_HASH UINT32_C(0x64e31dc5)" in RECTANGLE_SAMPLE and
        "bounds_error == GL_INVALID_VALUE" in RECTANGLE_SAMPLE and
        "ps5_egl_current_draw_status" in RECTANGLE_SAMPLE,
        "public rectangle sample lacks its pixel-coordinate discriminator")
require("PS5_ENABLE_COMPRESSED_FALLBACK_CANDIDATE" in EGL and
        "options.allow_compressed_fallback" in EGL and
        "PIPE_FORMAT_R8_SNORM" in SCREEN and
        "PIPE_FORMAT_R8G8_SNORM" in SCREEN and
        "*word1 = UINT32_C(0x00200000)" in SCREEN and
        "*word1 = UINT32_C(0x00f00000)" in SCREEN,
        "RGTC fallback lacks its Mesa option or signed destination formats")
require("GL_COMPRESSED_RED_RGTC1" in RG_SAMPLE and
        "GL_COMPRESSED_SIGNED_RED_RGTC1" in RG_SAMPLE and
        "GL_COMPRESSED_RG_RGTC2" in RG_SAMPLE and
        "GL_COMPRESSED_SIGNED_RG_RGTC2" in RG_SAMPLE and
        'has_extension(extensions, "GL_ARB_texture_compression_rgtc")' in
        RG_SAMPLE and
        '"rgtc2-snorm-fallback"' in RG_SAMPLE,
        "public RGTC fallback sample lacks all four format oracles")

require("extensions->ARB_half_float_vertex = GL_TRUE;" in EXTENSIONS,
        "Mesa no longer enables ARB_half_float_vertex by default")
require("case GL_HALF_FLOAT:" in VARRAY and
        "ctx->Extensions.ARB_half_float_vertex" in VARRAY and
        "PIPE_FORMAT_R16G16_FLOAT" in VARRAY,
        "Mesa half-float vec2 validation/format route changed")
require("{ PIPE_FORMAT_R16G16_FLOAT,         PIPE_FORMAT_R32G32_FLOAT }" in UVBUF,
        "u_vbuf half-float vec2 fallback route changed")
require("caps->format_translation[format] = vbuf_format_fallbacks[i].to;" in UVBUF and
        "caps->fallback_always = true;" in UVBUF and
        "u_upload_alloc(mgr->pipe->stream_uploader" in UVBUF and
        "tr->run(tr, 0, num_vertices, 0, 0, out_map);" in UVBUF and
        "pipe_resource_release(pipe, releasebuf);" in UVBUF,
        "u_vbuf activation, conversion, or transient lifetime route changed")
require("case PIPE_FORMAT_R16G16_FLOAT:" in TRANSLATE and
        "return &emit_R16G16_FLOAT;" in TRANSLATE,
        "generic half-float widening emitter is missing")
require("if (caps.fallback_always ||" in CSO and
        "cso->vbuf = u_vbuf_create(cso->base.pipe, &caps);" in CSO and
        "ctx->base.draw_vbo = u_vbuf_draw_vbo;" in CSO,
        "CSO no longer installs u_vbuf for an always-fallback screen")
require("PIPE_FORMAT_R32G32_FLOAT" in VERTEX_FORMAT and
        "PIPE_FORMAT_R16G16_FLOAT" not in VERTEX_FORMAT,
        "PS5 direct vertex-format boundary no longer forces half widening")
require("vertex_formats[(type & 0x3f) | ((int)doubles << 5)][index][size-1]" in VARRAY and
        "unsigned index = integer*2 + normalized;" in VARRAY and
        "PIPE_FORMAT_R16G16_SNORM" in VARRAY,
        "Mesa normalized GL_SHORT vec2 format route changed")
require("{ PIPE_FORMAT_R16G16_SNORM,         PIPE_FORMAT_R32G32_FLOAT }" in UVBUF,
        "u_vbuf normalized-short vec2 fallback route changed")
require("case PIPE_FORMAT_R16G16_SNORM:" in TRANSLATE and
        "return &emit_R16G16_SNORM;" in TRANSLATE,
        "generic normalized-short widening emitter is missing")
require("PIPE_FORMAT_R16G16_SNORM" not in VERTEX_FORMAT,
        "PS5 direct vertex-format boundary no longer forces normalized widening")
require("bool                 base_vertex_valid;" in PSBC_HEADER and
        "uint32_t             base_vertex_user_data_dword;" in PSBC_HEADER and
        "bool                 start_instance_valid;" in PSBC_HEADER and
        "uint32_t             start_instance_user_data_dword;" in PSBC_HEADER and
        "uint32_t         instance_divisor;" in PSBC_HEADER and
        "#define PSBC_SHADER_METADATA_VERSION 8u" in PSBC_HEADER and
        "metadata->version = PSBC_SHADER_METADATA_VERSION;" in PSBC_COMPILE and
        "ctx->rargs->ac.base_vertex.used" in PSBC_COMPILE and
        "ctx->rargs->ac.start_instance.used" in PSBC_COMPILE and
        "gfx_state.vi.instance_rate_inputs" in PSBC_COMPILE and
        "gfx_state.vi.instance_rate_divisors" in PSBC_COMPILE and
        "AC_UD_VS_BASE_VERTEX_START_INSTANCE" in PSBC_COMPILE,
        "PSBC instanced-input ABI is incomplete")
require("nir_load_first_vertex(b), nir_load_vertex_id_zero_base(b)" in RADV_INPUTS and
        "AC_UD_VS_BASE_VERTEX_START_INSTANCE" in RADV_ARGS and
        "case nir_intrinsic_load_first_vertex:" in AC_INTRINSICS and
        "s->args->base_vertex" in AC_INTRINSICS,
        "vendored RADV/ACO base-vertex ABI route changed")
require("ps5_lower_first_vertex" not in SCREEN and
        "(info->index_size && draws[0].index_bias)" not in SCREEN and
        ": draws[0].start;" in SCREEN and
        "(uint64_t)draws[0].start + draws[0].count" in SCREEN and
        '"[ps5-gallium] first-vertex first=%u count=%u descriptor-count=%u' in SCREEN and
        "effective_min = (int64_t)min_index + draws[0].index_bias;" in SCREEN and
        "effective_end = (int64_t)max_index + draws[0].index_bias + 1;" in SCREEN and
        "effective_min < 0 || effective_end > UINT32_MAX" in SCREEN and
        "vertex_metadata->base_vertex_user_data_dword" in SCREEN and
        "user_data[vertex_metadata->base_vertex_user_data_dword] = base_vertex;" in SCREEN,
        "PS5 base-vertex preservation, bounds, or user-data route is incomplete")
require("metadata->version != PSBC_SHADER_METADATA_VERSION" in PACKAGE,
        "PS5 package builder does not enforce the current PSBC metadata ABI")
require("caps->vs_instanceid = true;" in SCREEN and
        "caps->vertex_element_instance_divisor = true;" in SCREEN and
        "!info->instance_count" in SCREEN and
        "info->instance_count != 1" not in SCREEN and
        "attribute->instance_divisor = element->instance_divisor;" in SCREEN and
        "binding_records[element->vertex_buffer_index]" in SCREEN and
        "vertex_metadata->start_instance_user_data_dword" in SCREEN and
        "ps5_agc_gate2_set_instance_count(info->instance_count)" in SCREEN,
        "PS5 instancing cap, compiler, bounds, or submission route is incomplete")
require('"sceAgcDcbSetNumInstances"' in BACKEND and
        "ps5_agc_instance_count == 1" in BACKEND and
        "ps5_agc_set_instances(command, ps5_agc_instance_count)" in BACKEND and
        "ps5_agc_set_instances(command, 1)" in BACKEND,
        "AGC instance packet route or single-instance preservation is incomplete")

require("#define PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE 0" in SCREEN and
        "case PIPE_BLENDFACTOR_SRC1_COLOR:" in SCREEN and
        "*native = 15" in SCREEN and "*native = 16" in SCREEN and
        "*native = 17" in SCREEN and "*native = 18" in SCREEN and
        "caps->max_dual_source_render_targets =" in SCREEN and
        "ps5_agc_gate2_set_dual_source_blend" in SCREEN and
        "BITFIELD64_BIT(FRAG_RESULT_DUAL_SRC_BLEND)" in PSBC_COMPILE and
        "gfx_state.ps.epilog.mrt0_is_dual_src = true" in PSBC_COMPILE and
        "spi_shader_col_format = UINT32_C(0x44)" in PSBC_COMPILE and
        "ps5_agc_dual_source_blend" in BACKEND and
        "records, &count, 0x01d8u, 0" in BACKEND and
        "0x01e1u, UINT32_C(1) << 30" in BACKEND,
        "dual-source compiler, Gallium, or GFX10 blend pairing is incomplete")

require('return "1.4 PS5 experimental"' in EGL and
        "PS5_ENABLE_CORE_CONTEXT_CANDIDATE" in EGL and
        '"EGL_KHR_create_context" : ""' in EGL,
        "EGL identity/extensions are not conservative")
require("EGL_NON_CONFORMANT_CONFIG" in EGL and
        "EGL_OPENGL_BIT" in EGL and "EGL_WINDOW_BIT" in EGL,
        "single non-conformant OpenGL window config is incomplete")
require("major = compat / 10" in EGL and "minor = compat % 10" in EGL and
        "API_OPENGL_COMPAT" in EGL and "API_OPENGL_CORE" in EGL and
        "EGL_CONTEXT_MAJOR_VERSION_KHR" in EGL and
        "EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR" in EGL and
        '"[ps5-egl] versions core=%d compat=%d requested=%d.%d '
        'profile=%04x\\n"' in EGL,
        "context creation does not retain Mesa's truthful compatibility ceiling")
require("ps5_report_core_predicates" not in EGL and
        "core-predicates" not in EGL,
        "one-shot Core predicate diagnostic was not removed")
require("ps5_live_surface" in EGL and "ps5_live_context" in EGL and
        "value == ps5_live_surface" in EGL and
        "value == ps5_live_context" in EGL,
        "opaque EGL handles are not bounded to the one-object model")
require("PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET" in EGL and
        "ST_ATTACHMENT_FRONT_LEFT" in EGL and
        "st_api_make_current" in EGL and "st_context_flush" in EGL,
        "EGL facade is not wired to the proven fullscreen frontend path")
surface_body = EGL.split("struct ps5_egl_surface {", 1)[1].split("};", 1)[0]
require(surface_body.index("struct pipe_frontend_drawable drawable;") <
        surface_body.index("uint32_t magic;"),
        "drawable must lead ps5_egl_surface for callback base-pointer casts")
require("st_api_destroy_drawable" in EGL and "st_destroy_context" in EGL and
        "st_screen_destroy" in EGL and "pipe_resource_reference" in EGL,
        "EGL teardown ownership is incomplete")

require("MESA_LIBGLAPI_BRIDGE" in MAKEFILE and
        "egl_public_triangle.elf" in MAKEFILE and
        "egl_public_dual_source_blend.elf" in MAKEFILE and
        "egl_public_core33_dual_source.elf" in MAKEFILE and
        "ps5_egl_core33.o" in MAKEFILE and
        "ps5_screen_core33.o" in MAKEFILE and
        "-DPS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE=1" in MAKEFILE and
        "egl_public_texture_depth.elf" in MAKEFILE and
        "egl_public_mixed_fbo.elf" in MAKEFILE and
        "egl_public_render_to_texture.elf" in MAKEFILE and
        "egl_public_depth_texture.elf" in MAKEFILE and
        "egl_public_framebuffer_srgb.elf" in MAKEFILE and
        "egl_public_map_buffer_range.elf" in MAKEFILE and
        "egl_public_half_float_vertex.elf" in MAKEFILE and
        "egl_public_normalized_short_vertex.elf" in MAKEFILE and
        "egl_public_base_vertex.elf" in MAKEFILE and
        "egl_public_first_vertex.elf" in MAKEFILE and
        "egl_public_draw_instanced.elf" in MAKEFILE and
        "egl_public_instanced_arrays.elf" in MAKEFILE and
        "egl_public_srgb_texture.elf" in MAKEFILE and
        "egl_public_texture_rg.elf" in MAKEFILE and
        "egl_public_texture_formats_default.elf" in MAKEFILE and
        "egl_public_texture_rg_default.elf" in MAKEFILE and
        "egl_public_texture_swizzle_default.elf" in MAKEFILE and
        "egl_public_texture_mipmap_default.elf" in MAKEFILE and
        "-DPS5_TEXTURE_RG_DEFAULT_ONLY=1" in MAKEFILE and
        "egl_public_texture_rgtc.elf" in MAKEFILE and
        "ps5_egl_compressed.o" in MAKEFILE and
        "-DPS5_ENABLE_SRGB_CANDIDATE=1" in MAKEFILE and
        "-DPS5_ENABLE_TEXTURE_RG_CANDIDATE=1" in MAKEFILE and
        "src/egl/ps5_egl.c" in MAKEFILE,
        "PS5 target does not link the EGL facade and public GL bridge")
require("src/mesa/glapi/glapi/libglapi_bridge.a" in BUILD,
        "reproducible Mesa build omits the public GL bridge")

print("egl-public-samples: PASS api=EGL1.4/OpenGL compat-current "
      "surface=RGBA8 triangle=4096-magenta texture-depth=red,green "
      "mixed-fbo=padded-offline render-to-texture=tiled+release-mem "
      "depth-texture=d32f-raw+shadow-offline "
      "framebuffer-srgb=disabled/enabled "
      "map-range=offset64,length24 half-float=u_vbuf-r32g32 "
      "snorm16=u_vbuf-r32g32 base-vertex=sgpr+bounds first-vertex=sgpr "
      "draw-instanced=instanceid+packet instanced-arrays=divisor+per-binding-bounds "
      "srgb=decode-default texture-rg=measured-tiled-transfer+multitile "
      "rgtc=cpu-fallback packed-float=gated-upload+render "
      "texture-integer=hardware-rgba32-upload+render+sample rgb10-a2ui=gated-render+sample "
      "texture-rectangle=gated-lowered-coordinates core33-context=hardware "
      "dual-source-blend=hardware-proven")
