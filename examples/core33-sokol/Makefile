SHELL := bash
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk/
PS5_OPENGL_PREFIX ?= ../../build/sdk/ps5-opengl-core33
SOKOL_DIR ?= ../../third_party/sokol
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
include $(PS5_OPENGL_PREFIX)/share/ps5-opengl-core33/ps5-opengl-core33.mk
.PHONY: all check-source
all: check-source egl_public_core33_sokol.o
check-source:
	@test "$$(git -C $(SOKOL_DIR) rev-parse HEAD)" = 48c85905aeaa1350feb17515961aecb6c75447d8
	@test -z "$$(git -C $(SOKOL_DIR) status --porcelain)"
egl_public_core33_sokol.o: main.c
	$(CC) -std=gnu11 -O2 -g -fPIC -ffunction-sections -fdata-sections -Wall \
	    -Werror=implicit-function-declaration $(PS5_OPENGL_PUBLIC_CFLAGS) -I$(SOKOL_DIR) -c -o $@ $<
