#!/usr/bin/env python3
"""Generate the OpenGL 3.3 Core command-name table from Khronos gl.xml."""

import argparse
import xml.etree.ElementTree as ET
from pathlib import Path


def version_tuple(text: str) -> tuple[int, int]:
    major, minor = text.split(".", 1)
    return int(major), int(minor)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("registry", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    root = ET.parse(args.registry).getroot()
    commands: set[str] = set()
    for feature in root.findall("feature"):
        if feature.get("api") != "gl" or version_tuple(
            feature.get("number", "99.0")
        ) > (3, 3):
            continue
        for requirement in feature.findall("require"):
            if requirement.get("profile") == "compatibility":
                continue
            commands.update(
                command.get("name")
                for command in requirement.findall("command")
                if command.get("name")
            )
        for removal in feature.findall("remove"):
            if removal.get("profile") != "core":
                continue
            commands.difference_update(
                command.get("name")
                for command in removal.findall("command")
                if command.get("name")
            )

    lines = [
        "/* Generated from the Khronos gl.xml feature ladder through 3.3 Core. */",
        *[f'   "{name}",' for name in sorted(commands)],
        "",
    ]
    args.output.write_text("\n".join(lines), newline="\n")
    print(f"generated {len(commands)} OpenGL 3.3 Core entry points")


if __name__ == "__main__":
    main()
