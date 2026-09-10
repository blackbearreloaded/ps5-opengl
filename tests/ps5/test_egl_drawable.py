#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check the real EGL callback against Mesa's scalar resolve-output contract."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/egl/ps5_egl.c").read_text()
begin = source.index("static bool\nps5_validate_drawable(")
callback = source[begin:source.index("static bool\nps5_flush_swapbuffers(", begin)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
struct st_context { int unused; };
struct pipe_frontend_drawable { int unused; };
struct pipe_resource { int unused; };
enum st_attachment_type { ST_ATTACHMENT_FRONT_LEFT, ST_ATTACHMENT_DEPTH_STENCIL, OTHER };
struct ps5_egl_surface {
    struct pipe_frontend_drawable drawable;
    struct pipe_resource *targets[2], *depth_stencil;
    unsigned buffer_index;
};
static void pipe_resource_reference(struct pipe_resource **out, struct pipe_resource *value) {
    *out = value;
}
''' + callback + r'''
int main(void) {
    struct pipe_resource color[2], depth;
    struct ps5_egl_surface surface = {.targets={&color[0], &color[1]}, .depth_stencil=&depth};
    enum st_attachment_type attachments[] = {ST_ATTACHMENT_FRONT_LEFT, ST_ATTACHMENT_DEPTH_STENCIL};
    struct pipe_resource *outputs[2] = {NULL, NULL};
    struct { struct pipe_resource *resolve, *guard; } result = {&color[0], &depth};
    for (unsigned slot = 0; slot < 2; ++slot) {
        surface.buffer_index = slot;
        for (unsigned count = 0; count <= 2; ++count) {
            result.resolve = &color[0];
            assert(ps5_validate_drawable(NULL, &surface.drawable, attachments, count, outputs, &result.resolve));
            assert(result.resolve == NULL && result.guard == &depth);
            if (count) assert(outputs[0] == &color[slot]);
            if (count > 1) assert(outputs[1] == &depth);
        }
    }
    assert(ps5_validate_drawable(NULL, &surface.drawable, attachments, 2, outputs, NULL));
    enum st_attachment_type unsupported = OTHER;
    assert(!ps5_validate_drawable(NULL, &surface.drawable, &unsupported, 1, outputs, &result.resolve));
    assert(result.guard == &depth);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / "check.c").write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2",
                    str(path / "check.c"), "-o", str(path / "check")], check=True)
    subprocess.run([str(path / "check")], check=True)
print("EGL drawable: PASS scalar resolve output, attachment routing and both slots")
