#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compile real CS NIR and check native package/resource contracts without a GPU."""
import subprocess
import os
import hashlib
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PSBC = ROOT / "third_party/opengnm-psbc"
runtime = (ROOT / "src/platform/ps5_agc_native_runtime.c").read_text()
backend = (ROOT / "src/platform/ps5_agc_runtime_backend.c").read_text()
probe = (ROOT / "tests/ps5/egl_public_compute_probe.c").read_text()
probe = probe[probe.index("#define BUFFER_SPILL_VECTORS"):probe.index("int main(void)")]
compute_at = backend.index("int\nps5_agc_compute_execute(")
compute = backend[compute_at:backend.index("\n}\n#endif", compute_at) + 2]
release_at = runtime.index("static uint32_t *runtime_release_completion(")
release = runtime[release_at:runtime.index("\n}\n", release_at) + 3]
parser_at = runtime.index("static int shader_sections(")
parsers = runtime[parser_at:runtime.index("static int load_apis(", parser_at)]
types_at = runtime.index("typedef struct agc_register {")
types = runtime[types_at:runtime.index("typedef struct video_buffer {", types_at)]
api_at = runtime.index("typedef struct agc_api {")
types += runtime[api_at:runtime.index("} agc_api_t;", api_at) + len("} agc_api_t;")]
mock = r'''
#include <stdlib.h>
typedef struct { int unused; } video_api_t;
struct pipe_screen { int unused; };
struct pipe_resource { void *data; size_t size; bool image; size_t allocation_size; };
static int ps5_resource_storage_image_descriptor(struct pipe_resource *r,unsigned level,uint32_t d[8]) {
    if(!r || !r->image || level) return -1;
    const uint32_t srd[8]={(uintptr_t)r->data>>8,0,0x80000000,0x90000204,63,0x400000,0,0};
    memcpy(d,srd,sizeof(srd)); return 0;
}
static bool sampled_msaa;
static int ps5_resource_storage_image_descriptor_owned(struct pipe_resource *r,const uint32_t d[8]) {
    uint32_t expected[8];
    return ps5_resource_storage_image_descriptor(r,(d[3]>>12)&15u,expected) ||
        memcmp(expected,d,sizeof(expected)) ? -1 : 0;
}
static int ps5_resource_sampled_image_descriptor(struct pipe_resource *r,unsigned first,unsigned last,uint32_t d[8]) {
    if(first || last) return -1;
    int rc=ps5_resource_storage_image_descriptor(r,0,d);
    if(!rc && sampled_msaa) { d[3]=0xe1b20204; d[4]=0; d[5]=0x400020; }
    return rc;
}
static int ps5_resource_sampled_image_descriptor_owned(struct pipe_resource *r,const uint32_t d[8]) {
    uint32_t expected[8];
    if(!d) return -1;
    for(unsigned channel=0;channel<4;++channel) {
        unsigned selector=(d[3]>>(channel*3))&7u;
        if(selector==2 || selector==3) return -1;
    }
    if(ps5_resource_sampled_image_descriptor(r,0,0,expected)) return -1;
    expected[3]=(expected[3]&~0xfffu)|(d[3]&0xfffu);
    return memcmp(expected,d,sizeof(expected)) ? -1 : 0;
}
static int ps5_resource_texel_buffer_descriptor_owned(struct pipe_resource *r,const uint32_t d[4]) {
    if(!r || r->image || !d) return -1;
    const uint64_t address=d[0]|((uint64_t)(d[1]&0xffffu)<<32);
    const unsigned stride=d[1]>>16;
    const uint64_t bytes=(uint64_t)d[2]*stride;
    const uintptr_t start=(uintptr_t)r->data;
    return (stride==1||stride==2||stride==4||stride==8||stride==12||stride==16) &&
        d[2] && (d[3]&0x01000000u) && address>=start &&
        address-start<=r->size && bytes<=r->size-(address-start) ? 0 : -1;
}
static unsigned locked, allocations, submissions, dispatches, flushes, userdata_count;
static unsigned out_of_space;
static const void *watched_resource;
static size_t watched_allocation;
static unsigned watched_flushes, watched_acquires;
static int runtime_agc_initialized, fail_map, fail_emit, fail_alloc;
static int64_t direct_size=1024*1024;
static void *mapped_memory;
static size_t mapped_size;
static uint32_t saved_userdata[16], saved_groups[3];
static uint32_t saved_tmpring;
static bool expect_private, expect_scratch;
static uint32_t expected_tmpring;
static volatile uint32_t *saved_marker;
static uint32_t saved_expected;
#define DIRECT_MEMORY_TYPE 12
#define MAP_PROTECTION 0x33
static void ps5_screen_submit_lock(struct pipe_screen *s) { assert(s && !locked); locked=1; }
static void ps5_screen_submit_unlock(struct pipe_screen *s) { assert(s && locked); locked=0; }
static int ps5_resource_gpu_info(struct pipe_resource *r, void **p, size_t *n, size_t *a) {
    assert(!locked); /* The real function drains graphics under the same mutex. */
    if (!r) return -1;
    if (p) *p=r->data;
    if (n) *n=r->size;
    if (a) *a=r->allocation_size;
    return 0;
}
static void runtime_require_retirement(int done) { assert(done); }

static int64_t sceKernelGetDirectMemorySize(void) { return direct_size; }
static int sceKernelAllocateDirectMemory(int64_t a, int64_t b, size_t n, size_t align,
                                         int type, int64_t *p) {
    assert(locked && !a && b>0 && n<=(uint64_t)b && !(n&0x3fff) && align==0x4000 && type==12);
    if (fail_alloc) return -1;
    *p=1; ++allocations; return 0;
}
static int sceKernelMapDirectMemory(void **p, size_t n, int prot, int flags,
                                    int64_t phys, size_t align) {
    assert(prot==0x33 && !flags && phys==1);
    if (fail_map) return -1;
    int rc=posix_memalign(p, align, n);
    if (!rc) { mapped_memory=*p; mapped_size=n; }
    return rc;
}
static int mock_munmap(void *p, size_t n) {
    assert(locked && p==mapped_memory && n==mapped_size);
    free(p); mapped_memory=NULL; mapped_size=0; return 0;
}
#define munmap mock_munmap
static int sceKernelReleaseDirectMemory(int64_t p, size_t n) {
    assert(locked && p==1 && n && allocations); --allocations; return 0;
}
static void sceKernelUsleep(uint32_t n) { assert(n==1000); }
static unsigned runtime_completion_pause(int64_t *deadline) { (void)deadline; sceKernelUsleep(1000); return 1; }
static void flush_gpu_data(const void *p, size_t n) {
    assert(locked && p && n); ++flushes;
    if(p==watched_resource) { assert(n==watched_allocation); ++watched_flushes; }
}
static uint8_t command_out_of_space(agc_command_buffer_t *c, uint32_t n, void *p) {
    (void)c; (void)n; (void)p; out_of_space=1; return 0;
}
static uint32_t *emit(void *p) {
    agc_command_buffer_t *c=p;
    assert(locked && c->up+1<c->top);
    if (fail_emit) return NULL;
    return c->up++;
}
static int init(uint32_t n) { assert(locked && n==8); return 0; }
static int create(void **p, void *header, void *code) {
    assert(locked && code);
    *(void **)((uint8_t *)header+32)=(uint8_t *)header+96;
    *p=header; return 0;
}
static uint32_t *set_sh(void *p, const void *r, uint32_t n) { assert(r && n==8); return emit(p); }
static uint32_t *set_direct(void *p, uint32_t off, const uint32_t *data, uint32_t n) {
    if (off==0x218) { assert(n==1); saved_tmpring=*data; return emit(p); }
    assert(off==0x240 && n<=16); userdata_count=n;
    memcpy(saved_userdata, data, n*4); return emit(p);
}
uint32_t *sceAgcDcbAcquireMem(void *p, uint8_t engine, uint32_t coher, uint32_t gcr,
                              uint64_t address, uint64_t n, uint32_t poll) {
    assert(!engine && !coher && gcr==0x4380 && address && n && poll==0xa0);
    if(address==(uintptr_t)watched_resource) { assert(n==watched_allocation); ++watched_acquires; }
    return emit(p);
}
uint32_t *sceAgcCbDispatch(void *p, uint32_t x, uint32_t y, uint32_t z, uint32_t modifier) {
    assert(x && y && z && modifier==0x8000);
    saved_groups[0]=x; saved_groups[1]=y; saved_groups[2]=z;
    ++dispatches; return emit(p);
}
static uint32_t *release(void *p, uint8_t a, int16_t g, uint64_t s, int8_t d, void *dest,
                         uint32_t select, uint64_t data, uint16_t i, uint16_t c, int8_t e, int32_t r) {
    assert(a==40 && g==0x30c && !s && !d && dest && select==2 && !i && !c && e==3 && !r);
    saved_marker=dest; saved_expected=data; return emit(p);
}
static int submit(void *p) {
    agc_submit_description_t *s=p;
    assert(locked && s->words && s->word_count && saved_marker && !*saved_marker && flushes>=3);
    assert(saved_tmpring==expected_tmpring);
    if (expect_private || expect_scratch) {
        assert(((saved_userdata[1]>>31)!=0)==expect_scratch);
        uintptr_t address=saved_userdata[0] |
            ((uint64_t)(saved_userdata[1]&0x7fffffffu)<<32);
        assert(address==(uintptr_t)mapped_memory+0xc000);
        assert(mapped_size>0x10000);
        const size_t size=mapped_size-0x10000;
        const uint32_t *before=(const uint32_t *)(address-0x4000);
        const uint32_t *after=(const uint32_t *)(address+size);
        for(unsigned i=0;i<0x4000/4;++i) assert(before[i]==0xa5a5a5a5 && after[i]==0xa5a5a5a5);
        if (expect_private) memset((void *)address,0x37,size);
    } else assert(!saved_userdata[0] && !saved_userdata[1]);
    *saved_marker=saved_expected; ++submissions; return 0;
}
static int suspend_point(void) { assert(locked); return 0; }
static int load_apis(void *a, void *b, void *c, agc_api_t *api, video_api_t *v) {
    (void)v; assert(!a && !b && !c && locked);
    *api=(agc_api_t){.init=init, .create_shader=create, .set_sh=set_sh, .set_sh_direct=set_direct,
        .release_mem=release, .submit=submit, .suspend_point=suspend_point};
    return 0;
}
'''
code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler/nir/nir_builder.h"
#include "ps5_agc_package.h"
''' + types + mock + parsers + release + compute + "\n" + probe + r'''
static void submission_contract(PsbcShaderOutput *out) {
    expect_private=out->metadata.compute_private_stride!=0;
    expect_scratch=out->metadata.scratch_buffer_backed;
    expected_tmpring=expect_scratch ? PS5_AGC_COMPUTE_SCRATCH_WAVES |
        ((out->metadata.scratch_bytes_per_wave|1024u)/1024u<<12) : 0;
    _Alignas(16) uint32_t table_data[31*4+8*8+8*12]={0}, output[32]={0};
    const size_t logical_output=64;
    table_data[0]=(uintptr_t)output; table_data[1]=(uintptr_t)output>>32;
    table_data[2]=logical_output; table_data[3]=0x31016fac;
    memcpy(table_data+30*4,table_data,16); /* Highest UBO uses the same owned range. */
    struct pipe_screen screen={0};
    struct pipe_resource table={table_data, sizeof(table_data), false, sizeof(table_data)};
    struct pipe_resource buffer={output, logical_output, false, sizeof(output)};
    struct pipe_resource *buffers[]={&buffer};
    uint32_t groups[3]={2,3,4};
    out->metadata.address32_hi=(uintptr_t)table_data>>32;
    const unsigned old_submits=submissions;
    if (out->metadata.scratch_valid && !expect_scratch) {
        const unsigned old_dispatches=dispatches, old_flushes=flushes;
        assert(ps5_agc_compute_execute(&screen, out, &table, buffers, 1, groups)<0);
        assert(!locked && !allocations && !mapped_memory && submissions==old_submits);
        assert(dispatches==old_dispatches && flushes==old_flushes);
        return; /* Known native MEMVIOL: host success must not enable execution. */
    }
    watched_resource=output; watched_allocation=sizeof(output);
    watched_flushes=watched_acquires=0;
    if (expect_scratch) direct_size=64*1024*1024;
    assert(ps5_agc_compute_execute(&screen, out, &table, buffers, 1, groups)==0);
    assert(watched_flushes==2 && watched_acquires==1);
    watched_resource=NULL;
    assert(!locked && !allocations && submissions==old_submits+1);
    assert(userdata_count==out->metadata.user_sgpr_count);
    assert(saved_tmpring==expected_tmpring);
    assert(saved_userdata[2]==(uint32_t)(uintptr_t)table_data && !memcmp(saved_groups, groups, 12));
    if (out->metadata.compute_grid_size_valid) assert(!memcmp(saved_userdata+3, groups, 12));
    if (expect_scratch) {
        direct_size=1024*1024; expect_scratch=false; expected_tmpring=0;
        return;
    }
    const unsigned old_dispatches=dispatches;
    for (unsigned fault=0; fault<21; ++fault) {
        if (fault==0) groups[0]=0;
        if (fault==1) table.size=16;
        if (fault==2) table_data[2]=logical_output+1; /* Within allocation, outside logical range. */
        if (fault==3) table_data[3]=0;
        if (fault==4) out->metadata.address32_hi++;
        if (fault==5) fail_map=1;
        if (fault==6) fail_emit=1;
        if (fault==7) fail_alloc=1;
        if (fault==8) direct_size=0;
        if (fault==9) direct_size=-1;
        if (fault==10) direct_size=0x4000; /* Reject an arena larger than the heap. */
        if (fault==11) table_data[30*4+2]=logical_output+1;
        if (fault==12) table.size=31*16-1;
        if (fault==13) buffer.allocation_size=0;
        if (fault==14) buffer.allocation_size=logical_output-1;
        if (fault==15) buffer.allocation_size=(size_t)UINT32_MAX+1;
        if (fault==16) buffer.size=0;
        if (fault==17) table.allocation_size=0;
        if (fault==18) table.allocation_size=table.size-1;
        if (fault==19) table.allocation_size=(size_t)UINT32_MAX+1;
        if (fault==20) table_data[0]=(uintptr_t)output+logical_output; /* Padding is not an owned SSBO range. */
        assert(ps5_agc_compute_execute(&screen, out, &table, buffers, 1, groups)<0);
        assert(!locked && !allocations && submissions==old_submits+1 && dispatches==old_dispatches);
        groups[0]=2; table.size=table.allocation_size=sizeof(table_data);
        buffer.size=logical_output; buffer.allocation_size=sizeof(output);
        table_data[0]=(uintptr_t)output; table_data[2]=logical_output; table_data[3]=0x31016fac;
        out->metadata.address32_hi=(uintptr_t)table_data>>32; fail_map=fail_emit=fail_alloc=0;
        direct_size=1024*1024;
        memcpy(table_data+30*4,table_data,16);
    }
    const PsbcShaderMetadata saved=out->metadata;
    out->metadata.descriptor_binding_count=3;
    out->metadata.descriptor_bindings[2]=(PsbcDescriptorBinding){
        .binding=PSBC_GALLIUM_IMAGE_ARRAY_BINDING(PSBC_STAGE_COMPUTE),
        .type=PSBC_DESCRIPTOR_STORAGE_IMAGE,.array_size=8,.stride=32,.offset=31*16};
    _Alignas(256) uint32_t pixels[192]={0};
    struct pipe_resource image={pixels,sizeof(pixels),true,sizeof(pixels)};
    uint32_t expected[8]; assert(!ps5_resource_storage_image_descriptor(&image,0,expected));
    memcpy(table_data+31*4+7*8,expected,32);
    struct pipe_resource *all[PS5_AGC_COMPUTE_MAX_RESOURCES];
    for(unsigned i=0;i<PS5_AGC_COMPUTE_MAX_RESOURCES;++i) all[i]=i<31 ? &buffer : &image;
    assert(!ps5_agc_compute_execute(&screen,out,&table,all,39,groups));
    const unsigned after=submissions;
    for(unsigned word=0;word<8;++word) {
        table_data[31*4+7*8+word]^=1;
        assert(ps5_agc_compute_execute(&screen,out,&table,all,39,groups)<0);
        assert(!allocations && !locked && submissions==after);
        table_data[31*4+7*8+word]^=1;
    }
    assert(ps5_agc_compute_execute(&screen,out,&table,all,PS5_AGC_COMPUTE_MAX_RESOURCES+1,groups)<0);
    assert(ps5_agc_compute_execute(&screen,out,&table,buffers,1,groups)<0); /* Image not owned. */
    assert(!allocations && !locked && submissions==after);
    out->metadata.descriptor_binding_count=4;
    out->metadata.descriptor_bindings[3]=(PsbcDescriptorBinding){.binding=7,
        .type=PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,.array_size=1,.stride=48,
        .offset=31*16+8*32+7*48};
    uint32_t *sampled=table_data+31*4+8*8+7*12;
    memcpy(sampled,expected,32);
    assert(!ps5_agc_compute_execute(&screen,out,&table,all,47,groups));
    for(unsigned wrap=0;wrap<3;++wrap) for(unsigned filters=0;filters<4;++filters) {
        sampled[8]=wrap*0x49;
        sampled[10]=((filters&1)!=0)<<20 | ((filters&2)!=0)<<22;
        assert(!ps5_agc_compute_execute(&screen,out,&table,all,47,groups));
    }
    sampled[8]=0x88; sampled[10]=0;
    assert(!ps5_agc_compute_execute(&screen,out,&table,all,47,groups));
    sampled[8]=5u<<9 | 2u<<16 | 5u<<21;
    assert(ps5_agc_compute_execute(&screen,out,&table,all,47,groups)<0);
    sampled[8]=0;
    sampled[9]=0x300000; sampled[10]=0x08500000;
    assert(!ps5_agc_compute_execute(&screen,out,&table,all,47,groups));
    const uint32_t bad_lods[]={0x1000000,0xf01000,0xf01,0x100};
    for(unsigned i=0;i<4;++i) {
        sampled[9]=bad_lods[i];
        assert(ps5_agc_compute_execute(&screen,out,&table,all,47,groups)<0);
    }
    sampled[9]=0; sampled[10]=0x0c000000;
    assert(ps5_agc_compute_execute(&screen,out,&table,all,47,groups)<0);
    sampled[8]=sampled[9]=sampled[10]=0;
    sampled_msaa=true;
    assert(!ps5_resource_sampled_image_descriptor(&image,0,0,sampled));
    assert(!ps5_agc_compute_execute(&screen,out,&table,all,47,groups));
    for(unsigned word=0;word<8;++word) {
        sampled[word]^=word==3 ? 1u<<12 : 1u;
        assert(ps5_agc_compute_execute(&screen,out,&table,all,47,groups)<0);
        sampled[word]^=word==3 ? 1u<<12 : 1u;
    }
    sampled_msaa=false;
    memcpy(sampled,expected,32);
    for(unsigned channel=0;channel<4;++channel) for(unsigned selector=0;selector<8;++selector) {
        sampled[3]=(expected[3]&~(7u<<(channel*3)))|(selector<<(channel*3));
        int result=ps5_agc_compute_execute(&screen,out,&table,all,47,groups);
        assert((selector==2 || selector==3) ? result<0 : result==0);
    }
    memcpy(sampled,expected,32);
    const unsigned sampled_submissions=submissions;
    for(unsigned word=0;word<12;++word) {
        if(word==8) continue; /* Independent wrap bits have multiple valid values. */
        sampled[word]^=word==3 ? 1u<<12 : 1u;
        assert(ps5_agc_compute_execute(&screen,out,&table,all,47,groups)<0);
        assert(!allocations && !locked && submissions==sampled_submissions);
        sampled[word]^=word==3 ? 1u<<12 : 1u;
    }
    memset(sampled,0,48);
    sampled[0]=(uintptr_t)output;
    sampled[1]=((uintptr_t)output>>32)|(4u<<16);
    sampled[2]=logical_output/4;
    sampled[3]=0x01015fac;
    assert(!ps5_agc_compute_execute(&screen,out,&table,all,47,groups));
    sampled[2]++;
    assert(ps5_agc_compute_execute(&screen,out,&table,all,47,groups)<0);
    memset(table_data+31*4,0,8*32);
    assert(ps5_agc_compute_execute(&screen,out,&table,buffers,1,groups)<0);
    assert(!allocations && !locked && submissions==sampled_submissions+1);
    out->metadata=saved;
}
static const PsbcCompileOptions opts = {
    .target=PSBC_TARGET_PS5, .stage=PSBC_STAGE_COMPUTE, .optimise=true,
    .address32_hi=2, .gallium_buffer_arrays=true, .descriptor_binding_count=3,
    .descriptor_bindings={{.binding=PSBC_GALLIUM_SSBO_ARRAY_BINDING(PSBC_STAGE_COMPUTE),
        .type=PSBC_DESCRIPTOR_STORAGE_BUFFER, .array_size=16, .stride=16},
        {.binding=PSBC_GALLIUM_UBO_ARRAY_BINDING(PSBC_STAGE_COMPUTE),
        .type=PSBC_DESCRIPTOR_UNIFORM_BUFFER, .array_size=15, .stride=16, .offset=16*16},
        {.binding=PSBC_GALLIUM_IMAGE_ARRAY_BINDING(PSBC_STAGE_COMPUTE),
        .type=PSBC_DESCRIPTOR_STORAGE_IMAGE, .array_size=8, .stride=32, .offset=31*16}},
};
static nir_shader *shader(bool grid, bool shared) {
    nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
        psbc_get_nir_options(PSBC_STAGE_COMPUTE), "native-compute-contract");
    b.shader->info.workgroup_size[0]=16;
    b.shader->info.workgroup_size[1]=b.shader->info.workgroup_size[2]=1;
    b.shader->info.num_ssbos=16;
    nir_def *id=nir_channel(&b, nir_load_local_invocation_id(&b), 0);
    nir_def *value=nir_iadd_imm(&b, nir_imul_imm(&b, id, 3), 17);
    if (grid) value=nir_iadd(&b, value, nir_channel(&b, nir_load_num_workgroups(&b), 0));
    if (shared) {
        b.shader->info.shared_size=64;
        nir_store_shared(&b, value, nir_imul_imm(&b, id, 4), .align_mul=4, .write_mask=1);
        nir_barrier(&b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
            .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);
        value=nir_load_shared(&b, 1, 32,
            nir_imul_imm(&b, nir_ixor(&b, id, nir_imm_int(&b, 1)), 4), .align_mul=4);
    }
    nir_store_ssbo(&b, value, nir_imm_int(&b, 0), nir_imul_imm(&b, id, 4),
        .align_mul=4, .write_mask=1);
    nir_validate_shader(b.shader, "compute metadata input");
    return b.shader;
}
static void reject_package(const PsbcShaderOutput *out) {
    uint8_t *package=(uint8_t *)(uintptr_t)1;
    size_t size=1;
    assert(ps5_agc_package_build(out, 0, &package, &size)<0);
    assert(!package && !size);
}
static void compiled(bool grid, bool shared) {
    nir_shader *nir=shader(grid, shared);
    PsbcShaderOutput out={0};
    assert(psbc_compile_nir(nir, &opts, &out)==PSBC_RESULT_OK);
    PsbcShaderMetadata *m=&out.metadata;
    assert(m->version==PSBC_SHADER_METADATA_VERSION && m->hardware_stage==PSBC_HW_STAGE_COMPUTE);
    assert(m->shader_register_count==8 && !m->context_register_count && !m->linkage_valid);
    assert(m->compute_wave_size==32 && m->compute_workgroup_size[0]==16);
    assert(m->compute_lds_bytes==(shared ? 1024 : 0));
    assert(m->compute_grid_size_valid==grid && m->user_sgpr_count==(grid ? 6 : 3));
    assert(!grid || m->compute_grid_size_user_data_dword==3);
    assert(m->descriptor_set0_valid && m->descriptor_set0_user_data_dword==2);
    uint8_t *package=NULL;
    size_t size=0;
    assert(ps5_agc_package_build(&out, 0, &package, &size)==0);
    uint64_t sections, header_at;
    memcpy(&sections, package+40, 8);
    memcpy(&header_at, package+sections+2*64+24, 8);
    const uint8_t *header=package+header_at;
    assert(header[90]==0 && header[91]==0 && header[92]==8);
    assert(!memcmp(header+96, m->shader_registers, 8*sizeof(PsbcRegisterWrite)));
    free(package);
    const PsbcShaderMetadata good=*m;
    for (unsigned fault=0; fault<17; ++fault) {
        *m=good;
        switch (fault) {
        case 0: m->compute_workgroup_size[0]=0; break;
        case 1: m->compute_workgroup_size[0]=1025; break;
        case 2: m->compute_wave_size=16; break;
        case 3: m->compute_lds_bytes=65537; break;
        case 4: m->shader_registers[5].value++; break;
        case 5: m->shader_registers[4].offset=0x229; break;
        case 6: m->user_sgpr_count=17; break;
        case 7: m->descriptor_set0_user_data_dword=1; break;
        case 8: m->compute_grid_size_valid=true; m->compute_grid_size_user_data_dword=UINT32_MAX; break;
        case 9: m->compute_grid_size_valid=true; m->compute_grid_size_user_data_dword=2; break;
        case 10: m->shader_registers[3].value|=1; break; /* Hidden scratch request. */
        case 11: m->scratch_valid=true; break;
        case 12: m->shader_registers[0].value=256; break;
        case 13: m->source_stage=PSBC_STAGE_VERTEX; break;
        case 14: m->descriptor_set0_valid=false; break;
        case 15: m->compute_lds_bytes=1; break;
        case 16: m->compute_lds_bytes=shared ? 0 : 1024; break;
        }
        reject_package(&out);
    }
    *m=good;
    submission_contract(&out);
    printf("CS metadata: grid=%u shared=%u code=%zu LDS=%u userdata=%u package valid\n",
        grid, shared, out.machine_code_size, m->compute_lds_bytes, m->user_sgpr_count);
    psbc_free_output(&out); ralloc_free(nir);
}
static void shapes(void) {
    for (unsigned fault=0; fault<5; ++fault) {
        nir_shader *nir=shader(false, false);
        if (fault==0) nir->info.workgroup_size[1]=0;
        if (fault==1) nir->info.workgroup_size[0]=1025;
        if (fault==2) nir->info.workgroup_size[1]=65;
        if (fault==3) nir->info.workgroup_size_variable=true;
        if (fault==4) nir->info.shared_size=65537;
        PsbcShaderOutput out={0};
        assert(psbc_compile_nir(nir, &opts, &out)==PSBC_RESULT_COMPILE_NIR);
        assert(!out.machine_code && !out.data);
        ralloc_free(nir);
    }
}
static void native_cases(void) {
    for (unsigned test=0; test<BUFFER_SPILL; ++test) {
        nir_shader *nir=create_probe_shader(test);
        PsbcShaderOutput out={0};
        assert(psbc_compile_nir(nir, &opts, &out)==PSBC_RESULT_OK);
        assert(out.metadata.compute_wave_size==32);
        assert(out.metadata.scratch_valid==(test==SCRATCH || test==SCRATCH_GRID));
        assert(out.metadata.scratch_size_per_thread==(test==SCRATCH ? 16 : test==SCRATCH_GRID ? 128 : 0));
        assert(!memcmp(out.metadata.compute_workgroup_size, cases[test].local, sizeof(cases[test].local)));
        assert(out.metadata.compute_lds_bytes==(test==SHARED ? 1024 : test==LIMIT_SHARED ? 32768 : 0));
        assert(out.metadata.compute_grid_size_valid==(test==GRID || test==INDIRECT));
        uint8_t *package=NULL;
        size_t size=0;
        const int package_rc=ps5_agc_package_build(&out, 0, &package, &size);
        assert(package_rc==(out.metadata.scratch_valid ? -7 : 0));
        if (out.metadata.scratch_valid) assert(!package && !size);
        free(package);
        uint32_t output[OUTPUT_WORDS];
        for (unsigned i=0; i<OUTPUT_WORDS; ++i) output[i]=GUARD_WORD;
        for (unsigned i=0; i<cases[test].words; ++i) {
            output[i]=17+3*(test==SHARED ? (i^32) : i);
            if(test==COUNTER_SEQUENCE) {
                const uint32_t values[]={100,100,100,99,99,103,50,200,8,24,27,7,42};
                output[i]=values[i];
            }
            if(test==LIMIT_SHARED) output[i]=8*(17+3*(i^512))+28;
            if (test==GRID || test==INDIRECT) output[i]+=73;
            if (test==INDIRECT_ARGS) output[i]=i==1 ? 3 : 2;
            if (test==IMAGE_LOAD) output[i]+=100;
            if (test==IMAGE_SIZE) output[i]=317;
            if (test==IMAGE_BANK_STORE || test==IMAGE_BANK_LOAD)
                output[i]=1000*(i/16)+17+3*(i%16)+(test==IMAGE_BANK_LOAD ? 100 : 0);
            if (test==IMAGE_ATOMIC) output[i]=7017+63-i;
            if (test==IMAGE_SINT_ATOMIC) output[i]=(uint32_t)(-1063+(int)i);
            if (test==IMAGE_SINT_STORE || test==IMAGE_SINT_LOAD)
                output[i]=(uint32_t)(-1000+7*(int)i-(test==IMAGE_SINT_LOAD ? 100 : 0));
            if (test==IMAGE_FLOAT_STORE || test==IMAGE_FLOAT_LOAD) {
                float value=((int)i-8)/4.0f;
                if(test==IMAGE_FLOAT_LOAD) value=value*2+0.125f;
                memcpy(&output[i],&value,sizeof(value));
            }
            if (test==BUFFER_RANGES || test==BUFFER_ALIAS) {
                output[i]=1000+101*(i/4)+7*(i%4);
                if (test==BUFFER_ALIAS) output[i]=5*output[i]+2;
            }
            if (test==UBO_RANGES) {
                unsigned ubo=1000+101*(14-i/4)+7*(i%4), ssbo=1000+101*(i/4)+7*(i%4);
                output[i]=ubo+3*ssbo;
            }
            if (test==UBO_COPY) output[i]=700+11*i;
            if (test==ATOMIC) output[i]=i<256 ? 255-i : 256;
            if (test==FP32) output[i]=0;
            if (test==FP64) {
                double x=(i/2+1)*0.25, square=x*x;
                uint64_t bits;
                memcpy(&bits, &square, sizeof(bits));
                output[i]=bits>>(32*(i&1));
            }
        }
        assert(count_correct(test, output)==cases[test].words);
        if(test==IMAGE_STORE || test==IMAGE_BANK_STORE || test==IMAGE_ATOMIC ||
           test==IMAGE_SINT_STORE || test==IMAGE_SINT_ATOMIC || test==IMAGE_FLOAT_STORE) {
            uint32_t pixels[IMAGE_WORDS];
            for(unsigned i=0;i<IMAGE_WORDS;++i) pixels[i]=i>=65 && i<81 ?
                (test>=IMAGE_BANK_STORE ? 7000 : 0)+17+3*(i-65) : GUARD_WORD;
            if(test==IMAGE_ATOMIC) pixels[65]+=64;
            for(unsigned i=65;i<81;++i) {
                if(test==IMAGE_SINT_STORE || test==IMAGE_SINT_ATOMIC)
                    pixels[i]=(uint32_t)(-1000+7*((int)i-65)-(test==IMAGE_SINT_ATOMIC && i==65 ? 64 : 0));
                if(test==IMAGE_FLOAT_STORE) {
                    float value=((int)i-73)/4.0f;
                    memcpy(&pixels[i],&value,sizeof(value));
                }
            }
            assert(count_image_correct(test,7,pixels)==IMAGE_WORDS);
            pixels[0]=17; assert(count_image_correct(test,7,pixels)==IMAGE_WORDS-1);
            pixels[65]=0; assert(count_image_correct(test,7,pixels)==IMAGE_WORDS-2);
        }
        output[0]=UINT32_MAX;
        assert(count_correct(test, output)==cases[test].words-1);
        if(test==IMAGE_ATOMIC || test==IMAGE_SINT_ATOMIC) {
            output[0]=output[1]; assert(count_correct(test,output)==63);
        }
        if (test==ATOMIC) {
            output[0]=output[1]; /* A duplicated return value must fail. */
            assert(count_correct(test, output)==256);
            output[256]=257;
            assert(count_correct(test, output)==255);
        }
        printf("Native probe compiled/package/oracle: %s code=%zu LDS=%u package=%d\n",
            cases[test].name, out.machine_code_size, out.metadata.compute_lds_bytes, package_rc);
        psbc_free_output(&out); ralloc_free(nir);
    }
}
static void atomic_counter_lowering(void) {
    const nir_intrinsic_op operations[]={nir_intrinsic_atomic_counter_inc,
        nir_intrinsic_atomic_counter_pre_dec, nir_intrinsic_atomic_counter_post_dec,
        nir_intrinsic_atomic_counter_read, nir_intrinsic_atomic_counter_add,
        nir_intrinsic_atomic_counter_min, nir_intrinsic_atomic_counter_max,
        nir_intrinsic_atomic_counter_and, nir_intrinsic_atomic_counter_or,
        nir_intrinsic_atomic_counter_xor, nir_intrinsic_atomic_counter_exchange,
        nir_intrinsic_atomic_counter_comp_swap};
    for (unsigned binding=0; binding<=7; binding+=7) {
        for (unsigned operation=0; operation<ARRAY_SIZE(operations); ++operation) {
            nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
                psbc_get_nir_options(PSBC_STAGE_COMPUTE), "atomic-counter-lowering");
            b.shader->info.workgroup_size[0]=16;
            b.shader->info.workgroup_size[1]=b.shader->info.workgroup_size[2]=1;
            b.shader->info.num_ssbos=8; b.shader->info.num_abos=1;
            nir_variable *counter=nir_variable_create(b.shader, nir_var_uniform,
                glsl_atomic_uint_type(), "counter");
            counter->data.binding=binding; counter->data.explicit_binding=true;
            nir_intrinsic_instr *atomic=nir_intrinsic_instr_create(b.shader, operations[operation]);
            for (unsigned source=0; source<nir_intrinsic_infos[operations[operation]].num_srcs; ++source)
                atomic->src[source]=nir_src_for_ssa(nir_imm_int(&b, source ? source+3 : 12));
            nir_intrinsic_set_base(atomic, binding);
            nir_intrinsic_set_range_base(atomic, 16);
            nir_def_init(&atomic->instr, &atomic->def, 1, 32);
            nir_builder_instr_insert(&b, &atomic->instr);
            nir_store_ssbo(&b, &atomic->def, nir_imm_int(&b, 0), nir_imm_int(&b, 0),
                .align_mul=4, .write_mask=1);
            assert(nir_lower_atomics_to_ssbo(b.shader, 0));
            assert(!b.shader->info.num_abos && b.shader->info.num_ssbos==9+binding);
            nir_opt_constant_folding(b.shader);
            unsigned lowered=0;
            nir_foreach_function_impl(impl, b.shader) nir_foreach_block(block, impl)
                nir_foreach_instr(instr, block) {
                    if(instr->type!=nir_instr_type_intrinsic) continue;
                    nir_intrinsic_instr *intr=nir_instr_as_intrinsic(instr);
                    for(unsigned i=0;i<ARRAY_SIZE(operations);++i) assert(intr->intrinsic!=operations[i]);
                    if(intr->intrinsic==nir_intrinsic_ssbo_atomic ||
                       intr->intrinsic==nir_intrinsic_ssbo_atomic_swap || intr->intrinsic==nir_intrinsic_load_ssbo) {
                        assert(nir_src_as_uint(intr->src[0])==8+binding);
                        assert(nir_src_as_uint(intr->src[1])==28); ++lowered;
                    }
                }
            assert(lowered==1);
            nir_validate_shader(b.shader, "atomic counters lowered to reserved SSBO bank");
            PsbcShaderOutput out={0};
            assert(psbc_compile_nir(b.shader, &opts, &out)==PSBC_RESULT_OK);
            assert(out.machine_code_size && !out.metadata.scratch_valid);
            printf("Atomic counter lowering: operation=%u binding=%u SSBO=%u PASS\n", operation,binding,8+binding);
            psbc_free_output(&out); ralloc_free(b.shader);
        }
    }
}
static int atomic_uniform_slots(const struct glsl_type *type, bool bindless) {
    return glsl_count_attribute_slots(type,bindless);
}
static void atomic_alignment_lowering(void) {
    const unsigned storage_counts[]={0,1,8};
    for(unsigned stage=0;stage<2;++stage) for(unsigned count=0;count<3;++count)
    for(unsigned binding=0;binding<=7;binding+=7) for(unsigned swap=0;swap<2;++swap) {
        mesa_shader_stage mesa_stage=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
        PsbcStage psbc_stage=stage ? PSBC_STAGE_FRAGMENT : PSBC_STAGE_COMPUTE;
        nir_builder b=nir_builder_init_simple_shader(mesa_stage,psbc_get_nir_options(psbc_stage),"atomic-aligned-state");
        if(!stage) for(unsigned i=0;i<3;++i) b.shader->info.workgroup_size[i]=1;
        b.shader->info.num_ssbos=storage_counts[count]; b.shader->info.num_abos=1;
        nir_variable *counter=nir_variable_create(b.shader,nir_var_uniform,glsl_atomic_uint_type(),"counter");
        counter->data.binding=binding; counter->data.explicit_binding=true;
        nir_intrinsic_instr *atomic=nir_intrinsic_instr_create(b.shader,
            swap ? nir_intrinsic_atomic_counter_comp_swap : nir_intrinsic_atomic_counter_inc);
        for(unsigned i=0;i<nir_intrinsic_infos[atomic->intrinsic].num_srcs;++i)
            atomic->src[i]=nir_src_for_ssa(nir_imm_int(&b,i ? i+3 : 12));
        nir_intrinsic_set_base(atomic,binding); nir_intrinsic_set_range_base(atomic,16);
        nir_def_init(&atomic->instr,&atomic->def,1,32); nir_builder_instr_insert(&b,&atomic->instr);
        assert(nir_lower_atomics_to_ssbo(b.shader,STATE_ATOMIC_COUNTER_OFFSET));
        assert(!b.shader->info.num_abos && b.shader->info.num_ssbos==storage_counts[count]+binding+1);
        unsigned states=0;
        nir_foreach_uniform_variable(var,b.shader) {
            assert(var->num_state_slots==1 && var->state_slots[0].tokens[0]==STATE_ATOMIC_COUNTER_OFFSET);
            assert(var->state_slots[0].tokens[1]==binding);
            var->data.driver_location=0; ++states;
        }
        assert(states==1); b.shader->num_uniforms=1;
        nir_lower_io(b.shader,nir_var_uniform,atomic_uniform_slots,0);
        nir_lower_uniforms_to_ubo(b.shader,false,false);
        nir_lower_uniforms_to_ubo(b.shader,false,false);
        assert(b.shader->info.num_ubos==1 && b.shader->info.first_ubo_is_default_ubo);
        nir_shader *evaluated=nir_shader_clone(NULL,b.shader);
        unsigned loads=0,atomics=0;
        nir_foreach_function_impl(impl,evaluated) {
            nir_builder eval=nir_builder_create(impl);
            nir_foreach_block(block,impl) nir_foreach_instr_safe(instr,block) {
                if(instr->type!=nir_instr_type_intrinsic) continue;
                nir_intrinsic_instr *intr=nir_instr_as_intrinsic(instr);
                if(intr->intrinsic==nir_intrinsic_load_ubo) {
                    assert(nir_src_as_uint(intr->src[0])==0);
                    eval.cursor=nir_before_instr(instr);
                    nir_def_replace(&intr->def,nir_imm_int(&eval,4)); ++loads;
                }
            }
        }
        assert(loads==1); nir_opt_constant_folding(evaluated);
        nir_foreach_function_impl(impl,evaluated) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
            if(instr->type!=nir_instr_type_intrinsic) continue;
            nir_intrinsic_instr *intr=nir_instr_as_intrinsic(instr);
            if(intr->intrinsic==nir_intrinsic_ssbo_atomic || intr->intrinsic==nir_intrinsic_ssbo_atomic_swap) {
                assert(nir_src_as_uint(intr->src[0])==storage_counts[count]+binding);
                assert(nir_src_as_uint(intr->src[1])==32); /* 12 + declared16 + alignment4. */
                assert(16+nir_src_as_uint(intr->src[1])==20+16+12); ++atomics;
            }
        }
        assert(atomics==1); ralloc_free(evaluated);
        PsbcCompileOptions options={.target=PSBC_TARGET_PS5,.stage=psbc_stage,.optimise=true,
            .address32_hi=2,.gallium_buffer_arrays=true,.descriptor_binding_count=2,
            .descriptor_bindings={{.binding=PSBC_GALLIUM_SSBO_ARRAY_BINDING(psbc_stage),
                .type=PSBC_DESCRIPTOR_STORAGE_BUFFER,.array_size=16,.stride=16},
                {.binding=PSBC_GALLIUM_UBO_ARRAY_BINDING(psbc_stage),.type=PSBC_DESCRIPTOR_UNIFORM_BUFFER,
                 .array_size=stage ? 13 : 15,.stride=16,.offset=stage ? 512 : 256}}};
        nir_validate_shader(b.shader,"aligned atomic counter with CB0 state");
        PsbcShaderOutput out={0};
        assert(psbc_compile_nir(b.shader,&options,&out)==PSBC_RESULT_OK && out.machine_code_size);
        assert(!out.metadata.scratch_valid);
        printf("Atomic alignment: stage=%u original-ssbos=%u binding=%u swap=%u CB0=4 address=48 PASS\n",
            stage,storage_counts[count],binding,swap);
        psbc_free_output(&out); ralloc_free(b.shader);
    }
}
#include "private_array_fixture.h"
#include "private_buffer_fixture.h"
static void private_memory_plan_contract(void) {
    const uint32_t local[3]={2,2,4}, grid[3]={2,3,5};
    struct ps5_agc_compute_memory_layout plan;
    assert(!ps5_agc_compute_plan_memory(676,0,0,local,grid,SIZE_MAX,&plan));
    assert(plan.allocation_size==0x8000 && !plan.private_offset && !plan.private_size);
    assert(!ps5_agc_compute_plan_memory(676,128,0,local,grid,SIZE_MAX,&plan));
    assert(plan.private_offset==0xc000 && plan.private_size==0x10000 &&
        plan.allocation_size==0x20000);
    assert(!ps5_agc_compute_plan_memory(676,0,1024,local,grid,SIZE_MAX,&plan));
    assert(plan.scratch_offset==0xc000 && plan.scratch_size==0x120000 &&
        plan.allocation_size==0x130000);
    assert(ps5_agc_compute_plan_memory(676,128,1024,local,grid,SIZE_MAX,&plan)<0);
    assert(ps5_agc_compute_plan_memory(676,0,1025,local,grid,SIZE_MAX,&plan)<0);
    for(unsigned stride=4;stride<=4096;stride*=2) {
        assert(!ps5_agc_compute_plan_memory(676,stride,0,local,grid,SIZE_MAX,&plan));
        const size_t required=plan.allocation_size;
        assert(plan.private_size>=16u*30u*stride && !(plan.private_size&0x3fff));
        assert(plan.private_offset+plan.private_size+0x4000==required);
        assert(!ps5_agc_compute_plan_memory(676,stride,0,local,grid,required,&plan));
        assert(ps5_agc_compute_plan_memory(676,stride,0,local,grid,required-1,&plan)<0);
        assert(!plan.allocation_size && !plan.private_offset && !plan.private_size);
    }
    for(unsigned bad=0;bad<13;++bad) {
        uint32_t l[3]={2,2,4}, g[3]={2,3,5};
        uint32_t stride=128; size_t code=676, budget=SIZE_MAX;
        if(bad<3) l[bad]=0;
        if(bad>=3 && bad<6) g[bad-3]=0;
        if(bad==6) l[0]=1024; /* Product, not just individual dimensions. */
        if(bad==7) g[2]=65536;
        if(bad==8) g[0]=g[1]=g[2]=65535; /* Byte-index overflow. */
        if(bad==9) stride=4097;
        if(bad==10) stride=2;
        if(bad==11) code=16u*1024u*1024u+4;
        if(bad==12) budget=0;
        plan=(struct ps5_agc_compute_memory_layout){1,2,3};
        assert(ps5_agc_compute_plan_memory(code,stride,0,l,g,budget,&plan)<0);
        assert(!plan.allocation_size && !plan.private_offset && !plan.private_size);
    }
    assert(ps5_agc_compute_plan_memory(0,128,0,local,grid,SIZE_MAX,&plan)<0);
    assert(ps5_agc_compute_plan_memory(675,128,0,local,grid,SIZE_MAX,&plan)<0);
    assert(ps5_agc_compute_plan_memory(676,128,0,NULL,grid,SIZE_MAX,&plan)<0);
    assert(ps5_agc_compute_plan_memory(676,128,0,local,NULL,SIZE_MAX,&plan)<0);
    assert(ps5_agc_compute_plan_memory(676,128,0,local,grid,SIZE_MAX,NULL)<0);
    puts("Private/scratch arena planning: alignment, guards, exact budgets, overflow/rejection PASS");
}
/* Host-only ABI experiment. Never package or submit this shader: production
 * metadata and runtime allocation do not yet describe this private buffer. */
static bool private_internal_intrinsic(nir_builder *b,nir_intrinsic_instr *intr,void *data) {
    bool store=intr->intrinsic==nir_intrinsic_store_scratch;
    if (!store && intr->intrinsic!=nir_intrinsic_load_scratch) return false;
    nir_def *value=store?intr->src[0].ssa:&intr->def;
    assert(value->bit_size==32 && value->num_components==1);
    b->cursor=nir_before_instr(&intr->instr);
    /* Pinned GFX10 compute ABI: first argument is the reserved two-word pair.
     * This is an ordinary address, not the native flat-scratch aperture. */
    nir_def *base=nir_pack_64_2x32(b,nir_load_scalar_arg_amd(b,2,.base=0));
    nir_def *offset=nir_iadd(b,nir_imul_imm(b,private_invocation_index(b),*(unsigned *)data),
        intr->src[store?1:0].ssa);
    nir_def *address=nir_iadd(b,base,nir_u2u64(b,offset));
    if (store)
        nir_store_global(b,value,address,.align_mul=4,.write_mask=1,.access=ACCESS_VOLATILE);
    else {
        nir_barrier(b,.memory_scope=SCOPE_INVOCATION,.memory_semantics=NIR_MEMORY_ACQ_REL,
            .memory_modes=nir_var_mem_global);
        nir_def_rewrite_uses(value,nir_load_global(b,1,32,address,.align_mul=4,
            .access=ACCESS_VOLATILE));
    }
    nir_instr_remove(&intr->instr);
    return true;
}
static void private_internal_contract(void) {
    const uint16_t shape[3]={2,2,4};
    nir_builder b=private_array_fixture_shape(32,shape);
    unsigned stride=b.shader->scratch_size;
    assert(nir_shader_intrinsics_pass(b.shader,private_internal_intrinsic,
        nir_metadata_control_flow,&stride));
    b.shader->scratch_size=0;
    nir_validate_shader(b.shader,"host-only internal private address");
    PsbcShaderOutput out={0};
    assert(psbc_compile_nir(b.shader,&opts,&out)==PSBC_RESULT_OK);
    assert(!out.metadata.scratch_valid && !out.metadata.scratch_bytes_per_wave &&
        !out.metadata.scratch_size_per_thread);
    assert(b.shader->info.num_ssbos==2 && out.metadata.compute_grid_size_valid);
    assert(out.metadata.descriptor_binding_count==opts.descriptor_binding_count &&
        !memcmp(out.metadata.descriptor_bindings,opts.descriptor_bindings,
            opts.descriptor_binding_count*sizeof(opts.descriptor_bindings[0])));
    if (getenv("PS5_SCRATCH_DISASSEMBLE")) {
        printf("PRIVATE_INTERNAL_BYTES ");
        const unsigned char *bytes=(const unsigned char *)out.machine_code;
        for(size_t i=0;i<out.machine_code_size;++i) printf("%02x",bytes[i]);
        putchar('\n');
    }
    printf("Internal private address host-only: code=%zu application-ssbos=2 scratch=0 native=NOT-RUN\n",
        out.machine_code_size);
    psbc_free_output(&out); ralloc_free(b.shader);
}
static void private_compiler_runtime_contract(void) {
    const uint16_t shape[3]={2,2,4};
    PsbcCompileOptions options=opts;
    options.compute_private_buffer=true;
    const unsigned native_sizes[]={4,32,1024};
    for(unsigned i=0;i<3;++i) {
        nir_builder native=private_array_fixture_shape(native_sizes[i],shape);
        PsbcShaderOutput checked={0};
        assert(psbc_compile_nir(native.shader,&options,&checked)==PSBC_RESULT_OK);
        assert(checked.metadata.compute_private_stride==native_sizes[i]*4 &&
            !checked.metadata.scratch_valid && !checked.metadata.scratch_bytes_per_wave);
        printf("Opt-in native candidate host compile: stride=%u code=%zu PASS\n",
            checked.metadata.compute_private_stride,checked.machine_code_size);
        psbc_free_output(&checked); ralloc_free(native.shader);
    }
    nir_builder b=private_array_fixture_shape(32,shape);
    PsbcShaderOutput out={0};
    assert(psbc_compile_nir(b.shader,&options,&out)==PSBC_RESULT_OK);
    assert(out.metadata.compute_private_stride==128 && !out.metadata.scratch_valid &&
        !out.metadata.scratch_size_per_thread && !out.metadata.scratch_bytes_per_wave);
    assert(b.shader->scratch_size==128); /* Compiler must not mutate caller NIR. */
    submission_contract(&out);
    uint8_t *package=NULL; size_t size=0;
    for(unsigned bad=0;bad<4;++bad) {
        PsbcShaderMetadata saved=out.metadata;
        if(bad==0) out.metadata.compute_private_stride=4097;
        if(bad==1) out.metadata.compute_private_stride=2;
        if(bad==2) out.metadata.compute_grid_size_valid=false;
        if(bad==3) out.metadata.scratch_buffer_backed=true;
        assert(ps5_agc_package_build(&out,0,&package,&size)<0 && !package && !size);
        out.metadata=saved;
    }
    psbc_free_output(&out); ralloc_free(b.shader);
    /* Valid NIR with unsupported private size/stage must fail without outputs. */
    for(unsigned bad=0;bad<3;++bad) {
        nir_builder rejected=private_array_fixture_shape(32,shape);
        PsbcCompileOptions invalid=options;
        if(bad==0) rejected.shader->scratch_size=8192;
        if(bad==1) rejected.shader->scratch_size=129;
        if(bad==2) invalid.target=PSBC_TARGET_PS4_BASE;
        assert(psbc_compile_nir(rejected.shader,&invalid,&out)!=PSBC_RESULT_OK);
        assert(!out.data && !out.machine_code && !out.metadata.compute_private_stride);
        ralloc_free(rejected.shader);
    }
    expect_private=false;
    puts("Opt-in private compiler/runtime: stride metadata, ordinary binding, guards, allocation/map/emit cleanup PASS (mock GPU)");
}
static void private_buffer_contract(void) {
    const unsigned sizes[]={4,8,16,32,64,256,1024};
    const uint16_t shapes[][3]={{16,1,1},{4,4,1},{2,2,4}};
    for (unsigned shape=0;shape<3;++shape)
    for (unsigned test=0; test<sizeof(sizes)/sizeof(sizes[0]); ++test) {
        const unsigned words=sizes[test];
        nir_builder b=private_array_fixture_shape(words,shapes[shape]);
        assert(b.shader->scratch_size==words*4);
        private_buffer_lower(b.shader);
        PsbcShaderOutput out={0};
        assert(psbc_compile_nir(b.shader,&opts,&out)==PSBC_RESULT_OK);
        assert(!out.metadata.scratch_valid && !out.metadata.scratch_bytes_per_wave);
        assert(out.metadata.compute_grid_size_valid);
        for (unsigned axis=0;axis<3;++axis)
            assert(out.metadata.compute_workgroup_size[axis]==shapes[shape][axis]);
        uint8_t *package=NULL; size_t size=0;
        assert(!ps5_agc_package_build(&out,0,&package,&size) && package && size);
        printf("Private buffer host-only: words=%u local=%ux%ux%u code=%zu package=PASS native=NOT-RUN\n",
            words,shapes[shape][0],shapes[shape][1],shapes[shape][2],out.machine_code_size);
        free(package); psbc_free_output(&out); ralloc_free(b.shader);
    }
}
/* Host-only feasibility check, not a runtime fallback or GPU correctness test. */
static void private_array_lowering(void) {
    for (unsigned words=4; words<=32; words*=2) for (unsigned optimize=0; optimize<2; ++optimize) {
        nir_builder b=private_array_fixture(words);
        PsbcCompileOptions options=opts;
        options.optimise=optimize;
        PsbcShaderOutput original={0}, lowered={0};
        assert(psbc_compile_nir(b.shader,&options,&original)==PSBC_RESULT_OK);
        assert(original.metadata.scratch_valid);
        reject_package(&original);
        private_array_lower(b.shader);
        assert(!b.shader->scratch_size);
        unsigned loads=0, stores=0;
        nir_foreach_function_impl(impl,b.shader) nir_foreach_block(block,impl)
            nir_foreach_instr(instr,block) {
                if (instr->type!=nir_instr_type_intrinsic) continue;
                nir_intrinsic_op op=nir_instr_as_intrinsic(instr)->intrinsic;
                assert(op!=nir_intrinsic_load_scratch && op!=nir_intrinsic_store_scratch);
                loads+=op==nir_intrinsic_load_ssbo;
                stores+=op==nir_intrinsic_store_ssbo;
            }
        assert(loads==1 && stores==1); /* Runtime input and observable output remain. */
        assert(psbc_compile_nir(b.shader,&options,&lowered)==PSBC_RESULT_OK);
        assert(lowered.machine_code_size && !lowered.metadata.scratch_valid &&
            !lowered.metadata.scratch_bytes_per_wave && !lowered.metadata.scratch_size_per_thread);
        assert(!(lowered.metadata.shader_registers[3].value&1));
        uint8_t *package=NULL; size_t size=0;
        assert(!ps5_agc_package_build(&lowered,0,&package,&size) && package && size);
        printf("Private array host-only: words=%u optimize=%u original=%zu lowered=%zu scratch=0 package=PASS\n",
            words,optimize,original.machine_code_size,lowered.machine_code_size);
        free(package); psbc_free_output(&original); psbc_free_output(&lowered); ralloc_free(b.shader);
    }
}
static void scratch_machine_code(const char *name, const PsbcShaderOutput *out) {
    if (!getenv("PS5_SCRATCH_DISASSEMBLE")) return;
    printf("SCRATCH_BYTES %s ",name);
    const unsigned char *bytes=(const unsigned char *)out->machine_code;
    for (size_t i=0;i<out->machine_code_size;++i) printf("%02x",bytes[i]);
    putchar('\n');
}
static void spill_contract(void) {
    nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
        psbc_get_nir_options(PSBC_STAGE_COMPUTE),"forced-register-spill");
    /* Multi-wave workgroup avoids ACO's single-wave LDS spill shortcut. */
    b.shader->info.workgroup_size[0]=64;
    b.shader->info.workgroup_size[1]=b.shader->info.workgroup_size[2]=1;
    b.shader->info.num_ssbos=2;
    nir_def *id=nir_channel(&b,nir_load_local_invocation_id(&b),0);
    nir_def *base=nir_imul_imm(&b,id,BUFFER_SPILL_VECTORS*16);
    nir_def *values[BUFFER_SPILL_VECTORS];
    for (unsigned i=0;i<BUFFER_SPILL_VECTORS;++i)
        values[i]=nir_load_ssbo(&b,4,32,nir_imm_int(&b,1),nir_iadd_imm(&b,base,i*16),
            .align_mul=16,.access=ACCESS_VOLATILE);
    nir_barrier(&b,.execution_scope=SCOPE_WORKGROUP,.memory_scope=SCOPE_DEVICE,
        .memory_semantics=NIR_MEMORY_ACQ_REL,.memory_modes=nir_var_mem_ssbo);
    for (unsigned i=0;i<BUFFER_SPILL_VECTORS;++i)
        nir_store_ssbo(&b,values[i],nir_imm_int(&b,0),nir_iadd_imm(&b,base,i*16),
            .align_mul=16,.write_mask=15,.access=ACCESS_VOLATILE);
    nir_validate_shader(b.shader,"forced-register-spill input");
    assert(!b.shader->scratch_size);
    PsbcShaderOutput out={0};
    PsbcCompileOptions spill_options=opts;
    spill_options.compute_private_buffer=true;
    spill_options.compute_buffer_spills=true;
    assert(psbc_compile_nir(b.shader,&spill_options,&out)==PSBC_RESULT_OK);
    printf("Register spill host-only: source-private=%u final-wave-bytes=%u code=%zu\n",
        b.shader->scratch_size,out.metadata.scratch_bytes_per_wave,out.machine_code_size);
    fflush(stdout);
    assert(out.metadata.scratch_valid && out.metadata.scratch_bytes_per_wave);
    assert(!out.metadata.scratch_size_per_thread); /* NIR private bytes exclude ACO spills. */
    assert(!out.metadata.compute_private_stride); /* Opt-in must not disguise ACO spills. */
    scratch_machine_code("buffer-register-spill",&out);
    submission_contract(&out);
    psbc_free_output(&out);

    b.shader->scratch_size=16;
    nir_store_scratch(&b,id,nir_imm_int(&b,0),.align_mul=4,.write_mask=1);
    nir_def *private_value=nir_load_scratch(&b,1,32,nir_imm_int(&b,0),
        .align_mul=4);
    nir_store_ssbo(&b,private_value,nir_imm_int(&b,0),nir_imm_int(&b,0),
        .align_mul=4,.write_mask=1,.access=ACCESS_VOLATILE);
    nir_validate_shader(b.shader,"private-plus-register-spill input");
    assert(psbc_compile_nir(b.shader,&spill_options,&out)==PSBC_RESULT_OK);
    assert(out.metadata.scratch_buffer_backed && out.metadata.scratch_bytes_per_wave &&
        !out.metadata.compute_private_stride && out.metadata.compute_lds_bytes>=1024);
    printf("Private plus register spill host-only: LDS=%u wave-bytes=%u code=%zu\n",
        out.metadata.compute_lds_bytes,out.metadata.scratch_bytes_per_wave,
        out.machine_code_size);
    fflush(stdout);
    submission_contract(&out);
    psbc_free_output(&out);

    ralloc_free(b.shader);
    const uint16_t oversized_shape[3]={64,1,1};
    nir_builder oversized=private_array_fixture_shape(512,oversized_shape);
    PsbcResult oversized_rc=psbc_compile_nir(oversized.shader,&spill_options,&out);
    assert(oversized_rc==PSBC_RESULT_COMPILE_NIR);
    assert(!out.data && !out.machine_code);
    puts("Private plus register spill boundary: global address conflict rejected PASS");
    ralloc_free(oversized.shader);

    nir_builder rejected=nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
        psbc_get_nir_options(PSBC_STAGE_COMPUTE),"buffer-spill-rejection");
    rejected.shader->info.workgroup_size[0]=1;
    rejected.shader->info.workgroup_size[1]=rejected.shader->info.workgroup_size[2]=1;
    rejected.shader->scratch_size=16;
    PsbcCompileOptions invalid=opts;
    invalid.compute_buffer_spills=true;
    assert(psbc_compile_nir(rejected.shader,&invalid,&out)==PSBC_RESULT_COMPILE_NIR);
    assert(!out.data && !out.machine_code);
    invalid.target=PSBC_TARGET_PS4_BASE;
    assert(psbc_compile_nir(rejected.shader,&invalid,&out)==PSBC_RESULT_UNSUPPORTED_STAGE);
    assert(!out.data && !out.machine_code);
    ralloc_free(rejected.shader);
}
static void private_width_contract(void) {
    const struct { unsigned components, bits; } cases[]={{4,8},{4,16},{4,32},{2,64}};
    PsbcCompileOptions options=opts;
    options.compute_private_buffer=true;
    for(unsigned i=0;i<4;++i) {
        nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_COMPUTE,
            psbc_get_nir_options(PSBC_STAGE_COMPUTE),"private-width-contract");
        b.shader->info.workgroup_size[0]=2;
        b.shader->info.workgroup_size[1]=2;
        b.shader->info.workgroup_size[2]=4;
        b.shader->info.num_ssbos=1;
        b.shader->scratch_size=1024;
        nir_def *id=private_invocation_index(&b);
        nir_def *offset=nir_imul_imm(&b,nir_iand_imm(&b,id,15),16);
        nir_def *value;
        if(cases[i].bits==64)
            value=nir_replicate(&b,nir_imm_int64(&b,INT64_C(0x1122334455667788)),
                cases[i].components);
        else if(cases[i].components==4)
            value=nir_imm_ivec4_intN(&b,0x11,0x22,0x33,0x44,cases[i].bits);
        else abort();
        nir_store_scratch(&b,value,offset,.align_mul=cases[i].bits/8,
            .write_mask=BITFIELD_MASK(cases[i].components));
        nir_def *loaded=nir_load_scratch(&b,cases[i].components,cases[i].bits,offset,
            .align_mul=cases[i].bits/8);
        nir_def *output=nir_bcsel(&b,nir_ball_iequal(&b,loaded,value),
            nir_imm_int(&b,1),nir_imm_int(&b,0));
        nir_store_ssbo(&b,output,nir_imm_int(&b,0),nir_imul_imm(&b,id,16),
            .align_mul=4,.write_mask=BITFIELD_MASK(output->num_components));
        nir_validate_shader(b.shader,"private width input");
        PsbcShaderOutput out={0};
        PsbcResult rc=psbc_compile_nir(b.shader,&options,&out);
        assert(rc==PSBC_RESULT_OK);
        assert(out.metadata.compute_private_stride==1024 && !out.metadata.scratch_valid &&
            !out.metadata.scratch_bytes_per_wave);
        printf("Opt-in private width: components=%u bits=%u code=%zu PASS\n",
            cases[i].components,cases[i].bits,out.machine_code_size);
        psbc_free_output(&out); ralloc_free(b.shader);
    }
}
static void scratch_contract(void) {
    nir_shader *nir=create_probe_shader(SCRATCH);
    PsbcShaderOutput out={0};
    assert(psbc_compile_nir(nir,&opts,&out)==PSBC_RESULT_OK);
    assert(out.metadata.scratch_valid && out.metadata.scratch_size_per_thread==16);
    assert(out.metadata.scratch_bytes_per_wave>=16*32 && !(out.metadata.scratch_bytes_per_wave&1023));
    assert(out.metadata.scratch_buffer_table_user_data_dword==0);
    assert(out.metadata.shader_registers[3].value&1); /* SCRATCH_EN */
    scratch_machine_code("private-array",&out);
    submission_contract(&out);
    const PsbcShaderMetadata good=out.metadata;
    for (unsigned fault=0; fault<8; ++fault) {
        out.metadata=good;
        switch (fault) {
        case 0: out.metadata.scratch_bytes_per_wave=0; break;
        case 1: out.metadata.scratch_bytes_per_wave=1025; break;
        case 2: out.metadata.scratch_bytes_per_wave=0x40000u*1024u; break;
        case 3: out.metadata.scratch_size_per_thread=good.scratch_bytes_per_wave/32+1; break;
        case 4: out.metadata.scratch_buffer_table_user_data_dword=2; break;
        case 5: out.metadata.scratch_valid=false; break;
        case 6: out.metadata.shader_registers[3].value&=~1u; break;
        case 7: out.metadata.compute_wave_size=0; break;
        }
        reject_package(&out);
    }
    out.metadata=good;
    printf("CS scratch execution BLOCKED: code=%zu wave-bytes=%u thread-bytes=%u userdata=%u rsrc2=%08x\n",
        out.machine_code_size,out.metadata.scratch_bytes_per_wave,out.metadata.scratch_size_per_thread,
        out.metadata.scratch_buffer_table_user_data_dword,out.metadata.shader_registers[3].value);
    psbc_free_output(&out); ralloc_free(nir);
}
int main(void) {
    psbc_init();
    private_memory_plan_contract();
    for (unsigned i=0; i<4; ++i) compiled(i&1, i&2);
    shapes(); native_cases(); atomic_counter_lowering(); atomic_alignment_lowering(); scratch_contract(); private_array_lowering(); private_buffer_contract(); private_internal_contract(); private_compiler_runtime_contract(); private_width_contract(); spill_contract(); compiled(false, false); psbc_shutdown();
}
'''
with tempfile.TemporaryDirectory() as directory:
    obj = str(Path(directory) / "compute.o")
    package_obj = str(Path(directory) / "package.o")
    executable = str(Path(directory) / "compute")
    compile_command = ["clang-18", "-std=gnu11", "-Wall", "-Werror",
        "-DHAVE_ENDIAN_H=1", "-DHAVE_FUNC_ATTRIBUTE_PACKED=1", "-DHAVE_PTHREAD=1",
        "-DHAVE_STRUCT_TIMESPEC=1", "-D_GNU_SOURCE",
        "-I", str(PSBC / "include/mesa"), "-I", str(PSBC / "include"),
        "-I", str(PSBC / "src"), "-I", str(PSBC / "libpsbc"),
        "-I", str(ROOT / "src/platform"), "-I", str(ROOT / "tests/ps5"),
        "-include", str(ROOT / "third_party/mesa-26.2.0/src/mesa/program/prog_statevars.h"),
        "-x", "c", "-c", "-o", obj, "-"]
    subprocess.run(["clang-18", "-std=c11", "-Wall", "-Werror",
        "-I", str(PSBC / "libpsbc"), "-c", str(ROOT / "src/platform/ps5_agc_package.c"),
        "-o", package_obj], check=True)
    for defines in ([], ["-DPS5_COMPUTE_PIPE_PROBE=1"]):
        subprocess.run(compile_command + defines, input=code, text=True, check=True)
        subprocess.run(["g++", "-o", executable, obj, package_obj, str(PSBC / "libpsbc.a"),
            "-pthread", "-lm"], check=True)
        if not os.environ.get("PS5_SCRATCH_DISASSEMBLE"):
            subprocess.run([executable], check=True, timeout=30)
            continue
        run = subprocess.run([executable], check=True, timeout=30, text=True, capture_output=True)
        decoded = set()
        internal_decoded = False
        for line in run.stdout.splitlines():
            if line.startswith("PRIVATE_INTERNAL_BYTES "):
                machine_code = bytes.fromhex(line.split()[1])
                # ACO emit_vop3_instruction fills unused RDNA sources with
                # inline zero (0x80); LLVM 18/21 reject that unused src2 on
                # this v_mul_lo_u32. Normalize ONLY this exact known encoding
                # in a decoder copy; never change the compiled shader bytes.
                padded_mul = bytes.fromhex("000069d58c040202")
                assert machine_code.count(padded_mul) == 1
                at = machine_code.index(padded_mul)
                assert at % 4 == 0
                decode_copy = machine_code[:at] + bytes.fromhex("000069d58c040200") + machine_code[at+8:]
                disasm = subprocess.run(["llvm-mc-18", "--disassemble", "--triple=amdgcn",
                    "--mcpu=gfx1030"], input=" ".join(f"0x{b:02x}" for b in decode_copy),
                    text=True, capture_output=True, check=True, timeout=30)
                assert not disasm.stderr.strip(), disasm.stderr
                assert "global_load" in disasm.stdout and "global_store" in disasm.stdout
                assert "scratch_" not in disasm.stdout and "s_setreg" not in disasm.stdout
                assert "s[0:1]" in disasm.stdout
                internal_decoded = True
                print("LLVM decode (one unused operand normalized in decoder copy): "
                      "internal private global loads/stores, no hardware scratch PASS")
                continue
            if not line.startswith("SCRATCH_BYTES "):
                print(line)
                continue
            _, name, hex_code = line.split()
            machine_code = bytes.fromhex(hex_code)
            disasm = subprocess.run(["llvm-mc-18", "--disassemble", "--triple=amdgcn",
                "--mcpu=gfx1030"], input=" ".join(f"0x{b:02x}" for b in machine_code),
                text=True, capture_output=True, check=True, timeout=30)
            assert not disasm.stderr.strip(), disasm.stderr
            if name == "buffer-register-spill":
                assert "buffer_load_dword" in disasm.stdout and "buffer_store_dword" in disasm.stdout
                assert "scratch_" not in disasm.stdout and "s_setreg_b32" not in disasm.stdout
            else:
                for operation in ("scratch_load", "scratch_store", "s_setreg_b32"):
                    assert operation in disasm.stdout, (name, operation)
            decoded.add(name)
            print(f"Independent LLVM decode: {name} bytes={len(machine_code)} "
                  f"sha256={hashlib.sha256(machine_code).hexdigest()} PASS")
            print("\n".join(disasm.stdout.splitlines()[:12]))
        assert decoded == {"private-array", "buffer-register-spill"}
        assert internal_decoded
print("PASS: compute package, grid/LDS metadata, malformed inputs; mocked submission lock/bounds/cleanup")
print("PASS: allocated-span pre/post flush and acquire; logical SSBO/UBO ownership retained; invalid allocation extents rejected")
