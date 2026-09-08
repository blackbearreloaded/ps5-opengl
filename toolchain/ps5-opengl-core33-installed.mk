# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Relocatable consumer fragment installed under share/ps5-opengl-core33.

ps5_opengl_installed_dir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
PS5_OPENGL_PREFIX ?= $(abspath $(ps5_opengl_installed_dir)/../..)
PS5_OPENGL_PUBLIC_CFLAGS := -DGL_GLEXT_PROTOTYPES=1 \
	-I$(PS5_OPENGL_PREFIX)/include
PS5_OPENGL_LDLIBS := -Wl,-u,ps5_agc_gate2_run \
	-L$(PS5_OPENGL_PREFIX)/lib -Wl,--start-group \
	-lPS5OpenGLCore33 -Wl,--end-group -lSceAgc -lSceAgcDriver \
	-lSceVideoOut -lkernel_web -lSceSystemService
PS5_OPENGL_LDFLAGS := -Wl,--gc-sections -Wl,--build-id=sha1
