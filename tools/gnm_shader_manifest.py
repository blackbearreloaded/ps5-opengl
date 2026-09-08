#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Extract the clean ACO-derived fields carried by an opengnm GNM shader."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path


PSSL_SIZE = 0x24
GNM_FILE_SIZE = 0x10
ORB_SIZE = 0x1C
GNM_MAGIC = 0x72646853
ORB_MAGIC = b"OrbShdr"

STAGES = {1: "vertex", 2: "pixel"}
VS_REGISTERS = (
    "spi_shader_pgm_lo_vs",
    "spi_shader_pgm_hi_vs",
    "spi_shader_pgm_rsrc1_vs",
    "spi_shader_pgm_rsrc2_vs",
    "spi_vs_out_config",
    "spi_shader_pos_format",
    "pa_cl_vs_out_cntl",
)
PS_REGISTERS = (
    "spi_shader_pgm_lo_ps",
    "spi_shader_pgm_hi_ps",
    "spi_shader_pgm_rsrc1_ps",
    "spi_shader_pgm_rsrc2_ps",
    "spi_shader_z_format",
    "spi_shader_col_format",
    "spi_ps_input_ena",
    "spi_ps_input_addr",
    "spi_ps_in_control",
    "spi_baryc_cntl",
    "db_shader_control",
    "cb_shader_mask",
)


class ManifestError(ValueError):
    pass


def _unpack(fmt: str, data: bytes, offset: int, label: str):
    size = struct.calcsize(fmt)
    if offset < 0 or offset > len(data) or size > len(data) - offset:
        raise ManifestError(f"{label} is truncated")
    return struct.unpack_from(fmt, data, offset)


def _slice(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ManifestError(f"{label} is outside the file")
    return data[offset : offset + size]


def parse_shader(data: bytes) -> dict:
    if len(data) < PSSL_SIZE + GNM_FILE_SIZE:
        raise ManifestError("shader binary is too short")

    pssl_major, pssl_minor = data[0], data[1]
    pssl_stage, code_type = data[12], data[13]
    pssl_code_size = _unpack("<I", data, 16, "PSSL code size")[0]
    if pssl_code_size > len(data) - PSSL_SIZE:
        raise ManifestError("PSSL code region is outside the file")

    gnm = PSSL_SIZE
    magic, gnm_major, gnm_minor = _unpack("<IHH", data, gnm, "GNM header")
    if magic != GNM_MAGIC:
        raise ManifestError(f"GNM magic is 0x{magic:08x}, expected 0x{GNM_MAGIC:08x}")
    stage_type, header_dwords, aux_data, target_modes = _unpack(
        "<BBBB", data, gnm + 8, "GNM stage header")
    if stage_type not in STAGES:
        raise ManifestError(f"only vertex/pixel GNM shaders are supported, got type {stage_type}")

    stage_at = PSSL_SIZE + GNM_FILE_SIZE
    header_size = header_dwords * 4
    if header_size < (0x28 if stage_type == 1 else 0x3C):
        raise ManifestError("GNM stage header is shorter than its fixed prefix")
    _slice(data, stage_at, header_size, "GNM stage header")

    common_word, embedded_dqwords, scratch_dwords = _unpack(
        "<IHH", data, stage_at, "GNM common data")
    shader_size = common_word & 0x7FFFFF
    uses_srt = bool(common_word & (1 << 23))
    usage_count = common_word >> 24

    register_names = VS_REGISTERS if stage_type == 1 else PS_REGISTERS
    register_values = _unpack(
        "<" + "I" * len(register_names), data, stage_at + 8, "GNM registers")
    registers = dict(zip(register_names, register_values))

    fixed_size = 0x28 if stage_type == 1 else 0x3C
    cursor = stage_at + fixed_size
    usages = []
    for index in range(usage_count):
        usage, api_slot, start_register, flags = _unpack(
            "<BBBB", data, cursor, f"input usage {index}")
        usages.append({
            "usage_type": usage,
            "api_slot": api_slot,
            "start_register": start_register,
            "flags": flags,
        })
        cursor += 4

    if stage_type == 1:
        input_count, export_count, gs_mode, fetch_control = _unpack(
            "<BBBB", data, stage_at + 0x24, "vertex semantic counts")
        inputs = []
        for index in range(input_count):
            semantic, vgpr, elements, unused = _unpack(
                "<BBBB", data, cursor, f"vertex input semantic {index}")
            inputs.append({"semantic": semantic, "vgpr": vgpr, "elements": elements})
            cursor += 4
        exports = []
        for index in range(export_count):
            semantic, packed = _unpack("<BB", data, cursor, f"vertex export semantic {index}")
            exports.append({
                "semantic": semantic,
                "output_index": packed & 0x1F,
                "export_f16": packed >> 6,
            })
            cursor += 2
        io = {
            "input_semantics": inputs,
            "export_semantics": exports,
            "gs_mode": gs_mode,
            "fetch_control": fetch_control,
        }
    else:
        input_count = data[stage_at + 0x38]
        inputs = []
        for index in range(input_count):
            packed = _unpack("<H", data, cursor, f"pixel input semantic {index}")[0]
            inputs.append({
                "semantic": packed & 0xFF,
                "default_value": (packed >> 8) & 3,
                "flat": bool(packed & (1 << 10)),
                "linear": bool(packed & (1 << 11)),
                "custom": bool(packed & (1 << 12)),
            })
            cursor += 2
        io = {"input_semantics": inputs}

    if cursor > stage_at + header_size:
        raise ManifestError("semantic tables exceed the declared GNM header")
    padding = data[cursor : stage_at + header_size]
    if any(padding):
        raise ManifestError("GNM stage-header padding is nonzero")

    orb_at = data.rfind(ORB_MAGIC)
    if orb_at < stage_at + header_size or orb_at > len(data) - ORB_SIZE:
        raise ManifestError("bounded OrbShdr trailer was not found")
    orb_version = data[orb_at + 7]
    packed_info = _unpack("<I", data, orb_at + 8, "OrbShdr info")[0]
    machine_code_size = packed_info >> 8
    machine_code_at = orb_at - machine_code_size
    if machine_code_at != stage_at + header_size:
        raise ManifestError("machine-code start disagrees with the GNM header size")
    machine_code = _slice(data, machine_code_at, machine_code_size, "machine code")
    if shader_size != machine_code_size + ORB_SIZE:
        raise ManifestError("GNM common shader size disagrees with code plus OrbShdr")

    chunk_offset, orb_usage_count, orb_flags, orb_unused = _unpack(
        "<BBBB", data, orb_at + 12, "OrbShdr usage data")
    hash_low, hash_high, crc32 = _unpack("<III", data, orb_at + 16, "OrbShdr hashes")
    if orb_usage_count != usage_count:
        raise ManifestError("input-usage count disagrees between GNM and OrbShdr")

    return {
        "format": {
            "pssl_version": f"{pssl_major}.{pssl_minor}",
            "pssl_stage": pssl_stage,
            "code_type": code_type,
            "pssl_code_size": pssl_code_size,
            "gnm_version": f"{gnm_major}.{gnm_minor}",
            "gnm_stage_type": stage_type,
            "target_gpu_modes": target_modes,
            "aux_data": aux_data,
            "orb_version": orb_version,
        },
        "stage": STAGES[stage_type],
        "common": {
            "shader_size": shader_size,
            "uses_srt": uses_srt,
            "embedded_constant_buffer_dqwords": embedded_dqwords,
            "scratch_size_per_thread_dwords": scratch_dwords,
        },
        "machine_code": {
            "offset": machine_code_at,
            "size": machine_code_size,
            "sha256": hashlib.sha256(machine_code).hexdigest(),
        },
        "registers": registers,
        "input_usages": usages,
        "io": io,
        "orb": {
            "offset": orb_at,
            "chunk_usage_offset_dwords": chunk_offset,
            "flags": orb_flags,
            "unused": orb_unused,
            "shader_hash": f"{hash_high:08x}{hash_low:08x}",
            "crc32": f"{crc32:08x}",
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("shader", type=Path)
    parser.add_argument("--output", type=Path, help="write JSON to this file")
    args = parser.parse_args(argv)
    try:
        data = args.shader.read_bytes()
        manifest = parse_shader(data)
        manifest = {
            "source_file": args.shader.name,
            "file_size": len(data),
            "file_sha256": hashlib.sha256(data).hexdigest(),
            **manifest,
        }
        rendered = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8", newline="\n")
        else:
            print(rendered, end="")
        return 0
    except (OSError, ManifestError) as exc:
        print(f"{args.shader}: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
