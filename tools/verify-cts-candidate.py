#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Audit an explicit, single-binary CTS evidence set; never promote history."""

import argparse
import collections
import hashlib
import importlib
import json
from pathlib import Path
from opengl_receipts import normalize_log_tags, cts_receipt_parts
import re
import shlex
import xml.etree.ElementTree as ET

QPA = importlib.import_module("summarize-cts-qpa")
PREPARE = importlib.import_module("prepare-cts-shard")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def configuration_index(signature, cts_commit, tagged_release=False):
    # Only the historical adapter silently substituted a pbuffer for default.
    legacy = not tagged_release and cts_commit == "cf7edb26d3be2d8763595ed08fdc41f3c1b1966f"
    return PREPARE.configuration_index(signature, allow_implicit_pbuffer=legacy)


def require_completion(klog, app_log, executed):
    klog, app_log = normalize_log_tags(klog), normalize_log_tags(app_log)
    finished = f"[ps5-opengl-cts] finished state=passed executed={executed} failed=0 device_lost=0"
    require("[ps5-opengl-cts] finished" in klog.splitlines() and
            "[ps5-opengl-cts] starting GL33 CTS runner" in app_log.splitlines() and
            finished in app_log.splitlines(), "missing fresh completion evidence")


def require_render_target(node, expected):
    fields = {"Width", "Height", "RedBits", "GreenBits", "BlueBits", "AlphaBits",
              "DepthBits", "StencilBits", "SampleCount"}
    numbers = [n for n in node.iter("Number") if n.attrib["Name"] != "TestDuration"]
    actual = {n.attrib["Name"]: int(n.text) for n in numbers}
    require(set(expected) == fields and all(type(v) is int for v in expected.values())
            and len(numbers) == len(fields) and actual == expected,
            f"render-target mismatch: reported={actual} expected={expected}")
    return actual


def coverage(official, runs, exclusions):
    official_set = set(official)
    counts = {str(i): collections.Counter() for i in range(4)}
    seen = {str(i): set() for i in range(4)}
    for config, cases in runs:
        require(config in seen, f"unknown configuration: {config}")
        for name, status, message in cases:
            require(name in official_set, f"case outside must-pass: {name}")
            require(name not in seen[config], f"duplicate config {config}: {name}")
            require(status in {"Pass", "NotSupported"}, f"unaccepted {status}: {name}")
            if status == "NotSupported":
                review = exclusions.get(name, {})
                require(review.get("message") == message and review.get("basis")
                        and review.get("source"), f"unreviewed exclusion: {name}")
            seen[config].add(name)
            counts[config][status] += 1
    return {config: dict(executed=len(names), counts=dict(counts[config]),
                         missing=[name for name in official if name not in names])
            for config, names in seen.items()}


def audit(root, manifest, official):
    receipts = manifest["receipts"]
    require(len(receipts) == len(set(receipts)), "duplicate receipt in manifest")
    runs, render_targets = [], {}
    for relative in receipts:
        path = (root / relative).resolve()
        require(path.is_relative_to(root.resolve()) and
                path.name.endswith(QPA.CTS_QPA_SUFFIXES), "unsafe receipt path")
        prefix, namespace = cts_receipt_parts(path)
        lifecycle = json.loads(Path(prefix + "-result.json").read_text(encoding="utf-8-sig"))
        runner = json.loads(Path(prefix + "-runner.json").read_text(encoding="utf-8-sig"))
        require(lifecycle.get("ebootSha256", "").lower() == manifest["eboot_sha256"]
                and lifecycle.get("libcSha256", "").lower() == manifest["libc_sha256"],
                f"binary identity mismatch: {relative}")
        require(lifecycle.get("titleId") == "PPSA99005" and
                lifecycle.get("outcome") == "entered-eboot" and
                lifecycle.get("teardownSignal") == "runtime-layers-released" and
                runner.get("postHealthChecked") is True and runner.get("lockReleased") is True,
                f"unclean lifecycle: {relative}")
        require(set(lifecycle.get("evidenceWarnings", [])) <=
                {"Chiaki video readiness skipped by caller"}, f"unreviewed lifecycle warning: {relative}")
        args_path, cases_path = Path(prefix + "-cts-args.txt"), Path(prefix + "-cts-shard.txt")
        require(digest(args_path) == runner["argumentsSha256"] and
                digest(cases_path) == runner["caseListSha256"], f"input hash mismatch: {relative}")
        args = [line.strip() for line in args_path.read_text().splitlines()
                if line.strip() and not line.lstrip().startswith("#")]
        expected = [line.strip() for line in cases_path.read_text().splitlines() if line.strip()]
        text = path.read_text(errors="replace")
        command = re.search(r'^#sessionInfo commandLineParameters "(.*)"$', text, re.M)
        require(command and shlex.split(command[1]) == args, f"QPA argument mismatch: {relative}")
        release_name = manifest.get("cts_release_name", manifest["cts_commit"])
        require(release_name == manifest["cts_commit"] or
                release_name.endswith("-g" + manifest["cts_commit"]),
                "CTS release name does not identify the frozen commit")
        require(f"#sessionInfo releaseName {release_name}\n" in text,
                f"CTS revision mismatch: {relative}")
        options = dict(arg.split("=", 1) for arg in args if "=" in arg)
        signature = (int(options["--deqp-surface-width"]), int(options["--deqp-surface-height"]),
                     int(options["--deqp-base-seed"]), options.get("--deqp-surface-type", "default"),
                     options.get("--deqp-gl-config-name", "default"))
        config = configuration_index(signature, manifest["cts_commit"], "cts_release_name" in manifest)
        summary = QPA.summarize(text, expected)
        require(summary["complete"] and not summary["failed"], f"incomplete or failed QPA: {relative}")
        status = dict(field.split("=", 1) for field in
                      Path(prefix + f"-{namespace}-cts.status").read_text().split())
        wanted = dict(state="passed", complete="1", executed=str(len(expected)),
                      passed=str(summary["counts"].get("Pass", 0)),
                      not_supported=str(summary["counts"].get("NotSupported", 0)),
                      failed="0", warnings="0", waived="0", device_lost="0")
        require(status == wanted, f"status/QPA mismatch: {relative}")
        require_completion(Path(prefix + "-klog.log").read_text(errors="replace"),
                           Path(prefix + f"-{namespace}.log").read_text(errors="replace"), len(expected))
        cases = []
        for match in QPA.CASE.finditer(text):
            node = ET.fromstring(match["body"])
            result = next(node.iter("Result"))
            if match["name"] == "KHR-GL33.info.render_target":
                render_targets[config] = require_render_target(
                    node, manifest["expected_render_targets"][config])
            cases.append((match["name"], result.attrib["StatusCode"],
                          "".join(result.itertext()).strip()))
        runs.append((config, cases))
    configs = coverage(official, runs, manifest["not_supported"])
    return dict(scope="single-candidate-CTS-matrix-only", eboot_sha256=manifest["eboot_sha256"],
                complete=all(not value["missing"] for value in configs.values()),
                clean_cycles=len(receipts), configurations=configs, render_targets=render_targets)


def self_test():
    import copy
    import tempfile
    target = dict(Width=64, Height=8192, RedBits=8, GreenBits=8, BlueBits=8,
                  AlphaBits=8, DepthBits=24, StencilBits=8, SampleCount=-1)
    node = ET.Element("TestCaseResult")
    for name, value in target.items():
        ET.SubElement(node, "Number", Name=name).text = str(value)
    ET.SubElement(node, "Number", Name="TestDuration").text = "100"
    require_render_target(node, target)
    bad_targets = []
    for name, value in (("Width", 1920), ("Height", 1080), ("DepthBits", 32), ("SampleCount", 0)):
        bad = copy.deepcopy(node)
        bad.find(f"Number[@Name='{name}']").text = str(value)
        bad_targets.append(bad)
    missing, duplicate = copy.deepcopy(node), copy.deepcopy(node)
    missing.remove(missing.find("Number[@Name='StencilBits']"))
    duplicate.append(copy.deepcopy(node.find("Number[@Name='Width']")))
    for bad in [*bad_targets, missing, duplicate]:
        try:
            require_render_target(bad, target)
        except ValueError:
            continue
        raise AssertionError("wrong/missing/duplicate render-target field was accepted")
    klog = "[ps5-opengl-cts] finished\n"
    app = "[ps5-opengl-cts] starting GL33 CTS runner\n[ps5-opengl-cts] finished state=passed executed=2 failed=0 device_lost=0\n"
    require_completion(klog, app, 2)
    for bad_klog, bad_app, count in [("", app, 2), (klog, "", 2), (klog, app, 3)]:
        try:
            require_completion(bad_klog, bad_app, count)
        except ValueError:
            continue
        raise AssertionError("stale/mismatched completion was accepted")
    official = ["KHR-GL33.a", "KHR-GL33.b"]
    review = {official[1]: dict(message="optional", basis="source predicate", source="test.cpp:1")}
    runs = [(str(i), [(official[0], "Pass", "Pass"),
                      (official[1], "NotSupported", "optional")]) for i in range(4)]
    require(all(not c["missing"] for c in coverage(official, runs, review).values()), "complete fixture")
    require(coverage(official, runs[:1], review)["1"]["missing"] == official, "gap fixture")
    for bad_runs, bad_review in [(runs + runs[:1], review), (runs, {}),
                                 (runs, {official[1]: dict(message="wrong", basis="x", source="x")})]:
        try:
            coverage(official, bad_runs, bad_review)
        except ValueError:
            continue
        raise AssertionError("invalid coverage was accepted")
    for bad_status in ("Fail", "Waiver", "QualityWarning", "FutureStatus"):
        bad = copy.deepcopy(runs)
        bad[0][1][0] = (official[0], bad_status, "")
        try:
            coverage(official, bad, review)
        except ValueError:
            continue
        raise AssertionError("bad status was accepted")
    with tempfile.TemporaryDirectory() as directory:
        try:
            audit(Path(directory), dict(receipts=["../outside-ps5-opengl-cts.qpa"]), official)
        except ValueError:
            pass
        else:
            raise AssertionError("receipt traversal accepted")
    print("cts-candidate-audit: self-test PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, nargs="?")
    parser.add_argument("--results", type=Path, default=Path("results/native-app-cts"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--allow-incomplete", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--mustpass", type=Path, help="exact selected CTS release's must-pass list")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.manifest or not args.output:
        parser.error("manifest and --output are required")
    root = Path(__file__).resolve().parent.parent
    mustpass = args.mustpass or root / "third_party/VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gl/khronos_mustpass/main/gl33-main.txt"
    try:
        manifest = json.loads(args.manifest.read_text())
        require(digest(mustpass) == manifest["mustpass_sha256"], "must-pass identity mismatch")
        official = mustpass.read_text().splitlines()
        require(len(official) == len(set(official)) == 9886, "invalid must-pass count")
        report = audit(args.results, manifest, official)
        report["manifest_sha256"] = digest(args.manifest)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
        print(f"cts-candidate: complete={int(report['complete'])} clean_cycles={report['clean_cycles']}")
        for config, value in report["configurations"].items():
            print(f"config={config} executed={value['executed']}/9886 counts={value['counts']}")
        return 0 if report["complete"] or args.allow_incomplete else 1
    except (OSError, ValueError, KeyError, StopIteration, ET.ParseError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
