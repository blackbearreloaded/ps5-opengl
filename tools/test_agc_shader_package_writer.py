import copy
import unittest

import agc_shader_package as agc
import agc_shader_package_writer as writer


def metadata(stage=writer.HW_STAGE_PIXEL):
    if stage == writer.HW_STAGE_PIXEL:
        source = 5
        shader = [(0x008, 0), (0x009, 0), (0x00A, 0x1234), (0x00B, 4)]
        context = [(0x1C5, 4)]
        unresolved = writer.UNRESOLVED_CHECKSUM | writer.UNRESOLVED_LINKAGE
    else:
        source = 1
        shader = [(0x0C8, 0), (0x0C9, 0), (0x08A, 0x5678), (0x08B, 4)]
        context = [(0x2AB, 0), (0x1C3, 4)]
        unresolved = writer.KNOWN_UNRESOLVED
    result = {
        "version": 1,
        "target": writer.TARGET_PS5,
        "source_stage": source,
        "hardware_stage": stage,
        "machine_code_size": 16,
        "unresolved_fields": unresolved,
        "context_registers": [
            {"offset": offset, "value": value} for offset, value in context
        ],
        "shader_registers": [
            {"offset": offset, "value": value} for offset, value in shader
        ],
        "input_semantics": [],
        "output_semantics": [],
    }
    if stage == writer.HW_STAGE_NGG:
        result["linkage"] = {
            "ge_cntl": {"offset": 0x25B, "value": 0x10080},
            "stages_en": {"offset": 0x2D5, "value": 0x02002000},
            "user_vgpr_en": {"offset": 0x262, "value": 0},
        }
    else:
        result["linkage"] = None
    return result


class AgcShaderPackageWriterTests(unittest.TestCase):
    code = bytes(range(16))

    def test_pixel_round_trip_is_deterministic(self):
        first = writer.build_package(self.code, metadata(), allow_unresolved=True)
        second = writer.build_package(self.code, metadata(), allow_unresolved=True)
        self.assertEqual(first, second)
        report = agc.inspect_package(first)
        self.assertEqual("pixel", report["shader"]["stage_name"])
        self.assertEqual(54, len(first[report["sections"][2]["offset"] +
                                       report["shader"]["resource_layout_offset"]:
                                       report["sections"][2]["offset"] +
                                       report["shader"]["header_size"]]))
        self.assertEqual([0x008, 0x009, 0x00A, 0x00B],
                         [item["offset"] for item in report["shader"]["shader_registers"]])

    def test_rejects_unresolved_by_default(self):
        with self.assertRaisesRegex(writer.PackageBuildError, "unresolved fields remain"):
            writer.build_package(self.code, metadata())

    def test_ngg_ring_override(self):
        package = writer.build_package(
            self.code, metadata(writer.HW_STAGE_NGG), allow_unresolved=True,
            esgs_ring_itemsize=4
        )
        report = agc.inspect_package(package)
        ring = next(item for item in report["shader"]["context_registers"]
                    if item["offset"] == 0x2AB)
        self.assertEqual(4, ring["value"])
        self.assertEqual("geometry/fused-vertex", report["shader"]["stage_name"])
        self.assertNotEqual(0, report["shader"]["linkage_state_offset"])

    def test_rejects_ngg_without_linkage(self):
        bad = metadata(writer.HW_STAGE_NGG)
        bad["linkage"] = None
        with self.assertRaisesRegex(writer.PackageBuildError, "lacks typed linkage"):
            writer.build_package(self.code, bad, allow_unresolved=True)

    def test_rejects_nonadjacent_program_address_pair(self):
        bad = metadata()
        bad["shader_registers"][1], bad["shader_registers"][2] = (
            bad["shader_registers"][2], bad["shader_registers"][1]
        )
        with self.assertRaisesRegex(writer.PackageBuildError, "adjacent and ordered"):
            writer.build_package(self.code, bad, allow_unresolved=True)

    def test_rejects_code_size_mismatch(self):
        bad = metadata()
        bad["machine_code_size"] = 20
        with self.assertRaisesRegex(writer.PackageBuildError, "does not match"):
            writer.build_package(self.code, bad, allow_unresolved=True)

    def test_rejects_duplicate_register(self):
        bad = metadata()
        bad["context_registers"].append(copy.deepcopy(bad["context_registers"][0]))
        with self.assertRaisesRegex(writer.PackageBuildError, "duplicate offset"):
            writer.build_package(self.code, bad, allow_unresolved=True)

    def test_semantics_round_trip(self):
        pixel = metadata()
        pixel["input_semantics"] = [0]
        pixel["unresolved_fields"] &= ~writer.UNRESOLVED_LINKAGE
        pixel_package = writer.build_package(
            self.code, pixel, allow_unresolved=True
        )
        pixel_report = agc.inspect_package(pixel_package)["shader"]
        self.assertEqual(
            [{"raw": 0, "semantic": 0, "parameter": 0}],
            pixel_report["input_semantics"],
        )

        ngg = metadata(writer.HW_STAGE_NGG)
        ngg["output_semantics"] = [0x000, 0x101]
        ngg["unresolved_fields"] &= ~writer.UNRESOLVED_LINKAGE
        ngg_package = writer.build_package(
            self.code, ngg, allow_unresolved=True, esgs_ring_itemsize=4
        )
        ngg_report = agc.inspect_package(ngg_package)["shader"]
        self.assertEqual([0, 1],
                         [item["parameter"]
                          for item in ngg_report["output_semantics"]])

    def test_rejects_duplicate_semantic_key(self):
        bad = metadata()
        bad["input_semantics"] = [0x000, 0x100]
        with self.assertRaisesRegex(writer.PackageBuildError,
                                    "duplicate semantic keys"):
            writer.build_package(self.code, bad, allow_unresolved=True)

    def test_rejects_wrong_stage_semantics(self):
        bad = metadata(writer.HW_STAGE_NGG)
        bad["input_semantics"] = [0]
        with self.assertRaisesRegex(writer.PackageBuildError,
                                    "NGG metadata cannot contain input"):
            writer.build_package(self.code, bad, allow_unresolved=True)


if __name__ == "__main__":
    unittest.main()
