#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Build a minimal PS5 AGC shader ELF from raw ACO code and typed metadata."""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

import agc_shader_package as agc


HEADER_PREFIX_SIZE = 96
RESOURCE_LAYOUT_SIZE = 54
TARGET_PS5 = 2
HW_STAGE_PIXEL = 2
HW_STAGE_NGG = 3
UNRESOLVED_CHECKSUM = 1 << 0
UNRESOLVED_ESGS_RING = 1 << 1
UNRESOLVED_LINKAGE = 1 << 2
KNOWN_UNRESOLVED = (
    UNRESOLVED_CHECKSUM | UNRESOLVED_ESGS_RING | UNRESOLVED_LINKAGE
)

UNRESOLVED_NAMES = {
    UNRESOLVED_CHECKSUM: "program-checksum",
    UNRESOLVED_ESGS_RING: "NGG ESGS-ring-itemsize",
    UNRESOLVED_LINKAGE: "AGC linkage/semantics",
}

# hardware_stage: (AGC program type, source stage, PGM LO/HI, RSRC1/2)
STAGES = {
    HW_STAGE_PIXEL: (1, 5, (0x008, 0x009), (0x00A, 0x00B)),
    HW_STAGE_NGG: (2, 1, (0x0C8, 0x0C9), (0x08A, 0x08B)),
}


class PackageBuildError(ValueError):
    pass


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def _integer(value: object, label: str, maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise PackageBuildError(f"{label} must be an integer")
    if value < 0 or value > maximum:
        raise PackageBuildError(f"{label} is outside 0..{maximum}")
    return value


def _registers(metadata: dict, name: str) -> list[tuple[int, int]]:
    raw = metadata.get(name)
    if not isinstance(raw, list):
        raise PackageBuildError(f"{name} must be an array")
    result = []
    seen = set()
    for index, item in enumerate(raw):
        if not isinstance(item, dict):
            raise PackageBuildError(f"{name}[{index}] must be an object")
        offset = _integer(item.get("offset"), f"{name}[{index}].offset", 0xFFFF)
        value = _integer(item.get("value"), f"{name}[{index}].value")
        if offset in seen:
            raise PackageBuildError(f"{name} contains duplicate offset 0x{offset:04x}")
        seen.add(offset)
        result.append((offset, value))
    if len(result) > 0xFF:
        raise PackageBuildError(f"{name} has more than 255 entries")
    return result


def _semantics(metadata: dict, name: str) -> list[int]:
    raw = metadata.get(name, [])
    if not isinstance(raw, list):
        raise PackageBuildError(f"{name} must be an array")
    if len(raw) > 32:
        raise PackageBuildError(f"{name} has more than 32 entries")
    result = [_integer(value, f"{name}[{index}]")
              for index, value in enumerate(raw)]
    keys = [value & 0xFF for value in result]
    if len(set(keys)) != len(keys):
        raise PackageBuildError(f"{name} contains duplicate semantic keys")
    return result


def _require_register_pair(registers: list[tuple[int, int]], expected: tuple[int, int],
                           label: str, require_zero: bool = False) -> None:
    offsets = [offset for offset, _ in registers]
    try:
        index = offsets.index(expected[0])
    except ValueError as exc:
        raise PackageBuildError(f"missing {label} register 0x{expected[0]:04x}") from exc
    if index + 1 >= len(registers) or offsets[index + 1] != expected[1]:
        raise PackageBuildError(f"{label} LO/HI registers must be adjacent and ordered")
    if require_zero and (registers[index][1] or registers[index + 1][1]):
        raise PackageBuildError(f"serialized {label} LO/HI values must be zero")


def _linkage(metadata: dict, hardware_stage: int) -> list[tuple[int, int]] | None:
    raw = metadata.get("linkage")
    if raw is None:
        if hardware_stage == HW_STAGE_NGG:
            raise PackageBuildError("NGG metadata lacks typed linkage registers")
        return None
    if hardware_stage != HW_STAGE_NGG or not isinstance(raw, dict):
        raise PackageBuildError("linkage is only valid as an NGG object")

    result = []
    for name, expected_offset in (
        ("ge_cntl", 0x25B),
        ("stages_en", 0x2D5),
        ("user_vgpr_en", 0x262),
    ):
        item = raw.get(name)
        if not isinstance(item, dict):
            raise PackageBuildError(f"linkage.{name} must be an object")
        offset = _integer(item.get("offset"), f"linkage.{name}.offset", 0xFFFF)
        value = _integer(item.get("value"), f"linkage.{name}.value")
        if offset != expected_offset:
            raise PackageBuildError(
                f"linkage.{name} has unexpected register offset 0x{offset:04x}")
        result.append((offset, value))
    return result


def _prepare(code: bytes, metadata: dict, allow_unresolved: bool,
             esgs_ring_itemsize: int | None) -> tuple[int, list[tuple[int, int]],
                                                      list[tuple[int, int]],
                                                      list[tuple[int, int]] | None,
                                                      list[int], list[int],
                                                      int]:
    if not isinstance(metadata, dict):
        raise PackageBuildError("metadata root must be an object")
    if not code or len(code) % 4:
        raise PackageBuildError("machine code must be a nonempty dword array")
    if _integer(metadata.get("version"), "version") != 1:
        raise PackageBuildError("unsupported metadata version")
    if _integer(metadata.get("target"), "target") != TARGET_PS5:
        raise PackageBuildError("metadata does not target PS5")
    if _integer(metadata.get("machine_code_size"), "machine_code_size") != len(code):
        raise PackageBuildError("machine_code_size does not match the raw code file")

    hardware_stage = _integer(metadata.get("hardware_stage"), "hardware_stage")
    if hardware_stage not in STAGES:
        raise PackageBuildError("only pixel and NGG hardware stages are supported")
    agc_stage, expected_source, pgm_pair, rsrc_pair = STAGES[hardware_stage]
    if _integer(metadata.get("source_stage"), "source_stage") != expected_source:
        raise PackageBuildError("source_stage does not match hardware_stage")

    context = _registers(metadata, "context_registers")
    shader = _registers(metadata, "shader_registers")
    linkage = _linkage(metadata, hardware_stage)
    inputs = _semantics(metadata, "input_semantics")
    outputs = _semantics(metadata, "output_semantics")
    if hardware_stage == HW_STAGE_PIXEL and outputs:
        raise PackageBuildError("pixel metadata cannot contain output semantics")
    if hardware_stage == HW_STAGE_NGG and inputs:
        raise PackageBuildError("NGG metadata cannot contain input semantics")
    _require_register_pair(shader, pgm_pair, "program-address", require_zero=True)
    _require_register_pair(shader, rsrc_pair, "program-resource")

    unresolved = _integer(metadata.get("unresolved_fields"), "unresolved_fields")
    if unresolved & ~KNOWN_UNRESOLVED:
        raise PackageBuildError("metadata contains unknown unresolved-field bits")

    if esgs_ring_itemsize is not None:
        if hardware_stage != HW_STAGE_NGG:
            raise PackageBuildError("--esgs-ring-itemsize is only valid for NGG")
        esgs_ring_itemsize = _integer(esgs_ring_itemsize, "esgs_ring_itemsize")
        for index, (offset, _) in enumerate(context):
            if offset == 0x2AB:
                context[index] = (offset, esgs_ring_itemsize)
                break
        else:
            raise PackageBuildError("NGG metadata lacks VGT_ESGS_RING_ITEMSIZE")
        unresolved &= ~UNRESOLVED_ESGS_RING

    if unresolved and not allow_unresolved:
        names = ", ".join(
            name for bit, name in UNRESOLVED_NAMES.items() if unresolved & bit
        )
        raise PackageBuildError(f"unresolved fields remain: {names}")
    return agc_stage, context, shader, linkage, inputs, outputs, unresolved


def _build_header(code_size: int, stage: int, context: list[tuple[int, int]],
                  shader: list[tuple[int, int]],
                  linkage: list[tuple[int, int]] | None,
                  inputs: list[int], outputs: list[int]) -> bytes:
    shader_at = HEADER_PREFIX_SIZE
    context_at = shader_at + len(shader) * 8
    linkage_at = _align(context_at + len(context) * 8, 8)
    cursor = linkage_at + (48 if linkage else 0)
    input_at = cursor if inputs else 0
    cursor += len(inputs) * 4
    output_at = cursor if outputs else 0
    cursor += len(outputs) * 4
    resource_at = _align(cursor, 8)
    header = bytearray(resource_at + RESOURCE_LAYOUT_SIZE)

    struct.pack_into("<II", header, 0, agc.AGC_MAGIC, 24)
    struct.pack_into("<Q", header, 8, resource_at - 8)
    struct.pack_into("<Q", header, 16, 0)
    struct.pack_into("<Q", header, 24, context_at - 24 if context else 0)
    struct.pack_into("<Q", header, 32, shader_at - 32)
    struct.pack_into("<Q", header, 40, linkage_at - 40 if linkage else 0)
    struct.pack_into("<Q", header, 48, input_at - 48 if inputs else 0)
    struct.pack_into("<Q", header, 56, output_at - 56 if outputs else 0)
    struct.pack_into("<II", header, 64, len(header), code_size)
    struct.pack_into("<I", header, 80, len(inputs))
    struct.pack_into("<H", header, 86, len(outputs))
    struct.pack_into("<BBB", header, 90, stage, len(context), len(shader))

    for index, (offset, value) in enumerate(shader):
        struct.pack_into("<HHI", header, shader_at + index * 8, offset, 0, value)
    for index, (offset, value) in enumerate(context):
        struct.pack_into("<HHI", header, context_at + index * 8, offset, 0, value)
    if linkage:
        ge_cntl, stages_en, user_vgpr_en = linkage
        struct.pack_into("<HHI", header, linkage_at, ge_cntl[0], 0, ge_cntl[1])
        struct.pack_into("<HHI", header, linkage_at + 8,
                         stages_en[0], 0, stages_en[1])
        struct.pack_into("<HHI", header, linkage_at + 40,
                         user_vgpr_en[0], 0, user_vgpr_en[1])
    for index, value in enumerate(inputs):
        struct.pack_into("<I", header, input_at + index * 4, value)
    for index, value in enumerate(outputs):
        struct.pack_into("<I", header, output_at + index * 4, value)
    # A zero-initialized 54-byte resource layout is valid for resource-free shaders.
    return bytes(header)


def _build_elf(code: bytes, header: bytes) -> bytes:
    names = b"\0.shader_text\0.shader_header\0.shstrtab\0"
    text_at = 0x100
    header_at = _align(text_at + len(code), 8)
    names_at = header_at + len(header)
    section_table_at = _align(names_at + len(names), 8)
    result = bytearray(section_table_at + 4 * agc.SECTION_HEADER_SIZE)

    result[:4] = b"\x7fELF"
    result[4:7] = b"\x02\x01\x01"
    struct.pack_into("<HHIQQQIHHHHHH", result, 16,
                     2, agc.EM_AMDGPU, 1, 0, 0, section_table_at, 0,
                     agc.ELF_HEADER_SIZE, 56, 0, agc.SECTION_HEADER_SIZE, 4, 3)
    result[text_at:text_at + len(code)] = code
    result[header_at:header_at + len(header)] = header
    result[names_at:names_at + len(names)] = names

    def section(index: int, name: int, kind: int, flags: int, offset: int,
                size: int, alignment: int) -> None:
        struct.pack_into("<IIQQQQIIQQ", result,
                         section_table_at + index * agc.SECTION_HEADER_SIZE,
                         name, kind, flags, 0, offset, size, 0, 0, alignment, 0)

    section(0, 0, 0, 0, 0, 0, 0)
    section(1, 1, agc.SHT_PROGBITS, 0x6, text_at, len(code), 256)
    section(2, 14, agc.SHT_PROGBITS, 0x3, header_at, len(header), 8)
    section(3, 29, agc.SHT_STRTAB, 0, names_at, len(names), 1)
    return bytes(result)


def build_package(code: bytes, metadata: dict, *, allow_unresolved: bool = False,
                  esgs_ring_itemsize: int | None = None) -> bytes:
    stage, context, shader, linkage, inputs, outputs, _ = _prepare(
        code, metadata, allow_unresolved, esgs_ring_itemsize
    )
    package = _build_elf(
        code, _build_header(
            len(code), stage, context, shader, linkage, inputs, outputs
        )
    )
    # Keep the writer and independent bounds-checked reader in lockstep.
    agc.inspect_package(package)
    return package


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("code", type=Path, help="raw ACO machine-code file")
    parser.add_argument("metadata", type=Path, help="typed compiler metadata JSON")
    parser.add_argument("-o", "--output", required=True, type=Path)
    parser.add_argument("--allow-unresolved", action="store_true",
                        help="emit a creation-test package with unresolved execution fields")
    parser.add_argument("--esgs-ring-itemsize", type=int,
                        help="override NGG VGT_ESGS_RING_ITEMSIZE and resolve that field")
    args = parser.parse_args(argv)

    try:
        code = args.code.read_bytes()
        metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
        _, _, _, _, _, _, unresolved = _prepare(
            code, metadata, args.allow_unresolved, args.esgs_ring_itemsize
        )
        package = build_package(
            code, metadata, allow_unresolved=args.allow_unresolved,
            esgs_ring_itemsize=args.esgs_ring_itemsize
        )
        args.output.write_bytes(package)
    except (OSError, json.JSONDecodeError, PackageBuildError, agc.PackageError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if unresolved:
        names = ", ".join(
            name for bit, name in UNRESOLVED_NAMES.items() if unresolved & bit
        )
        print(f"warning: creation-test package retains: {names}", file=sys.stderr)
    print(f"wrote {args.output} ({len(package)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
