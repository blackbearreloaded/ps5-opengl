# PS5 target configuration for the pinned opengnm-psbc compiler subset.
# Included from third_party/opengnm-psbc by Makefile.ps5.

OPENGNM_INCLUDE ?= ../opengnm/include
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk

SHARED_FLAGS = \
	-Iinclude/ \
	-Ilibpsbc/ \
	-I$(OPENGNM_INCLUDE) \
	-I../Vulkan-Headers/include \
	-Isrc/ \
	-Isrc/amd \
	-Isrc/amd/common \
	-Isrc/amd/common/nir \
	-Isrc/amd/compiler \
	-Isrc/amd/vulkan \
	-Isrc/amd/vulkan/nir \
	-Isrc/vulkan/runtime \
	-Isrc/vulkan/runtime/bvh \
	-Isrc/vulkan/util \
	-Isrc/compiler \
	-Isrc/compiler/nir \
	-Isrc/compiler/spirv \
	-Isrc/gallium/include \
	-Isrc/mesa \
	-Isrc/mesa/main \
	-Isrc/util \
	-Icmd/psbc \
	-Iinclude/mesa \
	-D_GNU_SOURCE \
	-D_XOPEN_SOURCE=700 \
	-DUTIL_ARCH_LITTLE_ENDIAN=1 \
	-DUTIL_ARCH_BIG_ENDIAN=0 \
	-DHAVE_STRUCT_TIMESPEC=1 \
	-DHAVE_PTHREAD=1 \
	-DHAVE_SYSCONF=1 \
	-DHAVE_FUNC_ATTRIBUTE_PACKED=1 \
	-Dalloca=__builtin_alloca \
	-include strings.h \
	-DBLAKE3_NO_SSE2 \
	-DBLAKE3_NO_SSE41 \
	-DBLAKE3_NO_AVX2 \
	-DBLAKE3_NO_AVX512

CC = $(PS5_PAYLOAD_SDK)/bin/prospero-clang
CXX = $(PS5_PAYLOAD_SDK)/bin/prospero-clang++
AR = $(PS5_PAYLOAD_SDK)/bin/prospero-ar
PYTHON = python3

CFLAGS = -std=gnu11 -O2 -g -Wall -fPIC -DOPENGNM_PSBC_ORBIS=1 $(SHARED_FLAGS) \
	-Dstatic_assert=_Static_assert \
	-Wno-unused-function -Wno-unused-variable \
	-Wno-unreachable-code-generic-assoc
CXXFLAGS = -std=c++17 -O2 -g -Wall -fPIC -DNDEBUG -DOPENGNM_PSBC_ORBIS=1 $(SHARED_FLAGS) \
	-Wno-unused-function -Wno-unused-variable \
	-Wno-unreachable-code-generic-assoc
