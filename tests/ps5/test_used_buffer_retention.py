#!/usr/bin/env python3
"""Exercise actual draw retention against stale and active buffer bindings."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/gallium/ps5/ps5_screen.c').read_text()
a=s.index('static unsigned\nps5_batch_retain_resources(')
b=s.index('static bool\nps5_try_multi_draw_batch(',a)
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define PIPE_MAX_ATTRIBS 16
#define PS5_MAX_CONSTANT_BUFFERS 16
#define PS5_MAX_TEXTURE_UNITS 8
#define PS5_BATCH_RESOURCE_COUNT 80
#define BITFIELD_BIT(i) (1u<<(i))
#define MIN2(a,b) ((a)<(b)?(a):(b))
struct pipe_resource { unsigned refs; };
struct nir { struct { unsigned num_ubos; bool first_ubo_is_default_ubo; } info; };
struct ps5_shader { struct nir *nir; };
struct ps5_screen { struct pipe_resource *render_pool; };
struct ps5_context {
 struct { struct ps5_screen *screen; } base;
 struct { unsigned nr_cbufs; struct { struct pipe_resource *texture; } cbufs[4],zsbuf; } framebuffer;
 struct pipe_resource *border_color_storage;
 unsigned vertex_buffer_count;
 struct { struct { struct pipe_resource *resource; } buffer; } vertex_buffers[16];
 struct elements { unsigned count; struct { unsigned vertex_buffer_index; } elements[16]; } *vertex_elements;
 struct constant { bool valid,copied; struct pipe_resource *buffer; } constants[2][16];
 struct ps5_shader *vs,*fs,*gs,*tcs,*tes;
 struct view { struct pipe_resource *texture; } *sampler_views[2][8];
};
struct pipe_draw_info { unsigned index_size; struct { struct pipe_resource *resource; } index; };
static void pipe_resource_reference(struct pipe_resource **dst, struct pipe_resource *src) { *dst=src; if(src)src->refs++; }
static unsigned ps5_constant_state_binding(const struct ps5_shader *s,unsigned i) { return i+!s->nir->info.first_ubo_is_default_ubo; }
static bool ps5_texture_used(const struct ps5_context *c,const struct ps5_shader *s,void *m,unsigned i) { (void)s;(void)m;return c->sampler_views[1][i]!=0; }
"""+s[a:b]+r"""
int main(void) {
 struct ps5_screen screen={0};struct ps5_context c={.base={&screen}};
 struct nir vn={{2,false}},fn={{1,true}};struct ps5_shader vs={&vn},fs={&fn};
 c.vs=&vs;c.fs=&fs;
 struct pipe_resource r[40]={0};struct pipe_resource *retained[80]={0};
 struct elements elements={.count=2,.elements={{1},{3}}};c.vertex_elements=&elements;c.vertex_buffer_count=4;
 for(unsigned i=0;i<4;i++)c.vertex_buffers[i].buffer.resource=&r[i];
 for(unsigned st=0;st<2;st++)for(unsigned i=0;i<16;i++)c.constants[st][i]=(struct constant){true,false,&r[4+st*16+i]};
 struct pipe_draw_info info={.index_size=2,.index={&r[36]}};
 c.constants[0][2].copied=true;
 unsigned n=ps5_batch_retain_resources(&c,&info,retained);assert(n<=80);
 assert(r[0].refs==0&&r[1].refs==1&&r[2].refs==0&&r[3].refs==1);
 assert(r[4].refs==0&&r[5].refs==1&&r[6].refs==0&&r[7].refs==0);
 assert(r[20].refs==1&&r[21].refs==0&&r[36].refs==1);
 c.tcs=&vs;ps5_batch_retain_resources(&c,&info,retained);
 assert(r[4].refs==1&&r[5].refs==2&&r[6].refs==0&&r[7].refs==1&&r[35].refs==1);
 c.tcs=0;c.vertex_elements=0;c.vs=0;c.fs=0;ps5_batch_retain_resources(&c,&info,retained);
 assert(r[1].refs==2&&r[5].refs==2);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True)
 subprocess.run([str(p.with_suffix(''))],check=True)
print('Active/stale vertex and UBO retention, copied uniforms, tessellation fallback PASS')
