# PS5 OpenGL - native display profile checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib
from pathlib import Path
import subprocess
import tempfile
import unittest

CHECK = importlib.import_module('check-sdk-consumers')
ROOT = Path(__file__).resolve().parents[1]


class DisplayProfileTests(unittest.TestCase):
    def test_installed_header_matches_compiled_native_mode(self):
        installer = (ROOT / 'toolchain/install-ps5-opengl-core33.sh').read_text()
        start = installer.index('display_height=${PS5_SCANOUT_HEIGHT:-1080}')
        end = installer.index('\nfor library in ', start)
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary)
            (sdk / 'include').mkdir()
            self.assertEqual(CHECK.display_profile(sdk), dict(width=1920, height=1080, fps=60))
            for height, width in ((1080, 1920), (1440, 2560), (2160, 3840)):
                for fps in (60, 120):
                    subprocess.run(['bash', '-ec', 'prefix=$1; PS5_SCANOUT_HEIGHT=$2; PS5_SCANOUT_FPS=$3;\n'
                                    + installer[start:end], 'profile', str(sdk), str(height), str(fps)], check=True)
                    self.assertEqual(CHECK.display_profile(sdk), dict(width=width, height=height, fps=fps))
                    code = ('#include "ps5_opengl_display.h"\n#include "ps5_scanout.h"\n'
                            '_Static_assert(PS5_OPENGL_NATIVE_WIDTH == PS5_SCANOUT_WIDTH, "width");\n'
                            '_Static_assert(PS5_OPENGL_NATIVE_HEIGHT == PS5_SCANOUT_HEIGHT, "height");\n'
                            '_Static_assert(PS5_OPENGL_NATIVE_FPS == PS5_SCANOUT_FPS, "fps");\n')
                    subprocess.run(['cc', '-std=c11', '-fsyntax-only', '-x', 'c', '-', '-I', str(sdk / 'include'),
                                    '-I', str(ROOT / 'src/platform'), f'-DPS5_SCANOUT_HEIGHT={height}',
                                    f'-DPS5_SCANOUT_FPS={fps}'], input=code, text=True, check=True)
            path = sdk / 'include/ps5_opengl_display.h'
            good = path.read_text()
            for bad in (good.replace('3840', '1920'), good.replace('2160', '1440'),
                        good.replace('120', '90'), good.replace('120', '120 + 0'),
                        good + '#include "other.h"\n', good + '#define PS5_OPENGL_NATIVE_FPS 60\n'):
                path.write_text(bad)
                with self.assertRaises(ValueError):
                    CHECK.display_profile(sdk)
            path.unlink()
            path.symlink_to(sdk / 'missing')
            with self.assertRaises(ValueError):
                CHECK.display_profile(sdk)


if __name__ == '__main__':
    unittest.main()
