SHELL := bash

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk/
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
PS5_OPENGL_BUILD := $(abspath ../../build/core33-native-runtime)
PS5_OPENGL_RUNTIME_DEFINES := -DPS5_NATIVE_TITLE_RUNTIME=1
ifeq ($(PS5_DRAW_PROFILE),1)
PS5_OPENGL_RUNTIME_DEFINES += -DPS5_DRAW_PROFILE=1
endif
ifeq ($(PS5_DRAW_BATCH_PROBE),1)
PS5_OPENGL_RUNTIME_DEFINES += -DPS5_DRAW_BATCH_PROBE=1
endif
include ../../toolchain/ps5-opengl-core33.mk

.PHONY: all runtime print-static-libs
all: runtime
runtime: $(PS5_OPENGL_RUNTIME)

print-static-libs:
	@printf '%s\n' $(PS5_OPENGL_STATIC_LIBS)
