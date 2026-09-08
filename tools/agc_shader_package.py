#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Bounds-checked, read-only inspector for PS5 AGC shader ELF packages."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path


ELF_HEADER_SIZE = 64
SECTION_HEADER_SIZE = 64
SHT_NOBITS = 8
SHT_PROGBITS = 1
SHT_STRTAB = 3
AGC_MAGIC = 0x34333231
AGC_HEADER_MIN_SIZE = 96
EM_AMDGPU = 224

STAGE_NAMES = {
    0: "compute",
    1: "pixel",
    2: "geometry/fused-vertex",
    3: "hull",
    4: "geometry-front",
    5: "hull-front",
    6: "geometry-back",
    7: "hull-back",
    8: "function",
}

# GFX10.3 names corroborated against Mesa's pinned gfx103 register database.
CONTEXT_REGISTER_NAMES = {
    0x08F: "CB_SHADER_MASK",
    0x1B1: "SPI_VS_OUT_CONFIG",
    0x1B3: "SPI_PS_INPUT_ENA",
    0x1B4: "SPI_PS_INPUT_ADDR",
    0x1B6: "SPI_PS_IN_CONTROL",
    0x1B8: "SPI_BARYC_CNTL",
    0x1C2: "SPI_SHADER_IDX_FORMAT",
    0x1C3: "SPI_SHADER_POS_FORMAT",
    0x1C4: "SPI_SHADER_Z_FORMAT",
    0x1C5: "SPI_SHADER_COL_FORMAT",
    0x1FF: "GE_MAX_OUTPUT_PER_SUBGROUP",
    0x203: "DB_SHADER_CONTROL",
    0x207: "PA_CL_VS_OUT_CNTL",
    0x291: "VGT_GS_ONCHIP_CNTL",
    0x2AB: "VGT_ESGS_RING_ITEMSIZE",
    0x2CE: "VGT_GS_MAX_VERT_OUT",
    0x2D3: "GE_NGG_SUBGRP_CNTL",
    0x2E4: "VGT_GS_INSTANCE_CNT",
    0x310: "PA_SC_SHADER_CONTROL",
}

SHADER_REGISTER_NAMES = {
    1: {
        0x006: "SPI_SHADER_PGM_CHKSUM_PS",
        0x008: "SPI_SHADER_PGM_LO_PS",
        0x009: "SPI_SHADER_PGM_HI_PS",
        0x00A: "SPI_SHADER_PGM_RSRC1_PS",
        0x00B: "SPI_SHADER_PGM_RSRC2_PS",
    },
    2: {
        0x080: "SPI_SHADER_PGM_CHKSUM_GS",
        0x08A: "SPI_SHADER_PGM_RSRC1_GS",
        0x08B: "SPI_SHADER_PGM_RSRC2_GS",
        0x0C8: "SPI_SHADER_PGM_LO_ES",
        0x0C9: "SPI_SHADER_PGM_HI_ES",
    },
}

LINKAGE_REGISTER_NAMES = {
    0x25B: "GE_CNTL",
    0x262: "GE_USER_VGPR_EN",
    0x29B: "VGT_GS_OUT_PRIM_TYPE",
    0x2D5: "VGT_SHADER_STAGES_EN",
}


class PackageError(ValueError):
    pass


def _span(data: bytes, offset: int, size: int, label: str) -> memoryview:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise PackageError(f"{label} is outside the file")
    return memoryview(data)[offset : offset + size]


def _unpack(fmt: str, data: bytes | memoryview, offset: int, label: str):
    size = struct.calcsize(fmt)
    if offset < 0 or offset > len(data) or size > len(data) - offset:
        raise PackageError(f"{label} is truncated")
    return struct.unpack_from(fmt, data, offset)


def _section_record(data: bytes, shoff: int, shentsize: int, index: int) -> dict:
    at = shoff + index * shentsize
    values = _unpack("<IIQQQQIIQQ", data, at, f"section header {index}")
    keys = ("name_offset", "type", "flags", "address", "offset", "size",
            "link", "info", "alignment", "entry_size")
    return {"index": index, **dict(zip(keys, values))}


def _section_name(names: memoryview, offset: int, index: int) -> str:
    if offset >= len(names):
        raise PackageError(f"section {index} name offset is outside .shstrtab")
    end = bytes(names).find(b"\0", offset)
    if end < 0:
        raise PackageError(f"section {index} name is not NUL-terminated")
    try:
        return bytes(names[offset:end]).decode("ascii")
    except UnicodeDecodeError as exc:
        raise PackageError(f"section {index} name is not ASCII") from exc


def parse_elf(data: bytes) -> tuple[dict, list[dict]]:
    if len(data) < ELF_HEADER_SIZE or data[:4] != b"\x7fELF":
        raise PackageError("not an ELF file")
    if data[4] != 2 or data[5] != 1 or data[6] != 1:
        raise PackageError("only ELF64 little-endian version 1 is supported")

    e_type, machine, version = _unpack("<HHI", data, 16, "ELF identity")
    phoff, shoff = _unpack("<QQ", data, 32, "ELF table offsets")
    ehsize, phentsize, phnum, shentsize, shnum, shstrndx = _unpack(
        "<HHHHHH", data, 52, "ELF table sizes")
    if ehsize < ELF_HEADER_SIZE:
        raise PackageError("ELF header size is smaller than ELF64")
    if not shoff or not shnum:
        raise PackageError("ELF has no section table")
    if shentsize < SECTION_HEADER_SIZE:
        raise PackageError("ELF section records are smaller than ELF64")
    if shstrndx == 0xFFFF:
        raise PackageError("extended section-name indexes are unsupported")
    if shstrndx >= shnum:
        raise PackageError("section-name table index is out of range")
    _span(data, shoff, shentsize * shnum, "section table")

    raw = [_section_record(data, shoff, shentsize, i) for i in range(shnum)]
    names_record = raw[shstrndx]
    if names_record["type"] != SHT_STRTAB:
        raise PackageError("section-name table is not SHT_STRTAB")
    names = _span(data, names_record["offset"], names_record["size"], ".shstrtab")

    sections = []
    for section in raw:
        section["name"] = _section_name(names, section["name_offset"], section["index"])
        alignment = section["alignment"]
        if alignment and alignment & (alignment - 1):
            raise PackageError(f"section {section['index']} has non-power-of-two alignment")
        if section["type"] != SHT_NOBITS:
            _span(data, section["offset"], section["size"],
                  f"section {section['index']} ({section['name'] or '<null>'})")
            if alignment > 1 and section["offset"] % alignment:
                raise PackageError(f"section {section['index']} is not file-aligned")
        sections.append(section)

    header = {
        "type": e_type,
        "machine": machine,
        "version": version,
        "program_header_offset": phoff,
        "program_header_size": phentsize,
        "program_header_count": phnum,
        "section_header_offset": shoff,
        "section_header_size": shentsize,
        "section_header_count": shnum,
        "section_name_index": shstrndx,
    }
    return header, sections


def _required_section(data: bytes, sections: list[dict], name: str) -> tuple[dict, memoryview]:
    matches = [section for section in sections if section["name"] == name]
    if len(matches) != 1:
        raise PackageError(f"expected exactly one {name} section, found {len(matches)}")
    section = matches[0]
    if section["type"] != SHT_PROGBITS:
        raise PackageError(f"{name} is not SHT_PROGBITS")
    if not section["size"]:
        raise PackageError(f"{name} is empty")
    return section, _span(data, section["offset"], section["size"], name)


def _relative_offset(header: memoryview, field_offset: int, relative: int,
                     label: str, required: bool = False,
                     allow_end: bool = False) -> int:
    if not relative:
        if required:
            raise PackageError(f"{label} relative offset is zero")
        return 0
    resolved = field_offset + relative
    if resolved < 0 or resolved > len(header) or (resolved == len(header) and not allow_end):
        raise PackageError(f"{label} relative offset resolves outside .shader_header")
    return resolved


def _registers(header: memoryview, field_offset: int, relative: int,
               count: int, label: str) -> tuple[int, list[dict]]:
    if not count:
        return _relative_offset(header, field_offset, relative, label), []
    offset = _relative_offset(header, field_offset, relative, label, required=True)
    table = _span(header, offset, count * 8, f"{label} array")
    registers = [
        {"offset": reg_offset, "padding": padding, "value": value}
        for reg_offset, padding, value in
        (struct.unpack_from("<HHI", table, i * 8) for i in range(count))
    ]
    return offset, registers


def _semantics(header: memoryview, field_offset: int, relative: int,
               count: int, label: str) -> tuple[int, list[dict]]:
    if count > 32:
        raise PackageError(f"{label} count exceeds the hardware limit of 32")
    if not count:
        return _relative_offset(header, field_offset, relative, label), []
    offset = _relative_offset(header, field_offset, relative, label, required=True)
    table = _span(header, offset, count * 4, f"{label} array")
    values = [struct.unpack_from("<I", table, index * 4)[0]
              for index in range(count)]
    keys = [value & 0xFF for value in values]
    if len(set(keys)) != len(keys):
        raise PackageError(f"{label} contains duplicate semantic keys")
    return offset, [
        {
            "raw": value,
            "semantic": value & 0xFF,
            "parameter": (value >> 8) & 0x1F,
        }
        for value in values
    ]


def _name_registers(registers: list[dict], names: dict[int, str]) -> list[dict]:
    return [{**register, "name": names.get(register["offset"])} for register in registers]


def _resource_layout(header: memoryview, offset: int) -> dict | None:
    if not offset:
        return None
    _span(header, offset, 54, "resource layout prefix")
    relatives = list(_unpack("<QQQQQ", header, offset, "resource layout pointers"))
    resolved = [
        _relative_offset(header, offset + i * 8, relative,
                         f"resource layout pointer {i}", allow_end=True)
        for i, relative in enumerate(relatives)
    ]
    counts = list(_unpack("<HHHH", header, offset + 46, "resource class counts"))
    classes = []
    for kind, count in enumerate(counts):
        entries_offset = resolved[kind + 1]
        if count and not entries_offset:
            raise PackageError(
                f"resource class {kind} count is nonzero but its entry pointer is zero")
        entries = []
        if count:
            table = _span(header, entries_offset, count * 2,
                          f"resource class {kind} entries")
            for i in range(count):
                value = struct.unpack_from("<H", table, i * 2)[0]
                entries.append({
                    "raw": value,
                    "user_data_dword": value & 0x7FFF,
                    "small_descriptor": bool(value & 0x8000),
                })
        classes.append({
            "kind": kind,
            "count": count,
            "relative": relatives[kind + 1],
            "offset": entries_offset,
            "entries": entries,
        })
    return {
        "offset": offset,
        "primary_relative": relatives[0],
        "primary_offset": resolved[0],
        "classes": classes,
    }


def _linkage_state(header: memoryview, offset: int) -> dict | None:
    if not offset:
        return None
    block = _span(header, offset, 48, "linkage state")
    entries = []
    for at in (0, 8, 32, 40):
        reg_offset, padding, value = struct.unpack_from("<HHI", block, at)
        entries.append({
            "block_offset": at,
            "offset": reg_offset,
            "padding": padding,
            "value": value,
            "name": LINKAGE_REGISTER_NAMES.get(reg_offset),
        })
    return {
        "offset": offset,
        "registers": entries,
        "unknown_qwords": list(struct.unpack_from("<QQ", block, 16)),
    }


def inspect_package(data: bytes) -> dict:
    elf, sections = parse_elf(data)
    if elf["machine"] != EM_AMDGPU:
        raise PackageError(f"ELF machine is {elf['machine']}, expected AMDGPU ({EM_AMDGPU})")

    header_section, header = _required_section(data, sections, ".shader_header")
    text_section, text = _required_section(data, sections, ".shader_text")
    if len(header) < AGC_HEADER_MIN_SIZE:
        raise PackageError(".shader_header is shorter than 96 bytes")

    magic, format_version = _unpack("<II", header, 0, "AGC header identity")
    resource_relative = _unpack("<Q", header, 8, "AGC resource-layout offset")[0]
    code_address = _unpack("<Q", header, 16, "AGC code address")[0]
    context_relative, shader_relative, linkage_relative, input_relative, output_relative = (
        _unpack("<QQQQQ", header, 24, "AGC field-relative offsets"))
    declared_header_size, declared_code_size = _unpack("<II", header, 64, "AGC sizes")
    input_count = _unpack("<I", header, 80, "AGC input semantic count")[0]
    output_count = _unpack("<H", header, 86, "AGC output semantic count")[0]
    stage, context_count, shader_count = _unpack("<BBB", header, 90, "AGC stage/counts")
    if magic != AGC_MAGIC:
        raise PackageError(f"AGC header magic is 0x{magic:08x}, expected 0x{AGC_MAGIC:08x}")
    if stage not in STAGE_NAMES:
        raise PackageError(f"AGC program type {stage} is outside the known 0..8 range")
    if declared_header_size != len(header):
        raise PackageError(
            f"AGC header declares {declared_header_size} bytes, section has {len(header)}")
    if declared_code_size != len(text):
        raise PackageError(
            f"AGC header declares {declared_code_size} code bytes, section has {len(text)}")
    if code_address:
        raise PackageError("serialized AGC code-address field at +0x10 must be zero")

    context_offset, context_registers = _registers(
        header, 24, context_relative, context_count, "context register")
    shader_offset, shader_registers = _registers(
        header, 32, shader_relative, shader_count, "shader register")
    context_registers = _name_registers(context_registers, CONTEXT_REGISTER_NAMES)
    shader_registers = _name_registers(shader_registers, SHADER_REGISTER_NAMES.get(stage, {}))
    # sceAgcCreateShader resolves this pointer and then dereferences the layout
    # unconditionally.  A zero value is therefore not a valid creation input.
    resource_offset = _relative_offset(
        header, 8, resource_relative, "resource layout", required=True)
    resources = _resource_layout(header, resource_offset)
    linkage_offset = _relative_offset(header, 40, linkage_relative, "linkage state")
    linkage = _linkage_state(header, linkage_offset)
    input_offset, input_semantics = _semantics(
        header, 48, input_relative, input_count, "input semantics")
    output_offset, output_semantics = _semantics(
        header, 56, output_relative, output_count, "output semantics")

    warnings = []
    if format_version != 24:
        warnings.append(f"unobserved AGC header version {format_version} (known samples use 24)")
    if text_section["alignment"] < 256:
        warnings.append(".shader_text alignment is below the observed runtime requirement of 256")
    if header_section["alignment"] < 8:
        warnings.append(".shader_header alignment is below the observed runtime requirement of 8")
    return {
        "elf": elf,
        "sections": sections,
        "shader": {
            "magic": magic,
            "version": format_version,
            "stage": stage,
            "stage_name": STAGE_NAMES[stage],
            "header_size": len(header),
            "code_size": len(text),
            "code_address": code_address,
            "resource_layout_relative": resource_relative,
            "resource_layout_offset": resource_offset,
            "resources": resources,
            "context_register_relative": context_relative,
            "context_register_offset": context_offset,
            "shader_register_relative": shader_relative,
            "shader_register_offset": shader_offset,
            "linkage_state_relative": linkage_relative,
            "linkage_state_offset": linkage_offset,
            "linkage_state": linkage,
            "input_semantics_relative": input_relative,
            "input_semantics_offset": input_offset,
            "input_semantics": input_semantics,
            "output_semantics_relative": output_relative,
            "output_semantics_offset": output_offset,
            "output_semantics": output_semantics,
            "context_registers": context_registers,
            "shader_registers": shader_registers,
        },
        "warnings": warnings,
    }


def _print_report(path: Path, data: bytes, report: dict, show_registers: bool) -> None:
    elf = report["elf"]
    shader = report["shader"]
    print(f"{path}")
    print(f"  sha256: {hashlib.sha256(data).hexdigest()}")
    print(f"  ELF64-LE: type={elf['type']} machine={elf['machine']} "
          f"sections={elf['section_header_count']} phdrs={elf['program_header_count']}")
    print("  sections:")
    for section in report["sections"]:
        print(f"    [{section['index']:2}] {section['name'] or '<null>':16} "
              f"type={section['type']:2} flags=0x{section['flags']:x} "
              f"off=0x{section['offset']:x} size=0x{section['size']:x} "
              f"align={section['alignment']}")
    print(f"  AGC: magic=0x{shader['magic']:08x} version={shader['version']} "
          f"stage={shader['stage']} ({shader['stage_name']})")
    print(f"    header={shader['header_size']} code={shader['code_size']} "
          f"resource_layout=+0x{shader['resource_layout_offset']:x} "
          f"(field-relative 0x{shader['resource_layout_relative']:x})")
    print(f"    context_registers={len(shader['context_registers'])} "
          f"at +0x{shader['context_register_offset']:x} "
          f"(field-relative 0x{shader['context_register_relative']:x})")
    print(f"    shader_registers={len(shader['shader_registers'])} "
          f"at +0x{shader['shader_register_offset']:x} "
          f"(field-relative 0x{shader['shader_register_relative']:x})")
    print(f"    input_semantics={len(shader['input_semantics'])} "
          f"at +0x{shader['input_semantics_offset']:x}; "
          f"output_semantics={len(shader['output_semantics'])} "
          f"at +0x{shader['output_semantics_offset']:x}")
    if shader["resources"]:
        classes = shader["resources"]["classes"]
        print("    resources=" + ", ".join(
            f"class{item['kind']}:{item['count']}" for item in classes))
    if show_registers:
        for semantic in shader["input_semantics"]:
            print(f"      input semantic={semantic['semantic']} "
                  f"raw=0x{semantic['raw']:08x}")
        for semantic in shader["output_semantics"]:
            print(f"      output semantic={semantic['semantic']} "
                  f"parameter={semantic['parameter']} "
                  f"raw=0x{semantic['raw']:08x}")
        for kind in ("context", "shader"):
            for i, reg in enumerate(shader[f"{kind}_registers"]):
                print(f"      {kind}[{i:02}] offset=0x{reg['offset']:04x} "
                      f"padding=0x{reg['padding']:04x} value=0x{reg['value']:08x}" +
                      (f" {reg['name']}" if reg['name'] else ""))
        if shader["resources"]:
            for item in shader["resources"]["classes"]:
                for i, entry in enumerate(item["entries"]):
                    print(f"      resource[{item['kind']}][{i:02}] "
                          f"raw=0x{entry['raw']:04x} "
                          f"user_data_dword={entry['user_data_dword']} "
                          f"small={entry['small_descriptor']}")
        if shader["linkage_state"]:
            for item in shader["linkage_state"]["registers"]:
                print(f"      linkage[+0x{item['block_offset']:02x}] "
                      f"offset=0x{item['offset']:04x} "
                      f"padding=0x{item['padding']:04x} "
                      f"value=0x{item['value']:08x}" +
                      (f" {item['name']}" if item['name'] else ""))
    for warning in report["warnings"]:
        print(f"  warning: {warning}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packages", nargs="+", type=Path, help="AGC shader ELF package(s)")
    parser.add_argument("--registers", action="store_true", help="print decoded register writes")
    args = parser.parse_args(argv)

    failed = False
    for index, path in enumerate(args.packages):
        if index:
            print()
        try:
            data = path.read_bytes()
            _print_report(path, data, inspect_package(data), args.registers)
        except (OSError, PackageError) as exc:
            failed = True
            print(f"{path}: error: {exc}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
