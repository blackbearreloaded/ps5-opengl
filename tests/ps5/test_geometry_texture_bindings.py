#!/usr/bin/env python3
"""Exercise real descriptor helpers and NIR index remapping without a GPU."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
screen = (ROOT / "src/gallium/ps5/ps5_screen.c").read_text()


def function(name):
    start = screen.index("static bool\n" + name + "(")
    return screen[start:screen.index("\n}", start) + 3]


defines = "\n".join(re.findall(
    r"^#define PS5_(?:MAX_TEXTURE_UNITS|MERGED_TEXTURE_UNITS|MAX_CONSTANT_BUFFERS|"
    r"TEXTURE_DESCRIPTOR_STRIDE|TEXTURE_DESCRIPTOR_BYTES|CONSTANT_DATA_OFFSET) "
    r"(?:[^\n]*\\\n)?[^\n]*", screen, re.M))
code = r'''
#include <assert.h>
#include <string.h>
#include "compiler/nir/nir_builder.h"
#include "psbc_compile.h"
''' + defines + "\n" + function("ps5_append_texture_descriptor") + "\n" + \
    function("ps5_append_ubo_descriptors") + "\n" + \
    function("ps5_offset_geometry_texture") + r'''
int main(void) {
    PsbcCompileOptions options = {0};
    for (unsigned i = 0; i < 32; ++i) {
        assert(ps5_append_texture_descriptor(&options, i));
        assert(ps5_append_texture_descriptor(&options, i));
        assert(options.descriptor_binding_count == i + 1);
        assert(options.descriptor_bindings[i].offset == 48 * i);
    }
    assert(!ps5_append_texture_descriptor(&options, 32));
    assert(ps5_append_ubo_descriptors(&options, 0, 26));
    assert(options.descriptor_binding_count == 58);
    assert(!ps5_append_ubo_descriptors(&options, 26, 1));
    for (unsigned i = 0; i < 58; ++i) {
        const PsbcDescriptorBinding *a = &options.descriptor_bindings[i];
        assert(a->offset + a->stride <= PS5_CONSTANT_DATA_OFFSET);
        for (unsigned j = 0; j < i; ++j) {
            const PsbcDescriptorBinding *b = &options.descriptor_bindings[j];
            assert(a->binding != b->binding);
            assert(a->offset >= b->offset + b->stride ||
                   b->offset >= a->offset + a->stride);
        }
    }
    for (unsigned i = 0; i < 16; ++i) {
        bool valid = true;
        nir_tex_instr tex = {0};
        tex.instr.type = nir_instr_type_tex;
        tex.texture_index = tex.sampler_index = i;
        assert(ps5_offset_geometry_texture(NULL, &tex.instr, &valid));
        assert(valid && tex.texture_index == i + 16 && tex.sampler_index == i + 16);
        assert(!ps5_offset_geometry_texture(NULL, &tex.instr, &valid) && !valid);
    }
    bool valid = true;
    nir_instr other = {0};
    other.type = nir_instr_type_alu;
    assert(!ps5_offset_geometry_texture(NULL, &other, &valid) && valid);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "geometry-bindings")
    psbc = ROOT / "third_party/opengnm-psbc"
    subprocess.run([
        "clang-18", "-std=gnu11", "-Wall", "-Werror",
        "-DHAVE_ENDIAN_H=1", "-DHAVE_FUNC_ATTRIBUTE_PACKED=1", "-D_GNU_SOURCE",
        "-DHAVE_PTHREAD=1", "-DHAVE_STRUCT_TIMESPEC=1",
        "-I", str(psbc / "include/mesa"), "-I", str(psbc / "include"),
        "-I", str(psbc / "src"), "-I", str(psbc / "src/gallium/include"),
        "-I", str(psbc / "libpsbc"),
        "-x", "c", "-o", executable, "-"], input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: 32 sampler + 26 UBO descriptors, no alias/overlap; GS NIR index bounds")
