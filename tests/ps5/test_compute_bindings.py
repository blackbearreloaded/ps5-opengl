#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run actual Gallium CS binding/dispatch validation with native submission mocked."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MESA = ROOT / "third_party/mesa-26.2.0"
source = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
assert "#define PS5_COMPUTE_STORAGE_SLOTS PIPE_MAX_SHADER_BUFFERS" in source
assert "#define PS5_COMPUTE_IMAGE_SLOTS PS5_AGC_COMPUTE_MAX_IMAGES" in source
begin = source.index("static void\nps5_set_shader_buffers(")
functions = source[begin:source.index("\n#endif\n\nstatic void\nps5_lower_default_uniforms(", begin)]
setter_at = source.index("static void\nps5_set_constant_buffer(")
functions += source[setter_at:source.index("static void\nps5_set_vertex_buffers(", setter_at)]
sampler_setter_at = source.index("static void\nps5_set_sampler_views(")
functions += source[sampler_setter_at:source.index("static void *\nps5_create_sampler_state(", sampler_setter_at)]
sampler_at = source.index("static bool\nps5_texture_descriptor_wrap(")
bits_at = source.index("static uint32_t\nps5_float_bits(")
sampler_helpers = source[bits_at:source.index("static size_t\nps5_tiled_depth_layer_xor(", bits_at)]
sampler_helpers += source[sampler_at:source.index("static uint32_t\nps5_pack_float_12p4(", sampler_at)]
extent_at = source.index("static unsigned\nps5_linear_mip_storage_extent(")
extent_helper = source[extent_at:source.index("static bool\nps5_packed_depth_sample_layout(", extent_at)]
image_at = source.index("static int\nps5_resource_image_descriptor(")
image_descriptor = source[image_at:source.index("\nstruct pipe_resource *", image_at)]
texel_at = source.index("static bool\nps5_texel_buffer_descriptor(")
texel_descriptor = source[texel_at:source.index("\nstruct ps5_batch_flush_cache", texel_at)]
texel_format_at = source.index("static bool\nps5_texel_buffer_format(")
texel_format = source[texel_format_at:source.index("\nstatic bool\nps5_cube_texture_target", texel_format_at)]
tiled_at = source.index("static size_t\nps5_tiled_color_surface_size(")
tiled_helper = source[tiled_at:source.index("static uint32_t\nps5_color_target_info(", tiled_at)]
msaa_tile_at = source.index("static bool\nps5_tiled_color_msaa4_tile(")
msaa_tile_helper = source[msaa_tile_at:source.index("static size_t\nps5_tiled_color_msaa4_surface_size(", msaa_tile_at)]
array_at = source.index("static unsigned\nps5_storage_image_texel_size(")
array_layout = source[array_at:source.index("static unsigned\nps5_texture_format_size(", array_at)]
size_at = source.index("static unsigned\nps5_texture_format_size(")
size_helper = source[size_at:source.index("static bool\nps5_texture_descriptor_format(", size_at)]
format_at = source.index("static bool\nps5_integer_texture_format(")
linear_helpers = source[format_at:source.index("static bool\nps5_msaa4_color_format(", format_at)]
format_at = source.index("static bool\nps5_core_sampled_texture_format(")
linear_helpers += source[format_at:source.index("static bool\nps5_packed_vertex_format(", format_at)]
linear_at = source.index("static bool\nps5_linear_sampled_layout(")
linear_helpers += source[linear_at:source.index("static bool\nps5_color_render_target(", linear_at)]
for helper, following in (("ps5_cube_texture_target", "ps5_sampled_texture_target"),
                          ("ps5_color_render_target", "ps5_depth_render_target"),
                          ("ps5_render_staging_required", "ps5_depth_staging_required")):
    start = source.index("static bool\n" + helper + "(")
    linear_helpers += source[start:source.index("static bool\n" + following + "(", start)]
msaa_support_at = source.index("static bool\nps5_msaa4_color_format(")
msaa_support = source[msaa_support_at:source.index("static bool\nps5_msaa4_depth_support(", msaa_support_at)]
encoding_at = source.index("static bool\nps5_texture_descriptor_format(")
format_encoding = source[encoding_at:source.index("static bool\nps5_texture_descriptor_wrap(", encoding_at)]
barrier_at = source.index("static void\nps5_memory_barrier(")
barrier = source[barrier_at:source.index("static bool\nps5_draw_primitive(", barrier_at)]
fragment_at = source.index("static unsigned\nps5_shader_storage_count(")
fragment = source[fragment_at:source.index("static bool\nps5_prepare_constant(", fragment_at)]
constant_at = source.index("static bool\nps5_prepare_constant(")
constant = source[constant_at:source.index("static bool\nps5_prepare_texture(", constant_at)]
texture_at = source.index("static bool\nps5_prepare_texture(")
texture = source[texture_at:source.index("\nint64_t sceKernelGetDirectMemorySize", texture_at)]
assert "(texture_address & 0xffu) || texture_address >> 48" in texture
assert "(uint32_t)(texture_address >> 32) != metadata->address32_hi" not in texture
assert "(uint32_t)(texture_address >> 40)" in texture
assert "ps5_metadata_has_indirect_ubo(metadata, shader->stage)" in texture
assert ": indirect_ubo ? 1u : expected_ubo_count" in texture
assert "context->base.memory_barrier = ps5_memory_barrier;" in source
atomic_source = (MESA / "src/mesa/state_tracker/st_atom_atomicbuf.c").read_text()
atomic_at = atomic_source.index("static void\nst_binding_to_sb(")
atomic_handoff = atomic_source[atomic_at:atomic_source.index("\nvoid\nst_bind_vs_atomics(", atomic_at)]
storage_source = (MESA / "src/mesa/state_tracker/st_atom_storagebuf.c").read_text()
storage_at = storage_source.index("static void\nst_bind_ssbos(")
storage_handoff = storage_source[storage_at:storage_source.index("\nvoid st_bind_vs_ssbos(", storage_at)]
state_source = (MESA / "src/mesa/program/prog_statevars.c").read_text()
state_at = state_source.index("   case STATE_ATOMIC_COUNTER_OFFSET:")
atomic_offset_state = state_source[state_at:state_source.index("      return;", state_at) + len("      return;")]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_inlines.h"
#include "util/bitset.h"
#include "mesa/program/prog_statevars.h"
#include "mesa/main/config.h"
#include "mesa/state_tracker/st_atom.h"
#include "psbc_compile.h"
#include "ps5_agc_package.h"
#include "amd/common/amdgfxregs.h"
#include "amd/common/ac_descriptors.h"
#include "amd/common/gfx10_format_table.h"
#define PS5_ENABLE_UBO_CANDIDATE 1
#define PS5_ENABLE_GEOMETRY_CANDIDATE 1
#define PS5_ENABLE_TESSELLATION_CANDIDATE 1
#define PS5_ENABLE_GLSL_430_CANDIDATE 1
#define PS5_ENABLE_GLSL_420_CANDIDATE 1
#define PS5_ENABLE_MSAA4_CANDIDATE 1
#define PS5_ENABLE_MSAA_ARRAY_CANDIDATE 1
#define PS5_ENABLE_RENDER_TO_TEXTURE_CANDIDATE 1
#define PS5_ENABLE_LAYERED_RENDER_TARGET_CANDIDATE 1
#define PS5_ENABLE_TEXTURE_CUBE_ARRAY_CANDIDATE 1
#define PS5_ENABLE_TEXTURE_RECTANGLE_CANDIDATE 1
#define PS5_ENABLE_TEXTURE_1D_CANDIDATE 0
#define PS5_ENABLE_DYNAMIC_COLOR_TARGET_CANDIDATE 1
#define PS5_ENABLE_CORE_RENDER_FORMATS_CANDIDATE 1
#define PS5_ENABLE_CORE_TEXTURE_FORMATS_CANDIDATE 1
#define PS5_ENABLE_FRAMEBUFFER_SRGB_CANDIDATE 1
#define PS5_ENABLE_SRGB_CANDIDATE 1
#define PS5_ENABLE_TEXTURE_RG_CANDIDATE 1
#define PS5_ENABLE_TEXTURE_SNORM_CANDIDATE 1
#define PS5_ENABLE_TEXTURE_FLOAT_CANDIDATE 1
#define PS5_ENABLE_TEXTURE_INTEGER_CANDIDATE 1
#define PS5_ENABLE_TEXTURE_BUFFER_CANDIDATE 1
#define PS5_ENABLE_NARROW_TEXTURE_INTEGER_CANDIDATE 1
#define PS5_ENABLE_RGB10_A2UI_CANDIDATE 1
#define PS5_ENABLE_SHARED_EXPONENT_CANDIDATE 1
#define PS5_ENABLE_PACKED_FLOAT_CANDIDATE 1
#define PS5_ENABLE_DEPTH_TEXTURE_CANDIDATE 1
#define PS5_ENABLE_PACKED_DEPTH_STENCIL 1
#define PS5_MAX_COLOR_WIDTH 8192
#define PS5_MAX_COLOR_HEIGHT 8192
#define PS5_COLOR_TARGET_ALIGNMENT 0x10000u
#define PS5_MAX_CONSTANT_BUFFERS 13
#define PS5_VERTEX_STORAGE_OFFSET 2048
#define PS5_VERTEX_IMAGE_OFFSET 2560
#define PS5_GEOMETRY_STORAGE_OFFSET 3072
#define PS5_GEOMETRY_IMAGE_OFFSET 3584
#define PS5_CONSTANT_DATA_OFFSET 4096
#define PS5_TESSELLATION_BUFFER_OFFSET (4096+4*PS5_MAX_CONSTANT_BUFFER_SIZE)
#define PS5_TESSELLATION_BUFFER_STRIDE ((PS5_MAX_CONSTANT_BUFFERS+16)*16+8*32)
#define PS5_MAX_TEXTURE_UNITS 16u
#define PS5_TEXTURE_DESCRIPTOR_STRIDE 48u
#define PS5_TESSELLATION_TEXTURE_BINDING 16u
#define PS5_TESSELLATION_TEXTURE_OFFSET (PS5_TESSELLATION_BUFFER_OFFSET+4*PS5_TESSELLATION_BUFFER_STRIDE)
#define PS5_DESCRIPTOR_STORAGE_BYTES (PS5_TESSELLATION_TEXTURE_OFFSET+4*16*48)
#define PS5_FRAGMENT_UBO_OFFSET 512
#define PS5_TEXTURE_DESCRIPTOR_BYTES 1536
#define PS5_DIRECT_ALIGNMENT 16384
#define PS5_GEOMETRY_CONSTANT_SLOT 2
#define PS5_TESS_CTRL_CONSTANT_SLOT 3
#define PS5_TESS_EVAL_CONSTANT_SLOT 4
#define PS5_GEOMETRY_TEXTURE_SLOT 2
#define PS5_TESS_CTRL_TEXTURE_SLOT 3
#define PS5_TESS_EVAL_TEXTURE_SLOT 4
#define PS5_COMPUTE_STORAGE_SLOTS 16
#define PS5_COMPUTE_CONSTANT_SLOTS 15
#define PS5_COMPUTE_BUFFER_SLOTS 31
#define PS5_COMPUTE_IMAGE_SLOTS 8
#define PS5_COMPUTE_TEXTURE_SLOTS PS5_AGC_COMPUTE_MAX_TEXTURES
#define PS5_COMPUTE_TEXTURE_OFFSET (31*16+8*32)
#define PS5_COMPUTE_DESCRIPTOR_BYTES (PS5_COMPUTE_TEXTURE_OFFSET+PS5_COMPUTE_TEXTURE_SLOTS*48)
#define PS5_MAX_TEXTURE_2D_SIZE 8192
#define PS5_MAX_TEXTURE_3D_SIZE 2048
#define PS5_MAX_TEXTURE_ARRAY_LAYERS 2048u
#define PS5_MAX_TEXEL_BUFFER_ELEMENTS (1u << 20)
#define PS5_MAX_CONSTANT_BUFFER_SIZE 0x10000u
struct ps5_resource {
    struct pipe_resource base; uint8_t *data;
    size_t size, allocation_size, render_staging_offset, render_staging_size, depth_staging_offset, depth_staging_size, layer_stride;
    unsigned level_stride[PIPE_MAX_TEXTURE_LEVELS];
    size_t level_offset[PIPE_MAX_TEXTURE_LEVELS];
};
struct ps5_compute_shader { PsbcShaderOutput output; unsigned ssbos, ubos, images, textures, buffer_textures, filtered_textures, texture_lod[PS5_COMPUTE_TEXTURE_SLOTS], array_textures; };
struct ps5_sampler_state { struct pipe_sampler_state base; };
struct test_nir { struct { unsigned stage, num_ssbos, num_images, num_ubos, num_textures; BITSET_DECLARE(textures_used, 128); bool first_ubo_is_default_ubo; } info; };
struct test_variant { PsbcShaderOutput output; };
struct ps5_shader { struct test_nir *nir; struct test_variant *active; PsbcStage stage; };
static unsigned ps5_shader_texture_count(const struct ps5_shader *shader) { return shader->nir->info.num_textures; }
struct ps5_constant_state { bool valid, copied; unsigned size, offset; struct pipe_resource *buffer; };
struct ps5_context {
    struct pipe_context base;
    struct ps5_compute_shader *cs;
    struct pipe_resource *compute_descriptors;
    struct pipe_shader_buffer compute_buffers[31];
    struct pipe_shader_buffer fragment_buffers[16];
    struct pipe_shader_buffer geometry_buffers[16];
    struct pipe_shader_buffer preraster_buffers[3][16];
    bool preraster_bindings_invalid[3];
    bool fragment_bindings_invalid;
    bool geometry_bindings_invalid;
    struct ps5_shader *fs;
    struct ps5_shader *gs;
    struct ps5_shader *vs, *tcs, *tes;
    struct ps5_constant_state constants[5][13];
    struct pipe_resource *descriptor_storage[2];
    struct pipe_image_view compute_images[8];
    struct pipe_image_view fragment_images[8];
    bool fragment_images_invalid;
    struct pipe_image_view preraster_images[4][8];
    bool preraster_images_invalid[4];
    struct pipe_sampler_view *compute_views[PS5_COMPUTE_TEXTURE_SLOTS];
    struct pipe_sampler_view *sampler_views[5][16];
    void *samplers[5][16];
    bool compute_views_invalid;
    uint32_t compute_samplers[PS5_COMPUTE_TEXTURE_SLOTS][4];
    unsigned compute_sampler_mask;
    bool compute_samplers_invalid;
    bool compute_images_invalid;
    bool compute_bindings_invalid;
    uint32_t compute_constants_invalid;
    int last_compute_status;
    unsigned dispatches;
};
static unsigned submitted, destroyed;
static unsigned with_images;
static bool with_sampled;
static unsigned sampled_count=8;
static uint32_t expected_sampler[4];
static uint32_t expected_swizzle = UINT32_MAX;
static unsigned with_filtered;
static bool multi, with_constants, fail_upload;
static bool fail_info;
static bool render_condition_pass=true;
static struct ps5_resource *upload_resource;
static int ps5_packed_depth_sampled_descriptor(struct pipe_resource *base,
    enum pipe_format format, unsigned first, unsigned last,
    uint32_t descriptor[8]) {
    (void)base; (void)format; (void)first; (void)last; (void)descriptor;
    return -1;
}
''' + extent_helper + array_layout + msaa_tile_helper + linear_helpers + msaa_support + size_helper + format_encoding + tiled_helper + image_descriptor + r'''
static int ps5_resource_info(struct pipe_resource *base, void **address, size_t *size, size_t *allocation) {
    (void)allocation;
    if (fail_info) return -1;
    *address=((struct ps5_resource *)base)->data;
    *size=base->width0;
    return 0;
}
static bool fragment_mode;
static unsigned fragment_drains, barrier_submissions;
static void ps5_draw_batch_submit(void) { ++barrier_submissions; }
static void ps5_draw_batch_drain(void) { ++fragment_drains; }
static bool ps5_render_condition_passes(const struct ps5_context *context) { (void)context; return render_condition_pass; }
static void ps5_flush_gpu_data(const void *address, size_t size) { assert(address && (fragment_mode ? size==32 || size==64 || size==256 || size==512 || size==768 || size==2048 || size==4*PS5_TESSELLATION_BUFFER_STRIDE || size==PS5_DESCRIPTOR_STORAGE_BYTES : size==12)); }
''' + texel_format + texel_descriptor + r'''
static bool ps5_uses_merged_geometry_metadata(const struct ps5_context *c, const struct ps5_shader *s, const PsbcShaderMetadata *m) { return false; }
static unsigned ps5_texture_count(const struct ps5_context *c, const struct ps5_shader *s, const PsbcShaderMetadata *m) { return s->nir->info.num_textures; }
static unsigned ps5_constant_state_binding(const struct ps5_shader *s, unsigned i) { return i+!s->nir->info.first_ubo_is_default_ubo; }
''' + source[source.index("static size_t\nps5_copied_constant_offset("):source.index("static unsigned\nps5_shader_storage_count(")] + r'''
void u_upload_data_ref(struct u_upload_mgr *upload, unsigned minimum, unsigned size,
    unsigned alignment, const void *data, unsigned *offset, struct pipe_resource **buffer) {
    assert(upload && !minimum && alignment==16 && size<=64 && upload_resource);
    if (fail_upload) return;
    *offset=32;
    memcpy(upload_resource->data+32,data,size);
    pipe_resource_reference(buffer,&upload_resource->base);
}
void u_upload_unmap(struct u_upload_mgr *upload) { assert(upload); }
static void destroy(struct pipe_screen *s, struct pipe_resource *r) {
    assert(s && r && !r->reference.count); ++destroyed;
}
int ps5_agc_compute_execute(struct pipe_screen *s, const PsbcShaderOutput *shader,
    struct pipe_resource *table, struct pipe_resource *const *buffers, unsigned count,
    const uint32_t groups[3]) {
    assert(s && shader && groups[0]==2 && groups[1]==1 && groups[2]==1);
    if(with_sampled && sampled_count==15) {
        const struct ps5_resource *t=(const struct ps5_resource *)table;
        assert(count==15);
        for(unsigned i=0;i<16;++i) {
            const uint32_t *d=(const uint32_t *)(t->data+PS5_COMPUTE_TEXTURE_OFFSET+i*48);
            if(i<15) assert(!ps5_resource_texel_buffer_descriptor_owned(buffers[i],d));
            else for(unsigned w=0;w<12;++w) assert(!d[w]);
        }
        ++submitted; return 0;
    }
    if(with_sampled) {
        assert(count==sampled_count);
        const struct ps5_resource *t=(const struct ps5_resource *)table;
        for(unsigned i=0;i<sampled_count;++i) {
            uint32_t expected[12]={0};
            assert(!ps5_resource_sampled_image_descriptor(buffers[i],0,0,expected));
            if(expected_swizzle != UINT32_MAX)
                expected[3]=(expected[3]&~0xfffu)|expected_swizzle;
            if (with_filtered & (1u<<i))
                memcpy(expected+8,expected_sampler,16);
            assert(!memcmp(t->data+PS5_COMPUTE_TEXTURE_OFFSET+i*48,expected,48));
        }
        ++submitted; return 0;
    }
    if(with_images) {
        assert(count==with_images);
        struct ps5_resource *t=(struct ps5_resource *)table;
        for(unsigned i=0;i<8;++i) {
            uint32_t expected[8]={0};
            if(with_images==39 || i==7)
                assert(!ps5_resource_storage_image_descriptor(buffers[count-1],0,expected));
            assert(!memcmp(t->data+31*16+i*32,expected,32));
        }
        ++submitted; return 0;
    }
    assert(count==(with_constants ? 31 : multi ? 16 : 1));
    struct ps5_resource *t=(struct ps5_resource *)table, *b=(struct ps5_resource *)buffers[0];
    if (multi) {
        assert(b->base.reference.count==(with_constants ? 30 : 15) && !destroyed);
        for (unsigned i=0; i<16; ++i) {
            struct ps5_resource *r=(struct ps5_resource *)buffers[i];
            const uint32_t *d=(uint32_t *)t->data+i*4;
            const uintptr_t address=(uintptr_t)r->data+(i==15 ? 16 : i*16);
            assert(i==15 ? r->base.reference.count==1 : r==b);
            assert(d[0]==(uint32_t)address && d[1]==address>>32);
            assert(d[2]==(i==15 ? 64 : 16) && d[3]==0x31016fac);
        }
        for (unsigned i=0; i<15; ++i) {
            const uint32_t *d=(uint32_t *)t->data+(16+i)*4;
            if (with_constants) {
                const uintptr_t address=(uintptr_t)b->data+i*16;
                assert(buffers[16+i]==&b->base);
                assert(d[0]==(uint32_t)address && d[1]==address>>32);
                assert(d[2]==16 && d[3]==0x31016fac);
            } else assert(!(d[0]|d[1]|d[2]|d[3]));
        }
        ++submitted; return 0;
    }
    uint32_t *srd=(uint32_t *)t->data+15*4;
    assert(b->base.reference.count==1 && !destroyed);
    assert(srd[0]==(uint32_t)(uintptr_t)(b->data+16) && srd[1]==(uintptr_t)b->data>>32);
    assert(srd[2]==64 && srd[3]==0x31016fac);
    for (unsigned i=0; i<15*4; ++i) assert(((uint32_t *)t->data)[i]==0);
    ++submitted; return 0;
}

''' + barrier + fragment + constant + '\n#define PS5_ENABLE_BORDER_COLOR_CANDIDATE 1\n' + sampler_helpers + functions + r'''
/* Only fields read by the extracted Mesa handoff/state code are modeled. */
struct gl_buffer_object { struct pipe_resource *buffer; };
struct gl_buffer_binding {
    struct gl_buffer_object *BufferObject;
    GLintptr Offset;
    GLsizeiptr Size;
    GLboolean AutomaticSize;
};
struct gl_active_atomic_buffer { GLuint Binding; };
struct gl_uniform_block { GLuint Binding; };
struct gl_shader_program_data {
    unsigned NumAtomicBuffers;
    struct gl_active_atomic_buffer *AtomicBuffers;
};
struct gl_program {
    struct { unsigned num_ssbos; } info;
    struct {
        struct gl_shader_program_data *data;
        struct gl_uniform_block **ShaderStorageBlocks;
        unsigned ShaderStorageBlocksWriteAccess;
    } sh;
};
struct gl_context {
    struct gl_buffer_binding AtomicBufferBindings[8];
    struct gl_buffer_binding ShaderStorageBufferBindings[8];
    struct { unsigned ShaderStorageBufferOffsetAlignment; } Const;
};
struct st_context {
    struct pipe_context *pipe;
    struct pipe_screen *screen;
    struct gl_context *ctx;
    unsigned last_used_atomic_bindings[MESA_SHADER_MESH_STAGES];
    unsigned last_num_ssbos[MESA_SHADER_MESH_STAGES];
};
''' + atomic_handoff + storage_handoff + r'''
union atomic_state_value { int i; float f; };
static void atomic_state(struct gl_context *ctx, const uint16_t state[],
                         union atomic_state_value *val) {
    switch (state[0]) {
''' + atomic_offset_state + r'''
    default: assert(!"unexpected atomic state token");
    }
}
static void atomic_handoff_contract(void) {
    const unsigned counts[]={0,1,8}, bindings[]={0,7};
    const mesa_shader_stage stages[]={MESA_SHADER_COMPUTE,MESA_SHADER_FRAGMENT};
    const unsigned drains_before=fragment_drains;
    for(unsigned n=0;n<3;++n) for(unsigned b=0;b<2;++b) {
        struct pipe_screen screen={.resource_destroy=destroy};
        struct ps5_context c={.base={.screen=&screen,.set_shader_buffers=ps5_set_shader_buffers}};
        struct pipe_resource original={.screen=&screen,.target=PIPE_BUFFER,.width0=64};
        struct pipe_resource replacement={.screen=&screen,.target=PIPE_BUFFER,.width0=64};
        struct pipe_resource sentinel={.screen=&screen,.target=PIPE_BUFFER,.width0=64};
        pipe_reference_init(&original.reference,1);
        pipe_reference_init(&replacement.reference,1);
        pipe_reference_init(&sentinel.reference,1);
        struct gl_buffer_object obj={.buffer=&original};
        struct gl_context ctx={.Const.ShaderStorageBufferOffsetAlignment=16};
        ctx.AtomicBufferBindings[bindings[b]]=(struct gl_buffer_binding){
            .BufferObject=&obj,.Offset=20,.Size=16};
        struct gl_active_atomic_buffer atomic={.Binding=bindings[b]};
        struct gl_shader_program_data data={.NumAtomicBuffers=1,.AtomicBuffers=&atomic};
        struct gl_program prog={.info.num_ssbos=counts[n],.sh.data=&data};
        struct st_context st={.pipe=&c.base,.screen=&screen,.ctx=&ctx};
        const unsigned slot=counts[n]+bindings[b]; /* Includes highest slot 15. */
        struct pipe_shader_buffer seed[16];
        for(unsigned i=0;i<16;++i)
            seed[i]=(struct pipe_shader_buffer){.buffer=&sentinel,.buffer_size=16};
        /* Seed every slot to detect misplaced writes and preserve SSBOs/holes. */
        for(unsigned s=0;s<2;++s)
            ps5_set_shader_buffers(&c.base,stages[s],0,16,seed,0xffff);
        assert(sentinel.reference.count==33);
        for(unsigned s=0;s<2;++s) {
            struct pipe_shader_buffer *bound=s ? c.fragment_buffers : c.compute_buffers;
            struct pipe_shader_buffer *other=s ? c.compute_buffers : c.fragment_buffers;
            struct pipe_shader_buffer saved[16];
            memcpy(saved,other,sizeof(saved));
            for(unsigned step=0;step<4;++step) {
                /* Repeat original, replace resource, then exercise both missing forms. */
                obj.buffer=step<2 ? &original : &replacement;
                if(step==3) {
                    if(b) obj.buffer=NULL;
                    else ctx.AtomicBufferBindings[bindings[b]].BufferObject=NULL;
                }
                st_bind_atomics(&st,&prog,stages[s]);
                struct pipe_resource *expected=step==3 ? NULL : obj.buffer;
                assert(bound[slot].buffer==expected);
                assert(bound[slot].buffer_offset==(expected ? 16 : 0));
                assert(bound[slot].buffer_size==(expected ? 20 : 0));
                assert(original.reference.count==(step<2 ? 2 : 1));
                assert(replacement.reference.count==(step==2 ? 2 : 1));
                assert(sentinel.reference.count==32-(int)s);
                assert(!c.compute_bindings_invalid && !c.fragment_bindings_invalid);
                assert(st.last_used_atomic_bindings[stages[s]]==bindings[b]+1);
                assert(st.last_used_atomic_bindings[stages[1-s]]==(s ? bindings[b]+1 : 0));
                assert(!memcmp(saved,other,sizeof(saved)));
                for(unsigned i=0;i<16;++i) if(i!=slot) {
                    assert(bound[i].buffer==&sentinel);
                    assert(bound[i].buffer_offset==0 && bound[i].buffer_size==16);
                }
                if(expected) {
                    const uint16_t state[]={STATE_ATOMIC_COUNTER_OFFSET,bindings[b]};
                    union atomic_state_value value={.i=-1};
                    /* Run Mesa's emitted offset-state branch, not a copied formula. */
                    atomic_state(&ctx,state,&value);
                    assert(value.i==4 && bound[slot].buffer_offset+value.i==20);
                    assert(bound[slot].buffer_size-value.i==16);
                }
            }
            ctx.AtomicBufferBindings[bindings[b]].BufferObject=&obj;
        }
        /* Explicit teardown; the separate regression below traces stale cleanup. */
        for(unsigned s=0;s<2;++s)
            ps5_set_shader_buffers(&c.base,stages[s],0,16,NULL,0);
        assert(sentinel.reference.count==1 && original.reference.count==1 && replacement.reference.count==1);
    }
    fragment_drains=drains_before;
}
static void stale_cleanup_regression(void) {
    /* st_validate_state visits ascending atom indices from the actual list. */
    _Static_assert(ST_NEW_CS_ATOMICS<ST_NEW_CS_SSBOS, "CS atom order changed");
    _Static_assert(ST_NEW_FS_ATOMICS<ST_NEW_FS_SSBOS, "FS atom order changed");
    const mesa_shader_stage stages[]={MESA_SHADER_COMPUTE,MESA_SHADER_FRAGMENT};
    const unsigned drains_before=fragment_drains;
    for(unsigned s=0;s<2;++s) {
        struct pipe_screen screen={.resource_destroy=destroy};
        struct ps5_context c={.base={.screen=&screen,.set_shader_buffers=ps5_set_shader_buffers}};
        struct pipe_resource ssbo={.screen=&screen,.target=PIPE_BUFFER,.width0=64};
        struct pipe_resource old_atomic={.screen=&screen,.target=PIPE_BUFFER,.width0=64};
        struct pipe_resource new_atomic={.screen=&screen,.target=PIPE_BUFFER,.width0=64};
        pipe_reference_init(&ssbo.reference,1);
        pipe_reference_init(&old_atomic.reference,1);
        pipe_reference_init(&new_atomic.reference,1);
        struct gl_buffer_object objects[]={{&ssbo},{&old_atomic},{&new_atomic}};
        struct gl_context ctx={.Const.ShaderStorageBufferOffsetAlignment=16};
        struct gl_uniform_block blocks[8], *block_ptrs[8];
        for(unsigned i=0;i<8;++i) {
            blocks[i].Binding=i;
            block_ptrs[i]=&blocks[i];
            ctx.ShaderStorageBufferBindings[i]=(struct gl_buffer_binding){
                .BufferObject=&objects[0],.Offset=0,.Size=16};
        }
        ctx.AtomicBufferBindings[7]=(struct gl_buffer_binding){
            .BufferObject=&objects[1],.Offset=20,.Size=16};
        ctx.AtomicBufferBindings[0]=(struct gl_buffer_binding){
            .BufferObject=&objects[2],.Offset=20,.Size=16};
        struct gl_active_atomic_buffer atomics[]={{.Binding=7},{.Binding=0}};
        struct gl_shader_program_data data[]={
            {.NumAtomicBuffers=1,.AtomicBuffers=&atomics[0]},
            {.NumAtomicBuffers=1,.AtomicBuffers=&atomics[1]}};
        struct gl_program programs[]={
            {.info.num_ssbos=8,.sh={.data=&data[0],.ShaderStorageBlocks=block_ptrs,
                                   .ShaderStorageBlocksWriteAccess=255}},
            {.info.num_ssbos=1,.sh={.data=&data[1],.ShaderStorageBlocks=block_ptrs,
                                   .ShaderStorageBlocksWriteAccess=1}}};
        /* Same zero initialization as st_create_context_priv's CALLOC_STRUCT.
         * Never assign either bookkeeping field in the fixture. Both programs
         * use SSBOs AND atomics, so both atoms remain active across this switch
         * (st_program.c / main/state.c); no forced inactive cleanup assumed. */
        struct st_context st={.pipe=&c.base,.screen=&screen,.ctx=&ctx};
        struct pipe_shader_buffer *bound=s ? c.fragment_buffers : c.compute_buffers;
        struct pipe_shader_buffer *other=s ? c.compute_buffers : c.fragment_buffers;
        assert(st.last_num_ssbos[stages[s]]==0 && st.last_used_atomic_bindings[stages[s]]==0);
        for(unsigned i=0;i<16;++i) assert(!bound[i].buffer && !other[i].buffer);
        unsigned after_growth=0;
        for(unsigned p=0;p<2;++p) {
            st_bind_atomics(&st,&programs[p],stages[s]);
            st_bind_ssbos(&st,&programs[p],stages[s]);
            assert(st.last_used_atomic_bindings[stages[s]]==(p ? 1 : 8));
            assert(!c.compute_bindings_invalid && !c.fragment_bindings_invalid);
            assert(bound[0].buffer==&ssbo && bound[0].buffer_size==16);
            assert(bound[1].buffer==(p ? &new_atomic : &ssbo));
            assert(bound[1].buffer_offset==(p ? 16 : 0));
            assert(bound[1].buffer_size==(p ? 20 : 16));
            for(unsigned i=2;i<8;++i) {
                assert(bound[i].buffer==(p ? NULL : &ssbo));
                assert(bound[i].buffer_offset==0 && bound[i].buffer_size==(p ? 0 : 16));
            }
            for(unsigned i=8;i<15;++i) assert(!bound[i].buffer);
            assert(bound[15].buffer==(p ? NULL : &old_atomic));
            assert(bound[15].buffer_offset==(p ? 0 : 16) && bound[15].buffer_size==(p ? 0 : 20));
            assert(ssbo.reference.count==(p ? 2 : 9));
            assert(old_atomic.reference.count==(p ? 1 : 2) && new_atomic.reference.count==(p ? 2 : 1));
            /* Check after shrink so the original source fails on retained
             * bindings first, demonstrating the consequence of lost tracking. */
            if(!p) after_growth=st.last_num_ssbos[stages[s]];
            else assert(after_growth==16 && st.last_num_ssbos[stages[s]]==2);
            for(unsigned i=0;i<16;++i) assert(!other[i].buffer);
            assert(!st.last_num_ssbos[stages[1-s]] && !st.last_used_atomic_bindings[stages[1-s]]);
        }
        /* Exercise the atoms' 2-to-0 contract directly. Full Mesa validation
         * can mask resource-free programs' atoms via active_states; this final
         * step does not claim to test scheduling of an empty GL program. */
        struct gl_shader_program_data empty_data={0};
        struct gl_program empty={.sh.data=&empty_data};
        st_bind_atomics(&st,&empty,stages[s]);
        st_bind_ssbos(&st,&empty,stages[s]);
        assert(!st.last_num_ssbos[stages[s]] && !st.last_used_atomic_bindings[stages[s]]);
        for(unsigned i=0;i<16;++i) {
            assert(!bound[i].buffer && !bound[i].buffer_offset && !bound[i].buffer_size);
            assert(!other[i].buffer && !other[i].buffer_offset && !other[i].buffer_size);
        }
        assert(!st.last_num_ssbos[stages[1-s]] && !st.last_used_atomic_bindings[stages[1-s]]);
        assert(ssbo.reference.count==1 && old_atomic.reference.count==1 && new_atomic.reference.count==1);
    }
    fragment_drains=drains_before;
}
static void fragment_contract(void) {
    struct pipe_screen screen={.resource_destroy=destroy};
    uint32_t words[64]={0}, descriptors[128]={0}, userdata[16]={0};
    struct ps5_resource data={.base={.screen=&screen,.target=PIPE_BUFFER,.width0=256},.data=(void *)words,.size=256};
    struct ps5_resource table={.data=(void *)descriptors,.size=512};
    pipe_reference_init(&data.base.reference,1);
    struct test_nir nir={.info.num_ssbos=16};
    struct test_variant variant={0};
    struct ps5_shader shader={&nir,&variant};
    PsbcShaderMetadata *m=&variant.output.metadata;
    m->address32_hi=(uintptr_t)descriptors>>32;
    m->descriptor_set0_valid=true; m->descriptor_binding_count=1;
    m->descriptor_bindings[0]=(PsbcDescriptorBinding){.binding=PSBC_GALLIUM_SSBO_ARRAY_BINDING(PSBC_STAGE_FRAGMENT),
        .type=PSBC_DESCRIPTOR_STORAGE_BUFFER,.array_size=16,.stride=16};
    struct ps5_context c={.base.screen=&screen,.fs=&shader,.descriptor_storage[1]=&table.base};
    struct pipe_shader_buffer bindings[16];
    for(unsigned i=0;i<16;++i) bindings[i]=(struct pipe_shader_buffer){&data.base,16,64};
    fragment_mode=true;
    assert(!ps5_prepare_fragment_storage(&c,userdata,16)); /* Missing bank. */
    ps5_set_shader_buffers(&c.base,MESA_SHADER_FRAGMENT,0,16,bindings,65535);
    assert(fragment_drains==1 && data.base.reference.count==17 && !c.compute_buffers[0].buffer);
    ps5_set_shader_buffers(&c.base,MESA_SHADER_FRAGMENT,0,16,bindings,65535);
    ps5_set_shader_buffers(&c.base,MESA_SHADER_FRAGMENT,16,0,NULL,0);
    assert(fragment_drains==1 && !c.fragment_bindings_invalid && data.base.reference.count==17);
    bindings[15].buffer_offset=UINT32_MAX;
    ps5_set_shader_buffers(&c.base,MESA_SHADER_FRAGMENT,0,16,bindings,65535);
    assert(c.fragment_bindings_invalid && fragment_drains==1);
    bindings[15].buffer_offset=16;
    ps5_set_shader_buffers(&c.base,MESA_SHADER_FRAGMENT,0,16,bindings,65535);
    assert(!c.fragment_bindings_invalid && fragment_drains==1);
    assert(ps5_prepare_fragment_storage(&c,userdata,16));
    for(unsigned i=0;i<16;++i) {
        assert(descriptors[i*4]==(uint32_t)(uintptr_t)((uint8_t *)words+16));
        assert(descriptors[i*4+2]==64 && descriptors[i*4+3]==0x31016fac);
    }
    assert(userdata[0]==(uint32_t)(uintptr_t)descriptors);
    /* Actual uniform preparation must preserve storage banks and copied data. */
    uint8_t mixed_table[PS5_DESCRIPTOR_STORAGE_BYTES]; memset(mixed_table,0xab,sizeof(mixed_table));
    table.data=mixed_table; table.size=sizeof(mixed_table);
    m->address32_hi=(uintptr_t)mixed_table>>32;
    m->descriptor_binding_count=2;
    m->descriptor_bindings[1]=(PsbcDescriptorBinding){.binding=PSBC_GALLIUM_UBO_ARRAY_BINDING(PSBC_STAGE_FRAGMENT),
        .type=PSBC_DESCRIPTOR_UNIFORM_BUFFER,.array_size=13,.offset=512,.stride=16};
    nir.info.num_ubos=13; nir.info.first_ubo_is_default_ubo=true;
    for(unsigned i=0;i<13;++i) c.constants[1][i]=(struct ps5_constant_state){.valid=true,.size=64,.buffer=&data.base};
    c.constants[1][0].copied=true;
    assert(ps5_prepare_fragment_storage(&c,userdata,16));
    uint8_t preserved[512]; memcpy(preserved,mixed_table,512);
    assert(ps5_prepare_constant(&c,&shader,1,userdata,16,NULL));
    assert(!memcmp(preserved,mixed_table,512));
    for(unsigned i=0;i<13;++i) {
        const uint32_t *d=(const uint32_t *)(mixed_table+512+i*16);
        assert(d[0]==(uint32_t)(uintptr_t)(i ? (void *)words : mixed_table+PS5_CONSTANT_DATA_OFFSET) && d[2]==64);
    }
    for(unsigned i=720;i<sizeof(mixed_table);++i) assert(mixed_table[i]==0xab);
    c.constants[1][12].valid=false;
    assert(!ps5_prepare_constant(&c,&shader,1,userdata,16,NULL));
    c.constants[1][12].valid=true;
    m->descriptor_bindings[1].offset=496;
    assert(!ps5_prepare_constant(&c,&shader,1,userdata,16,NULL));
    nir.info.num_ubos=0; m->descriptor_binding_count=1;
    table.data=(void *)descriptors; table.size=sizeof(descriptors);
    m->address32_hi=(uintptr_t)descriptors>>32;
    bindings[15].buffer_size=UINT32_MAX;
    ps5_set_shader_buffers(&c.base,MESA_SHADER_FRAGMENT,0,16,bindings,65535);
    assert(c.fragment_bindings_invalid && data.base.reference.count==17 && fragment_drains==1);
    assert(!ps5_prepare_fragment_storage(&c,userdata,16));
    bindings[15].buffer_size=64;
    ps5_set_shader_buffers(&c.base,MESA_SHADER_FRAGMENT,0,16,bindings,65535);
    const PsbcShaderMetadata good=*m;
    for(unsigned fault=0;fault<5;++fault) {
        *m=good;
        if(fault==0) m->descriptor_set0_valid=false;
        if(fault==1) m->descriptor_set0_user_data_dword=16;
        if(fault==2) m->descriptor_bindings[0].offset=16;
        if(fault==3) m->descriptor_bindings[0].array_size=15;
        if(fault==4) m->address32_hi^=1;
        assert(!ps5_prepare_fragment_storage(&c,userdata,16));
    }
    *m=good;
    ps5_set_shader_buffers(&c.base,MESA_SHADER_FRAGMENT,0,16,NULL,0);
    assert(data.base.reference.count==1 && !ps5_prepare_fragment_storage(&c,userdata,16));
    nir.info.num_ssbos=0; nir.info.num_images=8;
    m->descriptor_binding_count=2;
    m->descriptor_bindings[1]=(PsbcDescriptorBinding){.binding=PSBC_GALLIUM_IMAGE_ARRAY_BINDING(PSBC_STAGE_FRAGMENT),
        .type=PSBC_DESCRIPTOR_STORAGE_IMAGE,.array_size=8,.offset=256,.stride=32};
    _Alignas(256) uint8_t pixels[768];
    struct ps5_resource image={.base={.screen=&screen,.target=PIPE_TEXTURE_2D,
        .format=PIPE_FORMAT_R32_UINT,.width0=17,.height0=3,.depth0=1,.array_size=1,
        .bind=PIPE_BIND_SHADER_IMAGE|PIPE_BIND_SAMPLER_VIEW},.data=pixels,.size=768,.allocation_size=sizeof(pixels),.level_stride={256}};
    pipe_reference_init(&image.base.reference,1);
    struct pipe_image_view views[8];
    for(unsigned i=0;i<8;++i) views[i]=(struct pipe_image_view){.resource=&image.base,
        .format=PIPE_FORMAT_R32_UINT,.access=PIPE_IMAGE_ACCESS_READ_WRITE,
        .u.tex.single_layer_view=true};
    assert(ps5_prepare_fragment_storage(&c,userdata,16));
    for(unsigned i=64;i<128;++i) assert(!descriptors[i]);
    ps5_set_shader_images(&c.base,MESA_SHADER_FRAGMENT,0,8,0,views);
    assert(image.base.reference.count==9 && !c.compute_images[0].resource);
    assert(ps5_prepare_fragment_storage(&c,userdata,16));
    for(unsigned i=0;i<8;++i) assert(descriptors[64+i*8]==(uint32_t)((uintptr_t)pixels>>8));
    views[7].u.tex.level=1;
    ps5_set_shader_images(&c.base,MESA_SHADER_FRAGMENT,0,8,0,views);
    assert(!c.fragment_images_invalid && image.base.reference.count==9);
    memset(descriptors+64+7*8,0xff,32);
    assert(ps5_prepare_fragment_storage(&c,userdata,16));
    for(unsigned i=0;i<8;++i) assert(!descriptors[64+7*8+i]);
    views[7].u.tex.level=0;
    ps5_set_shader_images(&c.base,MESA_SHADER_FRAGMENT,0,8,0,views);
    m->descriptor_bindings[1].offset=128;
    assert(!ps5_prepare_fragment_storage(&c,userdata,16));
    m->descriptor_bindings[1].offset=256;
    ps5_set_shader_images(&c.base,MESA_SHADER_FRAGMENT,0,0,8,NULL);
    assert(image.base.reference.count==1 && ps5_prepare_fragment_storage(&c,userdata,16));
    fragment_mode=false;
}
static void geometry_storage_contract(void) {
    struct pipe_screen screen={.resource_destroy=destroy};
    uint32_t words[32]={0}, userdata[16]={0};
    _Alignas(256) uint8_t descriptors[PS5_DESCRIPTOR_STORAGE_BYTES]={0};
    struct ps5_resource data={.base={.screen=&screen,.target=PIPE_BUFFER,.width0=sizeof(words)},
        .data=(void *)words,.size=sizeof(words)};
    struct ps5_resource table={.data=descriptors,.size=sizeof(descriptors)};
    pipe_reference_init(&data.base.reference,1);
    struct test_nir nir={.info.num_ssbos=1};
    struct test_variant variant={0};
    struct ps5_shader shader={&nir,&variant};
    PsbcShaderMetadata *m=&variant.output.metadata;
    m->address32_hi=(uintptr_t)descriptors>>32;
    m->descriptor_set0_valid=true; m->descriptor_binding_count=1;
    m->descriptor_bindings[0]=(PsbcDescriptorBinding){
        .binding=PSBC_GALLIUM_SSBO_ARRAY_BINDING(PSBC_STAGE_GEOMETRY),
        .type=PSBC_DESCRIPTOR_STORAGE_BUFFER,.array_size=PS5_COMPUTE_STORAGE_SLOTS,
        .offset=PS5_GEOMETRY_STORAGE_OFFSET,.stride=16};
    struct ps5_context c={.base.screen=&screen,.gs=&shader,.descriptor_storage[0]=&table.base};
    struct pipe_shader_buffer binding={&data.base,16,64};
    fragment_mode=true;
    ps5_set_shader_buffers(&c.base,MESA_SHADER_GEOMETRY,0,1,&binding,1);
    assert(!c.geometry_bindings_invalid && data.base.reference.count==2);
    assert(ps5_prepare_geometry_storage(&c,m,userdata,16));
    const uint32_t *srd=(const uint32_t *)(descriptors+PS5_GEOMETRY_STORAGE_OFFSET);
    assert(srd[0]==(uint32_t)(uintptr_t)((uint8_t *)words+16));
    assert(srd[2]==64 && srd[3]==0x31016fac);
    assert(userdata[0]==(uint32_t)(uintptr_t)descriptors);
    ps5_set_shader_buffers(&c.base,MESA_SHADER_GEOMETRY,0,1,NULL,0);
    assert(data.base.reference.count==1 && !ps5_prepare_geometry_storage(&c,m,userdata,16));
    fragment_mode=false;
}
static void preraster_image_contract(void) {
    unsigned drains=fragment_drains;
    bool saved_mode=fragment_mode;
    fragment_mode=true;
    struct pipe_screen screen={.resource_destroy=destroy};
    uint32_t userdata[16]={0};
    _Alignas(256) uint8_t descriptors[PS5_DESCRIPTOR_STORAGE_BYTES]={0};
    _Alignas(256) uint8_t pixels[2048]={0};
    struct ps5_resource table={.data=descriptors,.size=sizeof(descriptors)};
    struct ps5_resource image={.base={.screen=&screen,.target=PIPE_TEXTURE_2D,
        .format=PIPE_FORMAT_R32_UINT,.width0=8,.height0=8,.depth0=1,.array_size=1,
        .bind=PIPE_BIND_SHADER_IMAGE|PIPE_BIND_SAMPLER_VIEW},.data=pixels,.size=sizeof(pixels),
        .allocation_size=sizeof(pixels),.level_stride={256}};
    pipe_reference_init(&image.base.reference,1);
    struct pipe_image_view view={.resource=&image.base,.format=PIPE_FORMAT_R32_UINT,
        .access=PIPE_IMAGE_ACCESS_READ_WRITE,.u.tex.single_layer_view=true};
    const unsigned stages[]={MESA_SHADER_VERTEX,MESA_SHADER_GEOMETRY};
    const PsbcStage psbc_stages[]={PSBC_STAGE_VERTEX,PSBC_STAGE_GEOMETRY};
    const unsigned offsets[]={PS5_VERTEX_IMAGE_OFFSET,PS5_GEOMETRY_IMAGE_OFFSET};
    for(unsigned i=0;i<2;++i) {
        struct test_nir nir={.info={.stage=stages[i],.num_images=1}};
        struct test_variant variant={0};
        struct ps5_shader shader={.nir=&nir,.active=&variant,.stage=psbc_stages[i]};
        PsbcShaderMetadata *m=&variant.output.metadata;
        m->address32_hi=(uintptr_t)descriptors>>32;
        m->descriptor_set0_valid=true;
        m->descriptor_binding_count=1;
        m->descriptor_bindings[0]=(PsbcDescriptorBinding){
            .binding=PSBC_GALLIUM_IMAGE_ARRAY_BINDING(psbc_stages[i]),
            .type=PSBC_DESCRIPTOR_STORAGE_IMAGE,.array_size=8,
            .offset=offsets[i],.stride=32};
        struct ps5_context c={.base.screen=&screen,.descriptor_storage[0]=&table.base};
        ps5_set_shader_images(&c.base,stages[i],0,1,0,&view);
        assert(!c.preraster_images_invalid[stages[i]] && image.base.reference.count==2);
        assert(ps5_prepare_preraster_images(&c,&shader,m,userdata,16,offsets[i]));
        assert(*(uint32_t *)(descriptors+offsets[i])==(uint32_t)((uintptr_t)pixels>>8));
        assert(userdata[0]==(uint32_t)(uintptr_t)descriptors);
        ps5_set_shader_images(&c.base,stages[i],0,0,1,NULL);
        assert(image.base.reference.count==1);
    }
    fragment_drains=drains;
    fragment_mode=saved_mode;
}
static void indirect_ubo_contract(void) {
    struct pipe_screen screen={.resource_destroy=destroy};
    uint32_t words[64]={0}, userdata[16]={0};
    _Alignas(256) uint8_t descriptors[PS5_DESCRIPTOR_STORAGE_BYTES]={0};
    struct ps5_resource data={.base={.screen=&screen,.target=PIPE_BUFFER,.width0=sizeof(words)},
        .data=(void *)words,.size=sizeof(words)};
    struct ps5_resource table={.base={.target=PIPE_BUFFER},.data=descriptors,.size=sizeof(descriptors)};
    struct test_nir nir={.info={.num_ubos=3,.first_ubo_is_default_ubo=true}};
    struct test_variant variant={0};
    struct ps5_shader shader={&nir,&variant};
    PsbcShaderMetadata *m=&variant.output.metadata;
    m->address32_hi=(uintptr_t)descriptors>>32;
    m->descriptor_set0_valid=true; m->descriptor_binding_count=1;
    m->descriptor_bindings[0]=(PsbcDescriptorBinding){
        .binding=PSBC_GALLIUM_UBO_ARRAY_BINDING(PSBC_STAGE_VERTEX),
        .type=PSBC_DESCRIPTOR_UNIFORM_BUFFER,.array_size=3,
        .offset=PS5_TEXTURE_DESCRIPTOR_BYTES,.stride=16};
    struct ps5_context c={.base.screen=&screen,.descriptor_storage[0]=&table.base};
    for(unsigned i=0;i<3;++i) c.constants[0][i]=(struct ps5_constant_state){
        .valid=true,.size=64,.offset=i*16,.buffer=&data.base};
    fragment_mode=true;
    assert(ps5_prepare_constant(&c,&shader,0,userdata,16,NULL));
    for(unsigned i=0;i<3;++i) {
        const uint32_t *d=(const uint32_t *)(descriptors+PS5_TEXTURE_DESCRIPTOR_BYTES+i*16);
        assert(d[0]==(uint32_t)(uintptr_t)((uint8_t *)words+i*16) && d[2]==64);
    }
    assert(userdata[0]==(uint32_t)(uintptr_t)descriptors);
    m->descriptor_bindings[0].array_size=2;
    assert(!ps5_prepare_constant(&c,&shader,0,userdata,16,NULL));
    fragment_mode=false;
}
static void texel_buffer_descriptor_contract(void) {
    _Alignas(256) uint8_t data[64]={0};
    struct ps5_resource resource={.base={.target=PIPE_BUFFER,.width0=sizeof(data),
        .bind=PIPE_BIND_SHADER_IMAGE},
        .data=data,.size=sizeof(data)};
    struct pipe_sampler_view view={.texture=&resource.base,.target=PIPE_BUFFER,
        .format=PIPE_FORMAT_R32_SINT,.swizzle_r=PIPE_SWIZZLE_X,
        .swizzle_g=PIPE_SWIZZLE_0,.swizzle_b=PIPE_SWIZZLE_0,.swizzle_a=PIPE_SWIZZLE_1};
    view.u.buf.offset=16; view.u.buf.size=32;
    uint32_t descriptor[4]={0};
    assert(ps5_texel_buffer_descriptor(&view,(uintptr_t)data>>32,descriptor));
    assert(descriptor[0]==(uint32_t)(uintptr_t)(data+16));
    assert(descriptor[3]&0x01000000u);
    assert(!ps5_resource_texel_buffer_descriptor_owned(&resource.base,descriptor));
    descriptor[2]=13;
    assert(ps5_resource_texel_buffer_descriptor_owned(&resource.base,descriptor));
    descriptor[2]=8;
    descriptor[1]=(descriptor[1]&0xffffu)|(3u<<16);
    assert(ps5_resource_texel_buffer_descriptor_owned(&resource.base,descriptor));
    view.u.buf.offset=17;
    assert(!ps5_texel_buffer_descriptor(&view,(uintptr_t)data>>32,descriptor));
    const enum pipe_format rgb32[]={PIPE_FORMAT_R32G32B32_FLOAT,
        PIPE_FORMAT_R32G32B32_UINT,PIPE_FORMAT_R32G32B32_SINT};
    for(unsigned f=0;f<ARRAY_SIZE(rgb32);++f) {
        view.format=rgb32[f];
        view.u.buf.offset=16; view.u.buf.size=47;
        assert(ps5_texel_buffer_descriptor(&view,(uintptr_t)data>>32,descriptor));
        assert(descriptor[0]==(uint32_t)(uintptr_t)(data+16));
        assert(descriptor[1]>>16==12 && descriptor[2]==3);
        assert(!ps5_resource_texel_buffer_descriptor_owned(&resource.base,descriptor));
        view.u.buf.size=49;
        assert(!ps5_texel_buffer_descriptor(&view,(uintptr_t)data>>32,descriptor));
        view.u.buf.offset=17; view.u.buf.size=47;
        assert(!ps5_texel_buffer_descriptor(&view,(uintptr_t)data>>32,descriptor));
    }
    struct pipe_image_view image={.resource=&resource.base,.format=PIPE_FORMAT_R32_SINT,
        .access=PIPE_IMAGE_ACCESS_READ_WRITE};
    image.u.buf.offset=16; image.u.buf.size=32;
    uint32_t image_descriptor[8];
    assert(ps5_image_buffer_descriptor(&image,(uintptr_t)data>>32,image_descriptor));
    assert(!ps5_resource_texel_buffer_descriptor_owned(&resource.base,image_descriptor));
    const struct util_format_description *format=util_format_description(image.format);
    view.format=image.format;
    view.swizzle_r=PIPE_SWIZZLE_X; view.swizzle_g=PIPE_SWIZZLE_Y;
    view.swizzle_b=PIPE_SWIZZLE_Z; view.swizzle_a=PIPE_SWIZZLE_W;
    view.u.buf.offset=image.u.buf.offset; view.u.buf.size=image.u.buf.size;
    uint32_t sampled[4];
    assert(ps5_texel_buffer_descriptor(&view,(uintptr_t)data>>32,sampled));
    view.swizzle_r=format->swizzle[0]; view.swizzle_g=format->swizzle[1];
    view.swizzle_b=format->swizzle[2]; view.swizzle_a=format->swizzle[3];
    uint32_t expected[4];
    assert(ps5_texel_buffer_descriptor(&view,(uintptr_t)data>>32,expected));
    assert(!memcmp(sampled,expected,sizeof(expected)));
    assert(!memcmp(image_descriptor,expected,sizeof(expected)));
    for(unsigned i=4;i<8;++i) assert(!image_descriptor[i]);
    resource.base.target=PIPE_TEXTURE_2D;
    assert(!ps5_image_buffer_descriptor(&image,(uintptr_t)data>>32,image_descriptor));
}
static void preraster_binding_contract(void) {
    struct pipe_screen screen={.resource_destroy=destroy};
    struct ps5_context c={.base={.screen=&screen}};
    struct ps5_resource resource={.base={.screen=&screen,.target=PIPE_BUFFER,.width0=64},.size=64};
    struct pipe_resource *data=&resource.base;
    static uint8_t large_data[2 * PS5_MAX_CONSTANT_BUFFER_SIZE + 16];
    struct ps5_resource large_resource={.base={.screen=&screen,.target=PIPE_BUFFER,
        .width0=sizeof(large_data)},.data=large_data,.size=sizeof(large_data)};
    pipe_reference_init(&large_resource.base.reference,1);
    pipe_reference_init(&data->reference,1);
    const mesa_shader_stage stages[]={MESA_SHADER_VERTEX,MESA_SHADER_TESS_CTRL,MESA_SHADER_TESS_EVAL};
    struct pipe_shader_buffer binding={.buffer=data,.buffer_offset=16,.buffer_size=32};
    unsigned drains=fragment_drains;
    for (unsigned s=0;s<3;++s) {
        ps5_set_shader_buffers(&c.base,stages[s],15,1,&binding,1);
        assert(!c.preraster_bindings_invalid[stages[s]]);
        assert(data->reference.count==(int)s+2);
        for (unsigned other=0;other<3;++other)
            assert(c.preraster_buffers[other][15].buffer==(other<=s ? data : NULL));
        struct pipe_shader_buffer invalid=binding; invalid.buffer_size=64;
        ps5_set_shader_buffers(&c.base,stages[s],15,1,&invalid,1);
        assert(c.preraster_bindings_invalid[stages[s]]);
        assert(c.preraster_buffers[stages[s]][15].buffer==data);
    }
    for (unsigned s=0;s<3;++s)
        ps5_set_shader_buffers(&c.base,stages[s],0,16,NULL,0);
    assert(data->reference.count==1);
    fragment_drains=drains;
    _Alignas(16) uint8_t bytes[PS5_DESCRIPTOR_STORAGE_BYTES]={0};
    struct ps5_resource storage={.base={.screen=&screen},.data=bytes,.size=sizeof(bytes)};
    c.descriptor_storage[0]=&storage.base;
    const mesa_shader_stage uniform_stages[]={MESA_SHADER_GEOMETRY,MESA_SHADER_TESS_CTRL,MESA_SHADER_TESS_EVAL};
    static uint8_t large_user[PS5_MAX_CONSTANT_BUFFER_SIZE];
    large_user[0]=0x5a; large_user[sizeof(large_user)-1]=0xa5;
    for (unsigned s=0;s<3;++s) {
        uint32_t value=100+s;
        struct pipe_constant_buffer cb={.buffer_size=sizeof(value),.user_buffer=&value};
        ps5_set_constant_buffer(&c.base,uniform_stages[s],0,&cb);
        assert(c.constants[s+2][0].valid && c.constants[s+2][0].copied);
        for (unsigned other=0;other<=s;++other) {
            uint32_t saved; memcpy(&saved,bytes+ps5_copied_constant_offset(other+2),sizeof(saved));
            assert(saved==100+other);
        }
        struct pipe_constant_buffer bound={.buffer=data,.buffer_offset=16,.buffer_size=32};
        ps5_set_constant_buffer(&c.base,uniform_stages[s],1,&bound);
        assert(c.constants[s+2][1].buffer==data && c.constants[s+2][1].valid);
        assert(data->reference.count==(int)s+2);
    }
    for (unsigned s=0;s<3;++s) {
        struct pipe_constant_buffer cb={.buffer_size=sizeof(large_user),.user_buffer=large_user};
        ps5_set_constant_buffer(&c.base,uniform_stages[s],0,&cb);
        size_t copied=ps5_copied_constant_offset(s+2);
        assert(bytes[copied]==0x5a && bytes[copied+sizeof(large_user)-1]==0xa5);
        struct pipe_constant_buffer bound={.buffer=&large_resource.base,.buffer_offset=16,
            .buffer_size=PS5_MAX_CONSTANT_BUFFER_SIZE};
        ps5_set_constant_buffer(&c.base,uniform_stages[s],1,&bound);
        bound.buffer_offset=16+PS5_MAX_CONSTANT_BUFFER_SIZE;
        ps5_set_constant_buffer(&c.base,uniform_stages[s],2,&bound);
        assert(c.constants[s+2][1].valid && c.constants[s+2][1].size==PS5_MAX_CONSTANT_BUFFER_SIZE);
        assert(c.constants[s+2][2].valid && c.constants[s+2][2].offset==bound.buffer_offset);
    }
    for (unsigned s=0;s<3;++s) {
        ps5_set_constant_buffer(&c.base,uniform_stages[s],0,NULL);
        ps5_set_constant_buffer(&c.base,uniform_stages[s],1,NULL);
        ps5_set_constant_buffer(&c.base,uniform_stages[s],2,NULL);
    }
    assert(data->reference.count==1);
    assert(large_resource.base.reference.count==1);
}
static void test_tessellation_buffers(void) {
    struct pipe_screen screen={0}, foreign={0};
    struct ps5_context ctx={.base.screen=&screen};
    uint8_t data[PS5_DESCRIPTOR_STORAGE_BYTES], buffers[4][64];
    memset(data, 0xa5, sizeof(data));
    struct ps5_resource table={.base={.screen=&screen,.target=PIPE_BUFFER},
        .data=data,.size=sizeof(data)}, resources[4];
    struct test_nir nir[4]={0};
    struct ps5_shader shaders[4]={0};
    ctx.descriptor_storage[0]=&table.base;
    ctx.vs=&shaders[0]; ctx.tcs=&shaders[1]; ctx.tes=&shaders[2]; ctx.gs=&shaders[3];
    const unsigned slots[]={0,3,4,2};
    for(unsigned i=0;i<4;++i) {
        nir[i].info.stage=i;
        nir[i].info.num_ubos=nir[i].info.num_ssbos=1;
        nir[i].info.first_ubo_is_default_ubo=true;
        shaders[i].nir=&nir[i]; shaders[i].stage=PSBC_STAGE_VERTEX+i;
        resources[i]=(struct ps5_resource){.base={.screen=&screen,.target=PIPE_BUFFER},
            .data=buffers[i],.size=64};
        ctx.constants[slots[i]][0]=(struct ps5_constant_state){.valid=true,.copied=true,.size=64};
        struct pipe_shader_buffer bound={.buffer=&resources[i].base,.buffer_size=64};
        if(i==3) ctx.geometry_buffers[0]=bound; else ctx.preraster_buffers[i][0]=bound;
    }
    PsbcCompileOptions options={0};
    assert(ps5_tessellation_buffer_layout(&ctx,&options));
    assert(options.descriptor_binding_count==8 && options.gallium_buffer_arrays);
    PsbcShaderMetadata metadata={.descriptor_binding_count=8,.descriptor_set0_valid=true,
        .descriptor_set0_user_data_dword=2,.address32_hi=(uintptr_t)data>>32};
    memcpy(metadata.descriptor_bindings,options.descriptor_bindings,sizeof(options.descriptor_bindings));
    uint32_t user[8]={0};
    fragment_mode=true;
    for(unsigned sgpr=2;sgpr<4;++sgpr) {
        metadata.descriptor_set0_user_data_dword=sgpr;
        assert(ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
        assert(user[sgpr]==(uint32_t)(uintptr_t)data);
    }
    for(unsigned i=0;i<PS5_TESSELLATION_BUFFER_OFFSET;++i) assert(data[i]==0xa5);
    for(unsigned i=0;i<4;++i) {
        uint32_t *ubo=(uint32_t *)(data+options.descriptor_bindings[2*i].offset);
        uint32_t *ssbo=(uint32_t *)(data+options.descriptor_bindings[2*i+1].offset);
        uintptr_t addr=(uintptr_t)data+ps5_copied_constant_offset(slots[i]);
        assert(ubo[0]==(uint32_t)addr && ubo[1]==addr>>32 && ubo[2]==64);
        assert(ssbo[0]==(uint32_t)(uintptr_t)buffers[i] && ssbo[2]==64);
    }
    nir[2].info.num_ssbos=4;
    ctx.preraster_buffers[2][3]=ctx.preraster_buffers[2][0];
    assert(ps5_tessellation_buffer_layout(&ctx,&options));
    metadata.descriptor_binding_count=options.descriptor_binding_count;
    memcpy(metadata.descriptor_bindings,options.descriptor_bindings,sizeof(options.descriptor_bindings));
    assert(ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
    uint32_t *sparse=(uint32_t *)(data+options.descriptor_bindings[5].offset);
    assert(!sparse[4] && !sparse[8] && sparse[12]==(uint32_t)(uintptr_t)buffers[2]);
    nir[2].info.num_ssbos=1;
    memset(&ctx.preraster_buffers[2][3],0,sizeof(ctx.preraster_buffers[2][3]));
    assert(ps5_tessellation_buffer_layout(&ctx,&options));
    metadata.descriptor_binding_count=options.descriptor_binding_count;
    memcpy(metadata.descriptor_bindings,options.descriptor_bindings,sizeof(options.descriptor_bindings));
    metadata.descriptor_set0_user_data_dword=8;
    assert(!ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
    metadata.descriptor_set0_user_data_dword=2;
    ++metadata.descriptor_bindings[7].offset;
    assert(!ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
    --metadata.descriptor_bindings[7].offset;
    ctx.preraster_bindings_invalid[1]=true;
    assert(!ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
    ctx.preraster_bindings_invalid[1]=false;
    resources[2].base.screen=&foreign;
    assert(!ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
    resources[2].base.screen=&screen;
    ctx.preraster_buffers[2][0].buffer_offset=1;
    assert(!ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
    ctx.preraster_buffers[2][0].buffer_offset=0;
    ctx.constants[4][0].valid=false;
    assert(!ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
    ctx.constants[4][0].valid=true;
    _Alignas(256) uint8_t pixels[2048]={0};
    struct ps5_resource image={.base={.screen=&screen,.target=PIPE_TEXTURE_2D,
        .format=PIPE_FORMAT_R32_UINT,.width0=8,.height0=8,.depth0=1,.array_size=1,
        .bind=PIPE_BIND_SHADER_IMAGE|PIPE_BIND_SAMPLER_VIEW},.data=pixels,.size=sizeof(pixels),
        .allocation_size=sizeof(pixels),.level_stride={256}};
    pipe_reference_init(&image.base.reference,1);
    struct pipe_image_view view={.resource=&image.base,.format=PIPE_FORMAT_R32_UINT,
        .access=PIPE_IMAGE_ACCESS_READ_WRITE,.u.tex.single_layer_view=true};
    unsigned image_drains=fragment_drains;
    nir[1].info.num_images=1;
    ps5_set_shader_images(&ctx.base,MESA_SHADER_TESS_CTRL,0,1,0,&view);
    assert(fragment_drains==image_drains+1 && image.base.reference.count==2);
    ps5_set_shader_images(&ctx.base,MESA_SHADER_TESS_CTRL,0,1,0,&view);
    ps5_set_shader_images(&ctx.base,MESA_SHADER_TESS_CTRL,0,0,0,NULL);
    assert(fragment_drains==image_drains+1 && !ctx.preraster_images_invalid[MESA_SHADER_TESS_CTRL]);
    view.access=0;
    ps5_set_shader_images(&ctx.base,MESA_SHADER_TESS_CTRL,0,1,0,&view);
    assert(ctx.preraster_images_invalid[MESA_SHADER_TESS_CTRL] && fragment_drains==image_drains+1);
    view.access=PIPE_IMAGE_ACCESS_READ_WRITE;
    ps5_set_shader_images(&ctx.base,MESA_SHADER_TESS_CTRL,0,1,0,&view);
    assert(!ctx.preraster_images_invalid[MESA_SHADER_TESS_CTRL] && fragment_drains==image_drains+1);
    view.shader_access=PIPE_IMAGE_ACCESS_READ;
    ps5_set_shader_images(&ctx.base,MESA_SHADER_TESS_CTRL,0,1,0,&view);
    assert(fragment_drains==image_drains+2 && image.base.reference.count==2);
    assert(ps5_tessellation_buffer_layout(&ctx,&options));
    assert(options.descriptor_binding_count==9);
    metadata.descriptor_binding_count=options.descriptor_binding_count;
    memcpy(metadata.descriptor_bindings,options.descriptor_bindings,sizeof(options.descriptor_bindings));
    assert(ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
    assert(*(uint32_t *)(data+options.descriptor_bindings[4].offset)==
           (uint32_t)((uintptr_t)pixels>>8));
    ps5_set_shader_images(&ctx.base,MESA_SHADER_TESS_CTRL,0,0,1,NULL);
    assert(fragment_drains==image_drains+3 && image.base.reference.count==1);
    ps5_set_shader_images(&ctx.base,MESA_SHADER_TESS_CTRL,0,0,1,NULL);
    assert(fragment_drains==image_drains+3);
    fragment_drains=image_drains;
    nir[1].info.num_images=0;
    nir[1].info.num_ssbos=17;
    assert(!ps5_tessellation_buffer_layout(&ctx,&options));
    nir[1].info.num_ssbos=1;
    for(unsigned stage=0;stage<4;++stage)
        for(unsigned unit=0;unit<16;++unit)
            BITSET_SET(nir[stage].info.textures_used,unit);
    assert(ps5_tessellation_buffer_layout(&ctx,&options));
    assert(options.descriptor_binding_count==72);
    for(unsigned stage=0;stage<4;++stage)
        for(unsigned unit=0;unit<16;++unit) {
            const PsbcDescriptorBinding *binding=&options.descriptor_bindings[stage*18+2+unit];
            assert(binding->binding==16+stage*16+unit);
            assert(binding->type==PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER);
            assert(binding->offset==PS5_TESSELLATION_TEXTURE_OFFSET+(stage*16+unit)*48);
            assert(binding->offset+binding->stride<=sizeof(data));
            for(unsigned prior=0;prior<stage*18+2+unit;++prior)
                assert(binding->binding!=options.descriptor_bindings[prior].binding);
        }
    metadata.descriptor_binding_count=options.descriptor_binding_count;
    memcpy(metadata.descriptor_bindings,options.descriptor_bindings,sizeof(options.descriptor_bindings));
    assert(ps5_prepare_tessellation_buffers(&ctx,&metadata,user,8));
    for(unsigned i=PS5_TESSELLATION_TEXTURE_OFFSET;i<sizeof(data);++i)
        assert(data[i]==0xa5); /* Buffer preparation must not clobber textures. */
    fragment_mode=false;
}

static void vertex_storage_contract(void) {
    struct pipe_screen screen={0}, foreign={0};
    struct ps5_context ctx={.base.screen=&screen};
    uint8_t data[PS5_DESCRIPTOR_STORAGE_BYTES]={0}, bytes[64]={0};
    struct ps5_resource table={.base={.screen=&screen,.target=PIPE_BUFFER},
        .data=data,.size=sizeof(data)};
    struct ps5_resource resource={.base={.screen=&screen,.target=PIPE_BUFFER},
        .data=bytes,.size=sizeof(bytes)};
    struct test_nir nir={.info={.num_ssbos=4}};
    struct ps5_shader shader={.nir=&nir,.stage=PSBC_STAGE_VERTEX};
    ctx.vs=&shader; ctx.descriptor_storage[0]=&table.base;
    ctx.preraster_buffers[0][3]=(struct pipe_shader_buffer){
        .buffer=&resource.base,.buffer_offset=16,.buffer_size=32};
    PsbcShaderMetadata metadata={.descriptor_binding_count=1,
        .descriptor_set0_valid=true,.descriptor_set0_user_data_dword=2,
        .address32_hi=(uintptr_t)data>>32,.descriptor_bindings={{
            .binding=PSBC_GALLIUM_SSBO_ARRAY_BINDING(PSBC_STAGE_VERTEX),
            .type=PSBC_DESCRIPTOR_STORAGE_BUFFER,.array_size=16,
            .offset=PS5_VERTEX_STORAGE_OFFSET,.stride=16}}};
    uint32_t user[4]={0}; fragment_mode=true;
    assert(ps5_prepare_vertex_storage(&ctx,&metadata,user,4));
    uint32_t *srd=(uint32_t *)(data+PS5_VERTEX_STORAGE_OFFSET);
    assert(!srd[0] && !srd[4] && !srd[8]);
    assert(srd[12]==(uint32_t)(uintptr_t)(bytes+16) && srd[14]==32);
    resource.base.screen=&foreign;
    assert(!ps5_prepare_vertex_storage(&ctx,&metadata,user,4));
    fragment_mode=false;
}

static void test_stage_samplers(void) {
    struct ps5_context ctx={0};
    const mesa_shader_stage stages[]={MESA_SHADER_VERTEX,MESA_SHADER_FRAGMENT,
        MESA_SHADER_GEOMETRY,MESA_SHADER_TESS_CTRL,MESA_SHADER_TESS_EVAL};
    struct pipe_sampler_view views[5]={0};
    unsigned states[5]={0};
    for(unsigned stage=0;stage<5;++stage) {
        pipe_reference_init(&views[stage].reference,1);
        struct pipe_sampler_view *view=&views[stage];
        void *state=&states[stage];
        ps5_set_sampler_views(&ctx.base,stages[stage],11,1,0,&view);
        ps5_bind_sampler_states(&ctx.base,stages[stage],11,1,&state);
        assert(views[stage].reference.count==2);
        for(unsigned prior=0;prior<=stage;++prior) {
            assert(ctx.sampler_views[prior][11]==&views[prior]);
            assert(ctx.samplers[prior][11]==&states[prior]);
        }
        ps5_set_sampler_views(&ctx.base,stages[stage],16,1,0,&view);
        assert(views[stage].reference.count==2);
    }
    for(unsigned stage=0;stage<5;++stage) {
        ps5_set_sampler_views(&ctx.base,stages[stage],0,0,16,NULL);
        void *empty=NULL;
        ps5_bind_sampler_states(&ctx.base,stages[stage],11,1,&empty);
        assert(views[stage].reference.count==1);
        assert(!ctx.sampler_views[stage][11] && !ctx.samplers[stage][11]);
    }
}

static void constant_buffer_64k_contract(void) {
    struct pipe_screen screen={.resource_destroy=destroy};
    static uint8_t bytes[2 * PS5_MAX_CONSTANT_BUFFER_SIZE + 16];
    struct ps5_resource resource={.base={.screen=&screen,.target=PIPE_BUFFER,
        .width0=sizeof(bytes)},.data=bytes,.size=sizeof(bytes)};
    struct ps5_context context={.base={.screen=&screen}};
    pipe_reference_init(&resource.base.reference,1);
    const mesa_shader_stage stages[]={MESA_SHADER_VERTEX,MESA_SHADER_FRAGMENT,
        MESA_SHADER_GEOMETRY,MESA_SHADER_TESS_CTRL,MESA_SHADER_TESS_EVAL};
    const unsigned slots[]={0,1,2,3,4};
    for(unsigned stage=0;stage<5;++stage) {
        struct pipe_constant_buffer first={.buffer=&resource.base,.buffer_offset=16,
            .buffer_size=PS5_MAX_CONSTANT_BUFFER_SIZE};
        struct pipe_constant_buffer second=first;
        second.buffer_offset+=PS5_MAX_CONSTANT_BUFFER_SIZE;
        ps5_set_constant_buffer(&context.base,stages[stage],8,&first);
        ps5_set_constant_buffer(&context.base,stages[stage],9,&second);
        assert(context.constants[slots[stage]][8].valid &&
               context.constants[slots[stage]][8].size==PS5_MAX_CONSTANT_BUFFER_SIZE);
        assert(context.constants[slots[stage]][9].valid &&
               context.constants[slots[stage]][9].offset==second.buffer_offset);
    }
    struct pipe_constant_buffer first={.buffer=&resource.base,.buffer_offset=16,
        .buffer_size=PS5_MAX_CONSTANT_BUFFER_SIZE};
    struct pipe_constant_buffer second=first;
    second.buffer_offset+=PS5_MAX_CONSTANT_BUFFER_SIZE;
    ps5_set_compute_constant_buffer(&context.base,8,&first);
    ps5_set_compute_constant_buffer(&context.base,9,&second);
    assert(context.compute_buffers[PS5_COMPUTE_STORAGE_SLOTS+8].buffer_size==
           PS5_MAX_CONSTANT_BUFFER_SIZE);
    assert(context.compute_buffers[PS5_COMPUTE_STORAGE_SLOTS+9].buffer_offset==
           second.buffer_offset);
    for(unsigned stage=0;stage<5;++stage) {
        ps5_set_constant_buffer(&context.base,stages[stage],8,NULL);
        ps5_set_constant_buffer(&context.base,stages[stage],9,NULL);
    }
    ps5_set_compute_constant_buffer(&context.base,8,NULL);
    ps5_set_compute_constant_buffer(&context.base,9,NULL);
    assert(resource.base.reference.count==1);
}

int main(void) {
    test_stage_samplers();
    constant_buffer_64k_contract();
    vertex_storage_contract();
    test_tessellation_buffers();
    preraster_binding_contract();
    atomic_handoff_contract();
    stale_cleanup_regression();
    fragment_contract();
    geometry_storage_contract();
    preraster_image_contract();
    indirect_ubo_contract();
    texel_buffer_descriptor_contract();
    assert(PS5_COMPUTE_TEXTURE_SLOTS==16 && PS5_AGC_COMPUTE_MAX_RESOURCES==79);
    struct pipe_screen screen={.resource_destroy=destroy}, other_screen={0};
    struct pipe_context barrier_context={0};
    unsigned barrier_before=fragment_drains, submits_before=barrier_submissions;
    ps5_memory_barrier(&barrier_context,0);
    ps5_texture_barrier(&barrier_context,0);
    assert(fragment_drains==barrier_before && barrier_submissions==submits_before);
    for(unsigned mask=1;mask<=PIPE_BARRIER_ALL;++mask) {
        ps5_memory_barrier(&barrier_context,mask);
        if(mask & PIPE_BARRIER_MAPPED_BUFFER) ++barrier_before;
        else ++submits_before;
        assert(fragment_drains==barrier_before && barrier_submissions==submits_before);
    }
    for(unsigned mask=1;mask<=3;++mask) {
        ps5_texture_barrier(&barrier_context,mask);
        assert(fragment_drains==barrier_before && barrier_submissions==++submits_before);
    }
    ps5_memory_barrier(&barrier_context,1u<<31);
    ps5_texture_barrier(&barrier_context,1u<<31);
    assert(fragment_drains==barrier_before+2 && barrier_submissions==submits_before);
    uint8_t table_data[PS5_COMPUTE_DESCRIPTOR_BYTES], output[256];
    struct ps5_resource table={.data=table_data};
    struct ps5_resource buffer={.base={.screen=&screen,.target=PIPE_BUFFER,.width0=256},.data=output};
    pipe_reference_init(&buffer.base.reference, 1);
    struct ps5_compute_shader cs={.ssbos=16,.ubos=15,.images=8};
    cs.output.metadata.compute_workgroup_size[0]=16;
    cs.output.metadata.compute_workgroup_size[1]=cs.output.metadata.compute_workgroup_size[2]=1;
    struct ps5_context context={.base={.screen=&screen},.cs=&cs,.compute_descriptors=&table.base};
    struct pipe_shader_buffer binding={&buffer.base,16,64};
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,15,1,&binding,1);
    assert(!context.compute_bindings_invalid && buffer.base.reference.count==2);
    struct pipe_resource *caller=&buffer.base;
    pipe_resource_reference(&caller,NULL);
    assert(buffer.base.reference.count==1 && !destroyed);
    /* Rebind while the binding itself owns the last reference. */
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,15,1,&binding,1);
    assert(buffer.base.reference.count==1 && !destroyed);
    const struct pipe_grid_info good={.work_dim=1,.block={16,1,1},.grid={2,1,1}};
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && context.dispatches==1 && submitted==1);
    render_condition_pass=false;
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && context.dispatches==1 && submitted==1);
    render_condition_pass=true;
    struct pipe_shader_buffer pair[2]={binding,binding};
    pair[1].buffer_size=UINT32_MAX;
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,14,2,pair,3);
    assert(context.compute_bindings_invalid && !context.compute_buffers[14].buffer);
    assert(buffer.base.reference.count==1 && !destroyed);
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,15,1,&binding,1);
    for (unsigned fault=0; fault<8; ++fault) {
        struct pipe_shader_buffer bad=binding;
        if (fault==0) bad.buffer_offset=UINT32_MAX;
        if (fault==1) bad.buffer_offset=1;
        if (fault==2) bad.buffer_size=UINT32_MAX;
        if (fault==3) bad.buffer_size=0;
        if (fault==4) buffer.base.target=PIPE_TEXTURE_2D;
        if (fault==5) buffer.base.screen=&other_screen;
        unsigned start=fault==6 ? 16 : 15, count=fault==7 ? UINT32_MAX : 1;
        ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,start,count,&bad,1);
        assert(context.compute_bindings_invalid && buffer.base.reference.count==1 && !destroyed);
        assert(context.compute_buffers[15].buffer_offset==16 && context.compute_buffers[15].buffer_size==64);
        ps5_launch_grid(&context.base,&good);
        assert(context.last_compute_status<0 && submitted==1);
        buffer.base.target=PIPE_BUFFER; buffer.base.screen=&screen;
        ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,15,1,&binding,1);
    }
    for (unsigned fault=0; fault<8; ++fault) {
        struct pipe_grid_info bad=good;
        if (fault==0) bad.block[0]=17;
        if (fault==1) bad.grid_base[0]=1;
        if (fault==2) bad.last_block[0]=8;
        if (fault==3) bad.grid[0]=65536;
        if (fault==4) bad.variable_shared_mem=1024;
        if (fault==5) { bad.indirect=&buffer.base; bad.indirect_offset=1; }
        if (fault==6) bad.num_globals=1;
        if (fault==7) bad.work_dim=4;
        ps5_launch_grid(&context.base,&bad);
        assert(context.last_compute_status<0 && submitted==1);
    }
    struct pipe_grid_info empty=good; empty.grid[1]=0;
    ps5_launch_grid(&context.base,&empty);
    assert(!context.last_compute_status && submitted==1);
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,15,1,NULL,0);
    assert(!context.compute_buffers[15].buffer && destroyed==1);
    /* Fifteen nonoverlapping ranges of one retained resource plus output. */
    destroyed=0; multi=true;
    uint8_t input[256];
    struct ps5_resource ranges={.base={.screen=&screen,.target=PIPE_BUFFER,.width0=256},.data=input};
    pipe_reference_init(&ranges.base.reference,1);
    pipe_reference_init(&buffer.base.reference,1);
    struct pipe_shader_buffer bindings[16];
    for (unsigned i=0; i<15; ++i)
        bindings[i]=(struct pipe_shader_buffer){&ranges.base,i*16,16};
    bindings[15]=binding;
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,0,16,bindings,1u<<15);
    caller=&ranges.base; pipe_resource_reference(&caller,NULL);
    caller=&buffer.base; pipe_resource_reference(&caller,NULL);
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && submitted==2 && context.dispatches==2);
    for (unsigned i=0; i<15; ++i) {
        struct pipe_constant_buffer cb={.buffer=&ranges.base,.buffer_offset=i*16,.buffer_size=16};
        ps5_set_compute_constant_buffer(&context.base,i,&cb);
    }
    with_constants=true;
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && submitted==3 && context.dispatches==3);
    for (unsigned fault=0; fault<7; ++fault) {
        struct pipe_constant_buffer cb={.buffer=&ranges.base,.buffer_offset=224,.buffer_size=16};
        if (fault==0) cb.buffer_offset=UINT32_MAX;
        if (fault==1) cb.buffer_offset=1;
        if (fault==2) cb.buffer_size=UINT32_MAX;
        if (fault==3) cb.buffer_size=0;
        if (fault==4) ranges.base.target=PIPE_TEXTURE_2D;
        if (fault==5) ranges.base.screen=&other_screen;
        if (fault==6) cb.user_buffer=input; /* Ambiguous resource plus user pointer. */
        ps5_set_compute_constant_buffer(&context.base,14,&cb);
        assert(context.compute_constants_invalid==(1u<<14) && ranges.base.reference.count==30);
        assert(context.compute_buffers[30].buffer_offset==224 && context.compute_buffers[30].buffer_size==16);
        ranges.base.target=PIPE_BUFFER; ranges.base.screen=&screen;
        const struct pipe_constant_buffer other={.buffer=&ranges.base,.buffer_size=16};
        ps5_set_compute_constant_buffer(&context.base,0,&other);
        assert(context.compute_constants_invalid==(1u<<14));
        ps5_launch_grid(&context.base,&good);
        assert(context.last_compute_status<0 && submitted==3);
        cb=(struct pipe_constant_buffer){.buffer=&ranges.base,.buffer_offset=224,.buffer_size=16};
        ps5_set_compute_constant_buffer(&context.base,14,&cb);
        assert(!context.compute_constants_invalid);
    }
    for (unsigned i=0; i<15; ++i)
        ps5_set_compute_constant_buffer(&context.base,i,NULL);
    assert(ranges.base.reference.count==15);
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,0,16,NULL,0);
    assert(destroyed==2);
    /* The binding must own a copy after the caller changes stack constants. */
    uint8_t upload_data[256], constants[64]; memset(constants,0x57,sizeof(constants));
    struct ps5_resource uploaded={.base={.screen=&screen,.target=PIPE_BUFFER,.width0=256},.data=upload_data};
    pipe_reference_init(&uploaded.base.reference,1);
    upload_resource=&uploaded;
    context.base.const_uploader=(void *)(uintptr_t)1;
    const struct pipe_constant_buffer user={.user_buffer=constants,.buffer_size=sizeof(constants)};
    ps5_set_compute_constant_buffer(&context.base,0,&user);
    assert(!context.compute_constants_invalid && uploaded.base.reference.count==2);
    assert(context.compute_buffers[16].buffer_offset==32 && context.compute_buffers[16].buffer_size==64);
    memset(constants,0,sizeof(constants));
    for (unsigned i=32; i<96; ++i) assert(upload_data[i]==0x57);
    fail_upload=true;
    ps5_set_compute_constant_buffer(&context.base,0,&user);
    assert(context.compute_constants_invalid==1 && uploaded.base.reference.count==2);
    assert(context.compute_buffers[16].buffer==&uploaded.base);
    ps5_launch_grid(&context.base,&good);
    assert(context.last_compute_status<0 && submitted==3);
    fail_upload=false;
    ps5_set_compute_constant_buffer(&context.base,0,&user);
    assert(!context.compute_constants_invalid && uploaded.base.reference.count==2);
    ps5_set_compute_constant_buffer(&context.base,0,NULL);
    assert(uploaded.base.reference.count==1);
    caller=&uploaded.base; pipe_resource_reference(&caller,NULL);
    assert(destroyed==3);
    /* Indirect dimensions replace the caller's direct grid, after validation. */
    multi=with_constants=false; destroyed=0;
    pipe_reference_init(&buffer.base.reference,1);
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,15,1,&binding,1);
    caller=&buffer.base; pipe_resource_reference(&caller,NULL);
    const uint32_t dimensions[3]={2,1,1};
    memcpy(output+16,dimensions,sizeof(dimensions));
    struct pipe_grid_info indirect=good;
    memset(indirect.grid,0,sizeof(indirect.grid));
    indirect.indirect=&buffer.base; indirect.indirect_offset=16;
    ps5_launch_grid(&context.base,&indirect);
    assert(!context.last_compute_status && submitted==4 && context.dispatches==4);
    for (unsigned fault=0; fault<7; ++fault) {
        struct pipe_grid_info bad=indirect;
        if (fault==0) bad.indirect_offset=UINT32_MAX;
        if (fault==1) bad.indirect_offset=17;
        if (fault==2) bad.indirect_offset=248; /* Eight bytes are not a command. */
        if (fault==3) buffer.base.target=PIPE_TEXTURE_2D;
        if (fault==4) buffer.base.screen=&other_screen;
        if (fault==5) fail_info=true;
        if (fault==6) { uint32_t large=65536; memcpy(output+16,&large,4); }
        ps5_launch_grid(&context.base,&bad);
        assert(context.last_compute_status<0 && submitted==4 && buffer.base.reference.count==1);
        buffer.base.target=PIPE_BUFFER; buffer.base.screen=&screen; fail_info=false;
        memcpy(output+16,dimensions,sizeof(dimensions));
    }
    memset(output+16,0,4);
    ps5_launch_grid(&context.base,&indirect);
    assert(!context.last_compute_status && submitted==4);
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,15,1,NULL,0);
    assert(destroyed==1);
    /* Image descriptor, atomic binding updates, ownership and all 39 resources. */
    destroyed=0;
    _Alignas(256) uint8_t pixels[768];
    struct ps5_resource image={.base={.screen=&screen,.target=PIPE_TEXTURE_2D,
        .format=PIPE_FORMAT_R32_UINT,.width0=17,.height0=3,.depth0=1,.array_size=1,
        .bind=PIPE_BIND_SHADER_IMAGE|PIPE_BIND_SAMPLER_VIEW},.data=pixels,.size=sizeof(pixels),.allocation_size=sizeof(pixels),.level_stride={256}};
    uint32_t descriptor[8];
    assert(!ps5_resource_storage_image_descriptor(&image.base,0,descriptor));
    assert(descriptor[0]==(uint32_t)((uintptr_t)pixels>>8));
    assert(descriptor[2]==(4u|(2u<<14)|0x80000000u));
    assert(descriptor[3]==0x90000204 && descriptor[4]==63 && descriptor[5]==0x400000);
    /* Native 4x multisample storage images retain their sample layout and
     * array sublayer addressing through the storage-image path. */
    _Alignas(65536) static uint8_t msaa_pixels[131072];
    struct ps5_resource msaa=image;
    msaa.base.width0=8; msaa.base.height0=8;
    msaa.base.nr_samples=msaa.base.nr_storage_samples=4;
    msaa.base.bind=PIPE_BIND_SHADER_IMAGE;
    msaa.data=msaa_pixels; msaa.size=8*8*4*4;
    msaa.allocation_size=65536; msaa.level_stride[0]=8*4;
    msaa.layer_stride=8*8*4;
    assert(!ps5_resource_storage_image_descriptor(&msaa.base,0,descriptor));
    assert((descriptor[3]&0xf0000000u)==0xe0000000u && descriptor[4]==0);
    assert(((descriptor[5]>>4)&15)==2);
    assert(!ps5_resource_storage_image_descriptor_owned(&msaa.base,descriptor));
    msaa.base.target=PIPE_TEXTURE_2D_ARRAY; msaa.base.array_size=2;
    msaa.size*=2; msaa.allocation_size=sizeof(msaa_pixels);
    msaa.layer_stride=65536;
    assert(!ps5_resource_storage_image_descriptor(&msaa.base,0,descriptor));
    assert((descriptor[3]&0xf0000000u)==0xf0000000u && descriptor[4]==1);
    struct pipe_image_view msaa_layer={.resource=&msaa.base,
        .format=msaa.base.format,.access=PIPE_IMAGE_ACCESS_READ_WRITE,
        .u.tex={.first_layer=1,.last_layer=1,.single_layer_view=true}};
    assert(!ps5_storage_image_view_descriptor(&msaa_layer,descriptor));
    assert((descriptor[3]&0xf0000000u)==0xe0000000u && descriptor[4]==0);
    assert(!ps5_resource_storage_image_descriptor_owned(&msaa.base,descriptor));
    {
        struct ps5_resource srgb=image;
        srgb.base.format=PIPE_FORMAT_R8G8B8A8_SRGB;
        srgb.base.bind=PIPE_BIND_SAMPLER_VIEW;
        assert(!ps5_resource_sampled_image_descriptor(&srgb.base,0,0,descriptor));
        assert(((descriptor[1]>>20)&0x1ff)==0x82 && (descriptor[3]&0xfff)==0xfac);
        assert(ps5_resource_storage_image_descriptor(&srgb.base,0,descriptor));
    }
    {
        struct ps5_resource depth=image;
        depth.base.format=PIPE_FORMAT_Z32_FLOAT;
        depth.base.bind=PIPE_BIND_SAMPLER_VIEW;
        assert(!ps5_resource_sampled_image_descriptor(&depth.base,0,0,descriptor));
        assert((descriptor[3]&0xfff)==0x204);
        assert(ps5_resource_storage_image_descriptor(&depth.base,0,descriptor));
        depth.depth_staging_offset=depth.size;
        depth.depth_staging_size=256;
        assert(ps5_resource_sampled_image_descriptor(&depth.base,0,0,descriptor));
        depth.depth_staging_size=0;
        depth.level_stride[0]-=4;
        assert(ps5_resource_sampled_image_descriptor(&depth.base,0,0,descriptor));
    }
    _Alignas(256) uint8_t image_buffer_data[64];
    struct ps5_resource image_buffer={.base={.screen=&screen,.target=PIPE_BUFFER,
        .format=PIPE_FORMAT_R8_UNORM,.width0=sizeof(image_buffer_data)},.data=image_buffer_data,
        .size=sizeof(image_buffer_data),.allocation_size=sizeof(image_buffer_data)};
    pipe_reference_init(&image_buffer.base.reference,1);
    struct pipe_image_view image_buffer_view={.resource=&image_buffer.base,
        .format=PIPE_FORMAT_R32_UINT,.access=PIPE_IMAGE_ACCESS_READ};
    image_buffer_view.u.buf.size=sizeof(image_buffer_data);
    ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,1,0,&image_buffer_view);
    assert(!context.compute_images_invalid && image_buffer.base.reference.count==2);
    ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,0,1,NULL);
    assert(image_buffer.base.reference.count==1);
    /* Mesa default_bindings: ordinary R32F uses SAMPLER_VIEW|RENDER_TARGET,
     * without SHADER_IMAGE. Both descriptors must address canonical bytes. */
    _Alignas(65536) static uint8_t canonical_pixels[131072];
    struct ps5_resource canonical=image;
    canonical.base.format=PIPE_FORMAT_R32_FLOAT;
    canonical.base.bind=PIPE_BIND_SAMPLER_VIEW|PIPE_BIND_RENDER_TARGET;
    canonical.data=canonical_pixels; canonical.allocation_size=sizeof(canonical_pixels);
    canonical.render_staging_offset=65536; canonical.render_staging_size=65536;
    memset(canonical_pixels,0x79,sizeof(canonical_pixels));
    assert(ps5_linear_sampled_layout(&canonical.base));
    uint32_t canonical_srd[8], sampled_srd[8];
    assert(!ps5_resource_storage_image_descriptor(&canonical.base,0,canonical_srd));
    assert(!ps5_resource_sampled_image_descriptor(&canonical.base,0,0,sampled_srd));
    assert(!memcmp(canonical_srd,sampled_srd,sizeof(canonical_srd)));
    assert(canonical_srd[0]==(uint32_t)((uintptr_t)canonical_pixels>>8));
    for(unsigned i=0;i<sizeof(canonical_pixels);++i) assert(canonical_pixels[i]==0x79);
    pipe_reference_init(&canonical.base.reference,1);
    for(unsigned stage=0;stage<2;++stage) {
        mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
        struct pipe_image_view view={.resource=&canonical.base,.format=canonical.base.format,
            .access=PIPE_IMAGE_ACCESS_READ_WRITE};
        ps5_set_shader_images(&context.base,which,7,1,0,&view);
        assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
        assert(canonical.base.reference.count==2);
        view.format=PIPE_FORMAT_R32_UINT;
        ps5_set_shader_images(&context.base,which,7,1,0,&view);
        assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
        assert(canonical.base.reference.count==2);
        ps5_set_shader_images(&context.base,which,7,0,1,NULL);
        assert(canonical.base.reference.count==1);
    }
    for(unsigned fault=0;fault<15;++fault) {
        struct ps5_resource bad=canonical;
        if(fault==0) bad.base.bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SHADER_IMAGE;
        if(fault==1) bad.base.bind|=PIPE_BIND_DEPTH_STENCIL;
        if(fault==2) bad.base.bind|=PIPE_BIND_DISPLAY_TARGET;
        if(fault==3) bad.render_staging_size=0;
        if(fault==4) bad.render_staging_offset=0;
        if(fault==5) bad.render_staging_offset=bad.size-1;
        if(fault==6) bad.render_staging_offset=bad.allocation_size;
        if(fault==7) bad.render_staging_size++;
        if(fault==8) bad.allocation_size=bad.size-1;
        if(fault==9) bad.render_staging_size=SIZE_MAX;
        if(fault==10) bad.level_offset[0]=256;
        if(fault==11) bad.level_stride[0]=512;
        if(fault==12) bad.base.format=PIPE_FORMAT_R16G16B16A16_FLOAT; /* native tiled */
        if(fault==13) bad.base.bind=PIPE_BIND_SAMPLER_VIEW; /* orphan staging */
        if(fault==14) bad.render_staging_offset++;
        memset(descriptor,0xa5,sizeof(descriptor));
        assert(ps5_resource_storage_image_descriptor(&bad.base,0,descriptor)<0);
        assert(ps5_resource_sampled_image_descriptor(&bad.base,0,0,descriptor)<0);
        for(unsigned i=0;i<8;++i) assert(descriptor[i]==0xa5a5a5a5u);
    }
    canonical.base.bind=PIPE_BIND_SAMPLER_VIEW;
    canonical.render_staging_offset=canonical.render_staging_size=0;
    assert(!ps5_resource_storage_image_descriptor(&canonical.base,0,descriptor));
    assert(!memcmp(descriptor,canonical_srd,sizeof(descriptor)));
    /* Sampled and storage dimensions share canonical owned backing.
     * Volumes are single-level for now. */
    const unsigned sampled_targets[]={PIPE_TEXTURE_1D,PIPE_TEXTURE_1D_ARRAY,
        PIPE_TEXTURE_3D,PIPE_TEXTURE_RECT};
    const uint32_t sampled_types[]={0x80000000u,0xc0000000u,0xa0000000u,0x90000000u};
    for(unsigned target=0;target<ARRAY_SIZE(sampled_targets);++target) {
        struct ps5_resource dimensional=canonical;
        dimensional.base.target=sampled_targets[target];
        dimensional.base.height0=target<2 ? 1 : 3;
        dimensional.base.depth0=target==2 ? 4 : 1;
        dimensional.base.array_size=target==1 ? 4 : 1;
        dimensional.layer_stride=256*dimensional.base.height0;
        dimensional.size=dimensional.layer_stride*(target==1 || target==2 ? 4 : 1);
        assert(!ps5_resource_sampled_image_descriptor(&dimensional.base,0,0,descriptor));
        assert((descriptor[3]&0xf0000000u)==sampled_types[target]);
        assert(descriptor[4]==(target==1 || target==2 ? 3 : target==3 ? 63 : 0));
        assert(!ps5_resource_storage_image_descriptor(&dimensional.base,0,descriptor));
        for(unsigned stage=0;stage<2;++stage) {
            mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
            struct pipe_image_view v={.resource=&dimensional.base,.format=dimensional.base.format,
                .access=PIPE_IMAGE_ACCESS_READ_WRITE};
            v.u.tex.last_layer=target==1 || target==2 ? 3 : 0;
            v.u.tex.single_layer_view=target==0 || target==3;
            ps5_set_shader_images(&context.base,which,0,1,0,&v);
            assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
            ps5_set_shader_images(&context.base,which,0,0,1,NULL);
            assert(dimensional.base.reference.count==1);
        }
        /* Rectangle render targets now require staging; 1D hints do not. */
        if (target != 2) {
            dimensional.base.bind |= PIPE_BIND_RENDER_TARGET;
            if (target == 3) {
                assert(ps5_resource_sampled_image_descriptor(&dimensional.base,0,0,descriptor)<0);
                dimensional.render_staging_offset=65536;
                dimensional.render_staging_size=65536;
            }
            assert(!ps5_resource_sampled_image_descriptor(&dimensional.base,0,0,descriptor));
        }
        struct ps5_resource bad=dimensional;
        bad.allocation_size=bad.size-1;
        assert(ps5_resource_sampled_image_descriptor(&bad.base,0,0,descriptor)<0);
        if(target==1 || target==2) {
            bad=dimensional; --bad.size;
            assert(ps5_resource_sampled_image_descriptor(&bad.base,0,0,descriptor)<0);
            bad=dimensional; ++bad.layer_stride;
            assert(ps5_resource_sampled_image_descriptor(&bad.base,0,0,descriptor)<0);
        }
        if(target==2 || target==3) {
            bad=dimensional; bad.base.last_level=1;
            assert(ps5_resource_sampled_image_descriptor(&bad.base,0,0,descriptor)<0);
        }
    }
    /* Actual Mesa-generated table/driver encoding: RGBA16=65, RGBA8=56. */
    for (unsigned target=0; target<2; ++target) {
        _Alignas(256) uint8_t pixels[16*256];
        struct ps5_resource array=canonical;
        array.base.target=target ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_1D_ARRAY;
        array.base.height0=1; array.base.array_size=16;
        array.data=pixels; array.size=array.allocation_size=sizeof(pixels);
        array.layer_stride=256;
        assert(!ps5_resource_sampled_image_descriptor(&array.base,0,0,descriptor));
        assert(descriptor[4]==15);
        assert(!ps5_resource_storage_image_descriptor(&array.base,0,descriptor));
        --array.size;
        assert(ps5_resource_sampled_image_descriptor(&array.base,0,0,descriptor)<0);
        ++array.size; array.base.array_size=PS5_MAX_TEXTURE_ARRAY_LAYERS+1;
        assert(ps5_resource_sampled_image_descriptor(&array.base,0,0,descriptor)<0);
    }
    {
        _Alignas(256) uint8_t pixels[12*512];
        struct ps5_resource cube=canonical;
        cube.base.target=PIPE_TEXTURE_CUBE_ARRAY;
        cube.base.width0=cube.base.height0=2; cube.base.array_size=12;
        cube.data=pixels; cube.size=cube.allocation_size=sizeof(pixels);
        cube.layer_stride=512;
        assert(!ps5_resource_sampled_image_descriptor(&cube.base,0,0,descriptor));
        assert((descriptor[3]>>28)==11 && descriptor[4]==11);
        cube.base.array_size=6; cube.base.target=PIPE_TEXTURE_CUBE;
        assert(!ps5_resource_sampled_image_descriptor(&cube.base,0,0,descriptor));
        assert((descriptor[3]>>28)==11 && descriptor[4]==5);
        cube.base.array_size=12; cube.base.target=PIPE_TEXTURE_CUBE_ARRAY;
        struct pipe_image_view v={.resource=&cube.base,.format=cube.base.format,
            .access=PIPE_IMAGE_ACCESS_READ_WRITE};
        v.u.tex.last_layer=11;
        assert(!ps5_storage_image_view_descriptor(&v,descriptor));
        assert((descriptor[3]>>28)==13 && descriptor[4]==11);
        v.u.tex.first_layer=v.u.tex.last_layer=7; v.u.tex.single_layer_view=true;
        assert(!ps5_storage_image_view_descriptor(&v,descriptor));
        assert(descriptor[0]==(uint32_t)((uintptr_t)(pixels+7*512)>>8));
        assert((descriptor[3]>>28)==9 && descriptor[4]==63);
        uint32_t whole[8];
        assert(!ps5_resource_storage_image_descriptor(&cube.base,0,whole));
        assert(memcmp(whole,descriptor,sizeof(whole))); /* Previous submission check rejected this view. */
        assert(!ps5_resource_storage_image_descriptor_owned(&cube.base,descriptor));
        assert(ps5_resource_storage_image_descriptor_owned(&canonical.base,descriptor)<0);
        for(unsigned word=0;word<8;++word) {
            descriptor[word]^=1;
            assert(ps5_resource_storage_image_descriptor_owned(&cube.base,descriptor)<0);
            descriptor[word]^=1;
        }
        for(unsigned stage=0;stage<2;++stage) {
            mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
            ps5_set_shader_images(&context.base,which,0,1,0,&v);
            assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
            ps5_set_shader_images(&context.base,which,0,0,1,NULL);
        }
        v.u.tex.last_layer=12;
        assert(ps5_storage_image_view_descriptor(&v,descriptor)<0);
        v.u.tex.last_layer=7; v.u.tex.level=PIPE_MAX_TEXTURE_LEVELS;
        assert(ps5_storage_image_view_descriptor(&v,descriptor)<0);
        assert(cube.base.reference.count==1);
    }
    /* All core image formats use the same checked linear descriptor path. */
    {
        const enum pipe_format formats[]={
            PIPE_FORMAT_R8_UINT,
            PIPE_FORMAT_R8_SINT,
            PIPE_FORMAT_R8G8_UINT,
            PIPE_FORMAT_R8G8_SINT,
            PIPE_FORMAT_R8G8B8A8_UINT,
            PIPE_FORMAT_R8G8B8A8_SINT,
            PIPE_FORMAT_R16_UINT,
            PIPE_FORMAT_R16_SINT,
            PIPE_FORMAT_R16G16_UINT,
            PIPE_FORMAT_R16G16_SINT,
            PIPE_FORMAT_R16G16B16A16_UINT,
            PIPE_FORMAT_R16G16B16A16_SINT,
            PIPE_FORMAT_R32_UINT,
            PIPE_FORMAT_R32_SINT,
            PIPE_FORMAT_R32G32_UINT,
            PIPE_FORMAT_R32G32_SINT,
            PIPE_FORMAT_R32G32B32A32_UINT,
            PIPE_FORMAT_R32G32B32A32_SINT,
            PIPE_FORMAT_R8_UNORM,
            PIPE_FORMAT_R8_SNORM,
            PIPE_FORMAT_R8G8_UNORM,
            PIPE_FORMAT_R8G8_SNORM,
            PIPE_FORMAT_R8G8B8A8_UNORM,
            PIPE_FORMAT_R8G8B8A8_SNORM,
            PIPE_FORMAT_R16_UNORM,
            PIPE_FORMAT_R16_SNORM,
            PIPE_FORMAT_R16G16_UNORM,
            PIPE_FORMAT_R16G16_SNORM,
            PIPE_FORMAT_R16G16B16A16_UNORM,
            PIPE_FORMAT_R16G16B16A16_SNORM,
            PIPE_FORMAT_R16_FLOAT,
            PIPE_FORMAT_R16G16_FLOAT,
            PIPE_FORMAT_R16G16B16A16_FLOAT,
            PIPE_FORMAT_R32_FLOAT,
            PIPE_FORMAT_R32G32_FLOAT,
            PIPE_FORMAT_R32G32B32A32_FLOAT,
            PIPE_FORMAT_R10G10B10A2_UNORM,
            PIPE_FORMAT_R10G10B10A2_UINT,
            PIPE_FORMAT_R11G11B10_FLOAT};
        _Alignas(256) uint8_t pixels[2048];
        for(unsigned f=0;f<ARRAY_SIZE(formats);++f) {
            struct ps5_resource typed=canonical;
            typed.base.format=formats[f]; typed.base.width0=17; typed.base.height0=3;
            typed.data=pixels; typed.allocation_size=sizeof(pixels);
            const unsigned bytes=util_format_get_blocksize(formats[f]);
            typed.level_stride[0]=(17*bytes+255)&~255u;
            typed.size=typed.layer_stride=typed.level_stride[0]*3;
            assert(ps5_storage_image_texel_size(formats[f])==bytes);
            assert(!ps5_resource_storage_image_descriptor(&typed.base,0,descriptor));
            assert((descriptor[1]&0x3ff00000u)==(gfx10_format_table[formats[f]].img_format<<20));
            unsigned expected=0;
            for(unsigned lane=0;lane<4;++lane) {
                const unsigned selector=lane<util_format_get_nr_components(formats[f]) ? 4+lane : lane==3 ? 1 : 0;
                expected|=selector<<(lane*3);
            }
            assert((descriptor[3]&0xfffu)==expected);
            /* Equal-size image views preserve geometry and ownership in both stages. */
            for (unsigned alias=0;alias<ARRAY_SIZE(formats);++alias) {
                struct pipe_image_view v={.resource=&typed.base,.format=formats[alias],
                    .access=PIPE_IMAGE_ACCESS_READ_WRITE};
                const bool compatible=util_format_get_blocksize(formats[alias])==bytes;
                assert((ps5_storage_image_view_descriptor(&v,descriptor)==0)==compatible);
                if (compatible) {
                    assert(!ps5_resource_storage_image_descriptor_owned(&typed.base,descriptor));
                    descriptor[0]^=1;
                    assert(ps5_resource_storage_image_descriptor_owned(&typed.base,descriptor)<0);
                    descriptor[0]^=1;
                    descriptor[2]^=1;
                    assert(ps5_resource_storage_image_descriptor_owned(&typed.base,descriptor)<0);
                }
                for(unsigned stage=0;stage<2;++stage) {
                    mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
                    ps5_set_shader_images(&context.base,which,7,1,0,&v);
                    assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid)==compatible);
                    ps5_set_shader_images(&context.base,which,7,0,1,NULL);
                    assert(typed.base.reference.count==1);
                }
            }
            for(unsigned stage=0;stage<2;++stage) {
                mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
                struct pipe_image_view v={.resource=&typed.base,.format=formats[f],
                    .access=PIPE_IMAGE_ACCESS_READ_WRITE,.u.tex.single_layer_view=true};
                ps5_set_shader_images(&context.base,which,7,1,0,&v);
                assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
                ps5_set_shader_images(&context.base,which,7,0,1,NULL);
                assert(typed.base.reference.count==1);
            }
        }
    }
    const enum pipe_format normalized_formats[]={PIPE_FORMAT_R16G16B16A16_UNORM,PIPE_FORMAT_R8G8B8A8_UNORM};
    for(unsigned f=0;f<ARRAY_SIZE(normalized_formats);++f) {
    struct ps5_resource normalized=canonical;
    normalized.base.format=normalized_formats[f];
    const unsigned bytes=f ? 4 : 8, encoding=f ? 56 : 65;
    assert(ps5_storage_image_texel_size(normalized.base.format)==bytes);
    assert(ps5_storage_image_channels(normalized.base.format)==4);
    assert(gfx10_format_table[normalized.base.format].img_format==encoding &&
        !gfx10_format_table[normalized.base.format].buffers_only);
    for(unsigned staged=0;staged<2;++staged) {
        normalized.base.bind=PIPE_BIND_SAMPLER_VIEW | (staged ? PIPE_BIND_RENDER_TARGET : 0);
        normalized.render_staging_offset=normalized.render_staging_size=staged ? 65536 : 0;
        if(f && staged) {
            /* Single-mip RGBA8 render/sample backing is native tiled. Even
             * plausible linear strides/staging cannot authorize a linear SRD. */
            memset(descriptor,0xa5,sizeof(descriptor));
            assert(!ps5_linear_sampled_layout(&normalized.base));
            assert(ps5_resource_storage_image_descriptor(&normalized.base,0,descriptor)<0);
            assert(ps5_resource_sampled_image_descriptor(&normalized.base,0,0,descriptor)<0);
            for(unsigned i=0;i<8;++i) assert(descriptor[i]==0xa5a5a5a5u);
            /* Two levels select existing canonical-linear render staging:
             * ceil(17/2)xceil(3/2) -> 512 bytes, then 768-byte level zero. */
            normalized.base.last_level=1;
            normalized.level_stride[1]=256;
            normalized.level_offset[0]=512;
            normalized.size=1280;
        }
        assert(ps5_linear_sampled_layout(&normalized.base));
        assert(!ps5_resource_storage_image_descriptor(&normalized.base,0,descriptor));
        assert(descriptor[1]==((encoding<<20) | (uint32_t)((uintptr_t)normalized.data>>40)));
        assert(descriptor[3]==0x90000facu && descriptor[4]==(normalized.base.last_level ? 0 : 256/bytes-1));
        assert(!ps5_resource_sampled_image_descriptor(&normalized.base,0,0,sampled_srd));
        assert(!memcmp(descriptor,sampled_srd,sizeof(descriptor)));
        for(unsigned stage=0;stage<2;++stage) {
            mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
            struct pipe_image_view v={.resource=&normalized.base,.format=normalized.base.format,
                .access=PIPE_IMAGE_ACCESS_READ_WRITE,.u.tex.single_layer_view=true};
            ps5_set_shader_images(&context.base,which,7,1,0,&v);
            assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
            assert(normalized.base.reference.count==2);
            v.format=PIPE_FORMAT_R16G16B16A16_UINT;
            ps5_set_shader_images(&context.base,which,7,1,0,&v);
            assert((stage ? context.fragment_images_invalid : context.compute_images_invalid)==(bytes!=8));
            assert(normalized.base.reference.count==2);
            ps5_set_shader_images(&context.base,which,7,0,1,NULL);
            assert(normalized.base.reference.count==1);
        }
        for(unsigned fault=0;fault<5;++fault) {
            struct ps5_resource bad=normalized;
            if(fault==0) bad.size--;
            if(fault==1) bad.level_offset[0]++;
            if(fault==2) bad.level_stride[0]+=256;
            if(fault==3) bad.allocation_size=bad.size-1;
            if(fault==4) { bad.base.bind|=PIPE_BIND_RENDER_TARGET; bad.render_staging_size=1; bad.render_staging_offset=0; }
            memset(descriptor,0xa5,sizeof(descriptor));
            assert(ps5_resource_storage_image_descriptor(&bad.base,0,descriptor)<0);
            for(unsigned i=0;i<8;++i) assert(descriptor[i]==0xa5a5a5a5u);
        }
    }
    }
    /* Exact graphics-compatible tiled RGBA8 SRD, with a physical footprint
     * larger than the logical texels. Malformed backing must remain rejected. */
    static _Alignas(65536) uint8_t tiled_pixels[262144];
    const enum pipe_format ms_formats[]={PIPE_FORMAT_R32_FLOAT,PIPE_FORMAT_R32G32B32A32_FLOAT,
        PIPE_FORMAT_R8G8B8A8_UNORM,PIPE_FORMAT_R8G8B8A8_SINT,PIPE_FORMAT_R8G8B8A8_UINT};
    for(unsigned f=0;f<5;++f) for(unsigned array=0;array<2;++array) {
        const bool vector=f==1;
        struct ps5_resource ms=canonical;
        ms.base.format=ms_formats[f];
        ms.base.target=array ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
        ms.base.nr_samples=ms.base.nr_storage_samples=4;
        ms.base.array_size=array ? 2 : 1;
        ms.base.bind=PIPE_BIND_SAMPLER_VIEW|PIPE_BIND_RENDER_TARGET;
        const unsigned texel=vector ? 16 : 4;
        ms.data=tiled_pixels; ms.allocation_size=65536*ms.base.array_size;
        ms.level_stride[0]=17*texel; ms.size=17*3*texel*4*ms.base.array_size;
        ms.layer_stride=array ? 65536 : 17*3*texel;
        assert(!ps5_resource_sampled_image_descriptor(&ms.base,0,0,descriptor));
        assert(descriptor[3]==((array ? 0xf1b20000u : 0xe1b20000u)|(f ? 0xfac : 0x204)));
        /* AMD query_samples extracts log2(samples) from LAST_LEVEL. */
        assert((1u<<((descriptor[3]>>16)&15))==ms.base.nr_samples);
        assert(descriptor[4]==array && descriptor[5]==0x00400020);
        assert(!ps5_resource_storage_image_descriptor(&ms.base,0,descriptor));
        assert(!ps5_resource_storage_image_descriptor_owned(&ms.base,descriptor));
        for(unsigned fault=0;fault<7;++fault) {
            struct ps5_resource bad=ms;
            if(fault==0) --bad.allocation_size;
            if(fault==1) ++bad.layer_stride;
            if(fault==2) ++bad.size;
            if(fault==3) bad.base.nr_storage_samples=2;
            if(fault==4) bad.base.last_level=1;
            if(fault==5) bad.base.target=PIPE_TEXTURE_3D;
            if(fault==6) bad.base.bind=PIPE_BIND_SAMPLER_VIEW;
            assert(ps5_resource_sampled_image_descriptor(&bad.base,0,0,descriptor)<0);
        }
    }
    struct ps5_resource tiled=canonical;
    tiled.base.format=PIPE_FORMAT_R8G8B8A8_UNORM;
    tiled.base.bind=PIPE_BIND_SAMPLER_VIEW|PIPE_BIND_RENDER_TARGET;
    tiled.data=tiled_pixels;
    tiled.size=tiled.layer_stride=17*3*4;
    tiled.level_stride[0]=17*4;
    tiled.level_offset[0]=0;
    tiled.allocation_size=65536;
    assert(!ps5_linear_sampled_layout(&tiled.base));
    assert(ps5_tiled_color_surface_size(tiled.base.format,129,129)==sizeof(tiled_pixels));
    assert(!ps5_resource_storage_image_descriptor(&tiled.base,0,descriptor));
    const uint32_t tiled_srd[8]={
        (uint32_t)((uintptr_t)tiled.data>>8),
        0x03800000u|(uint32_t)((uintptr_t)tiled.data>>40),
        0x80008004u,0x91b00facu,0,0x00400000u,0,0};
    assert(!memcmp(descriptor,tiled_srd,sizeof(tiled_srd)));
    assert(!ps5_resource_sampled_image_descriptor(&tiled.base,0,0,descriptor));
    assert(!memcmp(descriptor,tiled_srd,sizeof(tiled_srd)));
    for(unsigned stage=0;stage<2;++stage) {
        mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
        struct pipe_image_view v={.resource=&tiled.base,.format=tiled.base.format,
            .access=PIPE_IMAGE_ACCESS_READ_WRITE,.u.tex.single_layer_view=true};
        ps5_set_shader_images(&context.base,which,7,1,0,&v);
        assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
        assert(tiled.base.reference.count==2);
        v.format=PIPE_FORMAT_R8G8B8A8_SRGB;
        ps5_set_shader_images(&context.base,which,7,1,0,&v);
        assert(stage ? context.fragment_images_invalid : context.compute_images_invalid);
        assert(tiled.base.reference.count==2);
        ps5_set_shader_images(&context.base,which,7,0,1,NULL);
        assert(tiled.base.reference.count==1);
    }
    /* RGBA16F uses the native tiled layout too, including image bindings. */
    {
        struct ps5_resource half=tiled;
        half.base.format=PIPE_FORMAT_R16G16B16A16_FLOAT;
        half.size=half.layer_stride=17*3*8;
        half.level_stride[0]=17*8;
        assert(!ps5_linear_sampled_layout(&half.base));
        assert(!ps5_resource_storage_image_descriptor(&half.base,0,descriptor));
        assert(descriptor[1]==(0x04700000u|(uint32_t)((uintptr_t)half.data>>40)));
        assert(descriptor[3]==tiled_srd[3]);
        for(unsigned stage=0;stage<2;++stage) {
            mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
            struct pipe_image_view v={.resource=&half.base,.format=half.base.format,
                .access=PIPE_IMAGE_ACCESS_READ_WRITE,.u.tex.single_layer_view=true};
            ps5_set_shader_images(&context.base,which,7,1,0,&v);
            assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
            ps5_set_shader_images(&context.base,which,7,0,1,NULL);
            assert(half.base.reference.count==1);
        }
        half.allocation_size=65535;
        assert(ps5_resource_storage_image_descriptor(&half.base,0,descriptor)<0);
    }
    for(unsigned fault=0;fault<14;++fault) {
        struct ps5_resource bad=tiled;
        if(fault==0) bad.data+=256;
        if(fault==1) bad.allocation_size=65535;
        if(fault==2) bad.size--;
        if(fault==3) bad.layer_stride++;
        if(fault==4) bad.level_stride[0]++;
        if(fault==5) bad.level_offset[0]=256;
        if(fault==6) bad.render_staging_size=65536;
        if(fault==7) bad.render_staging_offset=65536;
        if(fault==8) bad.base.bind|=PIPE_BIND_DISPLAY_TARGET;
        if(fault==9) bad.base.bind|=PIPE_BIND_DEPTH_STENCIL;
        if(fault==10) bad.base.nr_samples=4;
        if(fault==11) bad.base.nr_storage_samples=4;
        if(fault==12) bad.base.format=PIPE_FORMAT_R8G8B8A8_SRGB;
        if(fault==13) bad.base.last_level=1;
        memset(descriptor,0xa5,sizeof(descriptor));
        assert(ps5_resource_storage_image_descriptor(&bad.base,0,descriptor)<0);
        if(fault==12) {
            assert(!ps5_resource_sampled_image_descriptor(&bad.base,0,0,descriptor));
            assert(((descriptor[1]>>20)&0x1ff)==0x82);
            continue;
        }
        assert(ps5_resource_sampled_image_descriptor(&bad.base,0,0,descriptor)<0);
        for(unsigned i=0;i<8;++i) assert(descriptor[i]==0xa5a5a5a5u);
    }
    assert(ps5_resource_storage_image_descriptor(&tiled.base,1,descriptor)<0);
    const enum pipe_format still_gated[]={PIPE_FORMAT_R9G9B9E5_FLOAT,
        PIPE_FORMAT_R8G8B8A8_SRGB,PIPE_FORMAT_Z32_FLOAT,PIPE_FORMAT_R32G32B32_FLOAT};
    for(unsigned i=0;i<ARRAY_SIZE(still_gated);++i) {
        struct ps5_resource bad=canonical; bad.base.format=still_gated[i];
        assert(!ps5_storage_image_texel_size(bad.base.format));
        memset(descriptor,0xa5,sizeof(descriptor));
        assert(ps5_resource_storage_image_descriptor(&bad.base,0,descriptor)<0);
        for(unsigned j=0;j<8;++j) assert(descriptor[j]==0xa5a5a5a5u);
    }
    _Alignas(256) uint8_t mip_pixels[3840];
    struct ps5_resource mip=image;
    mip.data=mip_pixels; mip.size=mip.allocation_size=sizeof(mip_pixels);
    mip.base.width0=16; mip.base.height0=8; mip.base.last_level=3;
    const size_t offsets[4]={1792,768,256,0};
    for(unsigned level=0;level<4;++level) {mip.level_offset[level]=offsets[level];mip.level_stride[level]=256;}
    assert(!ps5_resource_sampled_image_descriptor(&mip.base,1,2,descriptor));
    assert(descriptor[3]==0x90021204 && descriptor[4]==0 && descriptor[5]==0x400030);
    pipe_reference_init(&mip.base.reference,1);
    {
        _Alignas(256) uint8_t pixels[4*3840];
        struct ps5_resource volume=mip;
        volume.base.target=PIPE_TEXTURE_3D; volume.base.depth0=4;
        volume.data=pixels; volume.layer_stride=3840;
        volume.size=volume.allocation_size=sizeof(pixels);
        for(unsigned level=0;level<4;++level) {
            struct pipe_image_view v={.resource=&volume.base,.format=volume.base.format,
                .access=PIPE_IMAGE_ACCESS_READ_WRITE};
            v.u.tex.level=level; v.u.tex.last_layer=MAX2(4u>>level,1u)-1;
            assert(!ps5_storage_image_view_descriptor(&v,descriptor));
            assert(descriptor[3]==(0xa0000204u|(level<<12)|(level<<16)));
            assert(descriptor[4]==3 && descriptor[5]==0x400030);
            assert(!ps5_resource_storage_image_descriptor_owned(&volume.base,descriptor));
            if (v.u.tex.last_layer) {
                v.u.tex.first_layer=v.u.tex.last_layer=1;
                v.u.tex.single_layer_view=true;
                v.u.tex.is_2d_view_of_3d=true;
                assert(!ps5_storage_image_view_descriptor(&v,descriptor));
                assert(descriptor[0]==(uint32_t)((uintptr_t)(pixels+3840)>>8));
                assert((descriptor[3]>>28)==9 && descriptor[4]==0);
                v.u.tex.first_layer=0;
                v.u.tex.last_layer=MAX2(4u>>level,1u)-1;
                v.u.tex.single_layer_view=false;
            }
            for(unsigned stage=0;stage<2;++stage) {
                mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
                ps5_set_shader_images(&context.base,which,7,1,0,&v);
                assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
                ps5_set_shader_images(&context.base,which,7,0,1,NULL);
            }
            ++v.u.tex.last_layer;
            assert(ps5_storage_image_view_descriptor(&v,descriptor)<0);
        }
        --volume.size;
        assert(ps5_resource_storage_image_descriptor(&volume.base,1,descriptor)<0);
        assert(volume.base.reference.count==1);
    }
    {
        static _Alignas(256) uint8_t pixels[2*1024*1024];
        struct ps5_resource volume=mip;
        volume.base.target=PIPE_TEXTURE_3D; volume.base.format=PIPE_FORMAT_R32_SINT;
        volume.base.width0=100; volume.base.height0=1; volume.base.depth0=2;
        volume.base.array_size=1; volume.base.last_level=0; volume.base.bind=0xa;
        volume.base.nr_samples=volume.base.nr_storage_samples=0;
        volume.data=pixels; volume.size=1024; volume.allocation_size=sizeof(pixels);
        volume.level_offset[0]=0; volume.level_stride[0]=512; volume.layer_stride=512;
        volume.render_staging_offset=65536; volume.render_staging_size=131072;
        struct pipe_image_view v={.resource=&volume.base,.format=volume.base.format,
            .access=PIPE_IMAGE_ACCESS_READ_WRITE};
        v.u.tex.last_layer=1;
        v.u.tex.is_2d_view_of_3d=true; /* Undefined for Mesa layered views. */
        assert(!ps5_storage_image_view_descriptor(&v,descriptor));
    }
    for(unsigned level=0;level<4;++level) {
        assert(!ps5_resource_storage_image_descriptor(&mip.base,level,descriptor));
        assert(descriptor[3]==(0x90000204u|(level<<12)|(level<<16)));
        assert(descriptor[5]==0x400030);
        struct pipe_image_view mv={.resource=&mip.base,.format=mip.base.format,.access=PIPE_IMAGE_ACCESS_WRITE};
        mv.u.tex.level=level;
        ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,0,1,0,&mv);
        assert(!context.compute_images_invalid && context.compute_images[0].u.tex.level==level && mip.base.reference.count==2);
        ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,0,0,1,NULL);
        assert(mip.base.reference.count==1);
    }
    assert(ps5_resource_storage_image_descriptor(&mip.base,4,descriptor)<0);
    for(unsigned stage=0;stage<2;++stage) for(unsigned level=0;level<4;++level) {
        mesa_shader_stage which=stage ? MESA_SHADER_FRAGMENT : MESA_SHADER_COMPUTE;
        struct pipe_image_view v={.resource=&mip.base,.format=mip.base.format,
            .access=PIPE_IMAGE_ACCESS_READ_WRITE};
        v.u.tex.level=level;
        uint32_t control[8];
        assert(!ps5_resource_storage_image_descriptor(v.resource,level,control));
        ps5_set_shader_images(&context.base,which,7,1,0,&v);
        v.u.tex.single_layer_view=true;
        ps5_set_shader_images(&context.base,which,7,1,0,&v);
        struct pipe_image_view *bound=stage ? context.fragment_images : context.compute_images;
        assert(!(stage ? context.fragment_images_invalid : context.compute_images_invalid));
        assert(bound[7].u.tex.single_layer_view && mip.base.reference.count==2);
        assert(!ps5_resource_storage_image_descriptor(bound[7].resource,bound[7].u.tex.level,descriptor));
        assert(!memcmp(control,descriptor,sizeof(control)));
        ps5_set_shader_images(&context.base,which,7,0,1,NULL);
        assert(mip.base.reference.count==1 && !bound[7].resource);
    }
    struct ps5_resource layered=mip;
    _Alignas(256) uint8_t array_pixels[3*3840];
    layered.data=array_pixels;
    layered.base.target=PIPE_TEXTURE_2D_ARRAY; layered.base.array_size=3;
    layered.size=layered.allocation_size=sizeof(array_pixels); layered.layer_stride=3840;
    assert(ps5_compute_image_array_resource(&layered.base));
    for(unsigned fault=0;fault<7;++fault) {
        struct pipe_resource bad=layered.base;
        if(fault==0) bad.target=PIPE_TEXTURE_3D;
        if(fault==1) bad.array_size=9;
        if(fault==2) bad.nr_samples=4;
        if(fault==3) bad.nr_storage_samples=4;
        if(fault==4) bad.bind|=PIPE_BIND_RENDER_TARGET;
        if(fault==5) bad.format=PIPE_FORMAT_R9G9B9E5_FLOAT;
        if(fault==6) bad.bind=PIPE_BIND_SHADER_IMAGE;
        assert(!ps5_compute_image_array_resource(&bad));
    }
    assert(!ps5_resource_sampled_image_descriptor(&layered.base,1,2,descriptor));
    assert(descriptor[3]==0xd0021204 && descriptor[4]==2);
    for(unsigned fault=0;fault<4;++fault) {
        struct ps5_resource bad=layered;
        if(fault==0) bad.size--;
        if(fault==1) bad.layer_stride++;
        if(fault==2) bad.base.array_size=9;
        if(fault==3) bad.base.array_size=0;
        assert(ps5_resource_storage_image_descriptor(&bad.base,1,descriptor)<0);
    }
    for(unsigned fault=0;fault<12;++fault) {
        struct ps5_resource bad=mip;
        if(fault<4) bad.level_offset[fault]+=256;
        if(fault>=4 && fault<8) bad.level_stride[fault-4]+=256;
        if(fault==8) bad.size--;
        if(fault==9) bad.base.last_level=PIPE_MAX_TEXTURE_LEVELS;
        assert(ps5_resource_sampled_image_descriptor(&bad.base,fault==10 ? 3 : 1,fault==11 ? 4 : 2,descriptor)<0);
    }
    const enum pipe_format scalar_formats[]={PIPE_FORMAT_R32_UINT,PIPE_FORMAT_R32_SINT,PIPE_FORMAT_R32_FLOAT};
    for(unsigned i=0;i<3;++i) {
        struct ps5_resource typed=image; typed.base.format=scalar_formats[i];
        assert(!ps5_resource_storage_image_descriptor(&typed.base,0,descriptor));
        assert((descriptor[1]&0x3ff00000u)==(0x1400000u+i*0x100000u));
        pipe_reference_init(&typed.base.reference,1);
        struct pipe_image_view v={.resource=&typed.base,.format=typed.base.format,.access=PIPE_IMAGE_ACCESS_READ_WRITE};
        ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,1,0,&v);
        assert(!context.compute_images_invalid && typed.base.reference.count==2);
        ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,0,1,NULL);
        assert(!context.compute_images_invalid && typed.base.reference.count==1);
    }
    const enum pipe_format vectors[]={PIPE_FORMAT_R32G32_UINT,PIPE_FORMAT_R32G32_SINT,PIPE_FORMAT_R32G32_FLOAT,
        PIPE_FORMAT_R32G32B32A32_UINT,PIPE_FORMAT_R32G32B32A32_SINT,PIPE_FORMAT_R32G32B32A32_FLOAT,
        PIPE_FORMAT_R16G16B16A16_UINT,PIPE_FORMAT_R16G16B16A16_SINT,PIPE_FORMAT_R16G16B16A16_FLOAT,
        PIPE_FORMAT_R16G16B16A16_UNORM,PIPE_FORMAT_R8G8B8A8_UNORM,
        PIPE_FORMAT_R8G8B8A8_UINT,PIPE_FORMAT_R8G8B8A8_SINT};
    _Alignas(256) uint8_t vector_pixels[1536];
    for(unsigned i=0;i<ARRAY_SIZE(vectors);++i) {
        struct ps5_resource v=image; v.base.format=vectors[i]; v.data=vector_pixels;
        unsigned bytes=i>=10 ? 4 : i>=3 && i<6 ? 16 : 8;
        unsigned stride=(17*bytes+255)&~255u;
        v.level_stride[0]=stride; v.size=stride*3; v.allocation_size=sizeof(vector_pixels);
        assert(ps5_storage_image_texel_size(v.base.format)==bytes);
        assert(!ps5_resource_storage_image_descriptor(&v.base,0,descriptor));
        if(i>=11) assert((descriptor[1]&0x3ff00000u)==((60u+i-11)<<20));
        assert((descriptor[3]&4095)==(i<3 ? 0x22c : 0xfac));
        assert(descriptor[4]==stride/bytes-1);
        v.size--;
        assert(ps5_resource_storage_image_descriptor(&v.base,0,descriptor)<0);
        v.size++; v.level_stride[0]+=256;
        assert(ps5_resource_storage_image_descriptor(&v.base,0,descriptor)<0);
    }
    /* Odd extents cross the RGBA row-alignment boundary; explicit offsets are
     * independent of the driver's layout helper. Host layout proof, not GPU proof. */
    _Alignas(256) uint8_t vector_array_pixels[8*7168];
    for(unsigned i=0;i<ARRAY_SIZE(vectors);++i) {
        struct ps5_resource v=layered;
        v.base.format=vectors[i]; v.base.width0=17; v.base.height0=9;
        v.base.array_size=8; v.data=vector_array_pixels;
        v.layer_stride=i>=3 && i<6 ? 7168 : 4864; v.size=8*v.layer_stride;
        v.allocation_size=sizeof(vector_array_pixels);
        const size_t vector_offsets[]={2560,1280,512,0};
        for(unsigned level=0;level<4;++level) {
            v.level_offset[level]=vector_offsets[level];
            v.level_stride[level]=i>=3 && i<6 && !level ? 512 : 256;
        }
        assert(ps5_compute_image_array_resource(&v.base));
        pipe_reference_init(&v.base.reference,1);
        for(unsigned level=0;level<4;++level) {
            assert(!ps5_resource_storage_image_descriptor(&v.base,level,descriptor));
            assert(descriptor[3]==(0xd0000000u|(i<3 ? 0x22cu : 0xfacu)|(level<<12)|(level<<16)));
            assert(descriptor[4]==7 && descriptor[5]==0x400030);
            struct pipe_image_view view={.resource=&v.base,.format=v.base.format,
                .access=PIPE_IMAGE_ACCESS_READ_WRITE};
            view.u.tex.level=level; view.u.tex.last_layer=7;
            ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,1,0,&view);
            assert(!context.compute_images_invalid && v.base.reference.count==2);
            /* A single-layer view cannot name several layers. */
            view.u.tex.single_layer_view=true;
            ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,1,0,&view);
            assert(context.compute_images_invalid && v.base.reference.count==2);
            assert(!context.compute_images[7].u.tex.single_layer_view);
            view.u.tex.single_layer_view=false;
            /* Sublayers rebase the descriptor; invalid ranges retain the binding. */
            view.u.tex.first_layer=1;
            ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,1,0,&view);
            assert(!context.compute_images_invalid && v.base.reference.count==2);
            assert(!ps5_storage_image_view_descriptor(&view,descriptor));
            assert(descriptor[0]==(uint32_t)(((uintptr_t)v.data+v.layer_stride)>>8));
            assert(descriptor[4]==6);
            assert(!ps5_resource_storage_image_descriptor_owned(&v.base,descriptor));
            view.u.tex.first_layer=8;
            ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,1,0,&view);
            assert(context.compute_images_invalid && v.base.reference.count==2);
            assert(context.compute_images[7].u.tex.first_layer==1);
            ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,0,1,NULL);
            assert(!context.compute_images_invalid && v.base.reference.count==1);
        }
        for(unsigned first=0;first<4;++first)
            for(unsigned last=first;last<4;++last)
                assert(!ps5_resource_sampled_image_descriptor(&v.base,first,last,descriptor));
        for(unsigned fault=0;fault<11;++fault) {
            struct ps5_resource bad=v;
            if(fault<4) bad.level_offset[fault]+=256;
            if(fault>=4 && fault<8) bad.level_stride[fault-4]+=256;
            if(fault==8) bad.size--;
            if(fault==9) bad.layer_stride++;
            if(fault==10) bad.base.array_size=9;
            memset(descriptor,0xa5,sizeof(descriptor));
            assert(ps5_resource_storage_image_descriptor(&bad.base,0,descriptor)<0);
            for(unsigned word=0;word<8;++word) assert(descriptor[word]==0xa5a5a5a5u);
        }
    }
    for(unsigned fault=0;fault<15;++fault) {
        struct ps5_resource bad=image;
        if(fault==0) bad.base.target=PIPE_BUFFER;
        if(fault==1) bad.base.format=PIPE_FORMAT_R9G9B9E5_FLOAT;
        if(fault==2) bad.base.width0=0;
        if(fault==3) bad.base.height0=8193;
        if(fault==4) bad.base.depth0=2;
        if(fault==5) bad.base.array_size=2;
        if(fault==6) bad.base.last_level=1;
        if(fault==7) bad.base.nr_samples=4;
        if(fault==8) bad.base.bind=0;
        if(fault==9) bad.base.bind|=PIPE_BIND_RENDER_TARGET;
        if(fault==10) bad.render_staging_size=1;
        if(fault==11) bad.depth_staging_size=1;
        if(fault==12) bad.data++;
        if(fault==13) bad.level_stride[0]=128;
        if(fault==14) bad.size=767;
        assert(ps5_resource_storage_image_descriptor(&bad.base,0,descriptor)<0);
    }
    pipe_reference_init(&image.base.reference,1);
    struct pipe_image_view view={.resource=&image.base,.format=PIPE_FORMAT_R32_UINT,.access=PIPE_IMAGE_ACCESS_READ_WRITE};
    view.u.tex.single_layer_view=true; /* Actual Mesa 2D image representation. */
    view.u.tex.is_2d_view_of_3d=true; /* Irrelevant for 2D; Mesa leaves it undefined. */
    ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,1,0,&view);
    caller=&image.base; pipe_resource_reference(&caller,NULL);
    assert(image.base.reference.count==1 && !context.compute_images_invalid);
    {
        struct pipe_image_view incomplete=view;
        incomplete.u.tex.level=1;
        struct pipe_image_view pair[2]={view,incomplete};
        ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,6,2,0,pair);
        assert(!context.compute_images_invalid && context.compute_images[6].resource &&
               context.compute_images[7].u.tex.level==1 && image.base.reference.count==2);
        ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,6,0,1,NULL);
        ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,1,0,&view);
    }
    for(unsigned fault=0;fault<5;++fault) {
        struct pipe_image_view bad=view;
        if(fault==0) bad.format=PIPE_FORMAT_R16_FLOAT; /* Different texel size. */
        if(fault==1) bad.access=0;
        if(fault==2) bad.access|=PIPE_IMAGE_ACCESS_TEX2D_FROM_BUFFER;
        if(fault==3) bad.u.tex.last_layer=1;
        if(fault==4) image.base.screen=&other_screen;
        struct pipe_image_view pair[2]={view,bad};
        ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,6,2,0,pair);
        assert(context.compute_images_invalid && !context.compute_images[6].resource && image.base.reference.count==1);
        ps5_launch_grid(&context.base,&good);
        assert(context.last_compute_status<0 && submitted==4);
        image.base.screen=&screen;
        ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,7,1,0,&view);
    }
    with_images=1;
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && submitted==5);
    struct pipe_image_view views[8]; for(unsigned i=0;i<8;++i) views[i]=view;
    ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,0,8,0,views);
    assert(image.base.reference.count==8);
    pipe_reference_init(&buffer.base.reference,1);
    struct pipe_shader_buffer all[16]; for(unsigned i=0;i<16;++i) all[i]=binding;
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,0,16,all,0xffff);
    for(unsigned i=0;i<15;++i) {
        struct pipe_constant_buffer cb={.buffer=&buffer.base,.buffer_size=16};
        ps5_set_compute_constant_buffer(&context.base,i,&cb);
    }
    caller=&buffer.base; pipe_resource_reference(&caller,NULL);
    assert(buffer.base.reference.count==31);
    with_images=39;
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && submitted==6 && context.dispatches==6);
    ps5_set_shader_images(&context.base,MESA_SHADER_COMPUTE,0,0,8,NULL);
    assert(!context.compute_images_invalid && destroyed==1);
    ps5_set_shader_buffers(&context.base,MESA_SHADER_COMPUTE,0,16,NULL,0);
    for(unsigned i=0;i<15;++i) ps5_set_compute_constant_buffer(&context.base,i,NULL);
    assert(destroyed==2);
    /* Sampled views are retained, validated atomically, and cannot be missing. */
    struct pipe_sampler_view sampled={.texture=&image.base,.target=PIPE_TEXTURE_2D,
        .format=PIPE_FORMAT_R32_UINT,.swizzle_r=PIPE_SWIZZLE_X,.swizzle_g=PIPE_SWIZZLE_Y,
        .swizzle_b=PIPE_SWIZZLE_Z,.swizzle_a=PIPE_SWIZZLE_W};
    pipe_reference_init(&sampled.reference,1);
    pipe_reference_init(&image.base.reference,1);
    struct pipe_sampler_view *sampled_views[8];
    for(unsigned i=0;i<8;++i) sampled_views[i]=&sampled;
    ps5_set_compute_sampler_views(&context.base,0,8,0,sampled_views);
    assert(!context.compute_views_invalid && sampled.reference.count==9);
    cs.textures=255; with_sampled=true; with_images=0;
    unsigned before_sampled=submitted;
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && submitted==before_sampled+1);
    {
        _Alignas(256) uint8_t rgba[1536];
        struct ps5_resource saved=image;
        struct pipe_sampler_view saved_view=sampled;
        image.base.format=PIPE_FORMAT_R32G32B32A32_FLOAT;
        image.data=rgba; image.size=image.allocation_size=sizeof(rgba);
        image.level_stride[0]=512;
        const unsigned selectors[]={4,5,6,7,0,1};
        for(unsigned channel=0;channel<4;++channel) for(unsigned swizzle=0;swizzle<6;++swizzle) {
            sampled=saved_view; sampled.format=image.base.format;
            if(channel==0) sampled.swizzle_r=swizzle;
            if(channel==1) sampled.swizzle_g=swizzle;
            if(channel==2) sampled.swizzle_b=swizzle;
            if(channel==3) sampled.swizzle_a=swizzle;
            expected_swizzle=(0xfacu&~(7u<<(3*channel)))|(selectors[swizzle]<<(3*channel));
            ps5_set_compute_sampler_views(&context.base,0,8,0,sampled_views);
            assert(!context.compute_views_invalid);
            ps5_launch_grid(&context.base,&good);
            assert(!context.last_compute_status);
        }
        image=saved; sampled=saved_view; expected_swizzle=UINT32_MAX;
        before_sampled=submitted-1;
    }
    cs.array_textures=1;
    ps5_launch_grid(&context.base,&good);
    assert(context.last_compute_status<0 && submitted==before_sampled+1);
    cs.array_textures=0;
    for(unsigned fault=0;fault<7;++fault) {
        struct pipe_sampler_view bad=sampled;
        if(fault==0) bad.u.tex.first_level=1;
        if(fault==1) bad.u.tex.last_layer=1;
        if(fault==2) bad.swizzle_a=6;
        if(fault==3) bad.format=PIPE_FORMAT_R32_FLOAT;
        if(fault==4) bad.target=PIPE_TEXTURE_2D_ARRAY;
        if(fault==5) image.base.screen=&other_screen;
        struct pipe_sampler_view *pair[2]={&sampled,&bad};
        ps5_set_compute_sampler_views(&context.base,fault==6 ? 16 : 0,2,0,pair);
        assert(context.compute_views_invalid && sampled.reference.count==9);
        ps5_launch_grid(&context.base,&good);
        assert(context.last_compute_status<0 && submitted==before_sampled+1);
        image.base.screen=&screen;
        ps5_set_compute_sampler_views(&context.base,0,8,0,sampled_views);
    }
    ps5_set_compute_sampler_views(&context.base,7,0,1,NULL);
    assert(sampled.reference.count==8);
    ps5_launch_grid(&context.base,&good);
    assert(context.last_compute_status<0 && submitted==before_sampled+1);
    ps5_set_compute_sampler_views(&context.base,0,0,8,NULL);
    assert(sampled.reference.count==1 && !context.compute_views_invalid);
    /* Mesa R/RG views use X001/XY01, matching the physical image SRD.
     * A rejected replacement must neither retain its valid prefix nor unbind
     * trailing views. Test real callbacks; no GPU sampling is implied. */
    const enum pipe_format canonical_formats[]={PIPE_FORMAT_R32_FLOAT,PIPE_FORMAT_R32_UINT,
        PIPE_FORMAT_R32_SINT,PIPE_FORMAT_R32G32_FLOAT,PIPE_FORMAT_R32G32_UINT,PIPE_FORMAT_R32G32_SINT};
    for(unsigned f=0;f<6;++f) {
        struct ps5_resource texture=canonical;
        texture.base.format=canonical_formats[f];
        struct pipe_sampler_view cv={.texture=&texture.base,.target=PIPE_TEXTURE_2D,
            .format=texture.base.format,.swizzle_r=PIPE_SWIZZLE_X,
            .swizzle_g=f<3 ? PIPE_SWIZZLE_0 : PIPE_SWIZZLE_Y,
            .swizzle_b=PIPE_SWIZZLE_0,.swizzle_a=PIPE_SWIZZLE_1};
        pipe_reference_init(&cv.reference,1);
        struct pipe_sampler_view identity=cv;
        identity.swizzle_g=PIPE_SWIZZLE_Y; identity.swizzle_b=PIPE_SWIZZLE_Z;
        identity.swizzle_a=PIPE_SWIZZLE_W;
        pipe_reference_init(&identity.reference,1);
        uint32_t physical[8];
        assert(!ps5_resource_sampled_image_descriptor(&texture.base,0,0,physical));
        assert((physical[3]&0xfffu)==(f<3 ? 0x204u : 0x22cu));
        struct pipe_sampler_view *initial[3]={&identity,&identity,&identity};
        ps5_set_compute_sampler_views(&context.base,0,3,0,initial);
        assert(!context.compute_views_invalid && identity.reference.count==4);
        struct pipe_sampler_view *replacement[2]={&cv,&cv};
        ps5_set_compute_sampler_views(&context.base,0,2,1,replacement);
        assert(!context.compute_views_invalid && cv.reference.count==3 && identity.reference.count==1);
        assert(context.compute_views[0]==&cv && context.compute_views[1]==&cv && !context.compute_views[2]);
        ps5_set_compute_sampler_views(&context.base,2,1,0,&initial[0]);
        assert(identity.reference.count==2);
        for(unsigned remap=0;remap<4;++remap) {
            struct pipe_sampler_view bad=cv;
            pipe_reference_init(&bad.reference,1);
            if(remap==0) bad.swizzle_r=6;
            if(remap==1) bad.swizzle_g=6;
            if(remap==2) bad.swizzle_b=6;
            if(remap==3) bad.swizzle_a=6;
            struct pipe_sampler_view *pair[2]={&identity,&bad};
            ps5_set_compute_sampler_views(&context.base,0,2,1,pair);
            assert(context.compute_views_invalid && bad.reference.count==1);
            assert(cv.reference.count==3 && identity.reference.count==2);
            assert(context.compute_views[0]==&cv && context.compute_views[1]==&cv && context.compute_views[2]==&identity);
        }
        ps5_set_compute_sampler_views(&context.base,0,0,3,NULL);
        assert(!context.compute_views_invalid && cv.reference.count==1 && identity.reference.count==1);
        assert(!context.compute_views[0] && !context.compute_views[1] && !context.compute_views[2]);
    }
    ps5_set_compute_sampler_views(&context.base,0,8,0,sampled_views);
    cs.filtered_textures=with_filtered=255;
    unsigned before_filter=submitted;
    ps5_launch_grid(&context.base,&good); /* Missing sampler must not submit. */
    assert(context.last_compute_status<0 && submitted==before_filter);
    image.base.format=sampled.format=PIPE_FORMAT_R32_FLOAT;
    struct ps5_sampler_state sampler={.base={.wrap_s=PIPE_TEX_WRAP_CLAMP_TO_EDGE,
        .wrap_t=PIPE_TEX_WRAP_CLAMP_TO_EDGE,.wrap_r=PIPE_TEX_WRAP_CLAMP_TO_EDGE,
        .min_mip_filter=PIPE_TEX_MIPFILTER_NONE}};
    void *states[8]; for(unsigned i=0;i<8;++i) states[i]=&sampler;
    const unsigned wraps[3]={PIPE_TEX_WRAP_REPEAT,PIPE_TEX_WRAP_MIRROR_REPEAT,PIPE_TEX_WRAP_CLAMP_TO_EDGE};
    for(unsigned wrap=0;wrap<3;++wrap) for(unsigned filter=0;filter<4;++filter) {
        sampler.base.wrap_s=sampler.base.wrap_t=sampler.base.wrap_r=wraps[wrap];
        expected_sampler[0]=wrap*0x49;
        sampler.base.min_img_filter=filter&1 ? PIPE_TEX_FILTER_LINEAR : PIPE_TEX_FILTER_NEAREST;
        sampler.base.mag_img_filter=filter&2 ? PIPE_TEX_FILTER_LINEAR : PIPE_TEX_FILTER_NEAREST;
        ps5_set_compute_sampler_states(&context.base,0,8,states);
        expected_sampler[2]=((filter&1)!=0)<<22 | ((filter&2)!=0)<<20;
        assert(!context.compute_samplers_invalid && context.compute_sampler_mask==255);
        ps5_launch_grid(&context.base,&good);
        assert(!context.last_compute_status && submitted==++before_filter);
    }
    sampler.base.wrap_s=PIPE_TEX_WRAP_REPEAT;
    sampler.base.wrap_t=PIPE_TEX_WRAP_MIRROR_REPEAT;
    sampler.base.wrap_r=PIPE_TEX_WRAP_CLAMP_TO_EDGE;
    ps5_set_compute_sampler_states(&context.base,0,8,states);
    assert(!context.compute_samplers_invalid && context.compute_samplers[7][0]==0x88);
    sampler.base.wrap_s=sampler.base.wrap_t=sampler.base.wrap_r=PIPE_TEX_WRAP_CLAMP_TO_EDGE;
    struct ps5_sampler_state valid=sampler;
    sampler.base.min_mip_filter=PIPE_TEX_MIPFILTER_LINEAR;
    sampler.base.max_lod=3;
    ps5_set_compute_sampler_states(&context.base,0,8,states);
    assert(!context.compute_samplers_invalid && context.compute_samplers[7][1]==0x300000 &&
        context.compute_samplers[7][2]==0x08500000);
    cs.texture_lod[7]=1;
    ps5_launch_grid(&context.base,&good); /* Even valid sampler state cannot exceed the view. */
    assert(context.last_compute_status<0 && submitted==before_filter);
    cs.texture_lod[7]=UINT_MAX;
    ps5_launch_grid(&context.base,&good); /* Unknown gradients require a complete mip chain. */
    assert(context.last_compute_status<0 && submitted==before_filter);
    cs.texture_lod[7]=0;
    for(unsigned i=0;i<2;++i) {
        sampler=valid; sampler.base.max_lod=i ? 1000 : 16;
        ps5_set_compute_sampler_states(&context.base,0,8,states);
        assert(!context.compute_samplers_invalid && context.compute_sampler_mask==255);
        assert(context.compute_samplers[7][1]==0x00f00000);
    }
    sampler=valid; sampler.base.unnormalized_coords=1;
    ps5_set_compute_sampler_states(&context.base,0,8,states);
    assert(!context.compute_samplers_invalid);
    assert(context.compute_samplers[7][0]==0x92u && context.compute_samplers[7][3]==0);
    for(unsigned compare=0;compare<8;++compare) {
        sampler=valid; sampler.base.compare_mode=PIPE_TEX_COMPARE_R_TO_TEXTURE;
        sampler.base.compare_func=compare;
        ps5_set_compute_sampler_states(&context.base,0,8,states);
        assert(!context.compute_samplers_invalid);
        assert((context.compute_samplers[7][0]&0x7000u)==compare<<12);
    }
    for(unsigned fault=5;fault<17;++fault) {
        sampler=valid;
        if(fault==5) sampler.base.max_anisotropy=17;
        if(fault==6) sampler.base.min_mip_filter=3;
        if(fault==7) sampler.base.min_lod=-1;
        if(fault==8) sampler.base.max_lod=NAN;
        if(fault==9) sampler.base.lod_bias=1;
        if(fault==12) sampler.base.min_lod=NAN;
        if(fault==13) sampler.base.min_lod=INFINITY;
        if(fault==14) sampler.base.max_lod=INFINITY;
        if(fault==15) sampler.base.max_lod=-1;
        if(fault==16) { sampler.base.min_lod=2; sampler.base.max_lod=1; }
        ps5_set_compute_sampler_states(&context.base,fault==10 ? 16 : 0,8,fault==11 ? NULL : states);
        assert(context.compute_samplers_invalid && context.compute_sampler_mask==255);
        ps5_launch_grid(&context.base,&good);
        assert(context.last_compute_status<0 && submitted==before_filter);
    }
    sampler=valid;
    sampler.base.min_img_filter=sampler.base.mag_img_filter=PIPE_TEX_FILTER_NEAREST;
    ps5_set_compute_sampler_states(&context.base,0,8,states);
    expected_sampler[2]=0;
    image.base.format=sampled.format=PIPE_FORMAT_R32_UINT;
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && submitted==++before_filter);
    sampler.base.min_img_filter=PIPE_TEX_FILTER_LINEAR;
    ps5_set_compute_sampler_states(&context.base,0,8,states);
    ps5_launch_grid(&context.base,&good);
    assert(context.last_compute_status<0 && submitted==before_filter);
    for(unsigned i=0;i<8;++i) states[i]=NULL;
    ps5_set_compute_sampler_states(&context.base,0,8,states);
    assert(!context.compute_sampler_mask && !context.compute_samplers_invalid);
    ps5_set_compute_sampler_views(&context.base,0,0,8,NULL);
    cs.filtered_textures=with_filtered=0;
    cs.textures=0; with_sampled=false;
    /* All sixteen bindings are retained and copied, including the upper half. */
    struct pipe_sampler_view *all_views[16]; void *all_states[16];
    image.base.format=sampled.format=PIPE_FORMAT_R32_FLOAT;
    for(unsigned i=0;i<16;++i) { all_views[i]=&sampled; all_states[i]=&sampler; }
    ps5_set_compute_sampler_views(&context.base,0,16,0,all_views);
    ps5_set_compute_sampler_states(&context.base,0,16,all_states);
    assert(!context.compute_views_invalid && !context.compute_samplers_invalid && sampled.reference.count==17);
    expected_sampler[2]=1u<<22;
    cs.textures=cs.filtered_textures=with_filtered=65535;
    with_sampled=true; sampled_count=16;
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && context.compute_sampler_mask==65535);
    ps5_set_compute_sampler_views(&context.base,0,0,16,NULL);
    for(unsigned i=0;i<16;++i) all_states[i]=NULL;
    ps5_set_compute_sampler_states(&context.base,0,16,all_states);
    assert(sampled.reference.count==1 && !context.compute_sampler_mask);
    /* CTS resources-max unbinds unit 15, then accesses units 0, 1 and 5. */
    struct pipe_sampler_view bv={.texture=&image_buffer.base,.target=PIPE_BUFFER,
        .format=PIPE_FORMAT_R32_UINT,.swizzle_r=PIPE_SWIZZLE_X,
        .swizzle_g=PIPE_SWIZZLE_0,.swizzle_b=PIPE_SWIZZLE_0,.swizzle_a=PIPE_SWIZZLE_1};
    bv.u.buf.size=12;
    pipe_reference_init(&bv.reference,1);
    for(unsigned i=0;i<16;++i) all_views[i]=i<15 ? &bv : NULL;
    ps5_set_compute_sampler_views(&context.base,0,16,0,all_views);
    cs.textures=cs.buffer_textures=65535; cs.filtered_textures=0;
    sampled_count=15;
    cs.output.metadata.address32_hi=(uintptr_t)image_buffer_data>>32;
    unsigned before_sparse=submitted;
    ps5_launch_grid(&context.base,&good);
    assert(!context.last_compute_status && submitted==before_sparse+1);
    ps5_set_compute_sampler_views(&context.base,0,0,16,NULL);
    assert(bv.reference.count==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    formats = Path(directory) / "util/format"
    formats.mkdir(parents=True)
    with (formats / "u_format_gen.h").open("w") as generated:
        subprocess.run([sys.executable, str(MESA / "src/util/format/u_format_table.py"),
            str(MESA / "src/util/format/u_format.yaml"), "--enums"], stdout=generated, check=True)
    executable = str(Path(directory) / "compute-bindings")
    table = Path(directory) / "gfx10_format_table.c"
    with table.open("w") as generated:
        subprocess.run([sys.executable, "-B", str(MESA / "src/amd/common/gfx10_format_table.py"),
            str(MESA / "src/util/format/u_format.yaml"),
            str(MESA / "src/amd/registers/gfx10-rsrc.json"),
            str(MESA / "src/amd/registers/gfx11-rsrc.json")], stdout=generated, check=True)
    subprocess.run(["clang-18", "-std=gnu11", "-O2", "-Wall", "-Werror",
        "-DHAVE_ENDIAN_H=1", "-DHAVE_FUNC_ATTRIBUTE_PACKED=1", "-D_GNU_SOURCE",
        "-I", str(MESA / "include"), "-I", str(MESA / "src"),
        "-I", str(MESA / "src/gallium/include"), "-I", str(MESA / "src/gallium/auxiliary"),
        "-I", directory,
        "-I", str(ROOT / "third_party/opengnm-psbc/libpsbc"),
        "-I", str(ROOT / "third_party/opengnm-psbc/src"),
        "-I", str(ROOT / "src/platform"),
        "-I", str(MESA / "src/amd/common"),
        "-I", str(ROOT / "third_party/opengnm-psbc/src/amd/common"),
        "-x", "c", "-o", executable, "-", str(table), "-x", "none",
        str(ROOT / "third_party/opengnm-psbc/libpsbc.a"),
        "-lstdc++", "-pthread", "-lm"], input=code, text=True, check=True)
    subprocess.run([executable], check=True, timeout=10)
print("PASS: Gallium 39-resource bindings, image descriptors/lifetime, upload failure, direct/indirect guards and unbind")
print("PASS: Mesa sampler+render canonical SRDs without image hint; staging/allocation/layout guards, CS/FS refs and format mismatch")
print("PASS: R/RG default views, 24 RGBA channel/constant swizzles, reserved selector rejection, atomic replacement/unbind and references")
print("PASS: RGBA16/RGBA8_UNORM real GFX10 encoding, canonical/staged CS/FS bindings, mip/array bounds; guarded base-only tiled RGBA8")
print("PASS: Mesa atomic handoff CS/FS, SSBO counts 0/1/8 + bindings 0/7, alignment/offset state, refs, isolation, replacement/unbind")
print("PASS: Mesa CS/FS zero-initialized 0-to-16-to-2-to-0 tracking, stale cleanup, reference counts and stage isolation")
print("PASS: VS/TCS/TES storage isolation, invalid update preservation, GS/TCS/TES copied uniforms and UBO references")
