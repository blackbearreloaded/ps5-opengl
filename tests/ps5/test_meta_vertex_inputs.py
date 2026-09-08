#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compile real meta VS input normalization, layout selection and PSBC lowering."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
path = "src/gallium/ps5/ps5_screen.c"
source = (subprocess.check_output(["git", "show", "fe47f6e:" + path], cwd=ROOT, text=True)
          if "--baseline" in sys.argv else (ROOT / path).read_text())


def function(name, result):
    start = source.index("static " + result + "\n" + name + "(")
    return source[start:source.index("\n}", start) + 2]


start = source.index("   if (stage == PSBC_STAGE_VERTEX && templ->ir.nir->info.io_lowered) {")
normalization = source[start:source.index("\n   }\n", start) + 6]
callback = function("ps5_rebase_meta_vertex_input", "bool") if "ps5_rebase_meta_vertex_input(" in source else ""
code = r'''
#include <assert.h>
#include <string.h>
#include "compiler/nir/nir_builder.h"
#include "pipe/p_state.h"
#include "psbc_compile.h"
struct ps5_vertex_layout { uint32_t count; PsbcVertexAttribute attributes[32]; };
''' + callback + "\n" + function("ps5_default_vertex_layout", "bool") + r'''
static nir_shader *normalize(nir_shader *nir) {
    struct pipe_shader_state storage = {.ir.nir = nir};
    const struct pipe_shader_state *templ = &storage;
    const PsbcStage stage = PSBC_STAGE_VERTEX;
''' + normalization + r'''
    return nir;
}
static void check(const unsigned *locations, unsigned count, uint64_t expected) {
    nir_builder b = nir_builder_init_simple_shader(
        MESA_SHADER_VERTEX, psbc_get_nir_options(PSBC_STAGE_VERTEX), "meta-inputs");
    b.shader->info.io_lowered = true;
    nir_def *position = nir_imm_vec4(&b, 0, 0, 0, 0);
    for (unsigned i = 0; i < count; ++i)
        position = nir_fadd(&b, position, nir_load_input(&b, 4, 32, nir_imm_int(&b, 0),
                                                       .io_semantics.location = locations[i]));
    nir_store_output(&b, position, nir_imm_int(&b, 0),
                     .src_type = nir_type_float32, .io_semantics.location = VARYING_SLOT_POS);
    nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
    assert(normalize(b.shader));
    assert(b.shader->info.inputs_read == expected);
    struct ps5_vertex_layout layout;
    assert(ps5_default_vertex_layout(b.shader, &layout));
    assert(layout.count == count);
    for (unsigned i = 0; i < count; ++i)
        assert(layout.attributes[i].binding == i);
    PsbcCompileOptions options = {
        .target = PSBC_TARGET_PS5, .stage = PSBC_STAGE_VERTEX, .optimise = true,
        .ngg = true, .primitive_type = 6, .address32_hi = 2,
        .vertex_attribute_count = layout.count,
    };
    memcpy(options.vertex_attributes, layout.attributes, sizeof(layout.attributes));
    PsbcShaderOutput out;
    assert(psbc_compile_nir(b.shader, &options, &out) == PSBC_RESULT_OK);
    assert(out.machine_code_size && out.metadata.vertex_buffer_table_valid);
    psbc_free_output(&out);
    ralloc_free(b.shader);
}
int main(void) {
    const unsigned meta[] = {VERT_ATTRIB_POS};
    const unsigned mixed[] = {VERT_ATTRIB_POS, VERT_ATTRIB_GENERIC0 + 5};
    const unsigned generic[] = {VERT_ATTRIB_GENERIC0 + 3, VERT_ATTRIB_GENERIC0 + 7};
    psbc_init();
    check(meta, 1, BITFIELD64_BIT(VERT_ATTRIB_GENERIC0));
    check(mixed, 2, UINT64_C(3) << VERT_ATTRIB_GENERIC0);
    check(generic, 2, BITFIELD64_BIT(VERT_ATTRIB_GENERIC0 + 3) |
                      BITFIELD64_BIT(VERT_ATTRIB_GENERIC0 + 7));
    psbc_shutdown();
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "meta-inputs")
    obj = str(Path(temporary) / "meta-inputs.o")
    psbc = ROOT / "third_party/opengnm-psbc"
    subprocess.run(["clang-18", "-std=gnu11", "-Wall", "-Werror",
                    "-DHAVE_ENDIAN_H=1", "-DHAVE_FUNC_ATTRIBUTE_PACKED=1",
                    "-DHAVE_PTHREAD=1", "-DHAVE_STRUCT_TIMESPEC=1", "-D_GNU_SOURCE",
                    "-I", str(psbc / "include/mesa"), "-I", str(psbc / "include"),
                    "-I", str(psbc / "src"), "-I", str(psbc / "src/gallium/include"),
                    "-I", str(psbc / "libpsbc"), "-x", "c", "-c", "-o", obj, "-"],
                   input=code, text=True, check=True)
    subprocess.run(["g++", "-o", executable, obj, str(psbc / "libpsbc.a"), "-pthread", "-lm"], check=True)
    subprocess.run([executable], check=True)
print("PASS: meta position input, mixed slots, untouched sparse generic inputs, real NGG compilation")
