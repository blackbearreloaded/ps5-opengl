# PS5 OpenGL - local CTS applicability correction checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compile the recorded patch's condition, not a rewritten Python predicate."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ComputeGuardTest(unittest.TestCase):
    def test_desktop_and_es_version_boundaries(self):
        patch = (ROOT / 'conformance/vk-gl-cts/patches/0005-gl33-negative-compute-guard.patch').read_text()
        versions = ('100_ES', '300_ES', '310_ES', '320_ES', '130', '140', '150',
                    '330', '400', '410', '420', '430', '440', '450', '460')
        # Match the pinned glu::GLSLVersion order; ES and desktop are separate ranges.
        declarations = ', '.join('GLSL_VERSION_' + v for v in versions)
        def condition(prefix):
            lines = '\n'.join(line[1:] for line in patch.splitlines()
                              if line.startswith(prefix) and not line.startswith(prefix * 3))
            return re.search(r'if\s*\((.*)\)\s*$', lines, re.S)[1]
        source = ('#include <cassert>\nenum GLSLVersion {' + declarations + '};\n'
                  'constexpr bool glslVersionIsES(GLSLVersion v) { return v <= GLSL_VERSION_320_ES; }\n'
                  'bool fixed(GLSLVersion m_glslVersion) { return ' + condition('+') + '; }\n'
                  'bool original(GLSLVersion m_glslVersion) { return ' + condition('-') + '; }\n'
                  'int main() { assert(original(GLSL_VERSION_330));\n')
        for version in versions:
            minimum = 310 if version.endswith('_ES') else 430
            expected = int(version.split('_')[0]) >= minimum
            source += f'assert(fixed(GLSL_VERSION_{version}) == {str(expected).lower()});\n'
        source += '}\n'
        with tempfile.TemporaryDirectory() as tmp:
            binary = str(Path(tmp) / 'guard')
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-x', 'c++', '-', '-o', binary],
                           input=source, text=True, check=True)
            subprocess.run([binary], check=True)


if __name__ == '__main__':
    unittest.main()
