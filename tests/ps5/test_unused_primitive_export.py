#!/usr/bin/env python3
"""Compile PrimitiveID liveness and producer/consumer linkage with real NIR/ACO."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler/nir/nir_builder.h"
#include "amd/common/amdgfxregs.h"
#include "psbc_compile.h"
#include "ps5_agc_package.h"

static unsigned config(const PsbcShaderOutput *out, unsigned offset) {
    for (unsigned i = 0; i < out->metadata.context_register_count; ++i)
        if (out->metadata.context_registers[i].offset == offset)
            return out->metadata.context_registers[i].value;
    assert(!"missing shader register"); return 0;
}
static void package(const PsbcShaderOutput *out) {
    uint8_t *data = NULL;
    size_t size = 0;
    assert(!(out->metadata.unresolved_fields & PSBC_UNRESOLVED_AGC_LINKAGE));
    assert(ps5_agc_package_build(out, 4, &data, &size) == 0 && size);
    if (out->metadata.hardware_stage == PSBC_HW_STAGE_NGG &&
        out->metadata.source_stage == PSBC_STAGE_VERTEX) {
        /* Check serialized state, not just compiler metadata: packaging used
         * to replace this stride with 4 even when NIR consumes vertex indices. */
        size_t header = (0x100 + out->machine_code_size + 7) & ~(size_t)7;
        uint64_t relative;
        assert(header + 96 <= size);
        memcpy(&relative, data + header + 24, sizeof(relative));
        size_t context = header + 24 + relative;
        unsigned count = data[header + 91];
        assert(context + count * 8 <= size);
        bool found = false;
        for (unsigned i = 0; i < count; ++i) {
            uint16_t offset;
            uint32_t value;
            memcpy(&offset, data + context + i * 8, sizeof(offset));
            memcpy(&value, data + context + i * 8 + 4, sizeof(value));
            if (offset == 0x2ab) { assert(value == 1); found = true; }
        }
        assert(found);
    }
    free(data);
}
static void check(unsigned varyings, bool explicit_id, bool last) {
    nir_builder b = nir_builder_init_simple_shader(
        MESA_SHADER_VERTEX, psbc_get_nir_options(PSBC_STAGE_VERTEX), "primitive-export");
    nir_variable *input = nir_variable_create(b.shader, nir_var_shader_in,
                                             glsl_vec4_type(), "input");
    input->data.location = VERT_ATTRIB_GENERIC0;
    nir_def *value = nir_load_var(&b, input);
    for (unsigned i = 0; i <= varyings; ++i) {
        nir_variable *out = nir_variable_create(b.shader, nir_var_shader_out,
                                               glsl_vec4_type(), "output");
        out->data.location = i ? VARYING_SLOT_VAR0 + i - 1 : VARYING_SLOT_POS;
        nir_store_var(&b, out, i == 2 ? nir_fmul_imm(&b, value, 0.5) : value, 15);
    }
    if (explicit_id) {
        nir_variable *id = nir_variable_create(b.shader, nir_var_shader_out,
                                               glsl_int_type(), "id");
        id->data.location = VARYING_SLOT_PRIMITIVE_ID;
        nir_store_var(&b, id, nir_load_vertex_id(&b), 1);
    }
    PsbcCompileOptions options = {.target=PSBC_TARGET_PS5, .stage=PSBC_STAGE_VERTEX,
        .optimise=true, .ngg=true, .primitive_type=4, .address32_hi=2,
        .provoking_vtx_last=last,
        .vertex_attribute_count=1,
        .vertex_attributes={{.location=0, .binding=0,
            .format=PSBC_VERTEX_FORMAT_R32G32B32A32_FLOAT, .stride=16, .alignment=16}}};
    size_t baseline_size = 0;
    for (unsigned i = 0; i < 3; ++i) {
        options.omit_implicit_primitive_id = i == 1;
        PsbcShaderOutput out;
        assert(psbc_compile_nir(b.shader, &options, &out) == PSBC_RESULT_OK);
        assert(out.machine_code_size && out.metadata.hardware_stage == PSBC_HW_STAGE_NGG);
        unsigned state = config(&out, 0x1b1);
        bool implicit_id = !explicit_id && i != 1;
        unsigned params = varyings + explicit_id + implicit_id;
        assert(G_0286C4_VS_EXPORT_COUNT(state) == (params ? params - 1 : 0));
        assert(G_0286C4_PRIM_EXPORT_COUNT(state) == 0);
        assert(G_0286C4_NO_PC_EXPORT(state) == (!params && i == 1));
        assert(G_028A84_NGG_DISABLE_PROVOK_REUSE(config(&out, 0x2a1)) == implicit_id);
        assert(G_028B54_PRIMGEN_PASSTHRU_EN(out.metadata.linkage_stages_en.value) == !implicit_id);
        if (!i) baseline_size = out.machine_code_size;
        if (explicit_id || i == 2) assert(out.machine_code_size == baseline_size);
        if (!explicit_id && i == 1) assert(out.machine_code_size < baseline_size);
        bool exports_id = explicit_id || i != 1;
        assert(out.metadata.output_semantic_count == varyings + exports_id);
        if (exports_id) {
            unsigned semantic = out.metadata.output_semantics[varyings];
            assert((semantic & 255) == PSBC_SEMANTIC_PRIMITIVE_ID);
            /* Explicit per-vertex ID is assigned before generic outputs;
             * implicit per-vertex ID retains the final export slot. */
            assert(((semantic >> 8) & 31) == (explicit_id ? 0 : varyings));
        }
        package(&out);
        printf("primitive-export varyings=%u explicit=%u omit=%u last=%u config=%x bytes=%zu\n",
               varyings, explicit_id, options.omit_implicit_primitive_id, last, state, out.machine_code_size);
        psbc_free_output(&out);
    }
    ralloc_free(b.shader);
}
static void consumer(unsigned varyings, bool mixed) {
    nir_builder b = nir_builder_init_simple_shader(
        MESA_SHADER_FRAGMENT, psbc_get_nir_options(PSBC_STAGE_FRAGMENT), "primitive-consumer");
    b.shader->info.io_lowered = true;
    nir_def *zero = nir_imm_int(&b, 0);
    /* io_lowered input follows Mesa's assigned FS attribute order. */
    nir_def *id = nir_load_input(&b, 1, 32, zero, .base=varyings, .dest_type=nir_type_int32,
        .io_semantics={.location=VARYING_SLOT_PRIMITIVE_ID, .num_slots=1});
    nir_def *value = nir_i2f32(&b, id);
    for (unsigned i = 0; i < varyings; ++i) {
        nir_def *bary = nir_load_barycentric_pixel(&b, 32, .interp_mode=INTERP_MODE_SMOOTH);
        nir_def *v = nir_load_interpolated_input(&b, 1, 32, bary, zero,
            .base=i, .dest_type=nir_type_float32,
            .io_semantics={.location=VARYING_SLOT_VAR0 + i, .num_slots=1});
        value = nir_fadd(&b, value, v);
    }
    if (mixed) {
        nir_def *flat = nir_load_input(&b, 1, 32, zero, .component=1,
            .dest_type=nir_type_float32,
            .io_semantics={.location=VARYING_SLOT_VAR0, .num_slots=1});
        value = nir_fadd(&b, value, flat);
    }
    nir_store_output(&b, nir_vec4(&b, value, value, value, nir_imm_float(&b, 1)), zero,
        .src_type=nir_type_float32, .io_semantics={.location=FRAG_RESULT_DATA0, .num_slots=1});
    nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
    PsbcCompileOptions options = {.target=PSBC_TARGET_PS5, .stage=PSBC_STAGE_FRAGMENT,
        .optimise=true, .primitive_type=4, .spi_shader_col_format=9};
    for (unsigned per_primitive = 0; per_primitive < 2; ++per_primitive) {
        void *first_code = NULL;
        size_t first_size = 0;
        for (unsigned last = 0; last < 2; ++last) {
            options.primitive_id_per_primitive = per_primitive;
            options.provoking_vtx_last = last;
            PsbcShaderOutput out;
            assert(psbc_compile_nir(b.shader, &options, &out) == PSBC_RESULT_OK);
            unsigned state = config(&out, 0x1b6);
            unsigned count = varyings + mixed + 1;
            assert(out.metadata.input_semantic_count == count);
            assert(G_0286D8_NUM_INTERP(state) == count - per_primitive);
            assert(G_0286D8_NUM_PRIM_INTERP(state) == per_primitive);
            unsigned id_attribute = per_primitive ? count - 1 : varyings;
            assert(out.metadata.input_semantics[id_attribute] ==
                (PSBC_SEMANTIC_PRIMITIVE_ID | (per_primitive ? 0 : 1u << 22)));
            if (mixed) {
                unsigned flat_attribute = per_primitive ? count - 2 : count - 1;
                assert(out.metadata.input_semantics[flat_attribute] == (15u | 1u << 22));
            }
            package(&out);
            if (!last) {
                first_size = out.machine_code_size;
                first_code = malloc(first_size);
                assert(first_code);
                memcpy(first_code, out.machine_code, first_size);
            } else {
                bool identical = first_size == out.machine_code_size &&
                    !memcmp(first_code, out.machine_code, first_size);
                assert(identical == (per_primitive && !mixed));
                free(first_code);
            }
            printf("primitive-consumer varyings=%u mixed=%u per-primitive=%u last=%u inputs=%u config=%x\n",
                varyings, mixed, per_primitive, last, count, state);
            psbc_free_output(&out);
        }
    }
    options.target = PSBC_TARGET_PS4_BASE;
    PsbcShaderOutput invalid;
    assert(psbc_compile_nir(b.shader, &options, &invalid) == PSBC_RESULT_UNSUPPORTED_STAGE);
    assert(!invalid.machine_code && !invalid.data);
    options.target = PSBC_TARGET_PS5;
    if (varyings) {
        /* Reject a malformed already-lowered producer/consumer alias. */
        nir_intrinsic_set_base(nir_instr_as_intrinsic(nir_def_instr(id)), 0);
        options.primitive_id_per_primitive = false;
        PsbcShaderOutput out;
        assert(psbc_compile_nir(b.shader, &options, &out) == PSBC_RESULT_INTERNAL_ERROR);
        assert(!out.machine_code && !out.data);
    }
    ralloc_free(b.shader);
}
int main(void) {
    psbc_init();
    for (unsigned i = 0; i <= 2; ++i)
        for (unsigned last = 0; last < 2; ++last) { check(i, false, last); check(i, true, last); }
    for (unsigned i = 0; i <= 2; ++i) {
        consumer(i, false);
        if (i) consumer(i, true);
    }
    psbc_shutdown();
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "primitive-export")
    obj = str(Path(temporary) / "primitive-export.o")
    psbc = ROOT / "third_party/opengnm-psbc"
    subprocess.run(["clang-18", "-std=gnu11", "-Wall", "-Werror",
        "-DHAVE_ENDIAN_H=1", "-DHAVE_FUNC_ATTRIBUTE_PACKED=1", "-DHAVE_PTHREAD=1",
        "-DHAVE_STRUCT_TIMESPEC=1", "-D_GNU_SOURCE",
        "-I", str(psbc / "include/mesa"), "-I", str(psbc / "include"),
        "-I", str(psbc / "src"), "-I", str(psbc / "libpsbc"),
        "-I", str(ROOT / "src/platform"),
        "-x", "c", "-c", "-o", obj, "-"], input=code, text=True, check=True)
    package_obj = str(Path(temporary) / "package.o")
    subprocess.run(["clang-18", "-std=c11", "-Wall", "-Werror",
        "-I", str(psbc / "libpsbc"), "-c", str(ROOT / "src/platform/ps5_agc_package.c"),
        "-o", package_obj], check=True)
    subprocess.run(["g++", "-o", executable, obj, package_obj, str(psbc / "libpsbc.a"),
                    "-pthread", "-lm"], check=True)
    subprocess.run([executable], check=True)
source = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
assert "variant->omit_implicit_primitive_id == omit_implicit_primitive_id" in source
assert "variant->omit_implicit_primitive_id = omit_implicit_primitive_id" in source
assert "options.omit_implicit_primitive_id = omit_implicit_primitive_id" in source
assert "variant->primitive_id_per_primitive == primitive_id_per_primitive" in source
assert "variant->primitive_id_per_primitive = primitive_id_per_primitive" in source
assert "options.primitive_id_per_primitive = primitive_id_per_primitive" in source
assert "!(context->fs->nir->info.inputs_read & VARYING_BIT_PRIMITIVE_ID)" in source
assert "SYSTEM_VALUE_PRIMITIVE_ID)" in source
print("PASS: PrimitiveID exports/consumers, mixed interpolation, provoking vertex, packages, cache keys")
