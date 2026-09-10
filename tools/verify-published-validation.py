#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Verify the published result export, not independently rerun GPU tests."""
import argparse
import collections
import csv
import gzip
import hashlib
import importlib
import json
from pathlib import Path
from opengl_receipts import normalize_log_tags
import re

AUDITOR = importlib.import_module("verify-cts-candidate")
ROOT = Path(__file__).resolve().parents[1]


def verify_renderer(name, output):
    frames = [line for line in output if line.startswith(f"[ps5-{name}] frame=")]
    if name == "imgui":
        pattern = (r"\[ps5-imgui\] frame=(\d+) scale=(\d+) split=(\d+) renderbuffer=(\d+) "
                   r"vertices=(\d+) indices=(\d+) font-ink=(\d+) partial=(\d+) PASS")
        matches = [re.fullmatch(pattern, line) for line in frames]
        AUDITOR.require(len(matches) == 6 and all(matches), "incomplete ImGui frame oracle")
        values = [tuple(map(int, match.groups())) for match in matches]
        AUDITOR.require([v[:4] for v in values] ==
                        [(i, 1 if i < 3 else 2, int(i % 3 == 1), int(i % 3 == 2)) for i in range(6)]
                        and all(min(v[4:7]) > 0 for v in values), "ImGui frame/font mismatch")
    else:
        expected = [f"[ps5-nanovg] frame={i} scale={scale} probes=15 dirty-stencil=0 PASS"
                    if name == "nanovg" else
                    f"[ps5-sokol] frame={i} scale={scale} components={307200 * scale * scale} mismatches=0 logs=0 PASS"
                    for i, scale in enumerate((1, 2, 1))]
        AUDITOR.require(frames == expected, f"{name} numeric oracle mismatch")
    shutdowns = [line for line in output if line.startswith("[ps5-agc] present-shutdown ")]
    AUDITOR.require(shutdowns and all(re.fullmatch(
        r"\[ps5-agc\] present-shutdown unregister=[0-9a-fA-F]{8} close=00000000 frames=\d+", line)
        for line in shutdowns) and any(line.endswith(f" frames={len(frames)}") for line in shutdowns),
        f"{name} presenter completion mismatch")


def verify(evidence):
    evidence = evidence.resolve()
    verified = set()
    for line in (evidence / "SHA256SUMS").read_text().splitlines():
        expected, name = line.split("  ", 1)
        path = (evidence / name).resolve()
        AUDITOR.require(path.parent == evidence, "unsafe evidence path")
        AUDITOR.require(name not in verified, "duplicate evidence checksum")
        AUDITOR.require(hashlib.sha256(path.read_bytes()).hexdigest() == expected,
                        f"evidence checksum mismatch: {name}")
        verified.add(name)
    AUDITOR.require(verified == {"candidate.json", "audit.json", "mustpass-gl33.txt",
                                "cases.csv.gz", "receipts.json", "renderers.json"},
                    "incomplete evidence checksum inventory")
    candidate = json.loads((evidence / "candidate.json").read_text())
    audit = json.loads((evidence / "audit.json").read_text())
    official = (evidence / "mustpass-gl33.txt").read_text().splitlines()
    AUDITOR.require(len(official) == len(set(official)) == 9886, "must-pass inventory mismatch")
    AUDITOR.require(hashlib.sha256((evidence / "mustpass-gl33.txt").read_bytes()).hexdigest() ==
                    candidate["mustpass_sha256"], "must-pass provenance mismatch")
    AUDITOR.require(hashlib.sha256((evidence / "candidate.json").read_bytes()).hexdigest() ==
                    audit["manifest_sha256"], "candidate provenance mismatch")
    with gzip.open(evidence / "cases.csv.gz", "rt", encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    receipts = json.loads((evidence / "receipts.json").read_text())
    grouped = collections.defaultdict(list)
    for row in rows:
        grouped[row["receipt"]].append((row["case"], row["status"], row["message"]))
    AUDITOR.require(set(grouped) == set(candidate["receipts"]) == {r["receipt"] for r in receipts}
                    and len(receipts) == len(grouped) == len(candidate["receipts"]) == audit["clean_cycles"],
                    "receipt inventory mismatch")
    runs = []
    for receipt in receipts:
        cases = grouped[receipt["receipt"]]
        AUDITOR.require(receipt["cases"] == len(cases) and receipt["post_health"] is True
                        and receipt["lock_released"] is True and type(receipt["uneventful"]) is bool
                        and receipt["outcome"] == "entered-eboot" and receipt["teardown"] == "runtime-layers-released",
                        "receipt count or lifecycle mismatch")
        AUDITOR.require(receipt["eboot_sha256"] == candidate["eboot_sha256"] and
                        receipt["libc_sha256"] == candidate["libc_sha256"], "mixed binary candidates")
        AUDITOR.require(all(row["configuration"] == receipt["configuration"] for row in rows
                            if row["receipt"] == receipt["receipt"]), "configuration mismatch")
        runs.append((receipt["configuration"], cases))
    actual = AUDITOR.coverage(official, runs, candidate["not_supported"])
    AUDITOR.require(audit["complete"] is True and actual == audit["configurations"]
                    and audit["eboot_sha256"] == candidate["eboot_sha256"]
                    and audit["render_targets"] == candidate["expected_render_targets"],
                    "export differs from strict audit")
    if "eventful_receipts" in candidate:
        AUDITOR.require({r["receipt"] for r in receipts if not r["uneventful"]} ==
                        set(candidate["eventful_receipts"]) and
                        all(candidate["eventful_receipts"].values()), "incident review mismatch")
    for config in actual.values():
        AUDITOR.require(not config["missing"] and config["executed"] == len(official),
                        "unexpected result totals")
    renderers = json.loads((evidence / "renderers.json").read_text())
    AUDITOR.require(set(renderers) == {"imgui", "nanovg", "sokol"}, "renderer inventory mismatch")
    if "renderer_eboot_sha256" in candidate:
        AUDITOR.require({name: renderer["eboot_sha256"] for name, renderer in renderers.items()} ==
                        candidate["renderer_eboot_sha256"], "renderer binary identity mismatch")
    for name, renderer in renderers.items():
        AUDITOR.require(renderer["teardown"] == "runtime-layers-released" and
                        renderer["post_health"] is True and renderer["lock_released"] is True and
                        f"[ps5-{name}] finished status=0" in renderer["output"] and
                        "[ps5-opengl-native] gate completed status=0" in map(normalize_log_tags, renderer["output"]),
                        "renderer completion mismatch")
        verify_renderer(name, renderer["output"])
    totals = collections.Counter(row["status"] for row in rows)
    print(f"Published export PASS: {len(rows):,} accounted = {totals['Pass']:,} Pass + "
          f"{totals['NotSupported']:,} reviewed NotSupported")
    print(f"Four complete configurations; no gaps or duplicates; "
          f"{sum(r['uneventful'] for r in receipts)}/{len(receipts)} uneventful CTS cycles")
    print("This checks exported evidence integrity, not a new hardware run or Khronos certification.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("evidence", type=Path, nargs="?", default=ROOT / "validation/2026-09-07")
    verify(parser.parse_args().evidence)


if __name__ == "__main__":
    main()
