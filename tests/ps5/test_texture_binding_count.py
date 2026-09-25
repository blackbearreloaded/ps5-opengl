#!/usr/bin/env python3
"""Differential stage-count check against the original slot scan, plus host timing."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'src/gallium/ps5/ps5_screen.c').read_text()
def extract(name):
 a=s.index('\n'+name+'('); a=s.rfind('static ',0,a)
 return s[a:s.index('\n}',a)+2]
old='static unsigned\nold_shader_texture_count(const struct ps5_shader *shader)\n{\n   unsigned count = 0;\n\n   for (unsigned unit = 0; unit < PS5_MAX_TEXTURE_UNITS; ++unit)\n      count += BITSET_TEST(shader->nir->info.textures_used, unit);\n   return count;\n}\n\nstatic unsigned\nold_texture_count(const struct ps5_context *context,\n                  const struct ps5_shader *shader,\n                  const PsbcShaderMetadata *metadata)\n{\n   unsigned count = 0;\n\n   const unsigned limit = ps5_uses_tessellation_metadata(context, metadata)\n      ? PS5_TESSELLATION_TEXTURE_BINDING + 4u * PS5_MAX_TEXTURE_UNITS : PS5_MERGED_TEXTURE_UNITS;\n   for (unsigned unit = 0; unit < limit; ++unit)\n      count += ps5_texture_used(context, shader, metadata, unit);\n   return count;\n}\n\n'
code=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "util/bitset.h"
#define PS5_MAX_TEXTURE_UNITS 16u
#define PS5_MERGED_TEXTURE_UNITS 32u
#define PS5_TESSELLATION_TEXTURE_BINDING 16u
#define util_bitcount __builtin_popcount
struct nir { struct { BITSET_DECLARE(textures_used,128); } info; };
struct ps5_shader { struct nir *nir; };
typedef struct { unsigned unused; } PsbcShaderMetadata;
struct output { PsbcShaderMetadata metadata; };
struct ps5_context {
 struct ps5_shader *vs,*fs,*gs,*tcs,*tes;
 struct output geometry_output,geometry_streamout_output;
 struct {struct output hs,tes;} tessellation_output;
};
"""+'\n'.join(extract(n) for n in ['ps5_uses_merged_geometry_metadata',
 'ps5_uses_tessellation_metadata','ps5_texture_used','ps5_shader_texture_count','ps5_texture_count'])+'\n'+old+r"""
static uint32_t seed=1;
static uint32_t next(void){seed=1664525u*seed+1013904223u;return seed;}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
int main(void){
 struct nir nir[5]={0};struct ps5_shader shaders[5];
 for(unsigned i=0;i<5;++i)shaders[i].nir=&nir[i];
 struct ps5_context c={.vs=&shaders[0],.fs=&shaders[1]};
 const PsbcShaderMetadata *metas[]={NULL,&c.geometry_output.metadata,
  &c.geometry_streamout_output.metadata,&c.tessellation_output.hs.metadata,&c.tessellation_output.tes.metadata};
 for(unsigned mask=0;mask<65536;++mask){
  for(unsigned i=0;i<5;++i)nir[i].info.textures_used[0]=(next()&0xffff0000u)|(mask^(i*713));
  c.gs=(mask&1)?&shaders[2]:NULL;c.tcs=(mask&2)?&shaders[3]:NULL;c.tes=(mask&4)?&shaders[4]:NULL;
  for(unsigned m=0;m<5;++m)for(unsigned sh=0;sh<2;++sh)
   assert(ps5_texture_count(&c,&shaders[sh],metas[m])==old_texture_count(&c,&shaders[sh],metas[m]));
 }
 volatile unsigned sum=0;
 for(unsigned old=0;old<2;++old){
  double start=now();
  for(unsigned i=0;i<2000000;++i){nir[0].info.textures_used[0]=i;
   sum+=old?old_texture_count(&c,c.vs,NULL):ps5_texture_count(&c,c.vs,NULL);}
  printf("%s ordinary-stage count: %.1f host ns/call\n",old?"original":"bitcount",(now()-start)/2000000);
 }
 printf("PASS 655360 stage/mask comparisons; checksum=%u\n",sum);
}
"""
mesa=root/'third_party/mesa-26.2.0'
with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'test')
 args=['clang-18','-std=c11','-D_POSIX_C_SOURCE=200809L','-I',str(mesa/'src'),'-I',str(mesa/'include')]
 for opts in [['-O1','-fsanitize=address,undefined','-fno-sanitize-recover=all'],['-O2']]:
  subprocess.run(args+opts+['-x','c','-o',exe,'-'],input=code,text=True,check=True)
  subprocess.run([exe],check=True,timeout=30)
