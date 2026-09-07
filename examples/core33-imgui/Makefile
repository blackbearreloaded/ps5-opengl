SHELL := bash

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk/
PS5_OPENGL_PREFIX ?= ../../build/sdk/ps5-opengl-core33
IMGUI_DIR ?= ../../third_party/imgui
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
include $(PS5_OPENGL_PREFIX)/share/ps5-opengl-core33/ps5-opengl-core33.mk

objects := main.o imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_opengl3.o
flags := -std=c++11 -O2 -g -fPIC -ffunction-sections -fdata-sections \
    -Wall -Wextra -Werror $(PS5_OPENGL_PUBLIC_CFLAGS) -I$(IMGUI_DIR) \
    -I$(IMGUI_DIR)/backends -DIMGUI_IMPL_OPENGL_LOADER_CUSTOM -include GL/gl.h

.PHONY: all check-source
all: check-source egl_public_core33_imgui.o

check-source:
	@test "$$(git -C $(IMGUI_DIR) rev-parse HEAD)" = f5befd2d29e66809cd1110a152e375a7f1981f06
	@test -z "$$(git -C $(IMGUI_DIR) status --porcelain)"

main.o: main.cpp
	$(CXX) $(flags) -c -o $@ $<

lifecycle.o: lifecycle.cpp main.cpp
	$(CXX) $(flags) -c -o $@ $<

egl_public_core33_imgui_lifecycle.o: lifecycle.o $(filter-out main.o,$(objects))
	ld.lld-18 -r -o $@ $^

tv_main.o: main.cpp tv_demo.h
	$(CXX) $(flags) -DPS5_IMGUI_TV_DEMO $(if $(filter 1,$(PS5_IMGUI_PROFILE)),-DPS5_IMGUI_PROFILE) -c -o $@ $<

egl_public_core33_imgui_tv.o: tv_main.o $(filter-out main.o,$(objects))
	ld.lld-18 -r -o $@ $^

imgui_impl_opengl3.o: $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
	$(CXX) $(flags) -c -o $@ $<

%.o: $(IMGUI_DIR)/%.cpp
	$(CXX) $(flags) -c -o $@ $<

egl_public_core33_imgui.o: $(objects)
	ld.lld-18 -r -o $@ $(objects)
