#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Summarize and validate one non-compact dEQP/GL CTS QPA log."""

from __future__ import annotations

import argparse
import collections
import json
import re
import shlex
import xml.etree.ElementTree as ET
from pathlib import Path
from opengl_receipts import CTS_QPA_SUFFIXES, cts_receipt_parts


CASE = re.compile(
    r"^#beginTestCaseResult (?P<name>\S+)\r?\n"
    r"(?P<body>.*?)^#endTestCaseResult\s*$",
    re.MULTILINE | re.DOTALL,
)
STATUS = re.compile(r'<Result\s+StatusCode="([^"]+)"')
DURATION = re.compile(r'<Number Name="TestDuration"[^>]*>(\d+)</Number>')
ACCEPTED = {
    "Pass",
    "NotSupported",
    "QualityWarning",
    "CompatibilityWarning",
    "CapabilityWarning",
    "Waiver",
}


def summarize(text: str, expected: list[str] | None = None) -> dict:
    results: list[tuple[str, str]] = []
    durations = {}
    for match in CASE.finditer(text):
        status = STATUS.search(match.group("body"))
        if status is None:
            raise ValueError(f"case has no Result element: {match.group('name')}")
        results.append((match.group("name"), status.group(1)))
        duration = DURATION.search(match.group("body"))
        if duration:
            durations[match.group("name")] = int(duration.group(1)) / 1_000_000

    opened = len(re.findall(r"^#beginTestCaseResult ", text, re.MULTILINE))
    incomplete = opened != len(results) or "#endSession" not in text
    incomplete |= not results or len({name for name, _ in results}) != len(results)
    if expected is not None:
        incomplete |= [name for name, _ in results] != expected

    counts = collections.Counter(status for _, status in results)
    failed = [name for name, status in results if status not in ACCEPTED]
    return {
        "complete": not incomplete,
        "executed": len(results),
        "expected": len(expected) if expected is not None else None,
        "counts": dict(sorted(counts.items())),
        "failed": failed,
        "seconds": sum(durations.values()),
        "cases": [dict(name=name, status=status, seconds=durations.get(name))
                  for name, status in results],
    }


def inventory(directory: Path, current_eboot: str | None) -> dict:
    # Development evidence only: never turn an old binary's pass into a new pass.
    import importlib
    prepare = importlib.import_module("prepare-cts-shard")
    latest, receipts, rejected = {}, {}, []
    for path in sorted((p for p in directory.rglob("*.qpa") if p.name.endswith(CTS_QPA_SUFFIXES)), key=lambda p: p.name):
        try:
            text = path.read_text(errors="replace")
            command = re.search(r'^#sessionInfo commandLineParameters "(.*)"$', text, re.M)
            if not command:
                raise ValueError("missing configuration")
            options = dict(arg.split("=", 1) for arg in shlex.split(command[1]) if "=" in arg)
            signature = (int(options["--deqp-surface-width"]),
                         int(options["--deqp-surface-height"]),
                         int(options["--deqp-base-seed"]),
                         options.get("--deqp-surface-type", "default"),
                         options.get("--deqp-gl-config-name", "default"))
            # Timing history is not acceptance; retain the actual requested surface.
            config = prepare.configuration_index(signature, allow_implicit_pbuffer=True)
            prefix, _ = cts_receipt_parts(path)
            case_list = Path(prefix + "-cts-shard.txt")
            expected = [s.strip() for s in case_list.read_text().splitlines() if s.strip()] if case_list.is_file() else None
            summary = summarize(text, expected)
            lifecycle = json.loads(Path(prefix + "-result.json").read_text(encoding="utf-8-sig"))
            runner_path = Path(prefix + "-runner.json")
            runner = json.loads(runner_path.read_text(encoding="utf-8-sig")) if runner_path.is_file() else {}
            eboot = lifecycle["ebootSha256"].lower()
            if not re.fullmatch(r"[0-9a-f]{64}", eboot):
                raise ValueError("missing executable identity")
            receipt_id = path.relative_to(directory).as_posix()
            receipts[receipt_id] = dict(
                eboot_sha256=eboot, libc_sha256=lifecycle.get("libcSha256", "").lower(),
                configuration=int(config), requested_surface=signature[3],
                complete=summary["complete"], ordered_inputs=expected is not None,
                teardown=lifecycle.get("teardownSignal"),
                entered=lifecycle.get("outcome") == "entered-eboot",
                current_binary=bool(current_eboot and eboot == current_eboot.lower()),
                # Legacy receipts did not persist service/lock evidence in JSON.
                post_health=runner.get("postHealthChecked"),
                lock_released=runner.get("lockReleased"))
            for case in summary["cases"]:
                latest.setdefault(str(config), {})[case["name"]] = dict(
                    status=case["status"], seconds=case["seconds"], receipt=receipt_id)
            completed = {case["name"] for case in summary["cases"]}
            for name in re.findall(r'^#beginTestCaseResult (\S+)', text, re.M):
                if name not in completed:
                    latest.setdefault(str(config), {})[name] = dict(
                        status="Incomplete", seconds=None, receipt=receipt_id)
        except (OSError, ValueError, KeyError, StopIteration) as error:
            rejected.append(dict(path=str(path), reason=str(error)))
    timings = {}
    for cases in latest.values():
        for name, case in cases.items():
            if case["status"] in {"Pass", "NotSupported"} and case["seconds"] is not None:
                timings[name] = max(timings.get(name, 0), case["seconds"])
    return dict(scope="development-history-not-release-certification", cases=latest,
                receipts=receipts, timings=timings, rejected=rejected)


def egl_configuration_inventory(text: str) -> dict:
    """Keep upstream results intact; reject synthetic defaults as EGL coverage."""
    summary = summarize(text, ["CTS-Configs.gl33"])
    if not summary["complete"] or summary["counts"] != {"Pass": 1}:
        raise ValueError("EGL inventory requires one complete passing CTS-Configs.gl33 case")
    try:
        root = ET.fromstring(next(CASE.finditer(text)).group("body").strip())
    except ET.ParseError as error:
        raise ValueError(f"invalid EGL inventory XML: {error}") from error
    configs = root.findall("./Section[@Name='Configs']")
    excluded = root.findall("./Section[@Name='ExcludedConfigs']")
    if len(configs) != 1 or len(excluded) != 1:
        raise ValueError("EGL inventory requires Configs and ExcludedConfigs sections")
    entries = ["".join(node.itertext()).strip() for node in configs[0].findall("Text")]
    errors, eligible = [], []
    for entry in entries:
        match = re.fullmatch(r"EGL\(([1-9][0-9]*)\): (.+)", entry)
        surfaces = match[2].split(", ") if match else []
        if not surfaces or not set(surfaces) <= {"window", "pixmap", "pbuffer"}:
            errors.append(f"not a real EGL configuration: {entry}")
        elif int(match[1]) in {config["id"] for config in eligible}:
            errors.append(f"duplicate EGL configuration: {entry}")
        else:
            eligible.append(dict(id=int(match[1]), surfaces=surfaces))
    if not eligible:
        errors.append("no eligible real EGL configurations")
    return dict(eligible=eligible, excluded=["".join(node.itertext()).strip()
                                           for node in excluded[0].findall("Text")],
                errors=errors)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("qpa", type=Path, nargs="?")
    parser.add_argument("--expected-list", type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--details", action="store_true")
    parser.add_argument("--inventory", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--current-eboot-sha256")
    parser.add_argument("--require-egl-configs", action="store_true",
                        help="require real EGL inventory from CTS-Configs.gl33, not a synthetic default")
    arguments = parser.parse_args()
    if arguments.inventory:
        if arguments.qpa or arguments.require_egl_configs or not arguments.output or not arguments.inventory.is_dir():
            parser.error("inventory requires an existing directory, --output, and no QPA argument")
        ledger = inventory(arguments.inventory, arguments.current_eboot_sha256)
        arguments.output.write_text(json.dumps(ledger, indent=2) + "\n")
        print(f"CTS inventory: receipts={len(ledger['receipts'])} timings={len(ledger['timings'])} "
              f"rejected={len(ledger['rejected'])} output={arguments.output}")
        return 0
    if not arguments.qpa:
        parser.error("a QPA receipt or --inventory is required")
    expected = [s.strip() for s in arguments.expected_list.read_text().splitlines() if s.strip()] if arguments.expected_list else None
    try:
        text = arguments.qpa.read_text(errors="replace")
        summary = summarize(text, expected)
        if arguments.require_egl_configs:
            summary["egl_configs"] = egl_configuration_inventory(text)
    except ValueError as error:
        parser.error(str(error))
    if not arguments.details:
        del summary["cases"]
    if arguments.json:
        print(json.dumps(summary, indent=2))
    else:
        counts_text = " ".join(f"{name}={count}" for name, count in summary["counts"].items())
        print(
            f"complete={int(summary['complete'])} executed={summary['executed']} "
            f"expected={summary['expected']} seconds={summary['seconds']:.2f} {counts_text}".rstrip()
        )
        for name in summary["failed"]:
            print(f"failed={name}")
        for error in summary.get("egl_configs", {}).get("errors", []):
            print(f"egl_inventory_error={error}")

    return 1 if (not summary["complete"] or summary["failed"] or
                 summary.get("egl_configs", {}).get("errors")) else 0


if __name__ == "__main__":
    raise SystemExit(main())
