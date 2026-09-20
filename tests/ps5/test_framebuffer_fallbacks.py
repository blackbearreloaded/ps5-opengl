#!/usr/bin/env python3
"""Exercise the production bounded layout counter, including saturation."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
start = source.index('static void\nps5_record_framebuffer_fallback(')
body = source[start:source.index('\n#endif', start)]
start = source.index('   struct {\n      unsigned key[16];')
fields = source[start:source.index('\n#endif', start)]
code = '''#include <assert.h>
#include <stdint.h>
#include <string.h>
struct ps5_context {
''' + fields + '\n};\n' + body + '''
int main(void) {
    struct ps5_context context = {0};
    unsigned key[16] = {3, 1, 0, 42};
    for (unsigned i = 0; i < 831; ++i)
        ps5_record_framebuffer_fallback(&context, key);
    assert(context.framebuffer_fallbacks[0].count == 831);
    for (unsigned i = 1; i < 32; ++i) {
        key[15] = i;
        ps5_record_framebuffer_fallback(&context, key);
    }
    key[15] = 32;
    ps5_record_framebuffer_fallback(&context, key);
    ps5_record_framebuffer_fallback(&context, key);
    assert(context.framebuffer_fallback_overflow == 2);
    key[15] = 0;
    ps5_record_framebuffer_fallback(&context, key);
    assert(context.framebuffer_fallbacks[0].count == 832);
    for (unsigned i = 1; i < 32; ++i) {
        assert(context.framebuffer_fallbacks[i].count == 1);
        assert(context.framebuffer_fallbacks[i].key[15] == i);
    }
    assert(!memcmp(context.framebuffer_fallbacks[0].key, key, sizeof(key)));
}
'''
with tempfile.TemporaryDirectory(prefix='ps5-fallbacks-') as tmp:
    path = Path(tmp)
    (path / 'check.c').write_text(code)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path / 'check.c'),
                    '-o', str(path / 'check')], check=True)
    subprocess.run([str(path / 'check')], check=True)
print('Framebuffer fallback counts, distinct layouts, saturation and retained counts PASS')
