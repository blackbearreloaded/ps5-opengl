import struct
import unittest

import agc_shader_package as agc


def build_package() -> bytearray:
    code = bytes(range(16))
    header = bytearray(168)
    struct.pack_into("<II", header, 0, agc.AGC_MAGIC, 24)
    # sceAgcCreateShader resolves each pointer as field_address + stored_value.
    struct.pack_into("<Q", header, 8, 112 - 8)
    struct.pack_into("<QQ", header, 24, 96 - 24, 104 - 32)
    struct.pack_into("<II", header, 64, len(header), len(code))
    struct.pack_into("<BBB", header, 90, 1, 1, 1)
    struct.pack_into("<HHI", header, 96, 0x123, 0, 0xAABBCCDD)
    struct.pack_into("<HHI", header, 104, 0x456, 0, 0x11223344)

    names = b"\0.shader_text\0.shader_header\0.shstrtab\0"
    text_at = 0x100
    header_at = text_at + len(code)
    names_at = header_at + len(header)
    shoff = (names_at + len(names) + 7) & ~7
    result = bytearray(shoff + 4 * 64)
    result[:4] = b"\x7fELF"
    result[4:7] = b"\x02\x01\x01"
    struct.pack_into("<HHI", result, 16, 2, agc.EM_AMDGPU, 1)
    struct.pack_into("<QQ", result, 32, 0, shoff)
    struct.pack_into("<HHHHHH", result, 52, 64, 56, 0, 64, 4, 3)
    result[text_at:text_at + len(code)] = code
    result[header_at:header_at + len(header)] = header
    result[names_at:names_at + len(names)] = names

    def section(index, name, kind, flags, offset, size, alignment):
        struct.pack_into("<IIQQQQIIQQ", result, shoff + index * 64,
                         name, kind, flags, 0, offset, size, 0, 0, alignment, 0)

    section(0, 0, 0, 0, 0, 0, 0)
    section(1, 1, 1, 6, text_at, len(code), 256)
    section(2, 14, 1, 3, header_at, len(header), 8)
    section(3, 29, 3, 0, names_at, len(names), 1)
    return result


class AgcShaderPackageTests(unittest.TestCase):
    def test_valid_package(self):
        report = agc.inspect_package(bytes(build_package()))
        self.assertEqual("pixel", report["shader"]["stage_name"])
        self.assertEqual(0xAABBCCDD, report["shader"]["context_registers"][0]["value"])
        self.assertEqual(0x456, report["shader"]["shader_registers"][0]["offset"])
        self.assertEqual(96, report["shader"]["context_register_offset"])
        self.assertEqual(104, report["shader"]["shader_register_offset"])
        self.assertEqual(112, report["shader"]["resource_layout_offset"])
        self.assertEqual([], report["warnings"])

    def test_rejects_out_of_range_section_table(self):
        package = build_package()
        struct.pack_into("<Q", package, 40, len(package) - 8)
        with self.assertRaisesRegex(agc.PackageError, "section table"):
            agc.inspect_package(bytes(package))

    def test_rejects_out_of_range_section(self):
        package = build_package()
        shoff = struct.unpack_from("<Q", package, 40)[0]
        struct.pack_into("<Q", package, shoff + 64 + 32, len(package))
        with self.assertRaisesRegex(agc.PackageError, "shader_text"):
            agc.inspect_package(bytes(package))

    def test_rejects_unterminated_section_name(self):
        package = build_package()
        shoff = struct.unpack_from("<Q", package, 40)[0]
        names_at = struct.unpack_from("<Q", package, shoff + 3 * 64 + 24)[0]
        names_size = struct.unpack_from("<Q", package, shoff + 3 * 64 + 32)[0]
        package[names_at:names_at + names_size] = b"X" * names_size
        with self.assertRaisesRegex(agc.PackageError, "NUL-terminated"):
            agc.inspect_package(bytes(package))

    def test_rejects_duplicate_required_section(self):
        package = build_package()
        shoff = struct.unpack_from("<Q", package, 40)[0]
        struct.pack_into("<I", package, shoff + 64, 14)
        with self.assertRaisesRegex(agc.PackageError, "exactly one .shader_header"):
            agc.inspect_package(bytes(package))

    def test_rejects_declared_size_mismatch(self):
        package = build_package()
        struct.pack_into("<I", package, 0x110 + 68, 15)
        with self.assertRaisesRegex(agc.PackageError, "declares 15 code bytes"):
            agc.inspect_package(bytes(package))

    def test_rejects_register_array_out_of_range(self):
        package = build_package()
        struct.pack_into("<Q", package, 0x110 + 24, 144)
        with self.assertRaisesRegex(agc.PackageError, "context register relative offset"):
            agc.inspect_package(bytes(package))

    def test_rejects_prepopulated_code_address(self):
        package = build_package()
        struct.pack_into("<Q", package, 0x110 + 16, 0x1234)
        with self.assertRaisesRegex(agc.PackageError, "code-address field"):
            agc.inspect_package(bytes(package))

    def test_rejects_truncated_resource_layout(self):
        package = build_package()
        struct.pack_into("<Q", package, 0x110 + 8, 112)
        with self.assertRaisesRegex(agc.PackageError, "resource layout prefix"):
            agc.inspect_package(bytes(package))

    def test_rejects_zero_resource_layout(self):
        package = build_package()
        struct.pack_into("<Q", package, 0x110 + 8, 0)
        with self.assertRaisesRegex(agc.PackageError, "resource layout relative offset is zero"):
            agc.inspect_package(bytes(package))

    def test_rejects_unknown_program_type(self):
        package = build_package()
        package[0x110 + 90] = 9
        with self.assertRaisesRegex(agc.PackageError, "outside the known 0..8 range"):
            agc.inspect_package(bytes(package))

    def test_rejects_excessive_semantic_count(self):
        package = build_package()
        struct.pack_into("<I", package, 0x110 + 80, 33)
        with self.assertRaisesRegex(agc.PackageError, "hardware limit of 32"):
            agc.inspect_package(bytes(package))


if __name__ == "__main__":
    unittest.main()
