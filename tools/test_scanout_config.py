"""Check shared display layout constants and the actual read-only status reporter."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ScanoutConfigTest(unittest.TestCase):
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
