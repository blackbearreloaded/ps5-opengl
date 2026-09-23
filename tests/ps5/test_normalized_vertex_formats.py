#!/usr/bin/env python3
"""The sampled normalized formats must reach native fetch, without conversion."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
driver = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
compiler = (root / 'third_party/opengnm-psbc/libpsbc/psbc_compile.c').read_text()

def function(source, name):
    match = re.search(r'(?m)^(?:static enum pipe_format )?' + name + r'\([^;{}]*\)\s*\{', source)
    start = source.index(name, match.start())
    return ' ' + source[start:source.index('\n}', start) + 2]

functions = ('bool' + function(driver, 'ps5_packed_vertex_format') + '\n' +
             'bool' + function(driver, 'ps5_vertex_format') + '\n' +
             'enum pipe_format' + function(compiler, 'psbc_vertex_pipe_format'))
pipe = sorted(set(re.findall(r'PIPE_FORMAT_\w+', functions)))
psbc = sorted(set(re.findall(r'PSBC_VERTEX_FORMAT_\w+', functions)))
flags = sorted(set(re.findall(r'PS5_ENABLE_\w+', functions)))
code = ('#include <assert.h>\n#include <stdbool.h>\n' +
        'enum pipe_format {' + ','.join(pipe) + '};\n' +
        'typedef enum {' + ','.join(psbc) + '} PsbcVertexFormat;\n' +
        '\n'.join('#define ' + flag + ' 1' for flag in flags) + '\n' + functions +
        '\nint main(void) { PsbcVertexFormat out;\n')
for fmt in ('R8G8B8A8_SNORM', 'R16G16_UNORM'):
    code += (f'assert(ps5_packed_vertex_format(PIPE_FORMAT_{fmt}));\n'
             f'assert(ps5_vertex_format(PIPE_FORMAT_{fmt}, &out));\n'
             f'assert(out == PSBC_VERTEX_FORMAT_{fmt});\n'
             f'assert(psbc_vertex_pipe_format(out) == PIPE_FORMAT_{fmt});\n')
code += '}\n'
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / 'normalized-vertex'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-x', 'c', '-',
                    '-o', str(exe)], input=code, text=True, check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: normalized vertex driver admission and compiler format mapping')
