#!/usr/bin/env python3
"""Cross-check PS5 swizzle-27 ramps against AMD GFX10 AddressLib."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


KNOWN = {
    1: (
        [0x0001, 0x0002, 0x0004, 0x0140, 0x0200, 0x0800, 0x2400, 0x8000],
        [0x0010, 0x0008, 0x0020, 0x0100, 0x0280, 0x0400, 0x1800, 0x4000],
    ),
    2: (
        [value << 1 for value in
         [0x0001, 0x0002, 0x0004, 0x00C0, 0x0100, 0x0400, 0x1200, 0x4000]],
        [value << 1 for value in
         [0x0008, 0x0010, 0x0020, 0x0080, 0x0900, 0x0200, 0x2400]],
    ),
    4: (
        [0x0004, 0x0008, 0x0080, 0x0100, 0x2200, 0x0800, 0x8400],
        [0x0010, 0x0020, 0x0040, 0x1100, 0x0200, 0x0400, 0x4800],
    ),
}
RGBA32_EQUATION = (
    [0x0010, 0x0040, 0x2000, 0x0100, 0x8200, 0x0800, 0x0400],
    [0x0020, 0x0080, 0x1000, 0x4100, 0x0200, 0x0400, 0x0800],
)
RGBA16_EQUATION = (
    [0x0008, 0x0020, 0x0040, 0x2100, 0x0200, 0x0800, 0x8400],
    [0x0010, 0x0080, 0x1000, 0x0100, 0x4200, 0x0400, 0x0800],
)


def extract_array(text: str, declaration: str) -> str:
    match = re.search(
        rf"const\s+{re.escape(declaration)}\s*=\s*\{{(.*?)\n\}};",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"array not found: {declaration}")
    return match.group(1)


def parse_nibbles(text: str, name: str, width: int) -> list[list[set[str]]]:
    body = extract_array(text, rf"UINT_64 {name}[][{{width}}]".format(width=width))
    rows: list[list[set[str]]] = []
    for values, index in re.findall(r"\{([^{}]+)\}\s*,\s*//\s*(\d+)", body):
        cells = [cell.strip() for cell in values.split(",") if cell.strip()]
        if len(cells) != width or int(index) != len(rows):
            raise ValueError(f"malformed {name} row {index}")
        rows.append([set() if cell == "0" else set(cell.split("^"))
                     for cell in cells])
    return rows


def parse_patinfo(text: str, name: str) -> list[tuple[list[int], str]]:
    body = extract_array(text, f"ADDR_SW_PATINFO {name}[]")
    rows = []
    for values, comment in re.findall(r"\{([^{}]+)\}\s*,?\s*//\s*([^\n]+)", body):
        fields = [int(value.strip()) for value in values.split(",")
                  if value.strip()]
        if len(fields) != 5:
            raise ValueError(f"malformed {name}: {values}")
        rows.append((fields, comment.strip()))
    return rows


def parse_driver_masks(text: str, name: str) -> list[int]:
    match = re.search(
        rf"static const uint16_t {re.escape(name)}\[\d+\] = \{{(.*?)\}};",
        text,
        re.DOTALL,
    )
    if not match:
        raise ValueError(f"driver mask array not found: {name}")
    return [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+",
                                                   match.group(1))]


def masks(pattern: list[set[str]]) -> tuple[list[int], list[int]]:
    result = {"X": [], "Y": []}
    for address_bit, terms in enumerate(pattern[:16]):
        for term in terms:
            match = re.fullmatch(r"([XY])(\d+)", term)
            if not match:
                continue
            channel, coordinate_bit = match.group(1), int(match.group(2))
            while len(result[channel]) <= coordinate_bit:
                result[channel].append(0)
            result[channel][coordinate_bit] |= 1 << address_bit
    return result["X"], result["Y"]


def multisample_masks(pattern: list[set[str]]) -> tuple[list[int], list[int], list[int]]:
    result = {"X": [], "Y": [], "S": []}
    for address_bit, terms in enumerate(pattern[:16]):
        for term in terms:
            match = re.fullmatch(r"([XYS])(\d+)", term)
            if not match:
                continue
            channel, coordinate_bit = match.group(1), int(match.group(2))
            while len(result[channel]) <= coordinate_bit:
                result[channel].append(0)
            result[channel][coordinate_bit] |= 1 << address_bit
    return result["X"], result["Y"], result["S"]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("header", type=Path)
    args = parser.parse_args()
    text = args.header.read_text(encoding="utf-8")
    nibbles = [
        parse_nibbles(text, "GFX10_SW_PATTERN_NIBBLE01", 8),
        parse_nibbles(text, "GFX10_SW_PATTERN_NIBBLE2", 4),
        parse_nibbles(text, "GFX10_SW_PATTERN_NIBBLE3", 4),
        parse_nibbles(text, "GFX10_SW_PATTERN_NIBBLE4", 4),
    ]

    matches = []
    for suffix in ("", "_RBPLUS"):
        name = f"GFX10_SW_64K_R_X_1xaa{suffix}_PATINFO"
        entries = parse_patinfo(text, name)
        for base in range(0, len(entries) - 4, 5):
            candidate = {}
            comments = []
            for bpe, (fields, comment) in zip((1, 2, 4, 8, 16),
                                               entries[base:base + 5]):
                _, nibble01, nibble2, nibble3, nibble4 = fields
                pattern = (nibbles[0][nibble01] + nibbles[1][nibble2] +
                           nibbles[2][nibble3] + nibbles[3][nibble4])
                candidate[bpe] = masks(pattern)
                comments.append(comment)
            if all(candidate[bpe] == KNOWN[bpe] for bpe in KNOWN):
                matches.append((name, base, comments, candidate[8],
                                candidate[16]))

    if len(matches) != 1:
        raise SystemExit(f"expected one PS5 topology match, found {len(matches)}")
    name, base, comments, rgba16_equation, (x_masks, y_masks) = matches[0]
    if rgba16_equation != RGBA16_EQUATION:
        raise SystemExit("AddressLib 64-bpp equation changed")
    equation = (x_masks, y_masks)
    if equation != RGBA32_EQUATION:
        raise SystemExit("AddressLib 128-bpp equation changed")

    z_entries = parse_patinfo(text, "GFX10_SW_64K_Z_X_1xaa_PATINFO")
    z_equations = {}
    for bpe, index in ((1, base), (4, base + 2)):
        fields, _ = z_entries[index]
        _, nibble01, nibble2, nibble3, nibble4 = fields
        pattern = (nibbles[0][nibble01] + nibbles[1][nibble2] +
                   nibbles[2][nibble3] + nibbles[3][nibble4])
        z_equations[bpe] = masks(pattern)
    for bpe, width, height in ((1, 256, 256), (4, 128, 128)):
        x_masks, y_masks = z_equations[bpe]
        offsets = {
            sum(mask for bit, mask in enumerate(x_masks)
                if x & (1 << bit)) ^
            sum(mask for bit, mask in enumerate(y_masks)
                if y & (1 << bit))
            for y in range(height)
            for x in range(width)
        }
        if offsets != set(range(0, 65536, bpe)):
            raise SystemExit(f"Z_X {bpe}-byte equation does not cover one tile")

    z_msaa_entries = parse_patinfo(text, "GFX10_SW_64K_Z_X_4xaa_PATINFO")
    z_msaa_equations = {}
    for bpe, index in ((1, base), (4, base + 2)):
        fields, _ = z_msaa_entries[index]
        _, nibble01, nibble2, nibble3, nibble4 = fields
        pattern = (nibbles[0][nibble01] + nibbles[1][nibble2] +
                   nibbles[2][nibble3] + nibbles[3][nibble4])
        z_msaa_equations[bpe] = multisample_masks(pattern)
    for bpe, width, height in ((1, 128, 128), (4, 64, 64)):
        x_masks, y_masks, sample_masks = z_msaa_equations[bpe]
        offsets = {
            sum(mask for bit, mask in enumerate(x_masks)
                if x & (1 << bit)) ^
            sum(mask for bit, mask in enumerate(y_masks)
                if y & (1 << bit)) ^
            sum(mask for bit, mask in enumerate(sample_masks)
                if sample & (1 << bit))
            for sample in range(4)
            for y in range(height)
            for x in range(width)
        }
        if offsets != set(range(0, 65536, bpe)):
            raise SystemExit(
                f"Z_X 4x {bpe}-byte equation does not cover one tile"
            )

    msaa_entries = parse_patinfo(text, "GFX10_SW_64K_R_X_4xaa_PATINFO")
    msaa_equations = {}
    for bpe, index in zip((1, 2, 4, 8, 16), range(base, base + 5)):
        _, nibble01, nibble2, nibble3, nibble4 = msaa_entries[index][0]
        pattern = (nibbles[0][nibble01] + nibbles[1][nibble2] +
                   nibbles[2][nibble3] + nibbles[3][nibble4])
        msaa_equations[bpe] = multisample_masks(pattern)
    for bpe, width, height in ((1, 128, 128), (2, 128, 64),
                               (4, 64, 64), (8, 64, 32),
                               (16, 32, 32)):
        msaa_x, msaa_y, msaa_s = msaa_equations[bpe]
        msaa_offsets = {
            sum(mask for bit, mask in enumerate(msaa_x)
                if x & (1 << bit)) ^
            sum(mask for bit, mask in enumerate(msaa_y)
                if y & (1 << bit)) ^
            sum(mask for bit, mask in enumerate(msaa_s)
                if sample & (1 << bit))
            for sample in range(4)
            for y in range(height)
            for x in range(width)
        }
        if msaa_offsets != set(range(0, 65536, bpe)):
            raise SystemExit(
                f"4x {bpe}-byte equation does not cover one 64 KiB tile"
            )
    driver_text = (
        Path(__file__).resolve().parents[2]
        / "src/gallium/ps5/ps5_screen.c"
    ).read_text(encoding="utf-8")
    for suffix, expected in (("x", KNOWN[4][0]), ("y", KNOWN[4][1])):
        actual = parse_driver_masks(driver_text, f"rgba8_{suffix}_masks")
        if actual != expected:
            raise SystemExit(
                f"driver rgba8_{suffix}_masks differs from AddressLib"
            )
    for bpe, (x_masks, y_masks, sample_masks) in msaa_equations.items():
        for suffix, expected in (("x", x_masks), ("y", y_masks),
                                 ("sample", sample_masks)):
            actual = parse_driver_masks(driver_text,
                                        f"bpe{bpe}_{suffix}_masks")
            if actual != expected:
                raise SystemExit(
                    f"driver bpe{bpe}_{suffix}_masks differs from AddressLib"
                )
    for block_y in range(2):
        for block_x in range(2):
            offsets = {
                sum(mask for bit, mask in enumerate(rgba16_equation[0])
                    if x & (1 << bit)) ^
                sum(mask for bit, mask in enumerate(rgba16_equation[1])
                    if y & (1 << bit))
                for y in range(block_y * 64, (block_y + 1) * 64)
                for x in range(block_x * 128, (block_x + 1) * 128)
            }
            if offsets != set(range(0, 65536, 8)):
                raise SystemExit("64-bpp equation does not cover each tile")
    for block_y in range(2):
        for block_x in range(2):
            offsets = {
                sum(mask for bit, mask in enumerate(equation[0])
                    if x & (1 << bit)) ^
                sum(mask for bit, mask in enumerate(equation[1])
                    if y & (1 << bit))
                for y in range(block_y * 64, (block_y + 1) * 64)
                for x in range(block_x * 64, (block_x + 1) * 64)
            }
            if offsets != set(range(0, 65536, 16)):
                raise SystemExit("128-bpp equation does not cover each tile")
    print(f"match={name}[{base}:{base + 5}]")
    print(f"topology={comments[0].split(' @ ')[0]}")
    print("rgba16_x=" + ",".join(f"0x{value:04x}"
                                  for value in rgba16_equation[0]))
    print("rgba16_y=" + ",".join(f"0x{value:04x}"
                                  for value in rgba16_equation[1]))
    print("rgba16_tiles=2x2/128x64-each/complete")
    print("rgba32_x=" + ",".join(f"0x{value:04x}" for value in equation[0]))
    print("rgba32_y=" + ",".join(f"0x{value:04x}" for value in equation[1]))
    print("rgba32_tiles=2x2/64x64-each/complete")
    for bpe, width, height in ((1, 128, 128), (2, 128, 64),
                               (4, 64, 64), (8, 64, 32),
                               (16, 32, 32)):
        msaa_x, msaa_y, msaa_s = msaa_equations[bpe]
        print(f"color{bpe}_4x_x=" + ",".join(
            f"0x{value:04x}" for value in msaa_x))
        print(f"color{bpe}_4x_y=" + ",".join(
            f"0x{value:04x}" for value in msaa_y))
        print(f"color{bpe}_4x_s=" + ",".join(
            f"0x{value:04x}" for value in msaa_s))
        print(f"color{bpe}_4x_tile={width}x{height}x4/complete")
    print("driver_color_4x=1,2,4,8,16-byte/exact")
    for name, bpe in (("s8", 1), ("d32", 4)):
        print(f"{name}_x=" + ",".join(
            f"0x{value:04x}" for value in z_equations[bpe][0]))
        print(f"{name}_y=" + ",".join(
            f"0x{value:04x}" for value in z_equations[bpe][1]))
    print("depth_stencil_tiles=d32:128x128,s8:256x256/complete")
    for name, bpe in (("s8_4x", 1), ("d32_4x", 4)):
        print(f"{name}_x=" + ",".join(
            f"0x{value:04x}" for value in z_msaa_equations[bpe][0]))
        print(f"{name}_y=" + ",".join(
            f"0x{value:04x}" for value in z_msaa_equations[bpe][1]))
        print(f"{name}_s=" + ",".join(
            f"0x{value:04x}" for value in z_msaa_equations[bpe][2]))
    print("depth_stencil_4x_tiles=d32:64x64x4,s8:128x128x4/complete")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
