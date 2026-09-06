#!/usr/bin/env python3
"""Recover affine native R8/RG8 tile-address equations from GPU dumps."""

import argparse
import collections
import json
from pathlib import Path

TILE_BYTES = 0x10000


def require(condition, message):
    if not condition:
        raise ValueError(message)


def expected_r8_histogram():
    return {value: 256 for value in range(256)}


def expected_rg8_histogram():
    return {value: 384 if value < 128 else 128 for value in range(256)}


def derive_affine(mapping, x_bits, y_bits):
    constant = mapping[(0, 0)]
    x_masks = [mapping[(1 << bit, 0)] ^ constant for bit in range(x_bits)]
    y_masks = [mapping[(0, 1 << bit)] ^ constant for bit in range(y_bits)]
    for (x, y), actual in mapping.items():
        predicted = constant
        for bit, mask in enumerate(x_masks):
            if x & (1 << bit):
                predicted ^= mask
        for bit, mask in enumerate(y_masks):
            if y & (1 << bit):
                predicted ^= mask
        require(predicted == actual,
                f"non-affine address at ({x},{y}): {actual:#x} != {predicted:#x}")
    return {
        "constant": constant,
        "x_masks": [f"0x{mask:04x}" for mask in x_masks],
        "y_masks": [f"0x{mask:04x}" for mask in y_masks],
    }


def analyze_r8(x_data, y_data):
    require(len(x_data) == TILE_BYTES and len(y_data) == TILE_BYTES,
            "R8 dumps must each be exactly 64 KiB")
    require(dict(collections.Counter(x_data)) == expected_r8_histogram(),
            "R8-x histogram mismatch")
    require(dict(collections.Counter(y_data)) == expected_r8_histogram(),
            "R8-y histogram mismatch")
    mapping = {}
    for offset, coordinate in enumerate(zip(x_data, y_data)):
        require(coordinate not in mapping,
                f"duplicate R8 coordinate {coordinate}")
        mapping[coordinate] = offset
    require(len(mapping) == 256 * 256, "R8 coordinate coverage mismatch")
    return derive_affine(mapping, 8, 8)


def analyze_rg8(data):
    require(len(data) == TILE_BYTES, "RG8 dump must be exactly 64 KiB")
    require(dict(collections.Counter(data)) == expected_rg8_histogram(),
            "RG8 histogram mismatch")
    for red_byte, green_byte in ((0, 1), (1, 0)):
        mapping = {}
        valid = True
        for byte_offset in range(0, TILE_BYTES, 2):
            coordinate = (data[byte_offset + red_byte],
                          data[byte_offset + green_byte])
            if coordinate[1] >= 128 or coordinate in mapping:
                valid = False
                break
            mapping[coordinate] = byte_offset // 2
        if valid and len(mapping) == 256 * 128:
            result = derive_affine(mapping, 8, 7)
            result["component_order"] = "RG" if red_byte == 0 else "GR"
            return result
    raise ValueError("RG8 byte pairs do not form a complete coordinate map")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("r8_x", type=Path)
    parser.add_argument("r8_y", type=Path)
    parser.add_argument("rg8_xy", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    report = {
        "r8": analyze_r8(args.r8_x.read_bytes(), args.r8_y.read_bytes()),
        "rg8": analyze_rg8(args.rg8_xy.read_bytes()),
    }
    rendered = json.dumps(report, indent=2)
    if args.json:
        args.json.write_text(rendered + "\n")
    print(rendered)


if __name__ == "__main__":
    main()
