#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Sanitize the actual RGBA8/sRGB layout policy, including disabled features."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
start = source.index('static bool\nps5_linear_sampled_layout(')
helper = source[start:source.index('\nstatic bool\nps5_color_render_target(', start)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define PS5_MAX_COLOR_WIDTH 8192
#define PS5_MAX_COLOR_HEIGHT 8192
enum { PIPE_TEXTURE_2D=1, PIPE_TEXTURE_RECT, ARRAY, CUBE, VOLUME };
enum { PIPE_FORMAT_R8G8B8A8_UNORM=1, PIPE_FORMAT_R8G8B8A8_SRGB, OTHER_COLOR,
       PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT };
enum { PIPE_BIND_RENDER_TARGET=1, PIPE_BIND_SAMPLER_VIEW=2, PIPE_BIND_DEPTH_STENCIL=4 };
struct pipe_resource { unsigned target, format, nr_samples, nr_storage_samples,
    last_level, width0, height0, bind; };
static bool ps5_sampled_texture_format(unsigned f) {
    return f >= 1 && f <= 5 && (f != PIPE_FORMAT_R8G8B8A8_SRGB || SRGB_SAMPLE);
}
static bool ps5_render_target_format(unsigned f) {
    return f == PIPE_FORMAT_R8G8B8A8_UNORM ||
           (f == PIPE_FORMAT_R8G8B8A8_SRGB && SRGB_RENDER);
}
''' + helper + r'''
int main(void) {
    assert(!ps5_linear_sampled_layout(NULL));
    const unsigned sizes[] = {1, 2, 63, 128, 1920, 3840, 8192, 8193};
    for (unsigned t=1; t<=5; ++t) for (unsigned f=1; f<=5; ++f)
    for (unsigned bind=0; bind<8; ++bind) for (unsigned mip=0; mip<=1; ++mip)
    for (unsigned samples=1; samples<=4; samples*=4)
    for (unsigned x=0; x<8; ++x) for (unsigned y=0; y<8; ++y) {
        struct pipe_resource r={t,f,samples,samples,mip,sizes[x],sizes[y],bind};
        bool expected = ps5_sampled_texture_format(f) && samples == 1 && (!(bind & PIPE_BIND_DEPTH_STENCIL) ||
            t == PIPE_TEXTURE_RECT || (f >= PIPE_FORMAT_Z32_FLOAT && mip)) &&
            (t != PIPE_TEXTURE_2D || !(bind & PIPE_BIND_RENDER_TARGET) ||
             (bind & PIPE_BIND_SAMPLER_VIEW) || mip);
#if PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE && PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE
        if (t == PIPE_TEXTURE_2D &&
            (f == PIPE_FORMAT_R8G8B8A8_UNORM ||
             (f == PIPE_FORMAT_R8G8B8A8_SRGB && SRGB_SAMPLE && SRGB_RENDER)) &&
            samples == 1 && !mip && sizes[x] <= 8192 && sizes[y] <= 8192 &&
            (bind & 3) == 3)
            expected = false;
#endif
        assert(ps5_linear_sampled_layout(&r) == expected);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    test, exe = Path(temp) / 'layout.c', Path(temp) / 'layout'
    test.write_text(code)
    for render, dynamic, sample, srgb_render in (
            (0, 0, 1, 1), (1, 0, 1, 1), (0, 1, 1, 1),
            (1, 1, 0, 0), (1, 1, 1, 0), (1, 1, 0, 1), (1, 1, 1, 1)):
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-g', '-fno-pie', '-no-pie',
                        f'-DSRGB_SAMPLE={sample}', f'-DSRGB_RENDER={srgb_render}',
                        f'-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE={render}',
                        f'-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE={dynamic}',
                        str(test), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('PASS: native RGBA8/sRGB layout bounds, targets, binds, mips, samples and disabled feature paths')
