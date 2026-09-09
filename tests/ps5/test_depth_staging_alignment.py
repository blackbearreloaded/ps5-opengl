#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""CPU allocation arithmetic versus actual native depth-pointer validators."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
start = source.index('   } else if (depth_staging) {')
block = source[start:source.index('   } else if ((ps5_depth_render_target', start)]
block = block[block.index('{') + 1:]
native = (root / 'src/platform/ps5_agc_native_runtime.c').read_text()


def function(name):
    start = native.index('int ' + name + '(')
    return native[start:native.index('\n}\n', start) + 3]


code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define PS5_RENDER_ALIGNMENT 0x200000u
#define PS5_COLOR_TARGET_ALIGNMENT 0x10000u
#define PIPE_FORMAT_Z32_FLOAT_S8X24_UINT 1
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
struct pipe_resource { unsigned width0,height0,nr_samples,format,array_size; };
static unsigned ps5_texture_level_layers(const struct pipe_resource *t,unsigned level) {
    assert(!level); return t->array_size;
}
static size_t ps5_tiled_depth_surface_size(unsigned w,unsigned h,unsigned samples) {
    assert(w && h && samples==1); return ((size_t)(w+127)/128)*((h+127)/128)*65536;
}
static bool ps5_packed_depth_sample_layout(const struct pipe_resource *t,size_t *size,unsigned *stride) {
    *size=(size_t)t->width0*t->height0*4; *stride=t->width0*4; return true;
}
struct allocation { size_t offset,staging,total,alignment; };
static struct allocation *allocate(const struct pipe_resource *templ,size_t size) {
    struct allocation *resource=malloc(sizeof(*resource)); assert(resource);
    size_t depth_staging_size=0,depth_staging_offset=0,allocation_size=0,allocation_alignment=0;
''' + block + r'''
    *resource=(struct allocation){depth_staging_offset,depth_staging_size,allocation_size,allocation_alignment};
    return resource;
}
static void *runtime_depth_buffer,*runtime_stencil_buffer;
static size_t runtime_depth_buffer_size,runtime_stencil_buffer_size;
static uint32_t runtime_depth_control,runtime_depth_view,runtime_stencil_control;
static uint32_t runtime_stencil_refmask,runtime_stencil_refmask_bf;
''' + function('ps5_agc_gate2_set_depth_buffer') + function('ps5_agc_gate2_set_depth_stencil_buffer') + r'''
int main(void) {
    const size_t sizes[]={256,1344,21504,65536,0x1fffff,0x200000,0x200001};
    for (unsigned packed=0;packed<2;++packed) for (unsigned layers=1;layers<=4;++layers) {
        struct pipe_resource templ={64,64,1,packed,layers};
        for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
            struct allocation *a=allocate(&templ,sizes[i]); assert(a);
            assert(a->offset>=sizes[i] && a->offset-sizes[i]<0x200000);
            assert(!(a->offset%0x200000) && a->alignment==0x200000 && !(a->total%0x200000));
            assert(a->staging==layers*65536 && a->total>=a->offset+a->staging);
            /* No pointer is dereferenced; the wrapper's established size floor
             * is independent of this test of its unchanged alignment contract. */
            void *depth=(void *)(uintptr_t)(0x400000+a->offset);
            if (packed)
                assert(!ps5_agc_gate2_set_depth_stencil_buffer(depth,0xa00000,(void *)0x1000000,0x280000,1,0,0,0));
            else
                assert(!ps5_agc_gate2_set_depth_buffer(depth,0xa00000,1));
            free(a);
        }
        assert(!allocate(&templ,SIZE_MAX));
        assert(!allocate(&templ,SIZE_MAX-0x1fffff));
    }
    assert(ps5_agc_gate2_set_depth_buffer((void *)0x410000,0xa00000,1)==-1);
    assert(ps5_agc_gate2_set_depth_stencil_buffer((void *)0x410000,0xa00000,(void *)0x1000000,0x280000,1,0,0,0)==-1);
}
'''
old = code.replace('(size + PS5_RENDER_ALIGNMENT - 1u) &\n         ~(size_t)(PS5_RENDER_ALIGNMENT - 1u);',
                   '(size + PS5_COLOR_TARGET_ALIGNMENT - 1u) &\n         ~(size_t)(PS5_COLOR_TARGET_ALIGNMENT - 1u);')
assert old != code
with tempfile.TemporaryDirectory() as directory:
    executable = str(Path(directory) / 'depth-alignment')
    for label, text in (('fixed', code), ('old-color-alignment', old)):
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-no-pie', '-x', 'c', '-',
                        '-o', executable], input=text, text=True, check=True)
        result = subprocess.run([executable], cwd=directory, text=True, capture_output=True)
        if label == 'fixed':
            assert result.returncode == 0, result.stderr
        else:
            assert result.returncode != 0 and 'Assertion' in result.stderr, result.stderr
print('PASS: actual depth staging allocation and native pointer validators; '
      'D32/D32S8, layers, rounding/overflow, unchanged 2MiB checks; old alignment rejected')
