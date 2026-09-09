#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Sanitize the actual RGBA8/sRGB/R8/RG8/RGBA16F policy and fallback guards."""
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
#include <stdint.h>
#define PS5_MAX_COLOR_WIDTH 8192
#define PS5_MAX_COLOR_HEIGHT 8192
enum { PIPE_TEXTURE_2D=1, PIPE_TEXTURE_RECT, ARRAY, CUBE, VOLUME };
enum { PIPE_FORMAT_R8G8B8A8_UNORM=1, PIPE_FORMAT_R8G8B8A8_SRGB,
       PIPE_FORMAT_R8_UNORM, PIPE_FORMAT_R8G8_UNORM, PIPE_FORMAT_R16G16B16A16_FLOAT,
       OTHER_COLOR, PIPE_FORMAT_Z32_FLOAT, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT, FORMAT_END };
enum { PIPE_BIND_RENDER_TARGET=1, PIPE_BIND_SAMPLER_VIEW=2, PIPE_BIND_DEPTH_STENCIL=4 };
struct pipe_resource { unsigned target, format, nr_samples, nr_storage_samples,
    last_level, width0, height0, bind, array_size, depth0; };
static bool ps5_sampled_texture_format(unsigned f) {
    return f >= 1 && f < FORMAT_END &&
           (f > PIPE_FORMAT_R16G16B16A16_FLOAT || (SAMPLE_MASK & (1u << (f-1))));
}
static bool ps5_render_target_format(unsigned f) {
    /* OTHER_COLOR has both capabilities but must stay outside the whitelist. */
    return f == OTHER_COLOR || (f >= 1 && f <= PIPE_FORMAT_R16G16B16A16_FLOAT &&
                               (RENDER_MASK & (1u << (f-1))));
}
''' + helper + r'''
static void check_guards(unsigned f) {
    struct pipe_resource good={.target=PIPE_TEXTURE_2D,.format=f,
        .width0=128,.height0=128,.depth0=1,.array_size=1,
        .bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW};
    const bool sampled=!!(SAMPLE_MASK & (1u << (f-1)));
    const bool native=PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE &&
                      PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE &&
                      sampled && (RENDER_MASK & (1u << (f-1)));
    assert(ps5_linear_sampled_layout(&good) == (sampled && !native));
    for (unsigned samples=0;samples<=1;++samples)
        for (unsigned storage=0;storage<=1;++storage) {
            struct pipe_resource r=good;
            r.nr_samples=samples; r.nr_storage_samples=storage;
            assert(ps5_linear_sampled_layout(&r) == (sampled && !native));
        }
    /* Ineligible native geometry keeps the existing sampled-layout decision;
     * a true result does not promise an invalid resource can be allocated. */
#define FALLBACK(field,value) do { struct pipe_resource r=good; r.field=value; \
    assert(ps5_linear_sampled_layout(&r) == sampled); } while (0)
    FALLBACK(target,PIPE_TEXTURE_RECT); FALLBACK(target,ARRAY);
    FALLBACK(target,CUBE); FALLBACK(target,VOLUME);
    FALLBACK(array_size,0); FALLBACK(array_size,2);
    FALLBACK(depth0,0); FALLBACK(depth0,2);
    FALLBACK(last_level,1); FALLBACK(last_level,15);
    FALLBACK(width0,0); FALLBACK(height0,0);
    FALLBACK(width0,PS5_MAX_COLOR_WIDTH+1); FALLBACK(height0,PS5_MAX_COLOR_HEIGHT+1);
    FALLBACK(width0,UINT32_MAX); FALLBACK(height0,UINT32_MAX);
    FALLBACK(nr_storage_samples,4); FALLBACK(bind,PIPE_BIND_SAMPLER_VIEW);
#undef FALLBACK
    struct pipe_resource r=good;
    r.width0=PS5_MAX_COLOR_WIDTH; r.height0=PS5_MAX_COLOR_HEIGHT;
    assert(ps5_linear_sampled_layout(&r) == (sampled && !native));
    r=good; r.nr_samples=4;
    assert(!ps5_linear_sampled_layout(&r)); /* Existing MSAA rejection. */
    r=good; r.bind=PIPE_BIND_RENDER_TARGET;
    assert(!ps5_linear_sampled_layout(&r)); /* Existing render-only layout. */
    r=good; r.format=OTHER_COLOR;
    assert(ps5_linear_sampled_layout(&r));
}
int main(void) {
    assert(!ps5_linear_sampled_layout(NULL));
    for (unsigned f=1; f<=PIPE_FORMAT_R16G16B16A16_FLOAT; ++f) check_guards(f);
    const unsigned sizes[] = {0, 1, 2, 63, 128, 1920, 3840, 8192, 8193, UINT32_MAX};
    for (unsigned t=1; t<=5; ++t) for (unsigned f=1; f<FORMAT_END; ++f)
    for (unsigned bind=0; bind<8; ++bind) for (unsigned mip=0; mip<=1; ++mip)
    for (unsigned samples=1; samples<=4; samples*=4)
    for (unsigned x=0; x<sizeof(sizes)/sizeof(sizes[0]); ++x)
    for (unsigned y=0; y<sizeof(sizes)/sizeof(sizes[0]); ++y) {
        struct pipe_resource r={.target=t,.format=f,.nr_samples=samples,.nr_storage_samples=samples,
            .last_level=mip,.width0=sizes[x],.height0=sizes[y],.bind=bind,.array_size=1,.depth0=1};
        bool expected = ps5_sampled_texture_format(f) && samples == 1 && (!(bind & PIPE_BIND_DEPTH_STENCIL) ||
            t == PIPE_TEXTURE_RECT || (f >= PIPE_FORMAT_Z32_FLOAT && mip)) &&
            (t != PIPE_TEXTURE_2D || !(bind & PIPE_BIND_RENDER_TARGET) ||
             (bind & PIPE_BIND_SAMPLER_VIEW) || mip);
#if PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE && PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE
        if (t == PIPE_TEXTURE_2D &&
            f <= PIPE_FORMAT_R16G16B16A16_FLOAT &&
            (SAMPLE_MASK & RENDER_MASK & (1u << (f-1))) &&
            samples == 1 && !mip && sizes[x] && sizes[y] && sizes[x] <= 8192 && sizes[y] <= 8192 &&
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
    # Five color capability bits; independently disable sampling/rendering for
    # the sRGB, RG and float gates, plus each global native-layout feature.
    configurations = [(0, 0, 31, 31), (1, 0, 31, 31), (0, 1, 31, 31), (1, 1, 31, 31)]
    for gate in (2, 12, 16):
        configurations.extend(((1, 1, 31 ^ gate, 31), (1, 1, 31, 31 ^ gate),
                               (1, 1, 31 ^ gate, 31 ^ gate)))
    for render, dynamic, sample_mask, render_mask in configurations:
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-g', '-fno-pie', '-no-pie',
                        f'-DSAMPLE_MASK={sample_mask}', f'-DRENDER_MASK={render_mask}',
                        f'-DPS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE={render}',
                        f'-DPS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE={dynamic}',
                        str(test), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print('PASS: native RGBA8/sRGB/R8/RG8/RGBA16F layout bounds, targets, binds, mips, '
      'layers/depth, samples and 13 feature/capability gate configurations')
