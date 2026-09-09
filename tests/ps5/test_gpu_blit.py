#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compile actual GPU blit guards/dispatch; native gate checks pixel/state effects."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[2] / "src/gallium/ps5/ps5_screen.c").read_text()
start = source.index("static bool\nps5_blit_gpu_color(")
guards = source[start:source.index("   if (!context->blitter)", start)]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <stdlib.h>
#define PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE 1
#define PS5_ENABLE_MRT_CANDIDATE 1
#define PS5_ENABLE_UBO_CANDIDATE 1
#define PS5_ENABLE_MSAA4_CANDIDATE 1
#define PS5_GPU_BLIT_MIN_PIXELS (512u * 512u)
#define PS5_MAX_COLOR_WIDTH 4096
#define PS5_MAX_COLOR_HEIGHT 4096
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
#define MIN2(a,b) ((a) < (b) ? (a) : (b))
enum { PIPE_TEXTURE_2D=2, PIPE_FORMAT_R8G8B8A8_UNORM=1,
       PIPE_MASK_RGBA=15, PIPE_TEX_FILTER_NEAREST=0, PIPE_TEX_FILTER_LINEAR=1,
       PIPE_BIND_RENDER_TARGET=1, PIPE_BIND_DISPLAY_TARGET=2 };
struct pipe_resource { unsigned target, last_level, depth0, array_size,
    nr_samples, nr_storage_samples, format, bind, width0, height0; };
struct ps5_resource { struct pipe_resource base; size_t render_staging_size,
    depth_staging_size, allocation_size; uint8_t *data; };
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_blit_info {
    struct { struct pipe_resource *resource; unsigned format,level; struct pipe_box box; } src,dst;
    unsigned mask,filter,num_window_rectangles,dst_sample;
    bool scissor_enable,alpha_blend,swizzle_enable,sample0_only;
    struct { unsigned minx,miny,maxx,maxy; } scissor;
};
struct pipe_context { int unused; };
struct ps5_context { struct pipe_context base; unsigned render_condition_query,
    stream_output_target_count,active_occlusion_query,active_primitives_generated_query,
    active_primitives_emitted_query; int last_draw_status; };
static bool linear;
static bool ps5_linear_sampled_layout(const struct pipe_resource *r) { (void)r; return linear; }
static size_t ps5_tiled_color_surface_size(unsigned f, unsigned w, unsigned h)
{ assert(f == 1); return (size_t)w*h*4; }
static size_t ps5_tiled_color_msaa4_surface_size(unsigned f, unsigned w, unsigned h)
{ return 4*ps5_tiled_color_surface_size(f,w,h); }
''' + guards + r'''
   return true;
}
int main(void) {
    struct ps5_resource r = {.base={.target=2,.depth0=1,.array_size=1,.format=1,
        .bind=1,.width0=1024,.height0=1024},.allocation_size=4194304,
        .data=(uint8_t *)(uintptr_t)0x1000000};
    struct ps5_resource d=r; d.data += r.allocation_size;
    struct ps5_context c={0};
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
    BAD_RESOURCE(base.width0,0); BAD_RESOURCE(base.height0,4097);
    BAD_RESOURCE(render_staging_size,1); BAD_RESOURCE(depth_staging_size,1);
    BAD_RESOURCE(data,NULL); BAD_RESOURCE(allocation_size,0); BAD_RESOURCE(allocation_size,4194303);
    BAD_RESOURCE(data,(uint8_t *)(UINTPTR_MAX-1));
    BAD_RESOURCE(data,d.data-1); BAD_RESOURCE(data,d.data+1);
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
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = str(Path(tmp) / "gpu-blit-guards")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-o", exe, "-"],
                   input=code, text=True, check=True)
    subprocess.run([exe], check=True)

# Pin integration ordering and state coverage; the native test verifies effects.
body = source[start:source.index("static void\nps5_blit(", start)]
for state in ("vertex_buffers", "vertex_elements", "vertex_shader", "geometry_shader",
              "so_targets", "rasterizer", "fragment_shader", "depth_stencil_alpha", "blend",
              "stencil_ref", "viewport", "scissor", "sample_mask", "framebuffer",
              "fragment_sampler_states", "fragment_sampler_views"):
    assert f"util_blitter_save_{state}(" in body, state
for flag in ("viewport_valid", "scissor_valid", "framebuffer_valid", "queries_enabled"):
    assert f"context->{flag} = {flag};" in body
attempt = body[body.index("   util_blitter_blit_generic("):]
assert "return false" not in attempt and "return true;" in attempt
assert "context->draw_calls == draws_before" in attempt
assert "deferred_color_clear =" not in body
assert body.index("   if (!view)") < body.index("   util_blitter_save_vertex_buffers(")
dispatch = source[source.index("static void\nps5_blit("):]
assert "ps5_draw_batch_drain();\n   if (ps5_blit_gpu_color(ps5, info))\n      return;" in dispatch
print("PASS: actual GPU-blit eligibility/bounds/alias guards, state-save contract, drain/no-replay dispatch")
