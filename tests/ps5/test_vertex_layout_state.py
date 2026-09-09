#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual shader-input/vertex-element matcher; no GPU required."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
start = source.rindex('static bool\nps5_vertex_layout_from_state(')
function = source[start:source.index('\n}', start) + 2]
assert source.count('element_index < vertex_layout.count;') == 2
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define VERT_ATTRIB_GENERIC0 16
#define PSBC_MAX_VERTEX_ATTRIBUTES 16
#define BITFIELD64_BIT(n) (UINT64_C(1) << (n))
typedef struct { unsigned location,binding,format,stride,alignment,offset,instance_divisor; } PsbcVertexAttribute;
struct pipe_vertex_element { unsigned dual_slot,src_format,vertex_buffer_index,src_offset,src_stride,instance_divisor; };
struct ps5_vertex_elements { unsigned count; struct pipe_vertex_element elements[16]; };
struct ps5_vertex_layout { unsigned count; PsbcVertexAttribute attributes[16]; };
struct nir { struct { uint64_t inputs_read; } info; };
struct ps5_shader { struct nir *nir; };
static bool ps5_vertex_format(unsigned f,unsigned *out) { *out=f; return f==1; }
''' + function + r'''
int main(void) {
    struct nir n={.info.inputs_read=BITFIELD64_BIT(16)};
    struct ps5_shader s={&n}; struct ps5_vertex_layout out;
    struct ps5_vertex_elements e={.count=2,.elements={{0,1,3,8,32,2},{1,99,9,0,0,0}}};
    assert(ps5_vertex_layout_from_state(&s,&e,&out));
    assert(out.count==1 && out.attributes[0].binding==3 && out.attributes[0].offset==8);
    assert(out.attributes[0].stride==32 && out.attributes[0].instance_divisor==2);
    n.info.inputs_read|=BITFIELD64_BIT(17);
    assert(!ps5_vertex_layout_from_state(&s,&e,&out));
    e.elements[1]=(struct pipe_vertex_element){0,1,4,16,32,0};
    assert(ps5_vertex_layout_from_state(&s,&e,&out) && out.count==2);
    e.count=1; assert(!ps5_vertex_layout_from_state(&s,&e,&out));
    assert(!ps5_vertex_layout_from_state(&s,0,&out));
    n.info.inputs_read=0;
    assert(ps5_vertex_layout_from_state(&s,&e,&out) && out.count==0);
    assert(ps5_vertex_layout_from_state(&s,0,&out) && out.count==0);
}
'''
with tempfile.TemporaryDirectory(prefix='ps5-vertex-layout-') as tmp:
    binary = str(Path(tmp) / 'test')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O1',
                    '-fsanitize=address,undefined','-fno-sanitize-recover=all',
                    '-fno-pie','-no-pie','-x','c','-o',binary,'-'],
                   input=code,text=True,check=True,timeout=30)
    subprocess.run([binary],check=True,timeout=30)
print('PASS: consumed vertex elements checked, unused trailing elements ignored')
