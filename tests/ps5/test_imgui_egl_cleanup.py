#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Exercise the real ImGui harness cleanup with each EGL failure injected."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / "examples/core33-imgui/main.cpp").read_text()
start = source.index("    if (display != EGL_NO_DISPLAY) {", source.index("int main()"))
cleanup = source[start:source.index('    printf("[ps5-imgui] finished', start)]
code = r'''
#include <stdio.h>
using EGLBoolean = unsigned;
constexpr unsigned EGL_TRUE = 1, EGL_SUCCESS = 0x3000;
constexpr int EGL_NO_DISPLAY = 0, EGL_NO_SURFACE = 0, EGL_NO_CONTEXT = 0;
unsigned calls;
int failure;
EGLBoolean step(int id) { calls |= 1u << (id - 1); return failure != id; }
EGLBoolean eglMakeCurrent(int, int, int, int) { return step(1); }
EGLBoolean eglDestroyContext(int, int) { return step(2); }
EGLBoolean eglDestroySurface(int, int) { return step(3); }
EGLBoolean eglTerminate(int) { return step(4); }
unsigned eglGetError() { return failure == 5 ? 0x3001 : EGL_SUCCESS; }
bool check(bool ok, const char*) { return ok; }
int probe(int injected, int result) {
    failure = injected;
    calls = 0;
    int display = 1, context = 1, surface = 1;
    EGLBoolean cleanup = EGL_TRUE;
    (void)cleanup;
''' + cleanup + r'''
    return calls == 15 ? result : 2;
}
int main() {
    for (int failure = 0; failure <= 5; ++failure)
        for (int render_failure = 0; render_failure <= 1; ++render_failure)
            if (probe(failure, render_failure) != (failure != 0 || render_failure)) {
                fprintf(stderr, "cleanup failure=%d render_failure=%d was lost\n", failure, render_failure);
                return 1;
            }
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "imgui-egl-cleanup")
    subprocess.run(["clang++-18", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-x", "c++", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: all four EGL cleanup calls attempted; each failure and prior render failure preserved")
