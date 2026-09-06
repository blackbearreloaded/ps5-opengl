SHELL := bash
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk/
PS5_OPENGL_PREFIX ?= ../../build/sdk/ps5-opengl-core33
NANOVG_DIR ?= ../../third_party/nanovg
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
include $(PS5_OPENGL_PREFIX)/share/ps5-opengl-core33/ps5-opengl-core33.mk
flags := -std=gnu11 -O2 -g -fPIC -ffunction-sections -fdata-sections \
    -Wall -Werror=implicit-function-declaration $(PS5_OPENGL_PUBLIC_CFLAGS) -I$(NANOVG_DIR)/src
.PHONY: all check-source
all: check-source egl_public_core33_nanovg.o
check-source:
	@test "$$(git -C $(NANOVG_DIR) rev-parse HEAD)" = ce3bf745eb2d2dbc14a50bf2446783f691ac4353
	@test -z "$$(git -C $(NANOVG_DIR) status --porcelain)"
main.o: main.c
	$(CC) $(flags) -c -o $@ $<
nanovg.o: $(NANOVG_DIR)/src/nanovg.c
	$(CC) $(flags) -c -o $@ $<
egl_public_core33_nanovg.o: main.o nanovg.o
	ld.lld-18 -r -o $@ $^
