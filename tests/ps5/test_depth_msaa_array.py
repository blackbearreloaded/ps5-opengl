#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""CPU-only actual capability predicate and descriptor expressions, not GPU proof."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
start = source.index('static bool\nps5_msaa4_depth_support(')
predicate = source[start:source.index('\n}\n', start) + 3]
start = source.index('      descriptor[3] = (tiled_depth_target && multisampled')
descriptor = source[start:source.index('      if (texture->base.target == PIPE_TEXTURE_2D &&', start)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
enum pipe_texture_target { PIPE_TEXTURE_2D, PIPE_TEXTURE_2D_ARRAY, PIPE_TEXTURE_1D,
    PIPE_TEXTURE_1D_ARRAY, PIPE_TEXTURE_CUBE, PIPE_TEXTURE_3D };
enum pipe_format { PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT, COLOR };
#define PIPE_BIND_DEPTH_STENCIL 1
#define PIPE_BIND_SAMPLER_VIEW 2
static unsigned PS5_ENABLE_MSAA4_CANDIDATE,PS5_ENABLE_MSAA_ARRAY_CANDIDATE,
    PS5_ENABLE_TEXTURE_ARRAY_CANDIDATE,PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE,
    PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE,PS5_ENABLE_PACKED_DEPTH_STENCIL;
''' + predicate + r'''
static void check_descriptor(unsigned target,unsigned first,unsigned last,unsigned format) {
    struct { struct { unsigned target,depth0,format; } base; } storage={{target,1,format}}, *texture=&storage;
    struct { union { struct { unsigned first_level,last_level,first_layer,last_layer; } tex; } u; }
        view_storage={.u.tex={0,0,first,last}},*view=&view_storage;
    bool tiled_depth_target=true,multisampled=true,tiled_render_target=false;
    uint32_t descriptor[8]={0},swizzle[4]={0,0,0,5};
''' + descriptor + r'''
    assert((descriptor[3] >> 28)==(target==PIPE_TEXTURE_2D_ARRAY ? 15u : 14u));
    assert((descriptor[3] & 0x0ffff000u)==0x01820000u);
    assert((descriptor[3] & 0xfffu)==(5u<<9));
    assert(descriptor[4]==(last | (first<<16)));
}
int main(void) {
    const unsigned samples[]={0,1,2,4,8};
    for (unsigned flags=0;flags<64;++flags) {
        PS5_ENABLE_MSAA4_CANDIDATE=flags&1;
        PS5_ENABLE_MSAA_ARRAY_CANDIDATE=flags&2;
        PS5_ENABLE_TEXTURE_ARRAY_CANDIDATE=flags&4;
        PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE=flags&8;
        PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE=flags&16;
        PS5_ENABLE_PACKED_DEPTH_STENCIL=flags&32;
        for (unsigned target=0;target<6;++target) for (unsigned format=0;format<3;++format)
        for (unsigned i=0;i<5;++i) for (unsigned j=0;j<5;++j) for (unsigned bindings=0;bindings<8;++bindings) {
            unsigned allowed=(flags&16) ? 3u : 1u;
            bool expected=(flags&1) && (target==PIPE_TEXTURE_2D ||
                (target==PIPE_TEXTURE_2D_ARRAY && (flags&30)==30)) &&
                samples[i]==4 && samples[j]==4 && bindings && !(bindings & ~allowed) &&
                (format==PIPE_FORMAT_Z32_FLOAT || (format==PIPE_FORMAT_Z32_FLOAT_S8X24_UINT && (flags&32)));
            assert(ps5_msaa4_depth_support(format,target,samples[i],samples[j],bindings)==expected);
        }
    }
    for (unsigned format=0;format<2;++format) {
        check_descriptor(PIPE_TEXTURE_2D,0,0,format);
        for (unsigned first=0;first<4;++first) for (unsigned last=first;last<4;++last)
            check_descriptor(PIPE_TEXTURE_2D_ARRAY,first,last,format);
    }
}
'''
old_predicate = code.replace('PS5_ENABLE_MSAA_ARRAY_CANDIDATE &&', 'false &&', 1)
old_descriptor = code.replace('UINT32_C(0xf1820000)', 'UINT32_C(0xe1820000)')
assert old_predicate != code and old_descriptor != code
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / 'depth-msaa-array')
    for label, text in (('fixed', code), ('old-capability', old_predicate), ('old-descriptor', old_descriptor)):
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-no-pie', '-x', 'c', '-',
                        '-o', executable], input=text, text=True, check=True)
        result = subprocess.run([executable], cwd=directory, text=True, capture_output=True)
        if label == 'fixed':
            assert result.returncode == 0, result.stderr
        else:
            assert result.returncode != 0 and 'Assertion' in result.stderr, (label, result.stderr)
print('PASS: actual MSAA depth predicate, all candidate prerequisites/bindings/sample counts; '
      '2D/array descriptor type/sample/layer fields; deliberate regressions rejected')
