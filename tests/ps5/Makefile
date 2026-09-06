SHELL := bash

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk/
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

PROJECT := ../..
MESA_SRC := $(PROJECT)/third_party/mesa-26.2.0
MESA_BUILD := $(PROJECT)/build/mesa-ps5-probe
DRIVER := $(PROJECT)/src/gallium/ps5
NATIVE_RUNTIME := $(PROJECT)/src/platform/ps5_agc_native_runtime.c
PSBC := $(PROJECT)/third_party/opengnm-psbc
GATE1 := $(PROJECT)/build/gate1

CFLAGS := -std=c11 -Os -g -Wall -Wextra -Werror
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -DHAVE_FUNC_ATTRIBUTE_PACKED=1
CFLAGS += -I$(MESA_BUILD)/src -I$(MESA_SRC)/src
CFLAGS += -I$(MESA_SRC)/include -I$(MESA_SRC)/src/gallium/include -I$(DRIVER)
CFLAGS += -I$(MESA_SRC)/src/gallium/auxiliary
LDFLAGS := -Wl,--gc-sections -Wl,--build-id=sha1 -lSceSystemService
MESA_LIBMESA := $(MESA_BUILD)/src/mesa/libmesa.a
MESA_LIBMESA_SSE41 := $(MESA_BUILD)/src/mesa/libmesa_sse41.a
MESA_LIBGALLIUM := $(MESA_BUILD)/src/gallium/auxiliary/libgallium.a
MESA_LIBGLAPI := $(MESA_BUILD)/src/mesa/glapi/shared-glapi/libglapi.a
MESA_LIBGLAPI_BRIDGE := $(MESA_BUILD)/src/mesa/glapi/glapi/libglapi_bridge.a
MESA_LIBGLSL := $(MESA_BUILD)/src/compiler/glsl/libglsl.a
MESA_LIBGLCPP := $(MESA_BUILD)/src/compiler/glsl/glcpp/libglcpp.a
MESA_LIBGLSL_UTIL := $(MESA_BUILD)/src/compiler/glsl/libglsl_util.a
MESA_LIBVTN := $(MESA_BUILD)/src/compiler/spirv/libvtn.a
MESA_LIBNIR := $(MESA_BUILD)/src/compiler/nir/libnir.a
MESA_LIBCOMPILER := $(MESA_BUILD)/src/compiler/libcompiler.a
MESA_LIBUTIL := $(MESA_BUILD)/src/util/libmesa_util.a
MESA_LIBUTIL_SIMD := $(MESA_BUILD)/src/util/libmesa_util_simd.a
MESA_LIBBLAKE3 := $(MESA_BUILD)/src/util/blake3/libblake3.a
MESA_LIBC11 := $(MESA_BUILD)/src/c11/impl/libmesa_util_c11.a
MESA_CONTEXT_LIBS := $(MESA_LIBMESA) $(MESA_LIBMESA_SSE41) \
	$(MESA_LIBGALLIUM) $(MESA_LIBGLAPI) \
	$(MESA_LIBGLSL) $(MESA_LIBGLCPP) $(MESA_LIBGLSL_UTIL) $(MESA_LIBVTN) \
	$(MESA_LIBNIR) $(MESA_LIBCOMPILER) $(MESA_LIBUTIL) \
	$(MESA_LIBUTIL_SIMD) $(MESA_LIBBLAKE3) $(MESA_LIBC11)
PSBC_CFLAGS := -DHAVE_PTHREAD=1 -DHAVE_STRUCT_TIMESPEC=1 \
	-Wno-error=unused-parameter -Wno-error=missing-field-initializers \
	-iquote$(PSBC)/src -I$(PSBC)/libpsbc -I$(PSBC)/src

ps5_screen.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_packed.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_ENABLE_PACKED_DEPTH_STENCIL=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_public_stencil.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_ENABLE_PACKED_DEPTH_STENCIL=1 \
		-DPS5_PUBLIC_STENCIL_TEST=1 -I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_srgb.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_ENABLE_SRGB_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_mixed_fbo.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_ENABLE_PADDED_FBO_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_render_to_texture.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_dynamic_color_target.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_color_blit.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_PADDED_FBO_CANDIDATE=1 \
		-DPS5_ENABLE_SOFTWARE_BLIT_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_texture_rg_render.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_TEXTURE_RG_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-DPS5_PUBLIC_TEXTURE_RG_RENDER_TEST=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_packed_float.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_PACKED_FLOAT_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_texture_integer.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_TEXTURE_INTEGER_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_texture_integer_narrow.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_TEXTURE_INTEGER_CANDIDATE=1 \
		-DPS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_rgb10_a2ui.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_RGB10_A2UI_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_texture_rectangle.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_texture_rg_tile.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_TEXTURE_RG_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-DPS5_PUBLIC_TEXTURE_RG_TILE_TEST=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_large_pool.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_RENDER_POOL_BYTES=0x4000000u \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_depth_texture.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_PADDED_FBO_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_DEPTH_TEXTURE_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_framebuffer_srgb.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_ENABLE_SRGB_CANDIDATE=1 \
		-DPS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_texture_formats.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_ENABLE_SRGB_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_RG_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_SWIZZLE_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_SNORM_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_FLOAT_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_EXPONENT_CANDIDATE=1 \
		-DPS5_ENABLE_SHADER_TEXTURE_LOD_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_MIPMAP_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_CUBE_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_ARRAY_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_3D_CANDIDATE=1 \
		-DPS5_ENABLE_UBO_CANDIDATE=1 \
		-DPS5_ENABLE_TIMER_QUERY_CANDIDATE=1 \
		-DPS5_ENABLE_OCCLUSION_QUERY_CANDIDATE=1 \
		-DPS5_ENABLE_DEPTH_CLAMP_CANDIDATE=1 \
		-DPS5_ENABLE_PACKED_VERTEX_CANDIDATE=1 \
		-DPS5_ENABLE_PACKED_DEPTH_STENCIL=1 \
		-DPS5_ENABLE_PADDED_FBO_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_DEPTH_TEXTURE_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_SHARED_RENDER_POOL_CANDIDATE=1 \
		-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE=1 \
		-DPS5_ENABLE_FAKE_SW_MSAA_CANDIDATE=1 \
		-DPS5_ENABLE_SEAMLESS_CUBE_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_geometry.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_ENABLE_UBO_CANDIDATE=1 \
		-DPS5_ENABLE_GEOMETRY_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_mrt.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_ENABLE_MRT_CANDIDATE=1 \
		-DPS5_ENABLE_INDEPENDENT_BLEND_CANDIDATE=1 \
		-DPS5_ENABLE_PADDED_FBO_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_dual_source_blend.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_core33.o: $(DRIVER)/ps5_screen.c $(DRIVER)/ps5_screen.h \
	$(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_RENDER_POOL_BYTES=0x4000000u \
		-DPS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE=1 \
		-DPS5_ENABLE_PACKED_FLOAT_CANDIDATE=1 \
		-DPS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE=1 \
		-DPS5_ENABLE_RGB10_A2UI_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_1D_CANDIDATE=1 \
		-DPS5_ENABLE_OCCLUSION_QUERY_CANDIDATE=1 \
		-DPS5_ENABLE_DEPTH_CLAMP_CANDIDATE=1 \
		-DPS5_ENABLE_GLSL_330_CANDIDATE=1 \
		-DPS5_ENABLE_PACKED_VERTEX_CANDIDATE=1 \
		-DPS5_ENABLE_INTEGER_VERTEX_CANDIDATE=1 \
		-DPS5_ENABLE_PACKED_DEPTH_STENCIL=1 \
		-DPS5_ENABLE_PADDED_FBO_CANDIDATE=1 \
		-DPS5_ENABLE_SOFTWARE_BLIT_CANDIDATE=1 \
		-DPS5_ENABLE_DEPTH_TEXTURE_CANDIDATE=1 \
		-DPS5_ENABLE_DYNAMIC_DEPTH_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_MSAA4_CANDIDATE=1 \
		-DPS5_ENABLE_MSAA_ARRAY_CANDIDATE=1 \
		-DPS5_ENABLE_SEAMLESS_CUBE_CANDIDATE=1 \
		-DPS5_ENABLE_GEOMETRY_CANDIDATE=1 \
		-DPS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE=1 \
		-DPS5_ENABLE_MRT_CANDIDATE=1 \
		-DPS5_ENABLE_INDEPENDENT_BLEND_CANDIDATE=1 \
		-DPS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE=1 \
		-DPS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE=1 \
		-DPS5_ENABLE_POINT_LINE_SIZE_CANDIDATE=1 \
		-DPS5_ENABLE_POINT_COORD_CANDIDATE=1 \
		-DPS5_ENABLE_BORDER_COLOR_CANDIDATE=1 \
		-DPS5_ENABLE_TEXTURE_BUFFER_CANDIDATE=1 \
		-DPS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE=1 \
		-DPS5_ENABLE_CORE_RENDER_FORMATS_CANDIDATE=1 \
		-DPS5_ENABLE_SMOOTH_RASTER_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_transform_feedback.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_occlusion_query.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_OCCLUSION_QUERY_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_depth_clamp.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_DEPTH_CLAMP_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_glsl330.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_GLSL_330_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

ps5_screen_packed_vertex.o: $(DRIVER)/ps5_screen.c \
	$(DRIVER)/ps5_screen.h $(PROJECT)/src/platform/ps5_agc_package.h
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) \
		-DPS5_ENABLE_PACKED_VERTEX_CANDIDATE=1 \
		-I$(PROJECT)/src/platform -c -o $@ $<

u_framebuffer.o: $(MESA_SRC)/src/gallium/auxiliary/util/u_framebuffer.c
	$(CC) $(CFLAGS) -Wno-error=unused-parameter -c -o $@ $<

gallium_lifecycle.o: gallium_lifecycle.c
	$(CC) $(CFLAGS) -c -o $@ $<

gallium_lifecycle.elf: gallium_lifecycle.o ps5_screen.o u_framebuffer.o \
	ps5_agc_package.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_lifecycle.o ps5_screen.o u_framebuffer.o \
		ps5_agc_package.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gate2_backend.o: $(PROJECT)/src/platform/ps5_agc_runtime_backend.c \
	$(NATIVE_RUNTIME)
	$(CC) $(CFLAGS) -Wno-error=unused-function \
		-Dmain=ps5_agc_gate2_run \
		-DAGC_TRIANGLE_SUBMIT=1 -DAGC_RUNTIME_PACKAGES=1 \
		-c -o $@ $(PROJECT)/src/platform/ps5_agc_runtime_backend.c

gate2_empty_backend.o: $(NATIVE_RUNTIME)
	$(CC) $(CFLAGS) -Wno-error=unused-function \
		-Dmain=ps5_agc_gate2_run \
		-DAGC_TRIANGLE_SUBMIT=1 -DAGC_RUNTIME_PACKAGES=1 \
		-DAGC_EXPECT_EMPTY_TARGET=1 \
		-c -o $@ $(NATIVE_RUNTIME)

gallium_gate2_triangle.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -c -o $@ $<

gallium_gate2_triangle.elf: gallium_gate2_triangle.o gate2_backend.o \
	ps5_screen.o ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_gate2_triangle.o ps5_screen.o ps5_agc_package.o \
		u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_mrt_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_MRT_TEST=1 -c -o $@ $<

gallium_mrt_candidate.elf: gallium_mrt_candidate.o gate2_backend.o \
	ps5_screen_mrt.o ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_mrt_candidate.o ps5_screen_mrt.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		-Wl,--start-group $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

gallium_packed_stencil_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_PACKED_STENCIL_TEST=1 \
		-c -o $@ $<

gallium_packed_stencil_candidate.elf: gallium_packed_stencil_candidate.o \
	gate2_backend.o ps5_screen_packed.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_LIBGALLIUM)
	$(CXX) -o $@ gallium_packed_stencil_candidate.o ps5_screen_packed.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(MESA_LIBGALLIUM) $(LDFLAGS)

gallium_fixed_state_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-c -o $@ $<

gallium_fixed_state_candidate.elf: gallium_fixed_state_candidate.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_fixed_state_candidate.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_color_mask_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_COLOR_MASK_TEST=1 -c -o $@ $<

gallium_color_mask_candidate.elf: gallium_color_mask_candidate.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_color_mask_candidate.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_cull_back_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_CULL_BACK_TEST=1 -c -o $@ $<

gallium_cull_back_candidate.elf: gallium_cull_back_candidate.o \
	gate2_empty_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_cull_back_candidate.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_empty_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_cull_front_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_CULL_FRONT_TEST=1 -c -o $@ $<

gallium_cull_front_candidate.elf: gallium_cull_front_candidate.o \
	gate2_empty_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_cull_front_candidate.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_empty_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_polygon_offset_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_POLYGON_OFFSET_TEST=1 -c -o $@ $<

gallium_polygon_offset_candidate.elf: gallium_polygon_offset_candidate.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_polygon_offset_candidate.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_polygon_offset_negative_units.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_POLYGON_OFFSET_NEGATIVE_UNITS_TEST=1 -c -o $@ $<

gallium_polygon_offset_negative_units.elf: \
	gallium_polygon_offset_negative_units.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_polygon_offset_negative_units.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_polygon_offset_slope_baseline.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_POLYGON_OFFSET_SLOPE_BASELINE=1 -c -o $@ $<

gallium_polygon_offset_slope_baseline.elf: \
	gallium_polygon_offset_slope_baseline.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_polygon_offset_slope_baseline.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_polygon_offset_slope_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_POLYGON_OFFSET_SLOPE_CANDIDATE=1 -c -o $@ $<

gallium_polygon_offset_slope_candidate.elf: \
	gallium_polygon_offset_slope_candidate.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_polygon_offset_slope_candidate.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_polygon_offset_negative_factor.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_POLYGON_OFFSET_NEGATIVE_FACTOR_TEST=1 -c -o $@ $<

gallium_polygon_offset_negative_factor.elf: \
	gallium_polygon_offset_negative_factor.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_polygon_offset_negative_factor.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_line_polygon_baseline.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_LINE_POLYGON_BASELINE=1 -c -o $@ $<

gallium_line_polygon_baseline.elf: gallium_line_polygon_baseline.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_line_polygon_baseline.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_line_polygon_offset_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_LINE_POLYGON_OFFSET_CANDIDATE=1 -c -o $@ $<

gallium_line_polygon_offset_candidate.elf: \
	gallium_line_polygon_offset_candidate.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_line_polygon_offset_candidate.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_point_polygon_baseline.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_POINT_POLYGON_BASELINE=1 -c -o $@ $<

gallium_point_polygon_baseline.elf: gallium_point_polygon_baseline.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_point_polygon_baseline.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_point_polygon_offset_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_POINT_POLYGON_OFFSET_CANDIDATE=1 -c -o $@ $<

gallium_point_polygon_offset_candidate.elf: \
	gallium_point_polygon_offset_candidate.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_point_polygon_offset_candidate.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_point_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_POINT_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_point_primitive.elf: gallium_point_primitive.o gate2_backend.o \
	ps5_screen.o ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_point_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_line_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_LINE_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_line_primitive.elf: gallium_line_primitive.o gate2_backend.o \
	ps5_screen.o ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_line_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_multi_point_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_MULTI_POINT_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_multi_point_primitive.elf: gallium_multi_point_primitive.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_multi_point_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_multi_line_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_MULTI_LINE_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_multi_line_primitive.elf: gallium_multi_line_primitive.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_multi_line_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_line_strip_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_LINE_STRIP_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_line_strip_primitive.elf: gallium_line_strip_primitive.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_line_strip_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_triangle_strip_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_TRIANGLE_STRIP_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_triangle_strip_primitive.elf: gallium_triangle_strip_primitive.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_triangle_strip_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_triangle_fan_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_TRIANGLE_FAN_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_triangle_fan_primitive.elf: gallium_triangle_fan_primitive.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_triangle_fan_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_indexed_multi_point_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_MULTI_POINT_PRIMITIVE_TEST=1 \
		-DPS5_INDEXED_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_indexed_multi_point_primitive.elf: \
	gallium_indexed_multi_point_primitive.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_indexed_multi_point_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_indexed_multi_line_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_MULTI_LINE_PRIMITIVE_TEST=1 \
		-DPS5_INDEXED_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_indexed_multi_line_primitive.elf: \
	gallium_indexed_multi_line_primitive.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_indexed_multi_line_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_indexed_line_strip_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_LINE_STRIP_PRIMITIVE_TEST=1 \
		-DPS5_INDEXED_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_indexed_line_strip_primitive.elf: \
	gallium_indexed_line_strip_primitive.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_indexed_line_strip_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_indexed_triangle_strip_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_TRIANGLE_STRIP_PRIMITIVE_TEST=1 \
		-DPS5_INDEXED_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_indexed_triangle_strip_primitive.elf: \
	gallium_indexed_triangle_strip_primitive.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_indexed_triangle_strip_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_indexed_triangle_fan_primitive.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_TRIANGLE_FAN_PRIMITIVE_TEST=1 \
		-DPS5_INDEXED_PRIMITIVE_TEST=1 -c -o $@ $<

gallium_indexed_triangle_fan_primitive.elf: \
	gallium_indexed_triangle_fan_primitive.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_indexed_triangle_fan_primitive.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gallium_logicop_candidate.o: gallium_gate2_triangle.c
	$(CC) $(CFLAGS) $(PSBC_CFLAGS) -DPS5_FIXED_STATE_TEST=1 \
		-DPS5_LOGICOP_TEST=1 -c -o $@ $<

gallium_logicop_candidate.elf: gallium_logicop_candidate.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ gallium_logicop_candidate.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

gate1_vs_spv.inc: $(GATE1)/passthrough.vert.spv
	xxd -i -n gate1_vs_spv $< > $@

gate1_ps_spv.inc: $(GATE1)/constant.frag.spv
	xxd -i -n gate1_ps_spv $< > $@

gate1_vs_expected.inc: $(GATE1)/passthrough.vert.ngg.bin
	xxd -i -n gate1_vs_expected $< > $@

gate1_ps_expected.inc: $(GATE1)/constant.frag.raw.bin
	xxd -i -n gate1_ps_expected $< > $@

gate1_vs_package_expected.inc: $(GATE1)/passthrough.vert.ngg.agc.sb
	xxd -i -n gate1_vs_package_expected $< > $@

gate1_ps_package_expected.inc: $(GATE1)/constant.frag.agc.sb
	xxd -i -n gate1_ps_package_expected $< > $@

ps5_agc_package.o: $(PROJECT)/src/platform/ps5_agc_package.c \
	$(PROJECT)/src/platform/ps5_agc_package.h $(PSBC)/libpsbc/psbc_compile.h
	$(CC) $(CFLAGS) -I$(PSBC)/libpsbc -I$(PROJECT)/src/platform -c -o $@ $<

psbc_runtime.o: psbc_runtime.c gate1_vs_spv.inc gate1_ps_spv.inc \
	gate1_vs_expected.inc gate1_ps_expected.inc gate1_vs_package_expected.inc \
	gate1_ps_package_expected.inc
	$(CC) $(CFLAGS) -I$(PSBC)/libpsbc -I$(PROJECT)/src/platform \
		-c -o $@ psbc_runtime.c

psbc_runtime.elf: psbc_runtime.o ps5_agc_package.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ psbc_runtime.o ps5_agc_package.o \
		$(PSBC)/libpsbc.ps5.a $(LDFLAGS)

psbc_nir_runtime.o: psbc_nir_runtime.c
	$(CC) $(CFLAGS) -DHAVE_PTHREAD=1 -DHAVE_STRUCT_TIMESPEC=1 \
		-Wno-error=unused-parameter -Wno-error=missing-field-initializers \
		-iquote$(PSBC)/src -I$(PSBC)/libpsbc -I$(PSBC)/src \
		-c -o $@ $<

psbc_nir_runtime.elf: psbc_nir_runtime.o $(PSBC)/libpsbc.ps5.a
	$(CXX) -o $@ psbc_nir_runtime.o $(PSBC)/libpsbc.ps5.a $(LDFLAGS)

mesa_context_probe.o: mesa_context_probe.c
	$(CC) $(CFLAGS) -DHAVE_PTHREAD=1 -DHAVE_STRUCT_TIMESPEC=1 \
		-Wno-error=unused-parameter -Wno-error=missing-field-initializers \
		-Wno-unreachable-code-generic-assoc \
		-I$(MESA_SRC)/src/mesa -c -o $@ $<

mesa_context_probe.elf: mesa_context_probe.o gate2_backend.o ps5_screen.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a \
	$(MESA_CONTEXT_LIBS)
	$(CXX) -o $@ mesa_context_probe.o ps5_screen.o ps5_agc_package.o \
		u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

mesa_context_public_stencil_probe.o: mesa_context_probe.c
	$(CC) $(CFLAGS) -DHAVE_PTHREAD=1 -DHAVE_STRUCT_TIMESPEC=1 \
		-DPS5_PUBLIC_STENCIL_TEST=1 -Wno-error=unused-parameter \
		-Wno-error=missing-field-initializers \
		-Wno-unreachable-code-generic-assoc \
		-I$(MESA_SRC)/src/mesa -c -o $@ $<

mesa_context_public_stencil_probe.elf: \
	mesa_context_public_stencil_probe.o gate2_backend.o \
	ps5_screen_public_stencil.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS)
	$(CXX) -o $@ mesa_context_public_stencil_probe.o \
		ps5_screen_public_stencil.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

ps5_egl.o: $(PROJECT)/src/egl/ps5_egl.c
	$(CC) $(CFLAGS) -DHAVE_PTHREAD=1 -DHAVE_STRUCT_TIMESPEC=1 \
		-Wno-error=unused-parameter -Wno-unreachable-code-generic-assoc \
		-I$(MESA_SRC)/src/mesa -c -o $@ $<

ps5_egl_compressed.o: $(PROJECT)/src/egl/ps5_egl.c
	$(CC) $(CFLAGS) -DHAVE_PTHREAD=1 -DHAVE_STRUCT_TIMESPEC=1 \
		-DPS5_ENABLE_COMPRESSED_FALLBACK_CANDIDATE=1 \
		-Wno-error=unused-parameter -Wno-unreachable-code-generic-assoc \
		-I$(MESA_SRC)/src/mesa -c -o $@ $<

ps5_egl_core33.o: $(PROJECT)/src/egl/ps5_egl.c
	$(CC) $(CFLAGS) -DHAVE_PTHREAD=1 -DHAVE_STRUCT_TIMESPEC=1 \
		-DPS5_ENABLE_COMPRESSED_FALLBACK_CANDIDATE=1 \
		-DPS5_ENABLE_CORE_CONTEXT_CANDIDATE=1 \
		-Wno-error=unused-parameter -Wno-unreachable-code-generic-assoc \
		-I$(MESA_SRC)/src/mesa -c -o $@ $<

egl_public_triangle.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_triangle.elf: egl_public_triangle.o ps5_egl.o gate2_backend.o \
	ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_triangle.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_dual_source_blend.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_DUAL_SOURCE_BLEND_TEST=1 -c -o $@ $<

egl_public_dual_source_blend.elf: egl_public_dual_source_blend.o \
	ps5_egl.o gate2_backend.o ps5_screen_dual_source_blend.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a \
	$(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_dual_source_blend.o ps5_egl.o \
		ps5_screen_dual_source_blend.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_texture_depth.o: egl_public_texture_depth.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_depth.elf: egl_public_texture_depth.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_depth.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_mixed_fbo.o: egl_public_texture_depth.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_MIXED_FBO_TEST=1 -c -o $@ $<

egl_public_mixed_fbo.elf: egl_public_mixed_fbo.o ps5_egl.o \
	gate2_backend.o ps5_screen_mixed_fbo.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_mixed_fbo.o ps5_egl.o ps5_screen_mixed_fbo.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_render_to_texture.o: egl_public_render_to_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_render_to_texture.elf: egl_public_render_to_texture.o ps5_egl.o \
	gate2_backend.o ps5_screen_render_to_texture.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_render_to_texture.o ps5_egl.o \
		ps5_screen_render_to_texture.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_dynamic_color_target.o: egl_public_render_to_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_DYNAMIC_COLOR_TARGET_TEST=1 -c -o $@ $<

egl_public_dynamic_color_target.elf: egl_public_dynamic_color_target.o \
	ps5_egl.o gate2_backend.o ps5_screen_dynamic_color_target.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a \
	$(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_dynamic_color_target.o ps5_egl.o \
		ps5_screen_dynamic_color_target.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_color_blit.o: egl_public_render_to_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_DYNAMIC_COLOR_TARGET_TEST=1 -DPS5_COLOR_BLIT_TEST=1 \
		-c -o $@ $<

egl_public_color_blit.elf: egl_public_color_blit.o ps5_egl.o \
	gate2_backend.o ps5_screen_color_blit.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_color_blit.o ps5_egl.o \
		ps5_screen_color_blit.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_rg_render.o: egl_public_texture_rg_render.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_rg_render.elf: egl_public_texture_rg_render.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_rg_render.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_rg_render.o ps5_egl.o \
		ps5_screen_texture_rg_render.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_packed_float.o: egl_public_packed_float.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_packed_float.elf: egl_public_packed_float.o ps5_egl.o \
	gate2_backend.o ps5_screen_packed_float.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_packed_float.o ps5_egl.o \
		ps5_screen_packed_float.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_core33_packed_float.o: egl_public_packed_float.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE_33_TEST=1 -c -o $@ $<

egl_public_core33_packed_float.elf: egl_public_core33_packed_float.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_packed_float.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_integer.o: egl_public_texture_integer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_integer.elf: egl_public_texture_integer.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_integer.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_integer.o ps5_egl.o \
		ps5_screen_texture_integer.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_integer_narrow.o: egl_public_texture_integer_narrow.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_integer_narrow_rgba16i.o: \
	egl_public_texture_integer_narrow.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_NARROW_FIRST_CASE=3 -DPS5_NARROW_CASE_COUNT=1 -c -o $@ $<

egl_public_texture_integer_narrow_uploads.o: \
	egl_public_texture_integer_narrow.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_NARROW_RUN_RENDER=0 -c -o $@ $<

egl_public_texture_integer_narrow.elf: \
	egl_public_texture_integer_narrow.o ps5_egl.o gate2_backend.o \
	ps5_screen_texture_integer_narrow.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_integer_narrow.o ps5_egl.o \
		ps5_screen_texture_integer_narrow.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_rgb10_a2ui.o: egl_public_rgb10_a2ui.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_rgb10_a2ui.elf: egl_public_rgb10_a2ui.o ps5_egl.o \
	gate2_backend.o ps5_screen_rgb10_a2ui.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_rgb10_a2ui.o ps5_egl.o \
		ps5_screen_rgb10_a2ui.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_core33_rgb10_a2ui.o: egl_public_rgb10_a2ui.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE_33_TEST=1 -c -o $@ $<

egl_public_core33_rgb10_a2ui.elf: egl_public_core33_rgb10_a2ui.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_rgb10_a2ui.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_rectangle.o: egl_public_texture_rectangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_rectangle.elf: egl_public_texture_rectangle.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_rectangle.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_rectangle.o ps5_egl.o \
		ps5_screen_texture_rectangle.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_core33_texture_rectangle.o: egl_public_texture_rectangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE_33_TEST=1 -c -o $@ $<

egl_public_core33_texture_rectangle.elf: \
	egl_public_core33_texture_rectangle.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_texture_rectangle.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_rg_tile.o: egl_public_texture_rg_tile.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_rg_tile.elf: egl_public_texture_rg_tile.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_rg_tile.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_rg_tile.o ps5_egl.o \
		ps5_screen_texture_rg_tile.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_depth_texture.o: egl_public_depth_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_depth_texture.elf: egl_public_depth_texture.o ps5_egl.o \
	gate2_backend.o ps5_screen_depth_texture.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_depth_texture.o ps5_egl.o \
		ps5_screen_depth_texture.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_core33_depth_texture.o: egl_public_depth_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE_33_TEST=1 -c -o $@ $<

egl_public_core33_depth_texture.elf: egl_public_core33_depth_texture.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_depth_texture.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_framebuffer_srgb.o: egl_public_render_to_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_FRAMEBUFFER_SRGB_TEST=1 -c -o $@ $<

egl_public_framebuffer_srgb.elf: egl_public_framebuffer_srgb.o ps5_egl.o \
	gate2_backend.o ps5_screen_framebuffer_srgb.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_framebuffer_srgb.o ps5_egl.o \
		ps5_screen_framebuffer_srgb.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_map_buffer_range.o: egl_public_map_buffer_range.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_map_buffer_range.elf: egl_public_map_buffer_range.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_map_buffer_range.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_half_float_vertex.o: egl_public_half_float_vertex.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_half_float_vertex.elf: egl_public_half_float_vertex.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_half_float_vertex.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_normalized_short_vertex.o: egl_public_normalized_short_vertex.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_integer_vertex.o: egl_public_core33_integer_vertex.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_integer_vertex_matrix.o: \
	egl_public_core33_integer_vertex_matrix.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_sampler_border.o: egl_public_core33_sampler_border.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_texture_1d.o: egl_public_core33_texture_1d.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_normalized_short_vertex.elf: \
	egl_public_normalized_short_vertex.o ps5_egl.o gate2_backend.o \
	ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_normalized_short_vertex.o ps5_egl.o \
		ps5_screen.o ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		-Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_base_vertex.o: egl_public_base_vertex.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_draw_range.o: egl_public_base_vertex.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_DRAW_RANGE_TEST=1 -c -o $@ $<

egl_public_base_vertex.elf: egl_public_base_vertex.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_base_vertex.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_draw_range.elf: egl_public_draw_range.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_draw_range.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_first_vertex.o: egl_public_first_vertex.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_first_vertex.elf: egl_public_first_vertex.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_first_vertex.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_draw_instanced.o: egl_public_draw_instanced.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_draw_instanced.elf: egl_public_draw_instanced.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_draw_instanced.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_instanced_arrays.o: egl_public_instanced_arrays.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_instanced_arrays.elf: egl_public_instanced_arrays.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_instanced_arrays.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_srgb_texture.o: egl_public_srgb_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_srgb_texture.elf: egl_public_srgb_texture.o ps5_egl.o \
	gate2_backend.o ps5_screen_srgb.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_srgb_texture.o ps5_egl.o ps5_screen_srgb.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_rg.o: egl_public_texture_rg.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_rg_default.o: egl_public_texture_rg.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_TEXTURE_RG_DEFAULT_ONLY=1 -c -o $@ $<

egl_public_texture_rg.elf: egl_public_texture_rg.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_formats.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_rg.o ps5_egl.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_formats_default.elf: egl_public_texture_rg.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_rg.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_rg_default.elf: egl_public_texture_rg_default.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_rg_default.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_rg_multitile.o: egl_public_texture_rg_multitile.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_rg_multitile.elf: egl_public_texture_rg_multitile.o \
	ps5_egl.o gate2_backend.o ps5_screen_texture_formats.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_rg_multitile.o ps5_egl.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_rgtc.o: egl_public_texture_rg.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_RGTC_FALLBACK_TEST=1 -c -o $@ $<

egl_public_texture_rgtc.elf: egl_public_texture_rgtc.o \
	ps5_egl_compressed.o gate2_backend.o ps5_screen_texture_formats.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a \
	$(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_rgtc.o ps5_egl_compressed.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_core33_texture_rgtc.o: egl_public_texture_rg.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_RGTC_FALLBACK_TEST=1 -DPS5_CORE_33_TEST=1 -c -o $@ $<

egl_public_core33_texture_rgtc.elf: egl_public_core33_texture_rgtc.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_texture_rgtc.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_swizzle.o: egl_public_texture_swizzle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_swizzle.elf: egl_public_texture_swizzle.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_formats.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_swizzle.o ps5_egl.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_extents.o: egl_public_texture_extents.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_extents.elf: egl_public_texture_extents.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_extents.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_multi_texture.o: egl_public_multi_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_sampler_array.o: egl_public_multi_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE33_SAMPLER_ARRAY_TEST=1 -c -o $@ $<

egl_public_multi_texture.elf: egl_public_multi_texture.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_formats.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_multi_texture.o ps5_egl.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_mipmap.o: egl_public_texture_mipmap.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_mipmap.elf: egl_public_texture_mipmap.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_formats.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_mipmap.o ps5_egl.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_cube.o: egl_public_texture_cube.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_texture_cube_default.o: egl_public_texture_cube.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_SKIP_SEAMLESS_CUBE_TEST=1 -c -o $@ $<

egl_public_core33_texture_cube_mipmap.o: egl_public_texture_cube.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE33_CUBE_MIPMAP_TEST=1 \
		-DPS5_SKIP_SEAMLESS_CUBE_TEST=1 -c -o $@ $<

egl_public_texture_cube.elf: egl_public_texture_cube.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_formats.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_cube.o ps5_egl.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_cube_default.elf: egl_public_texture_cube_default.o \
	ps5_egl.o gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_cube_default.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_texture_array.o: egl_public_texture_array.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_texture_array_mipmap.o: egl_public_texture_array.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE33_ARRAY_MIPMAP_TEST=1 -c -o $@ $<

egl_public_texture_array.elf: egl_public_texture_array.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_formats.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_array.o ps5_egl.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_3d.o: egl_public_texture_3d.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_texture_3d_mipmap.o: egl_public_texture_3d.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE33_3D_MIPMAP_TEST=1 -c -o $@ $<

egl_public_texture_3d.elf: egl_public_texture_3d.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_formats.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_texture_3d.o ps5_egl.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_texture_%_default.elf: egl_public_texture_%.o ps5_egl.o \
	gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ $< ps5_egl.o ps5_screen.o ps5_agc_package.o \
		u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_uniform_buffer.o: egl_public_uniform_buffer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_uniform_buffer_default.o: egl_public_uniform_buffer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_SKIP_MSAA_POLICY_TEST=1 -c -o $@ $<

egl_public_uniform_buffer.elf: egl_public_uniform_buffer.o ps5_egl.o \
	gate2_backend.o ps5_screen_texture_formats.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_uniform_buffer.o ps5_egl.o \
		ps5_screen_texture_formats.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_uniform_buffer_default.elf: egl_public_uniform_buffer_default.o \
	ps5_egl.o gate2_backend.o ps5_screen.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_uniform_buffer_default.o ps5_egl.o ps5_screen.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_geometry_shader.o: egl_public_uniform_buffer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -DPS5_GEOMETRY_TEST=1 \
		-DPS5_SKIP_MSAA_POLICY_TEST=1 \
		-c -o $@ $<

egl_public_geometry_shader.elf: egl_public_geometry_shader.o ps5_egl_core33.o \
	gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_geometry_shader.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_core33_geometry_minimal.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_GLSL_330_TEST=1 -DPS5_CORE_33_TEST=1 \
		-DPS5_GEOMETRY_MINIMAL_TEST=1 -c -o $@ $<

egl_public_core33_geometry_adjacency.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_GLSL_330_TEST=1 -DPS5_CORE_33_TEST=1 \
		-DPS5_GEOMETRY_ADJACENCY_TEST=1 -c -o $@ $<

egl_public_core33_geometry_texture.o: egl_public_core33_geometry_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_geometry_varying.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE_33_TEST=1 -DPS5_GLSL_330_TEST=1 \
		-DPS5_GEOMETRY_VARYING_TEST=1 -c -o $@ $<

egl_public_core33_geometry_varying_limit.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE_33_TEST=1 -DPS5_GLSL_330_TEST=1 \
		-DPS5_GEOMETRY_VARYING_LIMIT_TEST=1 -c -o $@ $<

egl_public_core33_default_uniform_limit.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_CORE_33_TEST=1 -DPS5_GLSL_330_TEST=1 \
		-DPS5_DEFAULT_UNIFORM_LIMIT_TEST=1 -c -o $@ $<

egl_public_core33_geometry_minimal.elf: \
	egl_public_core33_geometry_minimal.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_geometry_minimal.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_geometry_adjacency.elf: \
	egl_public_core33_geometry_adjacency.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_geometry_adjacency.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_multiple_render_targets.o: egl_public_multiple_render_targets.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_multiple_render_targets.elf: \
	egl_public_multiple_render_targets.o ps5_egl.o gate2_backend.o \
	ps5_screen_mrt.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_multiple_render_targets.o ps5_egl.o \
		ps5_screen_mrt.o ps5_agc_package.o u_framebuffer.o gate2_backend.o \
		-Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_transform_feedback.o: egl_public_transform_feedback.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_transform_feedback.elf: egl_public_transform_feedback.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o \
	ps5_agc_package.o u_framebuffer.o $(PSBC)/libpsbc.ps5.a \
	$(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_transform_feedback.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_transform_feedback_query.o: egl_public_transform_feedback_query.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_transform_feedback_query.elf: \
	egl_public_transform_feedback_query.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_transform_feedback_query.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_geometry_transform_feedback.o: \
	egl_public_transform_feedback_query.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_GEOMETRY_XFB_TEST=1 -c -o $@ $<

egl_public_geometry_transform_feedback.elf: \
	egl_public_geometry_transform_feedback.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_geometry_transform_feedback.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_transform_feedback_instanced.o: \
	egl_public_transform_feedback_instanced.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_transform_feedback_instanced.elf: \
	egl_public_transform_feedback_instanced.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_transform_feedback_instanced.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_transform_feedback_overflow.o: \
	egl_public_transform_feedback_overflow.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_transform_feedback_overflow.elf: \
	egl_public_transform_feedback_overflow.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_transform_feedback_overflow.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_transform_feedback_topologies.o: \
	egl_public_transform_feedback_topologies.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_transform_feedback_topologies.elf: \
	egl_public_transform_feedback_topologies.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_transform_feedback_topologies.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_large_pool.elf: egl_public_triangle.o ps5_egl.o gate2_backend.o \
	ps5_screen_large_pool.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_triangle.o ps5_egl.o ps5_screen_large_pool.o \
		ps5_agc_package.o u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_occlusion_query.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_OCCLUSION_QUERY_TEST=1 -c -o $@ $<

egl_public_occlusion_query.elf: egl_public_occlusion_query.o ps5_egl.o \
	gate2_backend.o ps5_screen_occlusion_query.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_occlusion_query.o ps5_egl.o \
		ps5_screen_occlusion_query.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_occlusion_query.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_OCCLUSION_QUERY_TEST=1 -DPS5_GLSL_330_TEST=1 \
		-DPS5_CORE_33_TEST=1 -c -o $@ $<

egl_public_core33_occlusion_query.elf: \
	egl_public_core33_occlusion_query.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_occlusion_query.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_depth_clamp.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_DEPTH_CLAMP_TEST=1 -c -o $@ $<

egl_public_depth_clamp.elf: egl_public_depth_clamp.o ps5_egl.o \
	gate2_backend.o ps5_screen_depth_clamp.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_depth_clamp.o ps5_egl.o \
		ps5_screen_depth_clamp.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_depth_clamp.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_DEPTH_CLAMP_TEST=1 -DPS5_GLSL_330_TEST=1 \
		-DPS5_CORE_33_TEST=1 -c -o $@ $<

egl_public_core33_depth_clamp.elf: egl_public_core33_depth_clamp.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_depth_clamp.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_glsl330.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_GLSL_330_TEST=1 -c -o $@ $<

egl_public_core33_glsl330.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_GLSL_330_TEST=1 -DPS5_CORE_33_TEST=1 -c -o $@ $<

egl_public_core33_glsl_suite.o: egl_public_core33_glsl_suite.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_visible_animation.o: egl_public_core33_visible_animation.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_glsl330.elf: egl_public_glsl330.o ps5_egl.o \
	gate2_backend.o ps5_screen_glsl330.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_glsl330.o ps5_egl.o \
		ps5_screen_glsl330.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_dual_source.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_DUAL_SOURCE_BLEND_TEST=1 -DPS5_CORE_33_TEST=1 \
		-c -o $@ $<

egl_public_core33_dual_source.elf: egl_public_core33_dual_source.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_dual_source.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_multi_draw.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_GLSL_330_TEST=1 -DPS5_CORE_33_TEST=1 \
		-DPS5_MULTI_DRAW_TEST=1 -c -o $@ $<

egl_public_core33_multi_draw.elf: egl_public_core33_multi_draw.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_multi_draw.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_sync.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_GLSL_330_TEST=1 -DPS5_CORE_33_TEST=1 \
		-DPS5_SYNC_TEST=1 -c -o $@ $<

egl_public_core33_sync.elf: egl_public_core33_sync.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_sync.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_msaa4.o: egl_public_core33_msaa4.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_color_target_limits.o: egl_public_core33_msaa4.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -DPS5_COLOR_TARGET_LIMITS_TEST=1 -c -o $@ $<

egl_public_core33_msaa_array.o: egl_public_core33_msaa_array.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_texture_buffer.o: egl_public_core33_texture_buffer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_msaa4.elf: egl_public_core33_msaa4.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_msaa4.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_msaa4_srgb.o: egl_public_core33_msaa4.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_MSAA_SRGB_TEST=1 -c -o $@ $<

egl_public_core33_msaa4_srgb.elf: egl_public_core33_msaa4_srgb.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_msaa4_srgb.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_msaa4_depth.o: egl_public_core33_msaa4_depth.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_msaa4_depth.elf: egl_public_core33_msaa4_depth.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_msaa4_depth.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_msaa4_depth_texture.o: \
	egl_public_core33_msaa4_depth_texture.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_msaa4_depth_texture.elf: \
	egl_public_core33_msaa4_depth_texture.o ps5_egl_core33.o \
	gate2_backend.o ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_msaa4_depth_texture.o \
		ps5_egl_core33.o ps5_screen_core33.o ps5_agc_package.o \
		u_framebuffer.o gate2_backend.o -Wl,--start-group \
		$(MESA_LIBGLAPI_BRIDGE) $(PSBC)/libpsbc.ps5.a \
		$(MESA_CONTEXT_LIBS) -Wl,--end-group $(LDFLAGS)

egl_public_core33_msaa4_stencil.o: egl_public_core33_msaa4_depth.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_MSAA4_STENCIL_TEST=1 -c -o $@ $<

egl_public_core33_msaa4_stencil.elf: egl_public_core33_msaa4_stencil.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_msaa4_stencil.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_msaa4_mask.o: egl_public_core33_msaa4.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_MSAA_SAMPLE_MASK_TEST=1 -c -o $@ $<

egl_public_core33_msaa4_mask.elf: egl_public_core33_msaa4_mask.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_msaa4_mask.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_msaa4_texture.o: egl_public_core33_msaa4.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_MSAA_TEXTURE_TEST=1 -c -o $@ $<

egl_public_core33_msaa4_texture.elf: egl_public_core33_msaa4_texture.o \
	ps5_egl_core33.o gate2_backend.o ps5_screen_core33.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_msaa4_texture.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_core33_msaa4_texture_fetch.o: egl_public_core33_msaa4.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_MSAA_TEXTURE_FETCH_TEST=1 -c -o $@ $<

egl_public_core33_scaled_linear_blit.o: egl_public_core33_scaled_linear_blit.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_depth_stencil_blit.o: \
	egl_public_core33_depth_stencil_blit.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_depth_stencil_blit_scaled.o: \
	egl_public_core33_depth_stencil_blit.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_DEPTH_STENCIL_SCALED_TEST=1 -c -o $@ $<

egl_public_core33_framebuffer_texture_layer.o: \
	egl_public_core33_framebuffer_texture_layer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_raster_semantics.o: egl_public_core33_raster_semantics.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_clip_distance.o: egl_public_core33_clip_distance.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_depth_only_fbo.o: egl_public_core33_depth_only_fbo.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_render_target_limits.o: egl_public_core33_depth_only_fbo.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -DPS5_PUBLIC_RENDER_TARGET_LIMITS_TEST=1 -c -o $@ $<

egl_public_core33_layered_mip_fbo.o: egl_public_core33_layered_mip_fbo.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_eight_draw_buffers.o: \
	egl_public_core33_eight_draw_buffers.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_msaa_alpha.o: egl_public_core33_msaa_alpha.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_sample_coverage.o: egl_public_core33_sample_coverage.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_texture_format_matrix.o: \
	egl_public_core33_texture_format_matrix.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_render_format_float.o: \
	egl_public_core33_render_format_float.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_render_format_integer.o: \
	egl_public_core33_render_format_integer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_render_format_blit.o: \
	egl_public_core33_render_format_blit.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_entrypoints.o: egl_public_core33_entrypoints.c \
	egl_public_core33_entrypoints.inc
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_buffer_copy.o: egl_public_core33_buffer_copy.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_texture_copy.o: egl_public_core33_texture_copy.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_layered_render.o: egl_public_core33_layered_render.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_current_vertex_attrib.o: \
	egl_public_core33_current_vertex_attrib.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_pixel_buffer.o: egl_public_core33_pixel_buffer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_depth_transfer.o: egl_public_core33_depth_transfer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_depth_array.o: egl_public_core33_depth_array.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_layered_depth.o: egl_public_core33_layered_depth.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_depth_targets.o: egl_public_core33_depth_targets.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_depth_mip_target.o: egl_public_core33_depth_mip_target.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_scissored_clear.o: egl_public_core33_scissored_clear.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_limits.o: egl_public_core33_limits.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_context_share.o: egl_public_core33_context_share.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_pbuffer.o: egl_public_core33_pbuffer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_surfaceless.o: egl_public_core33_surfaceless.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_rgtc_transfer.o: egl_public_core33_rgtc_transfer.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_depth_format_fallback.o: \
	egl_public_core33_depth_format_fallback.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_smooth_raster.o: egl_public_core33_smooth_raster.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_logic_op.o: egl_public_core33_logic_op.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_draw_matrix.o: egl_public_core33_draw_matrix.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_state_matrix.o: egl_public_core33_state_matrix.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_program_api.o: egl_public_core33_program_api.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_cts_ubo_link.o: egl_public_core33_cts_ubo_link.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_object_api.o: egl_public_core33_object_api.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_vertex_attrib_api.o: \
	egl_public_core33_vertex_attrib_api.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_compressed_dimensions.o: \
	egl_public_core33_compressed_dimensions.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 -c -o $@ $<

egl_public_core33_msaa4_texture_fetch.elf: \
	egl_public_core33_msaa4_texture_fetch.o ps5_egl_core33.o gate2_backend.o \
	ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
	$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) $(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_core33_msaa4_texture_fetch.o ps5_egl_core33.o \
		ps5_screen_core33.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

egl_public_packed_vertex.o: egl_public_triangle.c
	$(CC) $(CFLAGS) -DGL_GLEXT_PROTOTYPES=1 \
		-DPS5_PACKED_VERTEX_TEST=1 -c -o $@ $<

egl_public_packed_vertex.elf: egl_public_packed_vertex.o ps5_egl.o \
	gate2_backend.o ps5_screen_packed_vertex.o ps5_agc_package.o \
	u_framebuffer.o $(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
	$(MESA_LIBGLAPI_BRIDGE)
	$(CXX) -o $@ egl_public_packed_vertex.o ps5_egl.o \
		ps5_screen_packed_vertex.o ps5_agc_package.o u_framebuffer.o \
		gate2_backend.o -Wl,--start-group $(MESA_LIBGLAPI_BRIDGE) \
		$(PSBC)/libpsbc.ps5.a $(MESA_CONTEXT_LIBS) \
		-Wl,--end-group $(LDFLAGS)

.PHONY: clean
clean:
	rm -f gallium_lifecycle.elf gallium_lifecycle.o gallium_gate2_triangle.elf \
		gallium_gate2_triangle.o gallium_mrt_candidate.elf \
		gallium_mrt_candidate.o gallium_packed_stencil_candidate.elf \
		gallium_packed_stencil_candidate.o \
		gallium_fixed_state_candidate.elf gallium_fixed_state_candidate.o \
		gallium_color_mask_candidate.elf gallium_color_mask_candidate.o \
		gallium_cull_back_candidate.elf gallium_cull_back_candidate.o \
		gallium_cull_front_candidate.elf gallium_cull_front_candidate.o \
		gallium_polygon_offset_candidate.elf \
		gallium_polygon_offset_candidate.o \
		gallium_polygon_offset_negative_units.elf \
		gallium_polygon_offset_negative_units.o \
		gallium_polygon_offset_slope_baseline.elf \
		gallium_polygon_offset_slope_baseline.o \
		gallium_polygon_offset_slope_candidate.elf \
		gallium_polygon_offset_slope_candidate.o \
		gallium_polygon_offset_negative_factor.elf \
		gallium_polygon_offset_negative_factor.o \
		gallium_line_polygon_baseline.elf gallium_line_polygon_baseline.o \
		gallium_line_polygon_offset_candidate.elf \
		gallium_line_polygon_offset_candidate.o \
		gallium_point_polygon_baseline.elf \
		gallium_point_polygon_baseline.o \
		gallium_point_polygon_offset_candidate.elf \
		gallium_point_polygon_offset_candidate.o \
		gallium_point_primitive.elf gallium_point_primitive.o \
		gallium_line_primitive.elf gallium_line_primitive.o \
		gallium_multi_point_primitive.elf gallium_multi_point_primitive.o \
		gallium_multi_line_primitive.elf gallium_multi_line_primitive.o \
		gallium_line_strip_primitive.elf gallium_line_strip_primitive.o \
		gallium_triangle_strip_primitive.elf \
		gallium_triangle_strip_primitive.o \
		gallium_triangle_fan_primitive.elf \
		gallium_triangle_fan_primitive.o \
		gallium_indexed_multi_point_primitive.elf \
		gallium_indexed_multi_point_primitive.o \
		gallium_indexed_multi_line_primitive.elf \
		gallium_indexed_multi_line_primitive.o \
		gallium_indexed_line_strip_primitive.elf \
		gallium_indexed_line_strip_primitive.o \
		gallium_indexed_triangle_strip_primitive.elf \
		gallium_indexed_triangle_strip_primitive.o \
		gallium_indexed_triangle_fan_primitive.elf \
		gallium_indexed_triangle_fan_primitive.o \
		gallium_logicop_candidate.elf gallium_logicop_candidate.o \
		gate2_backend.o gate2_empty_backend.o ps5_screen.o \
		ps5_screen_packed.o ps5_screen_public_stencil.o ps5_screen_srgb.o \
		ps5_screen_mixed_fbo.o ps5_screen_render_to_texture.o \
		ps5_screen_dynamic_color_target.o ps5_screen_texture_rg_render.o \
		ps5_screen_texture_rg_tile.o ps5_screen_packed_float.o \
		ps5_screen_texture_integer.o ps5_screen_rgb10_a2ui.o \
		ps5_screen_texture_integer_narrow.o \
		ps5_screen_texture_rectangle.o \
		ps5_screen_framebuffer_srgb.o \
		ps5_screen_depth_texture.o \
		ps5_screen_texture_formats.o \
		ps5_screen_geometry.o ps5_screen_mrt.o \
		ps5_screen_dual_source_blend.o ps5_screen_core33.o \
		ps5_screen_transform_feedback.o ps5_screen_occlusion_query.o \
		ps5_screen_depth_clamp.o ps5_screen_glsl330.o \
		ps5_screen_packed_vertex.o ps5_egl_compressed.o ps5_egl_core33.o \
		u_framebuffer.o \
		psbc_runtime.elf psbc_runtime.o psbc_nir_runtime.elf \
		psbc_nir_runtime.o gate1_vs_spv.inc gate1_ps_spv.inc \
		gate1_vs_expected.inc gate1_ps_expected.inc \
		gate1_vs_package_expected.inc gate1_ps_package_expected.inc \
		ps5_agc_package.o
	rm -f mesa_context_probe.elf mesa_context_probe.o \
		mesa_context_public_stencil_probe.elf \
		mesa_context_public_stencil_probe.o egl_public_triangle.elf \
		egl_public_triangle.o egl_public_texture_depth.elf \
		egl_public_texture_depth.o egl_public_mixed_fbo.elf \
		egl_public_mixed_fbo.o egl_public_render_to_texture.elf \
		egl_public_render_to_texture.o egl_public_dynamic_color_target.elf \
		egl_public_dynamic_color_target.o egl_public_texture_rg_render.elf \
		egl_public_texture_rg_render.o egl_public_packed_float.elf \
		egl_public_packed_float.o egl_public_core33_packed_float.elf \
		egl_public_core33_packed_float.o egl_public_texture_integer.elf \
		egl_public_texture_integer.o egl_public_rgb10_a2ui.elf \
		egl_public_core33_rgb10_a2ui.elf \
		egl_public_core33_rgb10_a2ui.o \
		egl_public_texture_integer_narrow.elf \
		egl_public_texture_integer_narrow.o \
		egl_public_texture_integer_narrow_rgba16i.o \
		egl_public_texture_integer_narrow_uploads.o \
		egl_public_rgb10_a2ui.o egl_public_texture_rectangle.elf \
		egl_public_texture_rectangle.o \
		egl_public_core33_texture_rectangle.elf \
		egl_public_core33_texture_rectangle.o egl_public_texture_rg_tile.elf \
		egl_public_texture_rg_tile.o egl_public_depth_texture.elf \
		egl_public_depth_texture.o egl_public_framebuffer_srgb.elf \
		egl_public_framebuffer_srgb.o egl_public_map_buffer_range.elf \
		egl_public_map_buffer_range.o egl_public_half_float_vertex.elf \
		egl_public_half_float_vertex.o \
		egl_public_normalized_short_vertex.elf \
		egl_public_normalized_short_vertex.o egl_public_base_vertex.elf \
		egl_public_base_vertex.o egl_public_draw_range.elf \
		egl_public_draw_range.o egl_public_first_vertex.elf \
		egl_public_first_vertex.o egl_public_draw_instanced.elf \
		egl_public_draw_instanced.o egl_public_instanced_arrays.elf \
		egl_public_instanced_arrays.o egl_public_srgb_texture.elf \
		egl_public_srgb_texture.o egl_public_texture_rg.elf \
		egl_public_texture_rg.o egl_public_texture_formats_default.elf \
		egl_public_texture_rg_default.elf \
		egl_public_texture_rg_default.o \
		egl_public_texture_rg_multitile.elf \
		egl_public_texture_rg_multitile.o egl_public_texture_rgtc.elf \
		egl_public_texture_rgtc.o egl_public_core33_texture_rgtc.elf \
		egl_public_core33_texture_rgtc.o egl_public_texture_swizzle.elf \
		egl_public_texture_swizzle.o egl_public_texture_swizzle_default.elf \
		egl_public_texture_extents.elf \
		egl_public_texture_extents.o egl_public_multi_texture.elf \
		egl_public_multi_texture.o egl_public_core33_sampler_array.o \
		egl_public_texture_mipmap.elf \
		egl_public_texture_mipmap.o egl_public_texture_mipmap_default.elf \
		egl_public_texture_cube.elf \
		egl_public_texture_cube.o egl_public_texture_array.elf \
		egl_public_texture_array.o egl_public_texture_3d.elf \
		egl_public_texture_3d.o egl_public_texture_cube_default.elf \
		egl_public_texture_cube_default.o \
		egl_public_texture_array_default.elf \
		egl_public_texture_3d_default.elf egl_public_uniform_buffer.elf \
		egl_public_uniform_buffer.o egl_public_uniform_buffer_default.elf \
		egl_public_uniform_buffer_default.o ps5_egl.o
	rm -f egl_public_geometry_shader.elf egl_public_geometry_shader.o \
		egl_public_core33_geometry_adjacency.elf \
		egl_public_core33_geometry_adjacency.o \
		egl_public_core33_geometry_minimal.elf \
		egl_public_core33_geometry_minimal.o \
		egl_public_core33_geometry_texture.o \
		egl_public_core33_geometry_varying.o \
		egl_public_core33_geometry_varying_limit.o \
		egl_public_core33_default_uniform_limit.o
	rm -f egl_public_dual_source_blend.elf \
		egl_public_dual_source_blend.o egl_public_core33_dual_source.elf \
		egl_public_core33_dual_source.o egl_public_core33_multi_draw.elf \
		egl_public_core33_multi_draw.o egl_public_core33_sync.elf \
		egl_public_core33_sync.o egl_public_core33_msaa4.elf \
		egl_public_core33_msaa4.o egl_public_core33_msaa4_mask.elf \
		egl_public_core33_msaa_array.o \
		egl_public_core33_texture_buffer.o \
		egl_public_core33_msaa4_depth.elf \
		egl_public_core33_msaa4_depth.o \
		egl_public_core33_msaa4_depth_texture.elf \
		egl_public_core33_msaa4_depth_texture.o \
		egl_public_core33_msaa4_srgb.elf \
		egl_public_core33_msaa4_srgb.o \
		egl_public_core33_msaa4_stencil.elf \
		egl_public_core33_msaa4_stencil.o \
		egl_public_core33_msaa4_mask.o egl_public_core33_msaa4_texture.elf \
		egl_public_core33_msaa4_texture.o \
		egl_public_core33_msaa4_texture_fetch.elf \
		egl_public_core33_msaa4_texture_fetch.o \
		egl_public_core33_depth_stencil_blit.o \
		egl_public_core33_depth_stencil_blit_scaled.o \
		egl_public_core33_framebuffer_texture_layer.o \
		egl_public_core33_raster_semantics.o \
		egl_public_core33_clip_distance.o \
		egl_public_core33_depth_only_fbo.o \
		egl_public_core33_layered_mip_fbo.o \
		egl_public_core33_eight_draw_buffers.o \
		egl_public_core33_msaa_alpha.o \
		egl_public_core33_sample_coverage.o \
		egl_public_core33_texture_format_matrix.o \
		egl_public_core33_render_format_float.o \
		egl_public_core33_render_format_integer.o \
		egl_public_core33_render_format_blit.o \
		egl_public_core33_entrypoints.o \
		egl_public_core33_buffer_copy.o \
		egl_public_core33_texture_copy.o \
		egl_public_core33_layered_render.o \
		egl_public_core33_current_vertex_attrib.o \
		egl_public_core33_pixel_buffer.o \
		egl_public_core33_depth_transfer.o \
		egl_public_core33_depth_array.o \
		egl_public_core33_layered_depth.o \
		egl_public_core33_depth_targets.o \
		egl_public_core33_depth_mip_target.o \
		egl_public_core33_scissored_clear.o \
		egl_public_core33_limits.o \
		egl_public_core33_context_share.o \
		egl_public_core33_pbuffer.o \
		egl_public_core33_surfaceless.o \
		egl_public_core33_rgtc_transfer.o \
		egl_public_core33_depth_format_fallback.o \
		egl_public_core33_smooth_raster.o \
		egl_public_core33_logic_op.o \
		egl_public_core33_draw_matrix.o \
		egl_public_core33_state_matrix.o \
		egl_public_core33_program_api.o \
		egl_public_core33_cts_ubo_link.o \
		egl_public_core33_object_api.o \
		egl_public_core33_vertex_attrib_api.o \
		egl_public_core33_compressed_dimensions.o
	rm -f egl_public_multiple_render_targets.elf \
		egl_public_multiple_render_targets.o
	rm -f egl_public_transform_feedback.elf \
		egl_public_transform_feedback.o \
		egl_public_transform_feedback_query.elf \
		egl_public_transform_feedback_query.o \
		egl_public_geometry_transform_feedback.elf \
		egl_public_geometry_transform_feedback.o \
		egl_public_transform_feedback_instanced.elf \
		egl_public_transform_feedback_instanced.o \
		egl_public_transform_feedback_overflow.elf \
		egl_public_transform_feedback_overflow.o \
		egl_public_transform_feedback_topologies.elf \
		egl_public_transform_feedback_topologies.o \
		egl_public_occlusion_query.elf \
		egl_public_occlusion_query.o \
		egl_public_core33_occlusion_query.elf \
		egl_public_core33_occlusion_query.o egl_public_depth_clamp.elf \
		egl_public_depth_clamp.o egl_public_core33_depth_clamp.elf \
		egl_public_core33_depth_clamp.o egl_public_glsl330.elf \
		egl_public_glsl330.o egl_public_core33_glsl330.o \
		egl_public_core33_glsl_suite.o \
		egl_public_core33_visible_animation.o \
		egl_public_packed_vertex.elf \
		egl_public_packed_vertex.o
