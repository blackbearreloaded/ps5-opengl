#!/usr/bin/env python3
"""Actual sampler creation vs the frozen pre-cache encoding; no GPU required."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/gallium/ps5/ps5_screen.c').read_text()
def function(name):
 a=s.index('\n'+name+'('); start=s.rfind('static ',0,a)
 return s[start:s.index('\n}',a)+2]
a=s.index('struct ps5_sampler_state {')
state=s[a:s.index('\n};',a)+3]
# Frozen encoding extracted before the creation-time cache change.
reference=r'''
static bool
reference_sampler_words(const struct pipe_sampler_state *sampler, uint32_t words[3])
{
   uint32_t wrap[3], filter[2], mip_filter;
   uint32_t min_lod, max_lod, lod_bias, anisotropy;
   if (sampler->unnormalized_coords ||
          sampler->max_anisotropy > 16 ||
          !ps5_float_is_finite(sampler->min_lod) ||
          !ps5_float_is_finite(sampler->max_lod) ||
          !ps5_float_is_finite(sampler->lod_bias) ||
          !ps5_texture_descriptor_wrap(sampler->wrap_s, &wrap[0]) ||
          !ps5_texture_descriptor_wrap(sampler->wrap_t, &wrap[1]) ||
          !ps5_texture_descriptor_wrap(sampler->wrap_r, &wrap[2]) ||
          !ps5_texture_descriptor_filter(sampler->min_img_filter,
                                         sampler->max_anisotropy,
                                         &filter[0]) ||
          !ps5_texture_descriptor_filter(sampler->mag_img_filter,
                                         sampler->max_anisotropy,
                                         &filter[1]) ||
          !ps5_texture_descriptor_mip_filter(sampler->min_mip_filter,
                                             &mip_filter))
      return false;
      min_lod = ps5_texture_descriptor_unsigned_lod(sampler->min_lod);
      max_lod = ps5_texture_descriptor_unsigned_lod(sampler->max_lod);
      lod_bias = ps5_texture_descriptor_lod_bias(sampler->lod_bias);
      anisotropy = ps5_texture_descriptor_anisotropy(
         sampler->max_anisotropy);
      words[0] = wrap[0] | (wrap[1] << 3) | (wrap[2] << 6) |
                      (anisotropy << 9) | ((anisotropy >> 1) << 16) |
                      (anisotropy << 21) |
                      (sampler->compare_mode ?
                         sampler->compare_func << 12 : 0) |
                      ((PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE &&
                        !sampler->seamless_cube_map) << 28);
      words[1] = min_lod | (max_lod << 12) |
                      ((anisotropy ? anisotropy + 6u : 0u) << 24);
      words[2] = lod_bias | (filter[1] << 20) | (filter[0] << 22) |
                       (mip_filter << 26) |
                       (anisotropy ? 1u << 29 : 0u);
   return true;
}

'''

code=r"""
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pipe/p_state.h"
#define PS5_ENABLE_BORDER_COLOR_CANDIDATE 1
#define PS5_ENABLE_SEAMLESS_CUBE_CANDIDATE 1
#define PS5_BORDER_COLOR_COUNT 4096
struct pipe_context { unsigned unused; };
struct ps5_resource { void *data; };
struct ps5_context { struct pipe_context base; struct ps5_resource *border_color_storage; unsigned border_color_count; };
static void ps5_flush_gpu_data(const void *p,size_t bytes) { (void)p;(void)bytes; }
"""+state+'\n'+'\n'.join(function(n) for n in [
 'ps5_float_bits','ps5_float_is_finite','ps5_texture_descriptor_wrap',
 'ps5_texture_descriptor_anisotropy','ps5_texture_descriptor_filter',
 'ps5_texture_descriptor_mip_filter','ps5_texture_descriptor_unsigned_lod',
 'ps5_texture_descriptor_lod_bias','ps5_encode_sampler_words','ps5_create_sampler_state'])+'\n'+reference+r"""
static uint32_t seed=91;
static uint32_t next(void) {seed=1664525u*seed+1013904223u;return seed;}
int main(void) {
 union pipe_color_union borders[4096]={0};
 struct ps5_resource table={borders};
 struct ps5_context ctx={.border_color_storage=&table};
 const float lods[]={-INFINITY,INFINITY,NAN,-1000,-32,-0.0f,0,1.25f,15,31,1000};
 unsigned accepted=0,rejected=0;
 for(unsigned i=0;i<20000;++i) {
  struct pipe_sampler_state raw={0};
  raw.wrap_s=next()%8;raw.wrap_t=next()%8;raw.wrap_r=next()%8;
  raw.min_img_filter=next()%2; raw.mag_img_filter=next()%2; raw.min_mip_filter=next()%4;
  raw.max_anisotropy=next()%32; raw.compare_mode=next()%2;raw.compare_func=next()%8;
  raw.seamless_cube_map=next()%2;raw.unnormalized_coords=next()%2;
  raw.min_lod=lods[next()%11];raw.max_lod=lods[next()%11];raw.lod_bias=lods[next()%11];
  // Finite ordinary states as well as independently invalid states.
  if(i%2) {raw.unnormalized_coords=0;raw.max_anisotropy=i%17;raw.min_mip_filter=i%3;
   raw.min_lod=i%16;raw.max_lod=1000;raw.lod_bias=(int)(i%65)-32;}
  raw.border_color.f[0]=(i%5)*0.25f;raw.border_color.f[3]=1;
  uint32_t expected[3]={0};bool valid=reference_sampler_words(&raw,expected);
  struct ps5_sampler_state *cached=ps5_create_sampler_state(&ctx.base,&raw);
  assert(cached && cached->words_valid==valid);
  if(valid) {++accepted;assert(!memcmp(expected,cached->words,sizeof(expected)));}
  else ++rejected;
  memset(&raw,0xff,sizeof(raw)); // Caller storage is not the cached object's state.
  uint32_t again[3]={0};assert(reference_sampler_words(&cached->base,again)==valid);
  if(valid)assert(!memcmp(again,cached->words,sizeof(again)));
  free(cached);
 }
 assert(accepted>1000 && rejected>1000);
 // Border-free and custom-border states both run the encoding on creation.
 printf("PASS 20000 sampler creations: %u valid, %u invalid; immutable copied state\n",accepted,rejected);
}
"""
mesa=root/'third_party/mesa-26.2.0'
with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'test')
 args=['clang-18','-std=c11','-O1','-DHAVE_ENDIAN_H=1','-D_POSIX_C_SOURCE=200809L',
       '-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize-recover=all']
 for p in [mesa/'include',root/'build/mesa-ps5-probe/src',mesa/'src',mesa/'src/gallium/include']:
  args+=['-I',str(p)]
 subprocess.run(args+['-x','c','-o',exe,'-'],input=code,text=True,check=True)
 subprocess.run([exe],check=True,timeout=30)
