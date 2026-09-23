#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise real NIR compilation, persisted output, invalidation and damaged records."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
psbc = root / 'third_party/opengnm-psbc'
code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <dirent.h>
#include "compiler/nir/nir_builder.h"
#include "psbc_compile.h"
static unsigned compilations;
static PsbcResult counted(const nir_shader *nir, const PsbcCompileOptions *o, PsbcShaderOutput *out) {
    ++compilations; return psbc_compile_nir(nir,o,out);
}
#define psbc_compile_nir counted
#include "ps5_shader_cache.h"
#undef psbc_compile_nir
int main(int argc, char **argv) {
    assert(argc==3);
    assert(!setenv("PS5_SHADER_CACHE_DIR",argv[1],1));
    psbc_init();
    nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
        psbc_get_nir_options(PSBC_STAGE_FRAGMENT),"cache-check");
    nir_variable *color=nir_variable_create(b.shader,nir_var_shader_out,glsl_vec4_type(),"color");
    color->data.location=FRAG_RESULT_DATA0;
    nir_store_var(&b,color,nir_imm_vec4(&b,1,0.25,0.125,1),15);
    PsbcCompileOptions o; memset(&o,0,sizeof(o));
    o.target=PSBC_TARGET_PS5; o.stage=PSBC_STAGE_FRAGMENT; o.entrypoint="main"; o.optimise=true;
    PsbcShaderOutput a={0},cached={0};
    assert(ps5_compile_cached_nir(b.shader,&o,&a)==PSBC_RESULT_OK);
    assert(compilations==(unsigned)atoi(argv[2]));
    unsigned before=compilations;
    char entry[]="main"; o.entrypoint=entry;
    assert(ps5_compile_cached_nir(b.shader,&o,&cached)==PSBC_RESULT_OK);
    assert(compilations==before && cached.machine_code!=a.machine_code && cached.data!=a.data);
    assert(a.size==cached.size && a.machine_code_size==cached.machine_code_size);
    assert(!memcmp(a.data,cached.data,a.size));
    assert(!memcmp(a.machine_code,cached.machine_code,a.machine_code_size));
    assert(!memcmp(&a.metadata,&cached.metadata,sizeof(a.metadata)));
    psbc_free_output(&cached);
    if (!before) { psbc_free_output(&a); ralloc_free(b.shader); psbc_shutdown(); return 0; }
    /* A changed compile option must miss even with identical input NIR. */
    o.address32_hi=1;
    assert(ps5_compile_cached_nir(b.shader,&o,&cached)==PSBC_RESULT_OK && compilations==before+1);
    psbc_free_output(&cached); o.address32_hi=0;
    nir_builder changed=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
        psbc_get_nir_options(PSBC_STAGE_FRAGMENT),"changed-input");
    nir_variable *changed_color=nir_variable_create(changed.shader,nir_var_shader_out,glsl_vec4_type(),"color");
    changed_color->data.location=FRAG_RESULT_DATA0;
    nir_store_var(&changed,changed_color,nir_imm_vec4(&changed,0,1,0,1),15);
    before=compilations;
    assert(ps5_compile_cached_nir(changed.shader,&o,&cached)==PSBC_RESULT_OK && compilations==before+1);
    assert(a.machine_code_size!=cached.machine_code_size || memcmp(a.machine_code,cached.machine_code,a.machine_code_size));
    psbc_free_output(&cached); ralloc_free(changed.shader);
    /* Exercise exact record parser independently, including a colliding slot. */
    char path[4096]; snprintf(path,sizeof(path),"%s/test.bin",argv[1]);
    uint8_t key[BLAKE3_KEY_LEN]={0}, wrong[BLAKE3_KEY_LEN]={1};
    ps5_shader_cache_write(path,key,&a);
    assert(!ps5_shader_cache_read(path,wrong,&cached));
    assert(ps5_shader_cache_read(path,key,&cached)); psbc_free_output(&cached);
    FILE *f=fopen(path,"r+b"); assert(f); assert(!fseek(f,-1,SEEK_END));
    int value=fgetc(f); assert(!fseek(f,-1,SEEK_END)); fputc(value^1,f); fclose(f);
    assert(!ps5_shader_cache_read(path,key,&cached));
    f=fopen(path,"wb"); assert(f); fputc(1,f); fclose(f);
    assert(!ps5_shader_cache_read(path,key,&cached));
    struct ps5_shader_cache_record bad={0}; bad.code_size=UINT32_MAX;
    f=fopen(path,"wb"); assert(f); fwrite(&bad,sizeof(bad),1,f); fclose(f);
    assert(!ps5_shader_cache_read(path,key,&cached)); unlink(path);
    assert(!setenv("PS5_SHADER_CACHE_DIR","/nonexistent/cache",1)); before=compilations;
    assert(ps5_compile_cached_nir(b.shader,&o,&cached)==PSBC_RESULT_OK && compilations==before+1);
    psbc_free_output(&cached); psbc_free_output(&a); ralloc_free(b.shader); psbc_shutdown();
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    cache = directory / 'cache'
    cache.mkdir()
    member = directory / 'member.o'
    member.write_bytes(b'first compiler object')
    archive = directory / 'compiler.a'
    subprocess.run(['ar', 'rcT', str(archive), str(member)], check=True)
    header = directory / 'identity.h'
    identify = ['python3', str(root / 'tools/shader-cache-build-id.py'), str(header), str(archive)]
    subprocess.run(identify, check=True)
    original = header.read_text()
    member.write_bytes(b'changed compiler object')
    subprocess.run(identify, check=True)
    assert original != header.read_text(), 'Thin archive member change must invalidate cache'
    for version in ('first', 'changed'):
        obj, exe = str(directory / 'check.o'), str(directory / 'check')
        subprocess.run(['clang-18', '-std=gnu11', '-O2', '-Wall', '-Werror',
            '-DHAVE_ENDIAN_H=1', '-DHAVE_FUNC_ATTRIBUTE_PACKED=1', '-DHAVE_PTHREAD=1',
            '-DHAVE_STRUCT_TIMESPEC=1', '-D_GNU_SOURCE',
            f'-DPS5_SHADER_CACHE_BUILD_ID="{version}"',
            '-I', str(psbc / 'include/mesa'), '-I', str(psbc / 'include'),
            '-I', str(psbc / 'src'), '-I', str(psbc / 'libpsbc'),
            '-I', str(root / 'src/gallium/ps5'), '-x', 'c', '-c', '-o', obj, '-'],
            input=code, text=True, check=True)
        subprocess.run(['g++', '-o', exe, obj, str(psbc / 'libpsbc.a'), '-pthread', '-lm'], check=True)
        subprocess.run([exe, str(cache), '1'], check=True, timeout=30)
        subprocess.run([exe, str(cache), '0'], check=True, timeout=30)
print('PASS: real compiled output survives restart; option/build changes miss; corruption, truncation, oversized records and unavailable storage fall back')
