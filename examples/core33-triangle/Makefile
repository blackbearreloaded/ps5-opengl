# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

SHELL := bash

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk/
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
include ../../toolchain/ps5-opengl-core33.mk

.PHONY: all clean
all: core33-triangle.elf

main.o: main.c
	$(CC) -std=c11 -Os -g -Wall -Wextra -Werror \
		$(PS5_OPENGL_PUBLIC_CFLAGS) -c -o $@ $<

core33-triangle.elf: main.o $(PS5_OPENGL_RUNTIME)
	$(CXX) -o $@ main.o $(PS5_OPENGL_LDLIBS) $(PS5_OPENGL_LDFLAGS)

clean:
	rm -f main.o core33-triangle.elf
