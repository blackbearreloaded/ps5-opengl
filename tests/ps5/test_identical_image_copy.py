#!/usr/bin/env python3
"""Run direct image copy against reference bytes and rejection controls."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];s=(root/'src/gallium/ps5/ps5_screen.c').read_text()
a=s.index('static bool\nps5_copy_identical_image(');b=s.index('static void\nps5_resource_copy_region(',a)
assert 'context->base.resource_copy_region = ps5_resource_copy_region;' in s
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define PIPE_TEXTURE_2D 2
#define PIPE_BIND_DISPLAY_TARGET 1
struct pipe_resource { unsigned target,last_level,array_size,depth0,nr_samples,nr_storage_samples,format,bind,width0,height0; };
struct pipe_box { int x,y,z,width,height,depth; };
struct ps5_resource { struct pipe_resource base; uint8_t *data,*stencil_data;size_t size,allocation_size,stencil_allocation_size,layer_stride,level_offset[1],render_staging_size,depth_staging_size;unsigned stride,level_stride[1];bool external_cpu_access; };
static unsigned drains,flushes;static size_t bytes;
static void ps5_draw_batch_drain_buffer(struct pipe_resource *r){assert(r);drains++;}
static void ps5_flush_gpu_data(const void *p,size_t n){assert(p&&n);flushes++;bytes+=n;}
"""+s[a:b]+r"""
int main(void){
 uint8_t a[1024],b[1024],sa[256],sb[256];
 for(unsigned i=0;i<1024;i++)a[i]=(i*37+5)&255;
 for(unsigned i=0;i<256;i++)sa[i]=(i*13+2)&255;
 struct ps5_resource src={.base={.target=2,.array_size=1,.depth0=1,.format=77,.bind=4,.width0=16,.height0=16},.data=a,.size=1024,.allocation_size=1024,.stride=64,.layer_stride=1024,.level_stride={64}};
 struct ps5_resource dst=src;dst.data=b;struct pipe_box box={.width=16,.height=16,.depth=1};
 assert(ps5_copy_identical_image(&dst.base,0,0,0,0,&src.base,0,&box));assert(!memcmp(a,b,1024)&&drains==2&&flushes==2&&bytes==2048);
 src.stencil_data=sa;dst.stencil_data=sb;src.stencil_allocation_size=dst.stencil_allocation_size=256;
 assert(ps5_copy_identical_image(&dst.base,0,0,0,0,&src.base,0,&box));assert(!memcmp(sa,sb,256));
 struct ps5_resource good=dst;unsigned old=drains;
#define REJECT(field,value) do { dst=good;dst.field=value;assert(!ps5_copy_identical_image(&dst.base,0,0,0,0,&src.base,0,&box));assert(drains==old); } while(0)
 REJECT(base.target,3);REJECT(base.last_level,1);REJECT(base.array_size,2);REJECT(base.depth0,2);REJECT(base.nr_samples,4);REJECT(base.nr_storage_samples,4);
 REJECT(base.format,78);REJECT(base.bind,5);REJECT(base.width0,15);REJECT(base.height0,15);REJECT(render_staging_size,16);REJECT(depth_staging_size,16);REJECT(external_cpu_access,true);
 REJECT(allocation_size,512);REJECT(size,512);REJECT(stride,32);REJECT(layer_stride,512);REJECT(level_offset[0],16);REJECT(level_stride[0],32);REJECT(stencil_allocation_size,128);REJECT(stencil_data,NULL);REJECT(data,NULL);
 dst=good;box.width=15;assert(!ps5_copy_identical_image(&dst.base,0,0,0,0,&src.base,0,&box));box.width=16;
 assert(!ps5_copy_identical_image(&dst.base,0,1,0,0,&src.base,0,&box));assert(!ps5_copy_identical_image(&dst.base,1,0,0,0,&src.base,0,&box));
 assert(!ps5_copy_identical_image(&dst.base,0,0,0,0,&src.base,1,&box));assert(drains==old);
 assert(ps5_copy_identical_image(&src.base,0,0,0,0,&src.base,0,&box));return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True)
 subprocess.run([str(p.with_suffix(''))],check=True)
print('Identical whole image/color+stencil bytes, lifetime/flush ordering and incompatible-layout rejection PASS')
