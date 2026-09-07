SHELL := bash

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk/
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
PS5_OPENGL_BUILD := $(abspath ../../build/core33-native-runtime)
PS5_DEFERRED_DRAW_BATCH ?= 1
PS5_OPENGL_RUNTIME_DEFINES := -DPS5_NATIVE_TITLE_RUNTIME=1
ifneq ($(PS5_SCANOUT_FPS),)
ifneq ($(words $(PS5_SCANOUT_FPS)),1)
$(error PS5_SCANOUT_FPS must be one rate)
endif
ifneq ($(filter $(PS5_SCANOUT_FPS),60 120),$(PS5_SCANOUT_FPS))
$(error PS5_SCANOUT_FPS must be 60 or 120)
endif
PS5_OPENGL_RUNTIME_DEFINES += -DPS5_SCANOUT_FPS=$(PS5_SCANOUT_FPS)
endif
ifneq ($(PS5_SCANOUT_HEIGHT),)
ifneq ($(words $(PS5_SCANOUT_HEIGHT)),1)
$(error PS5_SCANOUT_HEIGHT must be one resolution)
endif
ifneq ($(filter $(PS5_SCANOUT_HEIGHT),1080 1440 2160),$(PS5_SCANOUT_HEIGHT))
$(error PS5_SCANOUT_HEIGHT must be 1080, 1440 or 2160)
endif
PS5_OPENGL_RUNTIME_DEFINES += -DPS5_SCANOUT_HEIGHT=$(PS5_SCANOUT_HEIGHT)
endif
ifeq ($(PS5_DRAW_PROFILE),1)
PS5_OPENGL_RUNTIME_DEFINES += -DPS5_DRAW_PROFILE=1
endif
ifeq ($(PS5_GPU_PRESENT_BATCH),1)
PS5_OPENGL_RUNTIME_DEFINES += -DPS5_GPU_PRESENT_BATCH=1
endif
ifeq ($(PS5_DRAW_BATCH_PROBE),1)
PS5_OPENGL_RUNTIME_DEFINES += -DPS5_DRAW_BATCH_PROBE=1
endif
ifneq ($(filter 1,$(PS5_MULTIDRAW_BATCH) $(PS5_DEFERRED_DRAW_BATCH)),)
PS5_OPENGL_RUNTIME_DEFINES += -DPS5_MULTIDRAW_BATCH=1
endif
ifeq ($(PS5_DEFERRED_DRAW_BATCH),1)
PS5_OPENGL_RUNTIME_DEFINES += -DPS5_DEFERRED_DRAW_BATCH=1
endif
include ../../toolchain/ps5-opengl-core33.mk

.PHONY: all runtime print-static-libs
all: runtime
runtime: $(PS5_OPENGL_RUNTIME)

print-static-libs:
	@printf '%s\n' $(PS5_OPENGL_STATIC_LIBS)
