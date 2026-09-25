#!/usr/bin/env python3
"""Offline full-value graphics encoder cache prototype; production remains unchanged."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/gallium/ps5/ps5_screen.c').read_text()
def function(name):
    a=s.index('\n'+name+'(')
    a=s.rfind('static ',0,a)
    return s[a:s.index('\n}',a)+2]
a=s.index('struct ps5_native_graphics_state {')
native=s[a:s.index('\n};',a)+3]
a=s.index('static bool\nps5_dual_source_blend_factor(')
encoders=s[a:s.index('static bool\nps5_stencil_uses_unit_op_value',a)]
code=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "pipe/p_state.h"
#define PS5_MAX_RENDER_TARGETS 8
#define PS5_MAX_VIEWPORTS 16
#define PS5_ENABLE_DUAL_SOURCE_BLEND_CANDIDATE 1
#define PS5_ENABLE_SMOOTH_RASTER_CANDIDATE 1
#define PS5_ENABLE_MSAA4_CANDIDATE 1
#define PS5_ENABLE_POINT_LINE_SIZE_CANDIDATE 1
#define PS5_ENABLE_POINT_COORD_CANDIDATE 1
#define PS5_MIN_POINT_LINE_SIZE 1.0f
#define PS5_MAX_POINT_LINE_SIZE 64.0f
struct ps5_context {
 const struct pipe_blend_state *blend;
 const struct pipe_rasterizer_state *rasterizer;
 struct pipe_framebuffer_state framebuffer;
 struct pipe_blend_color blend_color;
 struct pipe_viewport_state viewport[16];
 struct pipe_scissor_state scissor[16];
 uint16_t viewport_valid,scissor_valid;
};
'''+native+'\n'+'\n'.join(function(x) for x in ['ps5_float_bits','ps5_float_is_finite','ps5_pack_float_12p4'])+'\n'+encoders+r'''
struct key { struct ps5_context context; struct pipe_blend_state blend; struct pipe_rasterizer_state rasterizer; };
static struct key previous;
static struct ps5_native_graphics_state encoded;
static bool valid;
static unsigned hits;
__attribute__((noinline)) static bool cached(const struct ps5_context *ctx, struct ps5_native_graphics_state *out) {
 struct key key={0}; key.context=*ctx;
 if(ctx->blend) key.blend=*ctx->blend;
 if(ctx->rasterizer) key.rasterizer=*ctx->rasterizer;
 if(valid && !memcmp(&key,&previous,sizeof(key))) { *out=encoded; ++hits; return true; }
 bool ok=ps5_encode_graphics_state(ctx,out);
 if(ok) { previous=key; encoded=*out; valid=true; }
 return ok;
}
__attribute__((noinline)) static bool original(const struct ps5_context *ctx, struct ps5_native_graphics_state *out) {
 return ps5_encode_graphics_state(ctx,out);
}
static void compare(struct ps5_context *ctx) {
 struct ps5_native_graphics_state a={0},b={0};
 bool x=original(ctx,&a), y=cached(ctx,&b);
 assert(x==y); if(x) assert(!memcmp(&a,&b,sizeof(a)));
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1e9+t.tv_nsec; }
int main(void) {
 struct pipe_blend_state b={0}; struct pipe_rasterizer_state r={0};
 struct pipe_resource target={0}; struct ps5_context c={0};
 c.blend=&b; c.rasterizer=&r; c.framebuffer.width=1920; c.framebuffer.height=1080;
 c.framebuffer.nr_cbufs=1; c.framebuffer.cbufs[0].texture=&target; b.rt[0].colormask=15;
 for(unsigned i=0;i<10000;++i) {
  unsigned v=i%16;
  switch(i%11) {
   case 0:c.viewport_valid^=1u<<v; c.viewport[v].scale[0]=(float)(i%100); break;
   case 1:c.scissor_valid^=1u<<v; c.scissor[v].maxx=i%1921; break;
   case 2:c.framebuffer.width=640+i%1281; break;
   case 3:c.framebuffer.cbufs[0].texture=(i&1)?&target:NULL; break;
   case 4:b.rt[0].colormask=i%16; break;
   case 5:r.scissor=!r.scissor; r.offset_tri=!r.offset_tri; r.offset_units=(float)i; break;
   case 6:c.blend_color.color[i%4]=(float)i/10000; break;
   case 7:r.line_width=(i&1)?2.0f:100.0f; break;
   case 8:c.blend=c.blend?NULL:&b; break;
   case 9:c.rasterizer=c.rasterizer?NULL:&r; break;
   case 10:c.framebuffer.zsbuf.texture=(i&1)?&target:NULL; break;
  }
  compare(&c); compare(&c);
 }
 r.line_width=1; r.point_size=1; c.rasterizer=&r; c.blend=&b;
 compare(&c); unsigned old_hits=hits; compare(&c); assert(hits>old_hits);
 volatile uint32_t checksum=0; struct ps5_native_graphics_state out={0};
 for(unsigned kind=0;kind<2;++kind) for(unsigned mutation=0;mutation<2;++mutation) {
  double start=now();
  for(unsigned i=0;i<1000000;++i) {
   if(mutation) c.framebuffer.width=640+(i&1);
   assert((kind?cached:original)(&c,&out)); checksum+=out.target_mask;
  }
  printf("%s mutation=%u %.1f ns/draw\n",kind?"cached":"original",mutation,(now()-start)/1000000);
 }
 printf("PASS actual encoder 10000 mutations/repeats; checksum=%u key=%zu bytes\n",checksum,sizeof(previous));
}
'''
mesa=root/'third_party/mesa-26.2.0'
with tempfile.TemporaryDirectory() as d:
    exe=str(Path(d)/'test')
    flags=['clang-18','-std=c11','-DHAVE_ENDIAN_H=1','-D_POSIX_C_SOURCE=200809L']
    for p in [mesa/'include',root/'build/mesa-ps5-probe/src',mesa/'src',mesa/'src/gallium/include']:flags+=['-I',str(p)]
    for opt in [['-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all'],['-O2']]:
        subprocess.run(flags+opt+['-x','c','-o',exe,'-'],input=code,text=True,check=True)
        subprocess.run([exe],check=True,timeout=30)