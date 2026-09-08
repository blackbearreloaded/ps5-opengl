SHELL := /bin/bash
PS5_NATIVE_APP_TEMPLATE ?= $(abspath ../ps5-native-app-boilerplate)
PS5_PAYLOAD_SDK ?= $(PS5_NATIVE_APP_TEMPLATE)/.deps/native/ps5-payload-sdk
export PS5_NATIVE_APP_TEMPLATE PS5_PAYLOAD_SDK

.PHONY: help source-fetch cts-fetch sdk imgui-demo nanovg sokol sokol-cube cubes test test-imgui test-sokol-cube test-cubes test-glsl test-compiler test-multidraw test-layered-mip
help:
	@printf '%s\n' 'source-fetch: pinned graphics/example sources' \
	  'sdk: build the compiler, Mesa and installed native OpenGL SDK' \
	  'imgui-demo / nanovg / sokol: package one example using the installed SDK' \
	  'sokol-cube: package the upstream 3D sample; see examples/core33-sokol-cube' \
	  'cubes: package the 3D frame benchmark using the current source runtime' \
	  'test: dependency-free host tests and published validation audit' \
	  'test-imgui: software-Mesa renderer and TV-demo input checks' \
	  'cts-fetch: also fetch pinned optional CTS sources; see docs/testing.md'

source-fetch:
	python3 tools/fetch-sources.py
cts-fetch:
	python3 tools/fetch-sources.py --cts
sdk:
	bash toolchain/build-opengnm-psbc.sh
	$(MAKE) test-compiler
	bash toolchain/build-opengnm-psbc-ps5.sh
	PS5_MESA_CROSS_FILE="$(PS5_PAYLOAD_SDK)/toolchain/prospero.ini" bash toolchain/build-mesa-ps5.sh
	bash toolchain/install-ps5-opengl-core33.sh build/sdk/ps5-opengl-core33
	python3 tests/ps5/verify_gl33_capability_audit.py
imgui-demo:
	bash tools/build-native-test-app.sh egl_public_core33_imgui_tv
nanovg:
	bash tools/build-native-test-app.sh egl_public_core33_nanovg
sokol:
	bash tools/build-native-test-app.sh egl_public_core33_sokol
sokol-cube:
	bash tools/build-native-test-app.sh egl_public_core33_sokol_cube
test-sokol-cube:
	bash tools/test-sokol-cube-host.sh
cubes:
	bash tools/build-native-test-app.sh egl_public_core33_cubes
test-cubes:
	bash tools/test-cubes-host.sh
test-glsl:
	bash tools/test-glsl-host.sh
test-multidraw:
	bash tools/test-multidraw-host.sh
test-layered-mip:
	bash tools/test-layered-mip-host.sh
test:
	python3 -m unittest discover -s tools -p 'test_*.py'
	python3 tests/ps5/test_gpu_clear_state.py
	python3 tests/ps5/test_draw_profile.py
	python3 tests/ps5/test_gpu_present.py
	python3 tests/ps5/test_submit_batch_probe.py
	python3 tests/ps5/test_multidraw_lifetime.py
	python3 tests/ps5/test_submit_retirement.py
	python3 tests/ps5/test_present_shutdown.py
	python3 tests/ps5/test_transfer_staging.py
	python3 tests/ps5/test_color_staging.py
	python3 tests/ps5/test_resource_release.py
	python3 tests/ps5/test_app_heap.py
	python3 tools/summarize-imgui-profile.py --self-test
	python3 tools/summarize-app-heap.py --self-test
	python3 tools/summarize-cubes.py --self-test
	python3 tools/verify-cts-candidate.py --self-test
	python3 tools/verify-published-validation.py
test-imgui:
	bash tools/test-imgui-host.sh
	bash tools/test-imgui-host.sh --tv-demo
	python3 tests/ps5/test_imgui_egl_cleanup.py
test-compiler:
	python3 tools/fetch-sources.py --verify-psbc
	python3 tests/ps5/test_fragment_exports.py
	python3 tests/ps5/test_meta_vertex_inputs.py
	python3 tests/ps5/test_unused_primitive_export.py
	python3 tests/ps5/test_vertex_constants.py
	python3 tests/ps5/test_geometry_texture_bindings.py
