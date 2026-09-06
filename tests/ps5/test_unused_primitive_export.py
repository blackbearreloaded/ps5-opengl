#!/usr/bin/env python3
"""Compile the implicit PrimitiveID liveness contract with real NIR/ACO."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
code = r'''
#include <assert.h>
#include <stdio.h>
#include "compiler/nir/nir_builder.h"
#include "amd/common/amdgfxregs.h"
#include "psbc_compile.h"

static unsigned config(const PsbcShaderOutput *out) {
    for (unsigned i = 0; i < out->metadata.context_register_count; ++i)
        if (out->metadata.context_registers[i].offset == 0x1b1)
            return out->metadata.context_registers[i].value;
    assert(!"missing SPI_VS_OUT_CONFIG"); return 0;
}
static void check(unsigned varyings, bool explicit_id) {
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
        .vertex_attribute_count=1,
        .vertex_attributes={{.location=0, .binding=0,
            .format=PSBC_VERTEX_FORMAT_R32G32B32A32_FLOAT, .stride=16, .alignment=16}}};
    size_t baseline_size = 0;
    for (unsigned i = 0; i < 3; ++i) {
        options.omit_implicit_primitive_id = i == 1;
        PsbcShaderOutput out;
        assert(psbc_compile_nir(b.shader, &options, &out) == PSBC_RESULT_OK);
        assert(out.machine_code_size && out.metadata.hardware_stage == PSBC_HW_STAGE_NGG);
        unsigned state = config(&out);
        unsigned params = varyings + explicit_id;
        assert(G_0286C4_VS_EXPORT_COUNT(state) == (params ? params - 1 : 0));
        assert(G_0286C4_PRIM_EXPORT_COUNT(state) == (!explicit_id && i != 1));
        assert(G_0286C4_NO_PC_EXPORT(state) == (!params && i == 1));
        if (!i) baseline_size = out.machine_code_size;
        if (explicit_id || i == 2) assert(out.machine_code_size == baseline_size);
        if (!explicit_id && i == 1) assert(out.machine_code_size < baseline_size);
        printf("primitive-export varyings=%u explicit=%u omit=%u config=%x bytes=%zu\n",
               varyings, explicit_id, options.omit_implicit_primitive_id, state, out.machine_code_size);
        psbc_free_output(&out);
    }
    ralloc_free(b.shader);
}
int main(void) {
    psbc_init();
    for (unsigned i = 0; i <= 2; ++i) { check(i, false); check(i, true); }
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
        "-x", "c", "-c", "-o", obj, "-"], input=code, text=True, check=True)
    subprocess.run(["g++", "-o", executable, obj, str(psbc / "libpsbc.a"),
                    "-pthread", "-lm"], check=True)
    subprocess.run([executable], check=True)
source = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
assert "variant->omit_implicit_primitive_id == omit_implicit_primitive_id" in source
assert "variant->omit_implicit_primitive_id = omit_implicit_primitive_id" in source
assert "options.omit_implicit_primitive_id = omit_implicit_primitive_id" in source
assert "!(context->fs->nir->info.inputs_read & VARYING_BIT_PRIMITIVE_ID)" in source
assert "SYSTEM_VALUE_PRIMITIVE_ID)" in source
print("PASS: unknown/dead/explicit PrimitiveID, unchanged vertex exports, cache discriminator")
