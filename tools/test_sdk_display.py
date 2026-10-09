# PS5 OpenGL - native display profile checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import importlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

CHECK = importlib.import_module('check-sdk-consumers')
METADATA = importlib.import_module('native-display-metadata')
ROOT = Path(__file__).resolve().parents[1]


class DisplayProfileTests(unittest.TestCase):
    def test_gl46_is_the_public_build_and_release_surface(self):
        makefile = (ROOT / 'Makefile').read_text()
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        verifier = (ROOT / 'tools/verify-installed-sdk.sh').read_text()
        self.assertIn('sdk: sdk-gl46', makefile)
        self.assertIn('build/sdk/ps5-opengl-gl46', makefile)
        sdk = makefile.split('sdk-gl46:', 1)[1].split('\nimgui-demo:', 1)[0]
        self.assertLess(sdk.index('build-mesa-ps5.sh'), sdk.index('test-compiler'))
        self.assertIn('apply-mesa-ps5-patch.sh',
                      (ROOT / 'toolchain/build-mesa-ps5.sh').read_text())
        self.assertLess(sdk.index('install-ps5-opengl-gl46.sh'),
                        sdk.index('verify_gl46_link_surface.py'))
        self.assertIn('run: make sdk-gl46', workflow)
        self.assertIn('build/sdk/ps5-opengl-gl46', workflow)
        self.assertIn('name: ps5-opengl-4.6-sdk', workflow)
        # The SDK archive and the showcase app ZIP are signed (build provenance) by a pinned action.
        self.assertIn('uses: actions/attest@1e69f48acb82d1966a394da916b4c1698aa569d6 # v4.2.2', workflow)
        self.assertLess(workflow.index('- name: Attest the release files'),
                        workflow.index('name: ps5-opengl-4.6-sdk'))
        self.assertIn('commands=657', verifier)

    def test_release_never_replaces_or_removes_a_file(self):
        workflow = (ROOT / '.github/workflows/release.yml').read_text()
        for forbidden in ('--clobber', 'delete-asset', 'release delete', 'release edit'):
            self.assertNotIn(forbidden, workflow)
        publish = workflow[workflow.index('- name: Publish OpenGL 4.6 release'):]
        # No release for the tag: created as before. One exists: missing files only, with a warning for the rest.
        self.assertIn('gh release create "$GITHUB_REF_NAME" ./*.tar.gz ./*.tar.gz.sha256 ./*.zip ./*.zip.sha256 \\\n'
                      '            --repo "$GITHUB_REPOSITORY" --verify-tag --latest \\\n', publish)
        self.assertIn("--json assets --jq '.assets[].name'", publish)
        self.assertIn('gh release upload "$TAG" "$file" --repo "$GITHUB_REPOSITORY"\n', publish)
        self.assertIn('::warning title=Release file not from this run::A release for $TAG already has $1;', publish)

    def test_native_metadata_uses_sdk_rate_and_preserves_other_fields(self):
        original = dict(titleId="PPSA99005", attribute3=8, attribute2=17)
        high = METADATA.with_display_profile(original, 120)
        self.assertEqual(high, dict(original, attribute3=8 | 0x80040))
        self.assertEqual(METADATA.with_display_profile(high, 60), original)
        self.assertEqual(original["attribute3"], 8)
        for bad in (None, True, -1, 2**32, "0"):
            with self.assertRaises(ValueError):
                METADATA.with_display_profile(dict(original, attribute3=bad), 120)
        with tempfile.TemporaryDirectory() as tmp:
            sdk = Path(tmp)
            (sdk / "include").mkdir()
            (sdk / "include/ps5_opengl_display.h").write_text(
                "#pragma once\n#define PS5_OPENGL_NATIVE_WIDTH 3840\n"
                "#define PS5_OPENGL_NATIVE_HEIGHT 2160\n#define PS5_OPENGL_NATIVE_FPS 120\n")
            path = sdk / "param.json"
            path.write_text(json.dumps(original))
            subprocess.run(["python3", str(ROOT / "tools/native-display-metadata.py"),
                            str(path), "--sdk-prefix", str(sdk)], check=True)
            self.assertEqual(json.loads(path.read_text()), high)
        for name in ("build-native-cts-app.sh", "build-native-test-app.sh"):
            self.assertIn('tools/native-display-metadata.py', (ROOT / "tools" / name).read_text())

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
