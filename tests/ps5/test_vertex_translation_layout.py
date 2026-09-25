#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Translated vertex elements retain dual-slot metadata and aligned strides."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / 'third_party/mesa-26.2.0/src/gallium/auxiliary/util/u_vbuf.c').read_text()
start = source.index('            struct translate_element *te = &key[type].element[elem_index[type][i]];')
block = source[start:source.index('            /* elem_index', start)]
driver = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
assert 'caps->vertex_input_alignment = PIPE_VERTEX_INPUT_ALIGNMENT_ELEMENT;' in driver
assert 'attribute->alignment = ps5_vertex_format_alignment(element->src_format);' in driver
code = r'''
#include <assert.h>
#include <stdbool.h>
enum { VB_VERTEX, VB_INSTANCE, VB_CONST };
struct element { unsigned instance_divisor, src_format, src_offset, vertex_buffer_index, src_stride; bool dual_slot; };
struct translate_element { unsigned output_format, output_offset; };
struct key { struct translate_element element[1]; unsigned output_stride; };
struct source { struct element ve[1]; };
struct manager { struct { struct element velems[1]; } fallback_velems; struct source *ve; unsigned fallback_vbs[3]; };
int main(void) {
  struct source src = {.ve={{.instance_divisor=2,.dual_slot=true}}};
  struct manager storage={.ve=&src,.fallback_vbs={4,5,6}}, *mgr=&storage;
  struct key key[3]={{{{7,16}},32},{{{7,16}},32},{{{7,16}},32}};
  unsigned elem_index[3][1]={{0},{0},{0}}, i=0;
  for (unsigned type=0;type<3;++type) {
''' + block + r'''
    assert(mgr->fallback_velems.velems[0].dual_slot);
    assert(mgr->fallback_velems.velems[0].instance_divisor==2);
    assert(mgr->fallback_velems.velems[0].src_format==7);
    assert(mgr->fallback_velems.velems[0].src_offset==16);
    assert(mgr->fallback_velems.velems[0].src_stride==(type==VB_CONST?0:32));
    assert(mgr->fallback_velems.velems[0].vertex_buffer_index==4+type);
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    exe = str(Path(directory) / 'check')
    subprocess.run(['clang-18', '-x', 'c', '-std=c11', '-Wall', '-Werror', '-o', exe, '-'], input=code, text=True, check=True)
    subprocess.run([exe], check=True)
print('PASS: element alignment cap and translated vertex/instance/constant metadata')
