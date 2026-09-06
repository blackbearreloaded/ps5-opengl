# Reusable PS5 OpenGL 3.3 runtime build/link fragment.
# Include prospero.mk before this file, then link with PS5_OPENGL_LDLIBS.

ps5_opengl_default_goal := $(.DEFAULT_GOAL)
ps5_opengl_mk_self := $(abspath $(lastword $(MAKEFILE_LIST)))
ps5_opengl_mk_dir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PS5_OPENGL_ROOT ?= $(abspath $(ps5_opengl_mk_dir)/..)
PS5_OPENGL_BUILD ?= $(PS5_OPENGL_ROOT)/build/core33-runtime
PS5_OPENGL_MESA_SRC ?= $(PS5_OPENGL_ROOT)/third_party/mesa-26.2.0
PS5_OPENGL_MESA_BUILD ?= $(PS5_OPENGL_ROOT)/build/mesa-ps5-probe
PS5_OPENGL_PSBC ?= $(PS5_OPENGL_ROOT)/third_party/opengnm-psbc
PS5_OPENGL_RUNTIME_DEFINES ?=

PS5_OPENGL_DRIVER := $(PS5_OPENGL_ROOT)/src/gallium/ps5
PS5_OPENGL_PLATFORM := $(PS5_OPENGL_ROOT)/src/platform

PS5_OPENGL_PUBLIC_CFLAGS := -DGL_GLEXT_PROTOTYPES=1 \
	-I$(PS5_OPENGL_MESA_SRC)/include

PS5_OPENGL_COMMON_CFLAGS := -std=c11 -Os -g -Wall -Wextra -Werror \
	-ffunction-sections -fdata-sections -DHAVE_FUNC_ATTRIBUTE_PACKED=1 \
	-I$(PS5_OPENGL_MESA_BUILD)/src \
	-I$(PS5_OPENGL_MESA_SRC)/src \
	-I$(PS5_OPENGL_MESA_SRC)/include \
	-I$(PS5_OPENGL_MESA_SRC)/src/gallium/include \
	-I$(PS5_OPENGL_DRIVER) \
	-I$(PS5_OPENGL_MESA_SRC)/src/gallium/auxiliary

PS5_OPENGL_PSBC_CFLAGS := -DHAVE_PTHREAD=1 -DHAVE_STRUCT_TIMESPEC=1 \
	-Wno-error=unused-parameter -Wno-error=missing-field-initializers \
	-iquote$(PS5_OPENGL_PSBC)/src -I$(PS5_OPENGL_PSBC)/libpsbc \
	-I$(PS5_OPENGL_PSBC)/src

PS5_OPENGL_CORE33_DEFINES := \
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
	-DPS5_ENABLE_SMOOTH_RASTER_CANDIDATE=1

PS5_OPENGL_RUNTIME_OBJECTS := \
	$(PS5_OPENGL_BUILD)/ps5_egl.o \
	$(PS5_OPENGL_BUILD)/ps5_screen.o \
	$(PS5_OPENGL_BUILD)/ps5_agc_package.o \
	$(PS5_OPENGL_BUILD)/ps5_agc_runtime_backend.o \
	$(PS5_OPENGL_BUILD)/u_framebuffer.o
PS5_OPENGL_RUNTIME := $(PS5_OPENGL_BUILD)/libps5_opengl_core33.a
PS5_OPENGL_IMPORT_STUBS := \
	$(PS5_OPENGL_BUILD)/libSceAgc.so \
	$(PS5_OPENGL_BUILD)/libSceAgcDriver.so

PS5_OPENGL_MESA_LIBS := \
	$(PS5_OPENGL_MESA_BUILD)/src/mesa/libmesa.a \
	$(PS5_OPENGL_MESA_BUILD)/src/mesa/libmesa_sse41.a \
	$(PS5_OPENGL_MESA_BUILD)/src/gallium/auxiliary/libgallium.a \
	$(PS5_OPENGL_MESA_BUILD)/src/mesa/glapi/shared-glapi/libglapi.a \
	$(PS5_OPENGL_MESA_BUILD)/src/compiler/glsl/libglsl.a \
	$(PS5_OPENGL_MESA_BUILD)/src/compiler/glsl/glcpp/libglcpp.a \
	$(PS5_OPENGL_MESA_BUILD)/src/compiler/glsl/libglsl_util.a \
	$(PS5_OPENGL_MESA_BUILD)/src/compiler/spirv/libvtn.a \
	$(PS5_OPENGL_MESA_BUILD)/src/compiler/nir/libnir.a \
	$(PS5_OPENGL_MESA_BUILD)/src/compiler/libcompiler.a \
	$(PS5_OPENGL_MESA_BUILD)/src/util/libmesa_util.a \
	$(PS5_OPENGL_MESA_BUILD)/src/util/libmesa_util_simd.a \
	$(PS5_OPENGL_MESA_BUILD)/src/util/blake3/libblake3.a \
	$(PS5_OPENGL_MESA_BUILD)/src/c11/impl/libmesa_util_c11.a
PS5_OPENGL_GLAPI_BRIDGE := \
	$(PS5_OPENGL_MESA_BUILD)/src/mesa/glapi/glapi/libglapi_bridge.a

PS5_OPENGL_STATIC_LIBS := \
	$(PS5_OPENGL_RUNTIME) $(PS5_OPENGL_GLAPI_BRIDGE) \
	$(PS5_OPENGL_PSBC)/libpsbc.ps5.a $(PS5_OPENGL_MESA_LIBS)
PS5_OPENGL_LDLIBS = -Wl,-u,ps5_agc_gate2_run -Wl,--start-group \
	$(PS5_OPENGL_STATIC_LIBS) \
	-Wl,--end-group -L$(PS5_OPENGL_BUILD) -lSceAgc -lSceAgcDriver \
	-lSceVideoOut -lkernel_web -lSceSystemService
PS5_OPENGL_LDFLAGS := -Wl,--gc-sections -Wl,--build-id=sha1

$(PS5_OPENGL_BUILD):
	mkdir -p $@

$(PS5_OPENGL_BUILD)/ps5_egl.o: $(PS5_OPENGL_ROOT)/src/egl/ps5_egl.c \
	$(PS5_OPENGL_DRIVER)/ps5_screen.h | $(PS5_OPENGL_BUILD)
	$(CC) $(PS5_OPENGL_COMMON_CFLAGS) -DHAVE_PTHREAD=1 \
		-DHAVE_STRUCT_TIMESPEC=1 -DPS5_ENABLE_COMPRESSED_FALLBACK_CANDIDATE=1 \
		-DPS5_ENABLE_CORE_CONTEXT_CANDIDATE=1 \
		-Wno-error=unused-parameter -Wno-unreachable-code-generic-assoc \
		-I$(PS5_OPENGL_MESA_SRC)/src/mesa -c -o $@ $<

$(PS5_OPENGL_BUILD)/ps5_screen.o: $(PS5_OPENGL_DRIVER)/ps5_screen.c \
	$(PS5_OPENGL_DRIVER)/ps5_screen.h $(PS5_OPENGL_PLATFORM)/ps5_agc_package.h \
	$(PS5_OPENGL_PSBC)/libpsbc/psbc_compile.h \
	$(ps5_opengl_mk_self) \
	| $(PS5_OPENGL_BUILD)
	$(CC) $(PS5_OPENGL_COMMON_CFLAGS) $(PS5_OPENGL_PSBC_CFLAGS) \
		$(PS5_OPENGL_CORE33_DEFINES) $(PS5_OPENGL_RUNTIME_DEFINES) \
		-I$(PS5_OPENGL_PLATFORM) -c -o $@ $<

$(PS5_OPENGL_BUILD)/ps5_agc_package.o: \
	$(PS5_OPENGL_PLATFORM)/ps5_agc_package.c \
	$(PS5_OPENGL_PLATFORM)/ps5_agc_package.h \
	$(PS5_OPENGL_PSBC)/libpsbc/psbc_compile.h | $(PS5_OPENGL_BUILD)
	$(CC) $(PS5_OPENGL_COMMON_CFLAGS) -I$(PS5_OPENGL_PSBC)/libpsbc \
		-I$(PS5_OPENGL_PLATFORM) -c -o $@ $<

$(PS5_OPENGL_BUILD)/ps5_agc_runtime_backend.o: \
	$(PS5_OPENGL_PLATFORM)/ps5_agc_runtime_backend.c \
	$(PS5_OPENGL_DRIVER)/ps5_screen.h \
	$(PS5_OPENGL_PLATFORM)/ps5_agc_native_runtime.c | $(PS5_OPENGL_BUILD)
	$(CC) $(PS5_OPENGL_COMMON_CFLAGS) -Wno-error=unused-function \
		$(PS5_OPENGL_RUNTIME_DEFINES) \
		-Dmain=ps5_agc_gate2_run -DAGC_TRIANGLE_SUBMIT=1 \
		-DAGC_RUNTIME_PACKAGES=1 -c -o $@ $<

$(PS5_OPENGL_BUILD)/u_framebuffer.o: \
	$(PS5_OPENGL_MESA_SRC)/src/gallium/auxiliary/util/u_framebuffer.c \
	| $(PS5_OPENGL_BUILD)
	$(CC) $(PS5_OPENGL_COMMON_CFLAGS) -Wno-error=unused-parameter \
		-c -o $@ $<

$(PS5_OPENGL_BUILD)/agc_link_stub.o: \
	$(PS5_OPENGL_ROOT)/native-app/agc_link_stub.c | $(PS5_OPENGL_BUILD)
	$(CC) -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections \
		-c -o $@ $<

$(PS5_OPENGL_BUILD)/agc_driver_link_stub.o: \
	$(PS5_OPENGL_ROOT)/native-app/agc_driver_link_stub.c | $(PS5_OPENGL_BUILD)
	$(CC) -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections \
		-c -o $@ $<

$(PS5_OPENGL_BUILD)/libSceAgc.so: $(PS5_OPENGL_BUILD)/agc_link_stub.o
	$(LD) --shared -soname libSceAgc.prx -o $@ $<

$(PS5_OPENGL_BUILD)/libSceAgcDriver.so: \
	$(PS5_OPENGL_BUILD)/agc_driver_link_stub.o
	$(LD) --shared -soname libSceAgcDriver.prx -o $@ $<

# Ninja owns Mesa's source/header graph; checking archive existence is not enough.
.PHONY: ps5-opengl-mesa
ps5-opengl-mesa:
	ninja -j8 -C "$(PS5_OPENGL_MESA_BUILD)" $(patsubst $(PS5_OPENGL_MESA_BUILD)/%,%,$(PS5_OPENGL_MESA_LIBS) $(PS5_OPENGL_GLAPI_BRIDGE))

$(PS5_OPENGL_RUNTIME_OBJECTS): | ps5-opengl-mesa

$(PS5_OPENGL_RUNTIME): $(PS5_OPENGL_RUNTIME_OBJECTS) | $(PS5_OPENGL_IMPORT_STUBS)
	$(AR) rcs $@ $^

ifeq ($(ps5_opengl_default_goal),)
.DEFAULT_GOAL :=
endif
