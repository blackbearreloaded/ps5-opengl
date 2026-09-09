# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check shared display layout constants and the actual read-only status reporter."""
from pathlib import Path
import subprocess
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ScanoutConfigTest(unittest.TestCase):
    def test_capability_pool_inventory(self):
        audit = (ROOT / "tests/ps5/verify_gl33_capability_audit.py").read_text()
        body = audit[audit.index("core33_runtime_defines ="):audit.index('require("PS5_OPENGL_IMPORT_STUBS"')]
        standalone = (ROOT / "tests/ps5/Makefile").read_text().split("ps5_screen_core33.o:", 1)[1].split("\n\n", 1)[0]
        native = (ROOT / "toolchain/ps5-opengl-core33.mk").read_text()
        screen = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()

        def check(test=standalone, runtime=native, source=screen):
            exec(body, {"core33_build": test, "CORE33_MK": runtime,
                        "SCREEN": source, "require": self.assertTrue})

        check()
        for old, new in (("PS5_RENDER_ARENA_BYTES=0x2c00000u", "PS5_RENDER_ARENA_BYTES=0"),
                         ("-DPS5_RENDER_ARENA_BYTES=0x2c00000u", ""),
                         ("PS5_ENABLE_MSAA4_CANDIDATE=1", "PS5_ENABLE_MSAA4_CANDIDATE=0"),
                         ("$(ps5_opengl_mk_self)", "")):
            with self.subTest(old=old), self.assertRaises(AssertionError):
                check(runtime=native.replace(old, new))
        with self.assertRaises(AssertionError):
            check(test=standalone.replace("PS5_RENDER_POOL_BYTES=0x4000000u", "PS5_RENDER_POOL_BYTES=0"))
        with self.assertRaises(AssertionError):
            check(source=screen.replace("PS5_SCANOUT_POOL_BYTES + PS5_RENDER_ARENA_BYTES", "0"))

    def test_layout(self):
        source = r'''
#include <assert.h>
#include "ps5_screen.h"
int main(void) {
    assert(PS5_SCANOUT_WIDTH == EXPECT_WIDTH && PS5_SCANOUT_HEIGHT == EXPECT_HEIGHT);
    assert(PS5_RENDER_WIDTH == PS5_SCANOUT_WIDTH && PS5_RENDER_HEIGHT == PS5_SCANOUT_HEIGHT);
    assert(PS5_SCANOUT_BYTES == EXPECT_BYTES);
    assert(PS5_SCANOUT_BYTES >= PS5_SCANOUT_TILED_BYTES);
    assert(PS5_SCANOUT_BYTES % PS5_SCANOUT_ALIGNMENT == 0);
    assert(PS5_SCANOUT_BYTES - PS5_SCANOUT_TILED_BYTES < PS5_SCANOUT_ALIGNMENT);
    assert(PS5_SCANOUT_POOL_BYTES == 2u * PS5_SCANOUT_BYTES);
    assert(PS5_SCANOUT_POOL_BYTES + 0x2c00000u == EXPECT_POOL);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            executable = str(Path(tmp) / "layout")
            common = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-",
                      "-I" + str(ROOT / "src/gallium/ps5"), "-o", executable]
            for height, width, size, pool in ((1080, 1920, 0xa00000, 0x4000000),
                                             (1440, 2560, 0x1000000, 0x4c00000),
                                             (2160, 3840, 0x2000000, 0x6c00000)):
                flags = [f"-DEXPECT_WIDTH={width}", f"-DEXPECT_HEIGHT={height}",
                         f"-DEXPECT_BYTES={size}", f"-DEXPECT_POOL={pool}"]
                variants = [[f"-DPS5_SCANOUT_HEIGHT={height}"]]
                if height == 1080:
                    variants.append([])  # Existing default is byte-for-byte the same layout.
                if height == 2160:
                    variants.append(["-DAGC_4K=1"])
                for mode in variants:
                    subprocess.run(common + flags + mode, input=source, text=True, check=True)
                    subprocess.run([executable], check=True)
            for mode in (["-DPS5_SCANOUT_HEIGHT=0"], ["-DPS5_SCANOUT_HEIGHT=1081"],
                         ["-DPS5_SCANOUT_HEIGHT=4320"], ["-DPS5_SCANOUT_HEIGHT=1440", "-DAGC_4K=1"]):
                result = subprocess.run(common + mode, input='#include "ps5_screen.h"\nint main(void) { return 0; }\n',
                                        text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
        # All four consumers and incremental build dependencies must share this layout.
        for file, marker in (("src/egl/ps5_egl.c", "#define PS5_EGL_WIDTH ((EGLint)PS5_SCANOUT_WIDTH)"),
                             ("src/gallium/ps5/ps5_screen.c", "#define PS5_RENDER_TARGET_BYTES PS5_SCANOUT_BYTES"),
                             ("src/platform/ps5_agc_runtime_backend.c", "#define PS5_AGC_FRAMEBUFFER_BYTES PS5_SCANOUT_BYTES"),
                             ("src/platform/ps5_agc_native_runtime.c", "#define FRAMEBUFFER_BYTES PS5_SCANOUT_BYTES")):
            self.assertIn(marker, (ROOT / file).read_text())
        make = (ROOT / "toolchain/ps5-opengl-core33.mk").read_text()
        self.assertIn("$(PS5_OPENGL_PLATFORM)/ps5_scanout.h", make)
        self.assertIn("-DPS5_RENDER_ARENA_BYTES=0x2c00000u", make)

    def test_status_reporter(self):
        source = (ROOT / "src/platform/ps5_agc_native_runtime.c").read_text()
        start = source.index("typedef struct runtime_resolution_status {")
        body = source[start:source.index("\n#endif", start)]
        code = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include "ps5_scanout.h"
#define DISPLAY_WIDTH PS5_SCANOUT_WIDTH
#define DISPLAY_HEIGHT PS5_SCANOUT_HEIGHT
#define FRAMEBUFFER_BYTES PS5_SCANOUT_BYTES
static int runtime_video_handle = 7, failure, calls;
''' + body + r'''
int sceVideoOutGetResolutionStatus(int32_t handle, runtime_resolution_status_t *s) {
    assert(handle == 7 && !s->full_width && !s->refresh_rate); ++calls;
    if (failure) return -1;
    s->full_width = 3840; s->full_height = 2160;
    s->pane_width = 1920; s->pane_height = 1080; s->refresh_rate = 3;
    return 0;
}
int sceVideoOutGetOutputStatus(int32_t handle, runtime_output_status_t *s) {
    assert(handle == 7 && !s->refresh_rate); ++calls;
    if (failure) return -2;
    s->refresh_rate = 3; return 0;
}
int main(void) {
    runtime_video_report("warmup"); failure = 1; runtime_video_report("end");
    assert(calls == 4);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            executable = str(Path(tmp) / "status")
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-",
                            "-I" + str(ROOT / "src/platform"), "-o", executable],
                           input=code, text=True, check=True)
            output = subprocess.check_output([executable], text=True).splitlines()
        self.assertEqual(len(output), 2)
        self.assertIn("render_width=1920 render_height=1080 buffer_bytes=10485760", output[0])
        self.assertIn("full_width=3840 full_height=2160 pane_width=1920 pane_height=1080", output[0])
        self.assertIn("resolution_rc=ffffffff full_width=0", output[1])
        self.assertIn("output_rc=fffffffe output_refresh_id=0", output[1])

    def test_high_refresh_lifecycle(self):
        source = (ROOT / "src/platform/ps5_agc_native_runtime.c").read_text()
        start = source.index("#if PS5_SCANOUT_FPS > 60\n#ifndef PS5_NATIVE_TITLE_RUNTIME")
        body = source[start:source.index("\nint ps5_agc_gate2_shutdown_present", start)]
        code = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include "ps5_scanout.h"
#define PS5_NATIVE_TITLE_RUNTIME 1
static int runtime_video_handle = 7, support = 1, preset, restore, wait_result;
static int presets, restores, waits;
static int wait_vblank(int h) { assert(h == 7); ++waits; return wait_result; }
static struct { int (*wait_vblank)(int); } runtime_video_api = { wait_vblank };
''' + body + r'''
int sceVideoOutIsOutputSupported(int32_t h, uint32_t type, const void *a, const void *b, const void *c) {
    assert(h == 7 && type == 15 && !a && !b && !c); return support;
}
int sceVideoOutConfigureOutput(int32_t h, uint32_t type, const void *a, const void *b, const void *c) {
    assert(h == 7 && !a && !b && !c);
    if (type == 15) { ++presets; return preset; }
    assert(type == 1); ++restores; return restore;
}
int main(void) {
    for (support = -1; support <= 0; ++support) {
        assert(runtime_video_configure_output() != 0 && !runtime_output_needs_restore);
        assert(runtime_video_restore_output() == 0 && !presets && !restores && !waits);
    }
    support = 1; preset = -2;
    assert(runtime_video_configure_output() == -2 && runtime_output_needs_restore);
    assert(runtime_video_restore_output() == 0 && !runtime_output_needs_restore && waits == 2);
    preset = 0; assert(runtime_video_configure_output() == 0 && runtime_output_needs_restore);
    restore = -4; assert(runtime_video_restore_output() == -4 && runtime_output_needs_restore);
    restore = 0; wait_result = -5;
    assert(runtime_video_restore_output() == -5 && runtime_output_needs_restore);
    wait_result = 0; assert(runtime_video_restore_output() == 0 && !runtime_output_needs_restore);
    assert(runtime_video_configure_output() == 0 && runtime_output_needs_restore);
    assert(runtime_video_restore_output() == 0 && !runtime_output_needs_restore);
    int previous = restores; assert(runtime_video_restore_output() == 0 && restores == previous);
    assert(!runtime_output_reopen_pending); /* Configuration/restoration is not a successful close. */
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            executable = str(Path(tmp) / "hfr")
            for fps in (120,):
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-",
                                "-I" + str(ROOT / "src/platform"), f"-DPS5_SCANOUT_FPS={fps}", "-o", executable],
                               input=code, text=True, check=True)
                subprocess.run([executable], check=True, capture_output=True)
            for fps in (0, 30, 90, 91, 121):
                result = subprocess.run(["cc", "-x", "c", "-", "-I" + str(ROOT / "src/platform"),
                                         f"-DPS5_SCANOUT_FPS={fps}", "-o", executable],
                                        input='#include "ps5_scanout.h"\nint main(void) { return 0; }',
                                        text=True, capture_output=True)
                self.assertNotEqual(result.returncode, 0)
        shutdown = source[source.index("int ps5_agc_gate2_shutdown_present"):source.index("static int runtime_video_acquire")]
        self.assertLess(shutdown.index("runtime_video_wait_idle()"), shutdown.index("runtime_video_restore_output()"))
        self.assertLess(shutdown.index("runtime_video_restore_output()"), shutdown.index("runtime_video_api.close("))
        acquire = source[source.index("static int runtime_video_acquire"):source.index("static int runtime_video_wait_idle(void)\n{")]
        self.assertLess(acquire.index("runtime_video_configure_output()"), acquire.index("video->register_buffers2("))

    def test_high_refresh_metadata(self):
        builder = (ROOT / "tools/build-native-test-app.sh").read_text()
        body = builder.split("<<'HFR_METADATA'\n", 1)[1].split("\nHFR_METADATA", 1)[0]
        original = json.loads((ROOT / "native-app/param.json").read_text())
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "param.json"
            path.write_text(json.dumps(original))
            subprocess.run([sys.executable, "-", str(path)], input=body, text=True, check=True)
            actual = json.loads(path.read_text())
        self.assertEqual(actual, original | {"attribute3": 0x80040})
