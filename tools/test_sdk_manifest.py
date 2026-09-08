#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Runnable negative checks for the installed SDK manifest verifier."""
import importlib.util
from pathlib import Path
import json
import os
import shutil
import subprocess
from tempfile import TemporaryDirectory
import unittest

spec = importlib.util.spec_from_file_location('checker', Path(__file__).resolve().parents[1] / 'tools/check-sdk-consumers.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def fixture(root):
    (root / 'lib').mkdir()
    for name, data in {
        'libPS5OpenGLCore33.a': b'GROUP ( libglapi.a )\n',
        'libglapi.a': b'!<arch>\n',
        'libSceAgc.so': b'import',
        'libSceAgcDriver.so': b'import',
    }.items():
        (root / 'lib' / name).write_bytes(data)
    (root / 'manifest.sha256').write_text(''.join(
        f'{checker.digest(path)}  {path.relative_to(root).as_posix()}\n'
        for path in sorted((root / 'lib').iterdir())))


def append(root, text):
    with (root / 'manifest.sha256').open('a') as stream:
        stream.write(text)


cases = {
    'tampered bytes': lambda root: (root / 'lib/libglapi.a').write_bytes(b'bad'),
    'unlisted file': lambda root: (root / 'extra').write_bytes(b'extra'),
    'duplicate entry': lambda root: append(root, (root / 'manifest.sha256').read_text().splitlines()[0] + '\n'),
    'parent traversal': lambda root: append(root, '0' * 64 + '  ../escape\n'),
    'absolute path': lambda root: append(root, '0' * 64 + '  /escape\n'),
    'symlink': lambda root: (root / 'alias').symlink_to('lib/libglapi.a'),
    'missing file': lambda root: (root / 'lib/libglapi.a').unlink(),
}
for name, mutate in cases.items():
    with TemporaryDirectory(prefix='sdk-manifest-test-') as temporary:
        root = Path(temporary)
        fixture(root)
        assert checker.verify_manifest(root)['files'] == 4
        mutate(root)
        try:
            checker.verify_manifest(root)
        except ValueError:
            pass
        else:
            raise AssertionError(f'Accepted {name}')
print(f'PASS: valid fixture plus {len(cases)} rejected manifest mutations')


class InstalledVerifierTests(unittest.TestCase):
    def test_installer_rejects_unlisted_files_without_cleaning_source_example(self):
        with TemporaryDirectory(prefix='sdk-installer-test-') as temporary:
            root = Path(temporary)
            for name in ('tools', 'toolchain', 'build', 'examples/core33-triangle'):
                (root / name).mkdir(parents=True)
            for name in ('verify-installed-sdk.sh', 'check-sdk-consumers.py'):
                shutil.copyfile(Path(__file__).parent / name, root / 'tools' / name)
            # Stand in for compilation: leave an installed package with an unlisted file.
            (root / 'toolchain/install-ps5-opengl-core33.sh').write_text('exit 0\n')
            sdk = root / 'build/sdk'
            sdk.mkdir()
            fixture(sdk)
            (sdk / 'unlisted').write_text('not checksummed')
            original = root / 'examples/core33-triangle/main-installed.o'
            original.write_text('existing consumer output')
            result = subprocess.run(
                ['bash', str(root / 'tools/verify-installed-sdk.sh'), str(sdk)],
                env={**os.environ, 'PS5_PAYLOAD_SDK': str(root / 'payload')},
                capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            reports = list((root / 'build').glob('installed-sdk-checks.*/consumers/summary.json'))
            self.assertEqual(len(reports), 1, result.stdout + result.stderr)
            report = json.loads(reports[0].read_text())
            self.assertEqual(report['status'], 'FAIL')
            self.assertIn('Manifest file-set mismatch', report['error'])
            self.assertEqual(original.read_text(), 'existing consumer output')


if __name__ == '__main__':
    unittest.main()
