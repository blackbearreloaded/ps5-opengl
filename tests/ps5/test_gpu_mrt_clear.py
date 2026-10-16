#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile production MRT clear control flow with an instrumented blitter.

This checks admission, selection, state restoration and failure propagation.
The blitter is a test double: this does not qualify GPU pixels or performance.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = Path(os.environ.get("PS5_CLEAR_SOURCE", root / "src/gallium/ps5/ps5_screen.c")).read_text()
start = source.index("static bool\nps5_clear_gpu_color(")
body = source[start:source.index("static bool\nps5_clear_gpu_depth_stencil(", start)]
formats = ["R8G8B8A8_UNORM", "R8G8B8A8_SNORM", "B8G8R8A8_UNORM", "R8_UNORM",
           "R8G8_UNORM", "R16G16B16A16_FLOAT", "R11G11B10_FLOAT", "R8G8B8A8_SRGB",
           "R10G10B10A2_UNORM", "R16_FLOAT", "R32_FLOAT", "R16G16_FLOAT",
           "R32G32B32A32_UINT"]
code = "\n".join(f"#define PIPE_FORMAT_{f} {i + 1}" for i, f in enumerate(formats)) + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PS5_ENABLE_MRT_CANDIDATE 1
#define PS5_ENABLE_UBO_CANDIDATE 1
#define PS5_ENABLE_MSAA4_CANDIDATE 1
#define PS5_MAX_CONSTANT_BUFFER_SIZE 65536
#define PS5_GPU_CLEAR_MIN_PIXELS 16384u
#define PIPE_CLEAR_COLOR 0x3fcu
#define PIPE_CLEAR_COLOR0 4u
#define PIPE_MASK_RGBA 15u
#define PIPE_MAX_COLOR_BUFS 8
#define PIPE_TEXTURE_2D 2
#define PIPE_TEXTURE_2D_ARRAY 7
union pipe_color_union { uint32_t ui[4]; float f[4]; };
struct pipe_scissor_state { unsigned minx, miny, maxx, maxy; };
struct resource { unsigned target, nr_samples, nr_storage_samples, format, array_size; };
struct ps5_resource { struct resource base; unsigned render_staging_size; uint8_t *data; size_t size; };
struct pipe_surface { struct ps5_resource *texture; unsigned level, first_layer, last_layer, format; };
struct framebuffer { unsigned width, height, nr_cbufs; struct pipe_surface cbufs[8]; };
struct ps5_constant_state { struct resource *buffer; unsigned offset, size; bool valid, copied; };
struct pipe_constant_buffer { struct resource *buffer; unsigned buffer_offset, buffer_size; const void *user_buffer; };
struct blitter_context { bool running, saved; struct framebuffer fb; };
struct ps5_context {
    struct framebuffer framebuffer;
    bool framebuffer_valid, queries_enabled, deferred_blitter_draw;
    unsigned render_condition_query, stream_output_target_count, draw_calls;
    uint16_t viewport_valid;
    int last_draw_status;
    struct ps5_constant_state constants[2][1]; struct ps5_resource *descriptor_storage[2];
    struct blitter_context *blitter; unsigned base;
};
static struct ps5_context *active;
static struct blitter_context blitter;
static unsigned calls, fail_at, skip_at;
struct operation { struct ps5_resource *target; unsigned layer, x, y, w, h; uint32_t value; };
static struct operation operations[32];
static bool ps5_linear_sampled_layout(const struct resource *r) { (void)r; return false; }
static bool ps5_msaa4_color_format(unsigned f) { return f == PIPE_FORMAT_R8G8B8A8_UNORM; }
static unsigned ps5_linear_color_pitch(const struct pipe_surface *s) { (void)s; return 512; }
static unsigned ps5_surface_width(const struct pipe_surface *s) { (void)s; return 128; }
static unsigned ps5_surface_height(const struct pipe_surface *s) { (void)s; return 128; }
static unsigned ps5_surface_layer_count(const struct pipe_surface *s) { return s->texture->base.array_size; }
static size_t ps5_copied_constant_offset(unsigned slot) { assert(slot == 1); return 16; }
static void ps5_clear_bounds(const struct pipe_scissor_state *s, unsigned w, unsigned h,
    unsigned *x0, unsigned *y0, unsigned *x1, unsigned *y1) {
    *x0=s ? s->minx : 0; *y0=s ? s->miny : 0;
    *x1=s ? s->maxx : w; *y1=s ? s->maxy : h;
}
static struct blitter_context *util_blitter_create(unsigned *base) { (void)base; return &blitter; }
#define util_blitter_save_vertex_buffers(...) ((void)0)
#define util_blitter_save_vertex_elements(...) ((void)0)
#define util_blitter_save_vertex_shader(...) ((void)0)
#define util_blitter_save_tessctrl_shader(...) ((void)0)
#define util_blitter_save_tesseval_shader(...) ((void)0)
#define util_blitter_save_geometry_shader(...) ((void)0)
#define util_blitter_save_so_targets(...) ((void)0)
#define util_blitter_save_rasterizer(...) ((void)0)
#define util_blitter_save_fragment_shader(...) ((void)0)
#define util_blitter_save_depth_stencil_alpha(...) ((void)0)
#define util_blitter_save_blend(...) ((void)0)
#define util_blitter_save_stencil_ref(...) ((void)0)
#define util_blitter_save_viewport(...) ((void)0)
#define util_blitter_save_sample_mask(...) ((void)0)
static void util_blitter_save_fragment_constant_buffer_slot(struct blitter_context *b,
    const struct pipe_constant_buffer *cb) {
    (void)b;
    if (active->constants[1][0].copied) {
        assert(cb->buffer_size == 32 && cb->user_buffer);
        for (unsigned i=0; i<32; ++i) assert(((const uint8_t *)cb->user_buffer)[i] == 0xa5);
    }
}
static void util_blitter_save_framebuffer(struct blitter_context *b, const struct framebuffer *fb) {
    assert(!b->saved); b->fb=*fb; b->saved=true;
}
static void util_format_pack_rgba(unsigned f, void *out, const void *in, unsigned count) {
    (void)f; assert(count == 1); memcpy(out,in,16);
}
static void util_format_unpack_rgba(unsigned f, void *out, const void *in, unsigned count) {
    util_format_pack_rgba(f,out,in,count);
}
static void record_clear(struct blitter_context *b, const struct pipe_surface *s,
    const union pipe_color_union *color, unsigned x, unsigned y, unsigned w, unsigned h) {
    assert(active->deferred_blitter_draw && calls < 32);
    assert(s->first_layer == s->last_layer);
    operations[calls++] = (struct operation){s->texture,s->first_layer,x,y,w,h,color->ui[0]};
    if (calls != skip_at) ++active->draw_calls;
    active->last_draw_status = calls == fail_at ? -17 : 0;
    active->framebuffer_valid=false; active->viewport_valid=0; active->queries_enabled=false;
    active->framebuffer.nr_cbufs=1;
    active->framebuffer.cbufs[0]=*s;
    if (b->saved) { active->framebuffer=b->fb; b->saved=false; }
}
static void util_blitter_clear_render_target(struct blitter_context *b, struct pipe_surface *s,
    const union pipe_color_union *color, unsigned x, unsigned y, unsigned w, unsigned h) {
    assert(b->saved); record_clear(b,s,color,x,y,w,h);
}
static void util_blitter_clear(struct blitter_context *b, unsigned w, unsigned h, unsigned layers,
    unsigned mask, const union pipe_color_union *color, double depth, unsigned stencil, bool msaa) {
    (void)depth; (void)stencil; (void)msaa;
    assert(layers==1 && mask==PIPE_CLEAR_COLOR0);
    record_clear(b,&active->framebuffer.cbufs[0],color,0,0,w,h);
}
''' + body + r'''
int main(void) {
    struct ps5_resource resources[8] = {0};
    struct ps5_context c = {.framebuffer={.width=128,.height=128,.nr_cbufs=3},
        .framebuffer_valid=true,.viewport_valid=0x13,.queries_enabled=true,.draw_calls=10};
    active=&c;
    for (unsigned i=0;i<3;++i) {
        resources[i].base=(struct resource){.target=PIPE_TEXTURE_2D_ARRAY,
            .format=PIPE_FORMAT_R8G8B8A8_UNORM,.array_size=4};
        c.framebuffer.cbufs[i]=(struct pipe_surface){.texture=&resources[i],
            .format=PIPE_FORMAT_R8G8B8A8_UNORM,.first_layer=0,.last_layer=3};
    }
    /* Two nonadjacent MRT attachments, including first/last physical layers. */
    union pipe_color_union color={{0x12345678,0,0,0}};
    struct framebuffer original=c.framebuffer;
    assert(ps5_clear_gpu_color(&c, 4|16, 0xf0f, NULL, &color));
    assert(calls==8 && !memcmp(&c.framebuffer,&original,sizeof(original)));
    for (unsigned i=0;i<8;++i) {
        assert(operations[i].target==&resources[i/4*2] && operations[i].layer==i%4);
        assert(operations[i].value==color.ui[0] && operations[i].w==128);
    }
    assert(c.framebuffer_valid && c.viewport_valid==0x13 && c.queries_enabled);
    assert(!c.deferred_blitter_draw && !blitter.saved);
    calls=0;
    /* Reject the entire operation before drawing if a later target is ineligible. */
    resources[2].base.target=99;
    assert(!ps5_clear_gpu_color(&c,4|16,0xf0f,NULL,&color) && calls==0);
    resources[2].base.target=PIPE_TEXTURE_2D_ARRAY;
    assert(!ps5_clear_gpu_color(&c,4|16,0x70f,NULL,&color) && calls==0);
    assert(!ps5_clear_gpu_color(&c,4|32,0xffff,NULL,&color) && calls==0);
    /* An unused, invalid attachment and its partial mask do not block selected targets. */
    c.framebuffer.cbufs[1].texture=NULL;
    struct pipe_scissor_state scissor={2,3,7,11};
    c.framebuffer.cbufs[2].first_layer=c.framebuffer.cbufs[2].last_layer=3;
    assert(ps5_clear_gpu_color(&c,16,0xf00,&scissor,&color) && calls==1);
    assert(operations[0].target==&resources[2] && operations[0].layer==3);
    assert(operations[0].x==2 && operations[0].y==3 && operations[0].w==5 && operations[0].h==8);
    calls=0; fail_at=2;
    assert(ps5_clear_gpu_color(&c,4|16,0xf0f,NULL,&color) && calls==2 && c.last_draw_status==-17);
    assert(c.framebuffer_valid && c.viewport_valid==0x13 && c.queries_enabled && !blitter.saved);
    calls=0; fail_at=0; skip_at=1;
    assert(ps5_clear_gpu_color(&c,4|16,0xf0f,NULL,&color) && calls==1 && c.last_draw_status==-30);
    calls=0; skip_at=0;
    uint8_t data[48]; memset(data,0xa5,sizeof(data));
    struct ps5_resource storage={.data=data,.size=sizeof(data)};
    c.descriptor_storage[1]=&storage;
    c.constants[1][0]=(struct ps5_constant_state){.size=32,.valid=true,.copied=true};
    assert(ps5_clear_gpu_color(&c,4|16,0xf0f,NULL,&color) && calls==5);
    /* Keep the original single-target fast path. */
    calls=0; c.framebuffer.nr_cbufs=1; c.framebuffer.cbufs[0].last_layer=0;
    assert(ps5_clear_gpu_color(&c,4,15,NULL,&color) && calls==1);
    /* Exercise the highest attachment and its high-nibble mask without shifts
     * beyond the 32-bit mask or reading any of the unused framebuffer slots. */
    calls=0; c.framebuffer.nr_cbufs=8;
    resources[7]=resources[0]; c.framebuffer.cbufs[7]=c.framebuffer.cbufs[0];
    c.framebuffer.cbufs[7].texture=&resources[7];
    assert(ps5_clear_gpu_color(&c,PIPE_CLEAR_COLOR0<<7,UINT32_C(0xf0000000),NULL,&color));
    assert(calls==1 && operations[0].target==&resources[7]);
    puts("PASS: production MRT clear admission, layer selection, state restoration and strict failures");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    executable = str(Path(tmp) / ("mrt-clear.exe" if os.name == "nt" else "mrt-clear"))
    compiler = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c",
                              "-o", executable, "-"], input=code, text=True, check=True, timeout=25)
    subprocess.run([executable], check=True, timeout=5)
