#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Real compiler/driver checks for constant, float and packed color attributes."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
rejected = "--expect-rejected" in sys.argv
source = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()
descriptor = ""
if not rejected:
    for name in ("ps5_vertex_buffer_descriptor", "ps5_packed_vertex_format", "ps5_vertex_format"):
        start = source.rindex(f"static bool\n{name}(")
        descriptor += source[start:source.index("\n}", start) + 2] + "\n"
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "compiler/nir/nir_builder.h"
#include "amd/common/amdgfxregs.h"
#include "psbc_compile.h"
#include "util/format/u_format.h"
#define PS5_ENABLE_PACKED_VERTEX_CANDIDATE 1
#define PS5_ENABLE_INTEGER_VERTEX_CANDIDATE 1
''' + descriptor + r'''
int main(void) {
    psbc_init();
    nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX,
        psbc_get_nir_options(PSBC_STAGE_VERTEX), "constant-attribute");
    nir_variable *input = nir_variable_create(b.shader, nir_var_shader_in,
                                             glsl_vec4_type(), "input");
    input->data.location = VERT_ATTRIB_GENERIC0;
    nir_def *value = nir_load_var(&b, input);
    for (unsigned i = 0; i < 2; ++i) {
        nir_variable *output = nir_variable_create(b.shader, nir_var_shader_out,
                                                   glsl_vec4_type(), "output");
        output->data.location = i ? VARYING_SLOT_VAR0 : VARYING_SLOT_POS;
        nir_store_var(&b, output, value, 15);
    }
    PsbcCompileOptions options = {.target=PSBC_TARGET_PS5, .stage=PSBC_STAGE_VERTEX,
        .ngg=true, .optimise=true, .primitive_type=6, .address32_hi=2,
        .vertex_attribute_count=1, .vertex_attributes={{.location=0, .binding=0,
            .format=PSBC_VERTEX_FORMAT_R32G32B32A32_FLOAT, .alignment=4}}};
    for (unsigned stride = 0; stride <= 16; stride += 16)
        for (unsigned omit = 0; omit < 2; ++omit)
            for (unsigned offset = 0; offset <= 16; offset += 16)
            for (unsigned format = 0; format < (EXPECT_REJECTED ? 1 : 3); ++format) {
                const PsbcVertexFormat formats[] = {PSBC_VERTEX_FORMAT_R32G32B32A32_FLOAT,
                    PSBC_VERTEX_FORMAT_B8G8R8A8_UNORM, PSBC_VERTEX_FORMAT_R8G8B8A8_UNORM};
                options.vertex_attributes[0].format = formats[format];
                options.vertex_attributes[0].stride = stride;
                options.vertex_attributes[0].offset = offset;
                options.omit_implicit_primitive_id = omit;
                PsbcShaderOutput out;
                PsbcResult result = psbc_compile_nir(b.shader, &options, &out);
                assert(result == (EXPECT_REJECTED && !stride ? PSBC_RESULT_INTERNAL_ERROR : PSBC_RESULT_OK));
                printf("vertex-input format=%u stride=%u offset=%u omit=%u result=%u\n", format, stride, offset, omit, result);
                psbc_free_output(&out);
            }
    options.vertex_attributes[0].alignment = 0;
    PsbcShaderOutput out;
    assert(psbc_compile_nir(b.shader, &options, &out) == PSBC_RESULT_INTERNAL_ERROR);
    assert(!out.machine_code);
    ralloc_free(b.shader);
    psbc_shutdown();
#if !EXPECT_REJECTED
    PsbcVertexFormat format;
    assert(ps5_packed_vertex_format(PIPE_FORMAT_R8G8B8A8_UNORM));
    assert(ps5_vertex_format(PIPE_FORMAT_R8G8B8A8_UNORM, &format));
    assert(format == PSBC_VERTEX_FORMAT_R8G8B8A8_UNORM);
    assert(ps5_vertex_format(PIPE_FORMAT_B8G8R8A8_UNORM, &format));
    assert(format == PSBC_VERTEX_FORMAT_B8G8R8A8_UNORM);
    uint32_t desc[4];
    const uintptr_t address = UINT64_C(0x201234000);
    assert(ps5_vertex_buffer_descriptor(address, 64, 0, 1, desc));
    assert(desc[0] == (uint32_t)address && desc[1] == 2 && desc[2] == 64);
    assert(desc[3] == (UINT32_C(0x5204) | S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW)));
    assert(ps5_vertex_buffer_descriptor(address, 64, 16, 4, desc));
    assert(desc[1] == (2 | (16u << 16)) && desc[2] == 4 && desc[3] == 0x5204);
    assert(!ps5_vertex_buffer_descriptor(address, 0, 0, 1, desc));
    assert(!ps5_vertex_buffer_descriptor(address, UINT64_C(0x100000000), 0, 1, desc));
    assert(ps5_vertex_buffer_descriptor(address, UINT32_MAX, 0, 1, desc));
    assert(desc[2] == UINT32_MAX);
#endif
}
'''
with tempfile.TemporaryDirectory() as tmp:
    obj, exe = str(Path(tmp) / "test.o"), str(Path(tmp) / "test")
    psbc = ROOT / "third_party/opengnm-psbc"
    subprocess.run(["clang-18", "-std=gnu11", "-Wall", "-Werror", f"-DEXPECT_REJECTED={int(rejected)}",
        "-DHAVE_ENDIAN_H=1", "-DHAVE_FUNC_ATTRIBUTE_PACKED=1", "-DHAVE_PTHREAD=1",
        "-DHAVE_STRUCT_TIMESPEC=1", "-D_GNU_SOURCE", "-I", str(psbc / "include/mesa"),
        "-I", str(psbc / "include"), "-I", str(psbc / "src"), "-I", str(psbc / "libpsbc"),
        "-x", "c", "-c", "-o", obj, "-"], input=code, text=True, check=True)
    subprocess.run(["g++", "-o", exe, obj, str(psbc / "libpsbc.a"), "-pthread", "-lm"], check=True)
    subprocess.run([exe], check=True)
print("PASS: reproduced zero-stride rejection independent of PrimitiveID" if rejected else
      "PASS: float/RGBA8/BGRA8 inputs, constant/ordinary strides, offsets, both export variants and bounded RAW descriptors")
