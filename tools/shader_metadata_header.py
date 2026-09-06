#!/usr/bin/env python3
"""Emit the small C binding header needed by native hardware probes."""

import argparse
import json
from pathlib import Path


def required_u32(metadata, key):
    value = metadata.get(key)
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"{key} must be an integer")
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError(f"{key} is outside uint32_t")
    return value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("metadata", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--prefix", default="PSBC")
    args = parser.parse_args()

    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    address32_hi = required_u32(metadata, "address32_hi")
    user_sgpr_count = required_u32(metadata, "user_sgpr_count")

    prefix = args.prefix
    if not prefix.replace("_", "A").isalnum() or not prefix:
        raise ValueError("prefix must be a C identifier")
    lines = [
        "/* Generated from typed opengnm-psbc metadata. */",
        f"#define {prefix}_ADDRESS32_HI {address32_hi}u",
        f"#define {prefix}_USER_SGPR_COUNT {user_sgpr_count}u",
    ]
    vertex_buffer_dword = metadata.get("vertex_buffer_table_user_data_dword")
    if vertex_buffer_dword is not None:
        vertex_buffer_dword = required_u32(
            metadata, "vertex_buffer_table_user_data_dword")
        if vertex_buffer_dword >= user_sgpr_count:
            raise ValueError("vertex-buffer pointer lies outside user SGPR range")
        lines.append(
            f"#define {prefix}_VERTEX_BUFFER_USER_DWORD {vertex_buffer_dword}u")
    descriptor_dword = metadata.get("descriptor_set0_user_data_dword")
    if descriptor_dword is not None:
        descriptor_dword = required_u32(
            metadata, "descriptor_set0_user_data_dword")
        if descriptor_dword >= user_sgpr_count:
            raise ValueError("descriptor-set pointer lies outside user SGPR range")
        lines.append(
            f"#define {prefix}_DESCRIPTOR_SET0_USER_DWORD {descriptor_dword}u")
        for binding in metadata.get("descriptor_bindings", []):
            if binding.get("set") != 0:
                continue
            number = required_u32(binding, "binding")
            offset = required_u32(binding, "offset")
            stride = required_u32(binding, "stride")
            lines.extend([
                f"#define {prefix}_SET0_BINDING{number}_OFFSET {offset}u",
                f"#define {prefix}_SET0_BINDING{number}_STRIDE {stride}u",
            ])
    lines.append("")
    args.output.write_text("\n".join(lines), encoding="ascii")


if __name__ == "__main__":
    main()
