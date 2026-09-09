#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compare actual depth/stencil addressing against pinned AMD Addrlib tables.

Requires fetched Mesa sources. This proves the table match, not console behavior.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
patterns = (root / 'third_party/mesa-26.2.0/src/amd/addrlib/src/gfx10/gfx10SwizzlePattern.h').read_text()

def section(start, end):
    return source[source.index(start):source.index(end, source.index(start))]

def table(name):
    body = re.search(r'\b' + re.escape(name) + r'\[\][^=]*=\s*\{(.*?)\n\};', patterns, re.S)
    assert body, name
    return [(row.split(','), comment.strip()) for row, comment in
            re.findall(r'\{([^{}]+)\}\s*,\s*//([^\n]+)', body[1])]

code = '#include <assert.h>\n#include <stdint.h>\n#include <stddef.h>\n'
code += '#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))\n#define BITFIELD_BIT(i) (1u<<(i))\n'
code += section('static size_t\nps5_tiled_depth_layer_xor(', 'static size_t\nps5_tiled_surface_size(')
code += section('static size_t\nps5_tiled_affine_offset(', 'static size_t\nps5_tiled_color_offset(')
checks = []
for samples, bpe, name, width in ((1, 4, 'depth', 128), (1, 1, 'stencil', 256),
                                  (4, 4, 'depth_msaa4', 64), (4, 1, 'stencil_msaa4', 128)):
    comment = f'16 pipes {bpe} bpe @ SW_64K_Z_X {samples}xaa @ Navi1x'
    rows = [row for row, label in table(f'GFX10_SW_64K_Z_X_{samples}xaa_PATINFO') if label == comment]
    assert len(rows) == 1
    indices = [int(v) for v in rows[0] if v.strip()]
    equations = []
    for nibble, index in zip(('01', '2', '3'), indices[1:4]):
        equations.extend(v.strip() for v in table('GFX10_SW_PATTERN_NIBBLE' + nibble)[index][0] if v.strip())
    assert len(equations) == 16
    expression = []
    for bit, equation in enumerate(equations):
        if equation == '0':
            continue
        terms = []
        for term in equation.split('^'):
            match = re.fullmatch(r'([XYZS])(\d+)', term.strip())
            assert match, term
            variable = dict(X='x', Y='y', Z='layer', S='sample')[match[1]]
            terms.append(f'(({variable} >> {int(match[2])}) & 1u)')
        expression.append(f'((size_t)({" ^ ".join(terms)}) << {bit})')
    actual = f'ps5_tiled_{name}_offset(x, y, ' + ('sample, ' if samples == 4 else '') + f'{width * 3}, layer)'
    code += f'''\nstatic void check_{name}(void) {{
        for (unsigned layer = 0; layer < 32; ++layer)
            for (unsigned sample = 0; sample < {samples}; ++sample)
                for (unsigned y = 0; y < {width * 2}; ++y)
                    for (unsigned x = 0; x < {width * 2}; ++x) {{
                        (void)sample;
                        size_t local = {' | '.join(expression)};
                        size_t expected = ((size_t)(y / {width}) * 3 + x / {width}) * 65536 + local;
                        assert({actual} == expected);
                    }}
    }}\n'''
    checks.append(f'check_{name}();')
code += 'int main(void) { ' + ' '.join(checks) + ' }\n'
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / 'depth-layer-layout')
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-x', 'c',
                    '-o', executable, '-'], input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print('PASS: AMD table equivalence, depth/stencil 1x/4x, 32 layers, tile boundaries')
