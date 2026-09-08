# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

SHELL := bash
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk/
PS5_OPENGL_PREFIX ?= ../../build/sdk/ps5-opengl-core33
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
include $(PS5_OPENGL_PREFIX)/share/ps5-opengl-core33/ps5-opengl-core33.mk
.PHONY: all check-source
all: egl_public_core33_sokol_cube.o
check-source:
	python3 prepare.py
egl_public_core33_sokol_cube.o: main.c native_glue.h check-source
	$(CC) -std=gnu11 -O2 -g -fPIC -ffunction-sections -fdata-sections -Wall \
	    $(if $(filter 1,$(PS5_SOKOL_MAPPED_READBACK)),-DPS5_SOKOL_MAPPED_READBACK) \
	    $(if $(filter 1,$(PS5_SOKOL_HEAP_READBACK)),-DPS5_SOKOL_HEAP_READBACK) \
	    -Werror=implicit-function-declaration $(PS5_OPENGL_PUBLIC_CFLAGS) \
	    -I. -I../../build/sokol-cube-source -I../../third_party/sokol \
	    -I../../third_party/sokol-samples/libs/vecmath -c -o $@ $<
