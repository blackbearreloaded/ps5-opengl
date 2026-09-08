#!/usr/bin/env bash
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Quiet diagnostics must not disable GL validation, including repeat errors.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out="$root/build/diagnostics"
mkdir -p "$out"
clang-18 -std=c11 -Wall -Wextra -Werror \
    -I"$root/third_party/mesa-26.2.0/include" -x c - \
    -l:libEGL.so.1 -l:libGL.so.1 -o "$out/cts-error-logging" <<'C'
#include <assert.h>
#include <EGL/egl.h>
#include <GL/gl.h>
int main(void) {
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLConfig config;
    EGLint n;
    const EGLint attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                           EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    assert(eglInitialize(d, 0, 0) && eglBindAPI(EGL_OPENGL_API));
    assert(eglChooseConfig(d, attrs, &config, 1, &n) && n == 1);
    EGLContext c = eglCreateContext(d, config, EGL_NO_CONTEXT, 0);
    assert(c != EGL_NO_CONTEXT && eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c));
    for (unsigned i = 0; i < 1024; ++i) {
        glEnable(0xffffffffu);
        assert(glGetError() == GL_INVALID_ENUM);
        assert(glGetError() == GL_NO_ERROR);
    }
    assert(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    assert(eglDestroyContext(d, c) && eglTerminate(d));
}
C
export EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1
MESA_DEBUG=1 "$out/cts-error-logging" 2>"$out/cts-errors-verbose.log"
MESA_DEBUG=silent "$out/cts-error-logging" 2>"$out/cts-errors-quiet.log"
grep -q GL_INVALID_ENUM "$out/cts-errors-verbose.log"
test ! -s "$out/cts-errors-quiet.log"
printf 'PASS: 1024 GL errors preserved with no duplicate stderr diagnostics\n'
