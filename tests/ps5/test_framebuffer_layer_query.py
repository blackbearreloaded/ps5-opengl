#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Execute Mesa's actual attachment-layer query arm, not a GPU/GL context.

GL 3.3 core section 6.1.13, printed page 277, requires the selected array layer:
https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
mesa = root / 'third_party/mesa-26.2.0'
source = (mesa / 'src/mesa/main/fbobject.c').read_text()
start = source.index('   case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT:')
arm = source[start:source.index('   case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:', start)]
textures = (mesa / 'src/mesa/main/teximage.c').read_text()
start = textures.index('bool\n_mesa_is_array_texture(')
helper = textures[start:textures.index('\n}\n', start) + 3]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <GL/gl.h>
#include <GL/glext.h>
static GLenum last_error;
static bool _mesa_is_gles1(const bool *ctx) { return *ctx; }
static const char *_mesa_enum_to_string(GLenum value) { (void)value; return "enum"; }
static void _mesa_error(const bool *ctx,GLenum error,const char *fmt,...) {
    (void)ctx; (void)fmt; assert(!last_error); last_error=error;
}
struct texture { GLenum Target; };
struct attachment { GLenum Type; struct texture *Texture; GLint Zoffset; };
''' + helper + r'''
static void query(const bool *ctx,const struct attachment *att,GLint *params) {
    const char *caller="glGetFramebufferAttachmentParameteriv";
    GLenum pname=GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER,err=GL_INVALID_OPERATION;
    switch (pname) {
''' + arm + r'''
    default: assert(0);
    }
invalid_pname_enum:
    _mesa_error(ctx,GL_INVALID_ENUM,"invalid");
}
static void check(struct attachment *att,bool es1,GLint expected,GLenum error) {
    GLint value=-123; last_error=0; query(&es1,att,&value);
    assert(value==expected && last_error==error);
}
int main(void) {
    struct texture texture={0}; struct attachment att={GL_TEXTURE,&texture,0};
    const GLenum layered[]={GL_TEXTURE_3D,GL_TEXTURE_1D_ARRAY,GL_TEXTURE_2D_ARRAY,
        GL_TEXTURE_2D_MULTISAMPLE_ARRAY,GL_TEXTURE_CUBE_MAP_ARRAY};
    const GLenum plain[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_CUBE_MAP,
        GL_TEXTURE_RECTANGLE,GL_TEXTURE_2D_MULTISAMPLE};
    for (unsigned layer=0;layer<32;++layer) {
        att.Zoffset=(GLint)layer;
        for (unsigned i=0;i<sizeof(layered)/sizeof(layered[0]);++i) {
            texture.Target=layered[i]; check(&att,false,(GLint)layer,GL_NO_ERROR);
        }
        for (unsigned i=0;i<sizeof(plain)/sizeof(plain[0]);++i) {
            texture.Target=plain[i]; check(&att,false,0,GL_NO_ERROR);
        }
    }
    att.Texture=NULL; check(&att,false,0,GL_NO_ERROR);
    att.Type=GL_NONE; check(&att,false,-123,GL_INVALID_OPERATION);
    att.Type=GL_RENDERBUFFER; check(&att,false,-123,GL_INVALID_ENUM);
    att.Type=GL_TEXTURE; att.Texture=&texture; check(&att,true,-123,GL_INVALID_ENUM);
}
'''
old = code.replace('_mesa_is_array_texture(att->Texture->Target)',
                   'att->Texture->Target == GL_TEXTURE_2D_ARRAY')
assert old != code, 'the query patch is missing'
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / 'query')
    for label, candidate in (('patched', code), ('original-bug', old)):
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(mesa / 'include'), '-x', 'c', '-o', executable, '-'],
                       input=candidate, text=True, check=True)
        result = subprocess.run([executable], cwd=directory, text=True, capture_output=True)
        if label == 'patched':
            assert result.returncode == 0, result.stderr
        else:
            assert result.returncode != 0 and 'Assertion' in result.stderr, result.stderr
print('PASS: actual Mesa array-layer query arm, 32 layers, plain/null/error controls; original bug rejected')
