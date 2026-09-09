#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compile actual GPU blit/pitch/dispatch code with checked external callbacks.

Host checks cover control flow and view metadata, not GPU pixel/cache effects.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()
start = source.index("static bool\nps5_blit_gpu_color(")
body = source[start:source.index("static void\nps5_blit(", start)]


def function(name):
    match = re.search(r'^static (?:bool|unsigned)\n' + re.escape(name) + r'\(', source, re.M)
    assert match, name
    return source[match.start():source.index('\n}', match.end()) + 2]


states = ("vertex_buffers", "vertex_elements", "vertex_shader", "geometry_shader",
          "so_targets", "rasterizer", "fragment_shader", "depth_stencil_alpha", "blend",
          "stencil_ref", "viewport", "scissor", "sample_mask", "framebuffer",
          "fragment_sampler_states", "fragment_sampler_views")
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "src/gallium/ps5/ps5_screen.h"
#define PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE 1
#define PS5_ENABLE_MRT_CANDIDATE 1
#define PS5_ENABLE_UBO_CANDIDATE 1
#define PS5_ENABLE_MSAA4_CANDIDATE 1
#define PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE 1
#define PS5_GPU_BLIT_MIN_PIXELS (512u * 512u)
#define PS5_MAX_COLOR_WIDTH PS5_MAX_RENDER_SIZE
#define PS5_MAX_COLOR_HEIGHT PS5_MAX_RENDER_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
#define MIN2(a,b) ((a) < (b) ? (a) : (b))
enum { PIPE_TEXTURE_2D=2, PIPE_TEXTURE_3D=3, PIPE_TEXTURE_2D_ARRAY=4,
       PIPE_FORMAT_R8G8B8A8_UNORM=1, PIPE_FORMAT_R8_UNORM=2,
       PIPE_FORMAT_R8G8_UNORM=3, PIPE_FORMAT_R16G16B16A16_FLOAT=4,
       PIPE_MASK_RGBA=15, PIPE_TEX_FILTER_NEAREST=0, PIPE_TEX_FILTER_LINEAR=1,
       PIPE_BIND_RENDER_TARGET=1, PIPE_BIND_DISPLAY_TARGET=2 };
struct pipe_resource { unsigned target, last_level, depth0, array_size,
    nr_samples, nr_storage_samples, format, bind, width0, height0; };
struct ps5_resource { struct pipe_resource base; size_t render_staging_size,
    depth_staging_size, allocation_size, size, layer_stride, level_offset[16];
    unsigned level_stride[16]; uint8_t *data; bool linear; };
struct pipe_surface { struct pipe_resource *texture; unsigned format,level,first_layer,last_layer; };
struct pipe_sampler_view { struct pipe_resource *texture; unsigned format,target;
    union { struct { unsigned first_level,last_level,first_layer,last_layer; } tex; } u; };
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_scissor_state { unsigned minx,miny,maxx,maxy; };
struct pipe_blit_info {
    struct { struct pipe_resource *resource; unsigned format,level; struct pipe_box box; } src,dst;
    unsigned mask,filter,num_window_rectangles,dst_sample;
    bool scissor_enable,alpha_blend,swizzle_enable,sample0_only;
    struct pipe_scissor_state scissor;
};
struct pipe_context { struct pipe_sampler_view *(*create_sampler_view)(
    struct pipe_context *,struct pipe_resource *,const struct pipe_sampler_view *); };
struct blitter_context { struct pipe_context *pipe; bool running; };
struct ps5_context { struct pipe_context base; unsigned render_condition_query,
    stream_output_target_count,active_occlusion_query,active_primitives_generated_query,
    active_primitives_emitted_query,draw_calls; int last_draw_status;
    struct blitter_context *blitter;
    bool viewport_valid,scissor_valid,framebuffer_valid,queries_enabled; };
static bool linear;
static bool ps5_agc_gate2_set_color_target_layouts = true;
static bool ps5_linear_sampled_layout(const struct pipe_resource *r)
{ return linear || ((const struct ps5_resource *)r)->linear; }
static unsigned ps5_texture_format_size(unsigned f)
{ const unsigned bytes[]={0,4,1,2,8}; return f < ARRAY_SIZE(bytes) ? bytes[f] : 0; }
static size_t ps5_tiled_color_surface_size(unsigned f, unsigned w, unsigned h)
{ return (size_t)w*h*ps5_texture_format_size(f); }
static size_t ps5_tiled_color_msaa4_surface_size(unsigned f, unsigned w, unsigned h)
{ return 4*ps5_tiled_color_surface_size(f,w,h); }
static struct pipe_blit_info expected;
static unsigned attempts,live_views,releases,cpu_replays,drains;
static bool unsupported,view_failure,no_draw;
static int draw_status;
static struct blitter_context *util_blitter_create(struct pipe_context *pipe)
{ static struct blitter_context b; b.pipe=pipe; return &b; }
static bool util_blitter_is_blit_supported(struct blitter_context *b,const struct pipe_blit_info *i)
{ assert(b); expected=*i; return !unsupported; }
/* Pinned Mesa u_blitter.c: a selected mip view spans all array layers, with
 * srcbox.z selecting the layer. Generic blits take BASE dimensions, not mip dimensions. */
static void util_blitter_default_dst_texture(struct pipe_surface *s,struct pipe_resource *r,
                                            unsigned level,unsigned layer)
{ *s=(struct pipe_surface){r,r->format,level,layer,layer}; }
static void util_blitter_default_src_texture(struct blitter_context *b,struct pipe_sampler_view *v,
                                            struct pipe_resource *r,unsigned level)
{
    assert(b); memset(v,0,sizeof(*v)); v->format=r->format; v->target=r->target;
    v->u.tex.first_level=v->u.tex.last_level=level; v->u.tex.last_layer=r->array_size-1;
}
static struct pipe_sampler_view *create_view(struct pipe_context *p,struct pipe_resource *r,
                                             const struct pipe_sampler_view *t)
{
    static struct pipe_sampler_view v;
    assert(p && r==expected.src.resource && !live_views);
    assert(t->u.tex.first_level==expected.src.level && t->u.tex.last_level==expected.src.level);
    assert(t->u.tex.first_layer==0 && t->u.tex.last_layer==r->array_size-1);
    if (view_failure) return NULL;
    v=*t; v.texture=r; ++live_views; return &v;
}
static void pipe_sampler_view_reference(struct pipe_sampler_view **v,struct pipe_sampler_view *next)
{ assert(*v && !next && live_views==1); --live_views; ++releases; *v=NULL; }
static void util_blitter_blit_generic(struct blitter_context *b,const struct pipe_surface *s,
    const struct pipe_box *dst,const struct pipe_sampler_view *v,const struct pipe_box *src,
    unsigned width0,unsigned height0,unsigned mask,unsigned filter,
    const struct pipe_scissor_state *scissor,bool alpha,bool sample0,unsigned sample,void *override)
{
    assert(s->texture==expected.dst.resource && s->format==expected.dst.format);
    assert(s->level==expected.dst.level && s->first_layer==(unsigned)dst->z && s->last_layer==s->first_layer);
    assert(!memcmp(dst,&expected.dst.box,sizeof(*dst)) && !memcmp(src,&expected.src.box,sizeof(*src)));
    assert(v->texture==expected.src.resource && v->format==expected.src.format);
    assert(v->u.tex.first_level==expected.src.level && v->u.tex.last_level==expected.src.level);
    assert(width0==v->texture->width0 && height0==v->texture->height0);
    assert(mask==expected.mask && filter==expected.filter && !alpha && !sample0 && !sample && !override);
    assert((scissor!=NULL)==expected.scissor_enable);
    struct ps5_context *c=(struct ps5_context *)b->pipe;
    c->viewport_valid=!c->viewport_valid; c->scissor_valid=!c->scissor_valid;
    c->framebuffer_valid=!c->framebuffer_valid; c->queries_enabled=!c->queries_enabled;
    ++attempts; if (!no_draw) ++c->draw_calls; c->last_draw_status=draw_status;
}
''' + '\n'.join(f'#define util_blitter_save_{state}(...) ((void)0)' for state in states)
code += '\n' + '\n'.join(function(name) for name in (
    'ps5_texture_level_layers', 'ps5_surface_width', 'ps5_surface_height',
    'ps5_surface_layer_count', 'ps5_linear_color_pitch', 'ps5_blit_gpu_color'))
# Execute the actual top-level dispatch decision; the CPU implementation is an
# observed fallback boundary, not a second implementation of the GPU helper.
dispatch_start = source.index('   ps5_draw_batch_drain();', source.index('static void\nps5_blit('))
dispatch_end = source.index('   if (PS5_ENABLE_MSAA4_CANDIDATE', dispatch_start)
code += r'''
static void ps5_draw_batch_drain(void) { ++drains; }
static void dispatch(struct ps5_context *ps5,const struct pipe_blit_info *info) {
''' + source[dispatch_start:dispatch_end] + r'''
    ++cpu_replays;
}
static struct ps5_resource linear_resource(unsigned format)
{
    struct ps5_resource r={.base={.target=PIPE_TEXTURE_2D_ARRAY,.last_level=2,
        .depth0=1,.array_size=2,.format=format,.bind=PIPE_BIND_RENDER_TARGET,.width0=1024,.height0=1024},
        .render_staging_size=65536,.data=(uint8_t *)(uintptr_t)0x8000000,.linear=true};
    for (unsigned level=0;level<3;++level) {
        r.level_stride[level]=(1024u>>level)*ps5_texture_format_size(format);
        r.level_offset[level]=r.layer_stride;
        r.layer_stride+=(size_t)r.level_stride[level]*(1024u>>level);
    }
    r.size=2*r.layer_stride; r.allocation_size=r.size+r.render_staging_size;
    return r;
}
int main(void) {
    struct ps5_resource r = {.base={.target=2,.depth0=1,.array_size=1,.format=1,
        .bind=1,.width0=1024,.height0=1024},.allocation_size=4194304,
        .data=(uint8_t *)(uintptr_t)0x1000000};
    struct ps5_resource d=r; d.data=(uint8_t *)((uintptr_t)r.data+r.allocation_size);
    struct ps5_context c={.base={create_view},.draw_calls=10,.viewport_valid=true,.queries_enabled=true};
    struct pipe_blit_info b={.src={&r.base,1,0,{0,0,0,512,512,1}},
        .dst={&d.base,1,0,{1,2,0,512,512,1}},.mask=15};
    assert(ps5_blit_gpu_color(&c,&b));
    assert(!ps5_blit_gpu_color(NULL,&b)); assert(!ps5_blit_gpu_color(&c,NULL));
#define REJECT(field,value) do { struct pipe_blit_info bad=b; bad.field=value; \
    assert(!ps5_blit_gpu_color(&c,&bad)); } while (0)
    REJECT(src.resource,NULL); REJECT(dst.resource,NULL); REJECT(mask,7); REJECT(filter,2);
    REJECT(src.format,2); REJECT(dst.format,2); REJECT(src.level,1); REJECT(dst.level,1);
    REJECT(scissor_enable,true); REJECT(num_window_rectangles,1); REJECT(alpha_blend,true);
    REJECT(swizzle_enable,true); REJECT(sample0_only,true); REJECT(dst_sample,1);
    REJECT(src.box.x,-1); REJECT(dst.box.y,-1); REJECT(src.box.z,1); REJECT(dst.box.depth,2);
    REJECT(src.box.x,513); REJECT(dst.box.y,INT_MAX); REJECT(src.box.width,INT_MIN);
    REJECT(dst.box.width,-512); REJECT(src.box.height,0); REJECT(dst.resource,&r.base);
    struct pipe_blit_info flipped=b;
    flipped.src.box.x=512; flipped.src.box.y=512;
    flipped.src.box.width=-512; flipped.src.box.height=-512;
    assert(ps5_blit_gpu_color(&c,&flipped));
    flipped.src.box.x=511; assert(!ps5_blit_gpu_color(&c,&flipped));
    struct pipe_blit_info scaled=b;
    scaled.src.box.width=256; scaled.src.box.height=128;
    scaled.filter=PIPE_TEX_FILTER_LINEAR;
    assert(ps5_blit_gpu_color(&c,&scaled));
    scaled.src.box.width=1024; scaled.src.box.height=1024;
    assert(ps5_blit_gpu_color(&c,&scaled));
    scaled.scissor_enable=true;
    scaled.scissor.maxx=513; scaled.scissor.maxy=514;
    assert(ps5_blit_gpu_color(&c,&scaled));
    scaled.scissor.minx=2; assert(!ps5_blit_gpu_color(&c,&scaled));
    scaled.scissor.minx=UINT_MAX; assert(!ps5_blit_gpu_color(&c,&scaled));
    b.src.box.width=b.dst.box.width=511; assert(!ps5_blit_gpu_color(&c,&b));
    b.src.box.width=b.dst.box.width=512;
#define BAD_RESOURCE(field,value) do { struct ps5_resource old=r; r.field=value; \
    assert(!ps5_blit_gpu_color(&c,&b)); r=old; } while (0)
    BAD_RESOURCE(base.target,3); BAD_RESOURCE(base.last_level,1); BAD_RESOURCE(base.depth0,2);
    BAD_RESOURCE(base.array_size,2); BAD_RESOURCE(base.nr_samples,4); BAD_RESOURCE(base.nr_storage_samples,4);
    BAD_RESOURCE(base.format,2); BAD_RESOURCE(base.bind,0); BAD_RESOURCE(base.bind,3);
    BAD_RESOURCE(base.width0,0); BAD_RESOURCE(base.height0,PS5_MAX_COLOR_HEIGHT+1);
    BAD_RESOURCE(render_staging_size,1); BAD_RESOURCE(depth_staging_size,1);
    BAD_RESOURCE(data,NULL); BAD_RESOURCE(allocation_size,0); BAD_RESOURCE(allocation_size,4194303);
    BAD_RESOURCE(data,(uint8_t *)(UINTPTR_MAX-1));
    BAD_RESOURCE(data,(uint8_t *)((uintptr_t)d.data-1)); BAD_RESOURCE(data,(uint8_t *)((uintptr_t)d.data+1));
    struct ps5_resource msaa=r;
    msaa.base.nr_samples=msaa.base.nr_storage_samples=4;
    msaa.allocation_size*=4; msaa.data=(uint8_t *)(uintptr_t)0x4000000;
    struct pipe_blit_info resolved=b; resolved.src.resource=&msaa.base;
    assert(ps5_blit_gpu_color(&c,&resolved));
    msaa.allocation_size--; assert(!ps5_blit_gpu_color(&c,&resolved)); msaa.allocation_size++;
    resolved.filter=PIPE_TEX_FILTER_LINEAR; assert(!ps5_blit_gpu_color(&c,&resolved));
    resolved.filter=PIPE_TEX_FILTER_NEAREST; resolved.src.box.width=256;
    assert(!ps5_blit_gpu_color(&c,&resolved));
    resolved=b; resolved.dst.resource=&msaa.base; assert(!ps5_blit_gpu_color(&c,&resolved));
    linear=true; assert(!ps5_blit_gpu_color(&c,&b)); linear=false;
#define BAD_CONTEXT(field) do { c.field=1; assert(!ps5_blit_gpu_color(&c,&b)); c.field=0; } while (0)
    BAD_CONTEXT(render_condition_query); BAD_CONTEXT(stream_output_target_count);
    BAD_CONTEXT(active_occlusion_query); BAD_CONTEXT(active_primitives_generated_query);
    BAD_CONTEXT(active_primitives_emitted_query);
    for (unsigned format=1;format<=4;++format) {
        struct ps5_resource t=linear_resource(format);
        struct pipe_blit_info mip={.src={&t.base,format,0,{0,0,0,1024,1024,1}},
            .dst={&t.base,format,1,{0,0,0,512,512,1}},.mask=15,.filter=PIPE_TEX_FILTER_LINEAR};
        assert(ps5_blit_gpu_color(&c,&mip)); /* Adjacent disjoint mips in ONE allocation. */
        assert(c.viewport_valid && !c.scissor_valid && !c.framebuffer_valid && c.queries_enabled);
        struct pipe_blit_info reverse=mip;
        reverse.src.level=1; reverse.src.box.width=reverse.src.box.height=512;
        reverse.dst.level=0; reverse.dst.box.width=reverse.dst.box.height=1024;
        assert(ps5_blit_gpu_color(&c,&reverse)); /* Nonzero source view level. */
        reverse.src.box.x=1; assert(!ps5_blit_gpu_color(&c,&reverse)); /* Mip bounds, not base bounds. */
        struct pipe_blit_info layers=mip;
        layers.dst.level=0; layers.dst.box.z=1;
        assert(ps5_blit_gpu_color(&c,&layers)); /* Disjoint selected array layers. */
        layers.src.box.z=1; layers.dst.box.z=0;
        assert(ps5_blit_gpu_color(&c,&layers));
        layers.src.box.z=2; assert(!ps5_blit_gpu_color(&c,&layers));
        layers.src.box.z=0; layers.src.box.width=512; layers.dst.box.x=512;
        assert(!ps5_blit_gpu_color(&c,&layers)); /* Disjoint boxes still alias the same selected surface. */
        struct ps5_resource alias=t;
        struct pipe_blit_info aliased=mip; aliased.dst.resource=&alias.base; aliased.dst.level=0;
        assert(!ps5_blit_gpu_color(&c,&aliased)); /* Distinct resource wrappers, same bytes. */
        size_t offset=t.level_offset[1];
        t.level_offset[1]=0; assert(!ps5_blit_gpu_color(&c,&mip));
        t.level_offset[1]=offset-256; assert(!ps5_blit_gpu_color(&c,&mip));
        t.level_offset[1]=offset+1; assert(!ps5_blit_gpu_color(&c,&mip));
        t.level_offset[1]=offset;
        ps5_agc_gate2_set_color_target_layouts=false;
        assert(!ps5_blit_gpu_color(&c,&mip)); ps5_agc_gate2_set_color_target_layouts=true;
        unsigned stride=t.level_stride[1]; t.level_stride[1]--;
        assert(!ps5_blit_gpu_color(&c,&mip)); t.level_stride[1]=stride;
        t.size=t.allocation_size+1; assert(!ps5_blit_gpu_color(&c,&mip));
    }
    /* Attempt failures consume the blit: no CPU replay, even with zero draws. */
    unsigned saved_attempts=attempts, saved_releases=releases;
    no_draw=true; dispatch(&c,&b); no_draw=false;
    assert(c.last_draw_status==-30 && attempts==saved_attempts+1 && !cpu_replays);
    draw_status=-77; dispatch(&c,&b); draw_status=0;
    assert(c.last_draw_status==-77 && attempts==saved_attempts+2 && !cpu_replays);
    assert(releases==saved_releases+2 && !live_views && drains==2);
    assert(c.viewport_valid && !c.scissor_valid && !c.framebuffer_valid && c.queries_enabled);
    view_failure=true; dispatch(&c,&b); view_failure=false;
    unsupported=true; dispatch(&c,&b); unsupported=false;
    assert(attempts==saved_attempts+2 && cpu_replays==2 && !live_views);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = str(Path(tmp) / "gpu-blit-guards")
    subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-fno-omit-frame-pointer", "-no-pie", "-I", str(root), "-x", "c", "-o", exe, "-"],
                   input=code, text=True, check=True, timeout=60)
    subprocess.run([exe], check=True, timeout=30)

# Pin integration ordering and state coverage; the native test verifies effects.
for state in states:
    assert f"util_blitter_save_{state}(" in body, state
for flag in ("viewport_valid", "scissor_valid", "framebuffer_valid", "queries_enabled"):
    assert f"context->{flag} = {flag};" in body
attempt = body[body.index("   util_blitter_blit_generic("):]
assert "return false" not in attempt and "return true;" in attempt
assert "context->draw_calls == draws_before" in attempt
assert "deferred_color_clear =" not in body
assert 'color_target_count == 1 ? ps5_linear_color_pitch(surface) : 0' in source
assert '(context->framebuffer.nr_cbufs != 1 || !ps5_linear_color_pitch(surface)) &&\n             !ps5_stage_color_surface(surface, false)' in source
assert body.index("   if (!view)") < body.index("   util_blitter_save_vertex_buffers(")
dispatch = source[source.index("static void\nps5_blit("):]
assert "ps5_draw_batch_drain();\n   if (ps5_blit_gpu_color(ps5, info))\n      return;" in dispatch
print("PASS: actual GPU-blit/pitch guards, four formats, same-allocation mip/layer aliases, "
      "source views/base dimensions, state flags/view cleanup, attempted-failure no-replay dispatch (ASan/UBSan)")
