#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check actual builtin-color broadcasting and host PSBC compilation."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
path = "src/gallium/ps5/ps5_screen.c"
source = (subprocess.check_output(["git", "show", "584a1cd:" + path], cwd=ROOT, text=True)
          if "--baseline" in sys.argv else (ROOT / path).read_text())
start = source.index("static bool\nps5_lower_fragment_color(")
callback = source[start:source.index("\n}", start) + 2]
code = r'''
#include <assert.h>
#include <string.h>
#include "compiler/nir/nir_builder.h"
#include "psbc_compile.h"
#include "pssl_types.h"
''' + callback + r'''
static void check(unsigned mask) {
    nir_builder b = nir_builder_init_simple_shader(
        MESA_SHADER_FRAGMENT, psbc_get_nir_options(PSBC_STAGE_FRAGMENT), "meta-outputs");
    b.shader->info.io_lowered = true;
    b.shader->info.fs.untyped_color_outputs = true;
    nir_store_output(&b, nir_imm_vec4(&b, .75, .25, .5, .125), nir_imm_int(&b, 0),
                     .io_semantics.location = FRAG_RESULT_COLOR);
    nir_shader_instructions_pass(b.shader, ps5_lower_fragment_color,
                                 nir_metadata_control_flow, &mask);
    nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
    assert(b.shader->info.outputs_written == (uint64_t)mask << FRAG_RESULT_DATA0);
    nir_validate_shader(b.shader, "after builtin color broadcast");
    PsbcCompileOptions options = {
        .target = PSBC_TARGET_PS5, .stage = PSBC_STAGE_FRAGMENT, .optimise = true,
        .spi_shader_col_format = 0x99999944,
    };
    PsbcShaderOutput out;
    assert(psbc_compile_nir(b.shader, &options, &out) == PSBC_RESULT_OK);
    assert(out.machine_code_size);
    unsigned expected = 0, actual = ~0u, expected_mask = 0, actual_mask = ~0u;
    unsigned packed_slot = 0;
    for (unsigned i = 0; i < 8; ++i)
        if (mask & (1u << i)) {
            expected |= ((options.spi_shader_col_format >> (4 * i)) & 15u)
                        << (4 * packed_slot++);
            expected_mask |= 15u << (4 * i);
        }
    for (unsigned i = 0; i < out.metadata.context_register_count; ++i) {
        if (out.metadata.context_registers[i].offset == 0x1c5)
            actual = out.metadata.context_registers[i].value;
        if (out.metadata.context_registers[i].offset == 0x8f)
            actual_mask = out.metadata.context_registers[i].value;
    }
    assert(actual == expected);
    assert(actual_mask == expected_mask);
    GnmPsShader binary;
    assert(out.size >= sizeof(PsslBinaryHeader) + sizeof(GnmShaderFileHeader) + sizeof(binary));
    memcpy(&binary, (const char *)out.data + sizeof(PsslBinaryHeader) + sizeof(GnmShaderFileHeader), sizeof(binary));
    assert(binary.registers.spishadercolformat == expected);
    assert(binary.registers.cbshadermask == expected_mask);
    psbc_free_output(&out);
    ralloc_free(b.shader);
}
int main(void) {
    psbc_init();
    check(7); check(6); check(1); check(0x80); check(0);
    psbc_shutdown();
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "meta-outputs")
    obj = str(Path(temporary) / "meta-outputs.o")
    psbc = ROOT / "third_party/opengnm-psbc"
    subprocess.run(["clang-18", "-std=gnu11", "-Wall", "-Werror",
                    "-DHAVE_ENDIAN_H=1", "-DHAVE_FUNC_ATTRIBUTE_PACKED=1",
                    "-DHAVE_PTHREAD=1", "-DHAVE_STRUCT_TIMESPEC=1", "-D_GNU_SOURCE",
                    "-I", str(psbc / "include/mesa"), "-I", str(psbc / "include"),
                    "-I", str(psbc / "src"), "-I", str(psbc / "libpsbc"),
                    "-I", str(ROOT / "third_party/opengnm/include"),
                    "-x", "c", "-c", "-o", obj, "-"], input=code, text=True, check=True)
    subprocess.run(["g++", "-o", executable, obj, str(psbc / "libpsbc.a"), "-pthread", "-lm"], check=True)
    subprocess.run([executable], check=True)
print("PASS: builtin color broadcast, sparse/high/zero targets, mixed export formats, real PSBC compilation")
