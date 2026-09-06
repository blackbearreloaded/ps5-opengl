#!/usr/bin/env python3
"""Verify that PS5 static libraries export every OpenGL 3.3 Core command."""

import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REGISTRY = ROOT / "third_party/mesa-26.2.0/src/mesa/glapi/glapi/registry/gl.xml"
LIBRARIES = (
    ROOT / "build/mesa-ps5-probe/src/mesa/glapi/glapi/libglapi_bridge.a",
    ROOT / "build/mesa-ps5-probe/src/mesa/glapi/shared-glapi/libglapi.a",
)

commands = set()
for feature in ET.parse(REGISTRY).getroot().findall("feature"):
    if (feature.get("api") != "gl" or
            tuple(map(int, feature.get("number").split("."))) > (3, 3)):
        continue
    for requirement in feature.findall("require"):
        if requirement.get("profile") in (None, "core"):
            commands.update(command.get("name")
                            for command in requirement.findall("command"))
    for removal in feature.findall("remove"):
        if removal.get("profile") == "core":
            commands.difference_update(command.get("name")
                                       for command in removal.findall("command"))

output = subprocess.run(
    ["nm", "-g", "--defined-only", *(str(path) for path in LIBRARIES)],
    check=True, text=True, stdout=subprocess.PIPE).stdout
symbols = set(re.findall(r"\b(gl[A-Za-z0-9_]+)$", output, re.MULTILINE))
missing = sorted(commands - symbols)
if missing:
    raise SystemExit("missing OpenGL 3.3 symbols: " + ", ".join(missing))
if len(commands) != 344:
    raise SystemExit(f"unexpected OpenGL 3.3 registry surface: {len(commands)}")
print("gl33-link-surface: PASS commands=344 exported=344")
