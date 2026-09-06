#!/usr/bin/env python3
"""Verify the published result export, not independently rerun GPU tests."""
import collections
import csv
import gzip
import hashlib
import importlib
import json
from pathlib import Path

AUDITOR = importlib.import_module("verify-cts-candidate")
ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "validation/2026-09-06"


def main():
    verified = set()
    for line in (EVIDENCE / "SHA256SUMS").read_text().splitlines():
        expected, name = line.split("  ", 1)
        path = (EVIDENCE / name).resolve()
        AUDITOR.require(path.parent == EVIDENCE.resolve(), "unsafe evidence path")
        AUDITOR.require(name not in verified, "duplicate evidence checksum")
        AUDITOR.require(hashlib.sha256(path.read_bytes()).hexdigest() == expected,
                        f"evidence checksum mismatch: {name}")
        verified.add(name)
    AUDITOR.require(verified == {"candidate.json", "audit.json", "mustpass-gl33.txt",
                                "cases.csv.gz", "receipts.json", "renderers.json"},
                    "incomplete evidence checksum inventory")
    candidate = json.loads((EVIDENCE / "candidate.json").read_text())
    audit = json.loads((EVIDENCE / "audit.json").read_text())
    official = (EVIDENCE / "mustpass-gl33.txt").read_text().splitlines()
    AUDITOR.require(len(official) == len(set(official)) == 9886, "must-pass inventory mismatch")
    AUDITOR.require(hashlib.sha256((EVIDENCE / "mustpass-gl33.txt").read_bytes()).hexdigest() ==
                    candidate["mustpass_sha256"], "must-pass provenance mismatch")
    AUDITOR.require(hashlib.sha256((EVIDENCE / "candidate.json").read_bytes()).hexdigest() ==
                    audit["manifest_sha256"], "candidate provenance mismatch")
    with gzip.open(EVIDENCE / "cases.csv.gz", "rt", encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream))
    receipts = json.loads((EVIDENCE / "receipts.json").read_text())
    grouped = collections.defaultdict(list)
    for row in rows:
        grouped[row["receipt"]].append((row["case"], row["status"], row["message"]))
    AUDITOR.require(set(grouped) == set(candidate["receipts"]) and len(receipts) == len(grouped) == 21,
                    "receipt inventory mismatch")
    runs = []
    for receipt in receipts:
        cases = grouped[receipt["receipt"]]
        AUDITOR.require(receipt["cases"] == len(cases) and receipt["post_health"] and receipt["lock_released"]
                        and receipt["outcome"] == "entered-eboot" and receipt["teardown"] == "runtime-layers-released",
                        "receipt count or lifecycle mismatch")
        AUDITOR.require(receipt["eboot_sha256"] == candidate["eboot_sha256"] and
                        receipt["libc_sha256"] == candidate["libc_sha256"], "mixed binary candidates")
        AUDITOR.require(all(row["configuration"] == receipt["configuration"] for row in rows
                            if row["receipt"] == receipt["receipt"]), "configuration mismatch")
        runs.append((receipt["configuration"], cases))
    actual = AUDITOR.coverage(official, runs, candidate["not_supported"])
    AUDITOR.require(audit["complete"] and actual == audit["configurations"], "export differs from strict audit")
    AUDITOR.require(sum(r["uneventful"] for r in receipts) == 20, "incident-free cycle count mismatch")
    for config in actual.values():
        AUDITOR.require(not config["missing"] and config["counts"] == {"Pass": 9351, "NotSupported": 535},
                        "unexpected result totals")
    renderers = json.loads((EVIDENCE / "renderers.json").read_text())
    AUDITOR.require(set(renderers) == {"imgui", "nanovg", "sokol"}, "renderer inventory mismatch")
    for name, renderer in renderers.items():
        AUDITOR.require(renderer["teardown"] == "runtime-layers-released" and
                        renderer["post_health"] and renderer["lock_released"] and
                        f"[ps5-{name}] finished status=0" in renderer["output"] and
                        "[pss-opengl-native] gate completed status=0" in renderer["output"],
                        "renderer completion mismatch")
    print("Published export PASS: 39,544 accounted = 37,404 Pass + 2,140 reviewed NotSupported")
    print("Four complete configurations; no gaps or duplicates; 20 uneventful CTS cycles")
    print("This checks exported evidence integrity, not a new hardware run or Khronos certification.")


if __name__ == "__main__":
    main()
