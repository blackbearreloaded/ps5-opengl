#!/usr/bin/env python3
"""Re-audit private receipts, then export only case results and provenance."""
import argparse
import csv
import gzip
import hashlib
import importlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import xml.etree.ElementTree as ET

AUDITOR = importlib.import_module("verify-cts-candidate")
PUBLISHED = importlib.import_module("verify-published-validation")
ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True, help="new evidence directory; never overwritten")
    parser.add_argument("--mustpass", type=Path, default=ROOT /
                        "third_party/VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gl/khronos_mustpass/main/gl33-main.txt")
    parser.add_argument("--renderer", action="append", required=True,
                        help="NAME=receipt prefix, once each for imgui, nanovg and sokol")
    args = parser.parse_args()
    raw = args.results.resolve()
    manifest_path = args.manifest
    manifest = json.loads(manifest_path.read_text())
    mustpass = args.mustpass
    destination = args.destination.resolve()
    AUDITOR.require(not destination.exists(), "refusing to overwrite existing evidence")
    renderer_paths = dict(value.split("=", 1) for value in args.renderer)
    AUDITOR.require(len(args.renderer) == 3 and set(renderer_paths) == {"imgui", "nanovg", "sokol"},
                    "provide exactly one prefix per renderer")
    eventful = manifest.get("eventful_receipts", {})
    AUDITOR.require(set(eventful) <= set(manifest["receipts"]) and all(eventful.values()),
                    "eventful receipts need an exact selected receipt and review")
    AUDITOR.require(sha(mustpass) == manifest["mustpass_sha256"], "must-pass identity mismatch")
    audit = AUDITOR.audit(raw, manifest, mustpass.read_text().splitlines())
    AUDITOR.require(audit["complete"], "refusing to export an incomplete matrix")
    audit["manifest_sha256"] = sha(manifest_path)
    rows, receipts = [], []
    for relative in manifest["receipts"]:
        path = raw / relative
        prefix = str(path).removesuffix("-pss-opengl-cts.qpa")
        config = relative.split("/")[0].removeprefix("config-")
        text = path.read_text(errors="replace")
        count = 0
        for match in AUDITOR.QPA.CASE.finditer(text):
            node = ET.fromstring(match["body"])
            result = next(node.iter("Result"))
            status = result.attrib["StatusCode"]
            message = "".join(result.itertext()).strip() if status == "NotSupported" else ""
            rows.append((config, match["name"], status, message, relative))
            count += 1
        lifecycle = json.loads(Path(prefix + "-result.json").read_text(encoding="utf-8-sig"))
        runner = json.loads(Path(prefix + "-runner.json").read_text(encoding="utf-8-sig"))
        receipts.append(dict(
            receipt=relative, configuration=config, cases=count,
            eboot_sha256=lifecycle["ebootSha256"].lower(),
            libc_sha256=lifecycle["libcSha256"].lower(),
            outcome=lifecycle["outcome"], teardown=lifecycle["teardownSignal"],
            post_health=runner["postHealthChecked"], lock_released=runner["lockReleased"],
            uneventful=relative not in eventful,
            raw_sha256={suffix: sha(Path(prefix + suffix)) for suffix in
                        ("-pss-opengl-cts.qpa", "-pss-opengl-cts.status", "-result.json",
                         "-runner.json", "-klog.log", "-pss-opengl.log", "-cts-args.txt", "-cts-shard.txt")}))
    # These allowlisted prefixes contain renderer results, not raw kernel or device state.
    examples = {}
    for name, prefix in renderer_paths.items():
        log = Path(str(prefix) + "-opengl.log")
        lifecycle = json.loads(Path(str(prefix) + "-result.json").read_text(encoding="utf-8-sig"))
        runner = json.loads(Path(str(prefix) + "-runner.json").read_text(encoding="utf-8-sig"))
        text = log.read_text()
        AUDITOR.require(lifecycle["titleId"] == "PPSA99005" and lifecycle["outcome"] == "entered-eboot"
                        and lifecycle["libcSha256"].lower() == manifest["libc_sha256"]
                        and lifecycle["teardownSignal"] == "runtime-layers-released"
                        and runner["postHealthChecked"] is True and runner["lockReleased"] is True
                        and set(lifecycle.get("evidenceWarnings", [])) <=
                        {"Chiaki video readiness skipped by caller"},
                        f"unclean external renderer: {name}")
        examples[name] = dict(eboot_sha256=lifecycle["ebootSha256"].lower(),
                             raw_log_sha256=sha(log), teardown=lifecycle["teardownSignal"],
                             post_health=True, lock_released=True,
                             output=[line for line in text.splitlines() if line.startswith(
                                 (f"[ps5-{name}]", "[ps5-agc] present-shutdown", "[pss-opengl-native] gate completed"))])
    names = ("candidate.json", "audit.json", "mustpass-gl33.txt", "cases.csv.gz", "receipts.json", "renderers.json")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".validation-", dir=destination.parent) as temporary:
        stage = Path(temporary) / "export"
        stage.mkdir()
        shutil.copyfile(manifest_path, stage / "candidate.json")
        shutil.copyfile(mustpass, stage / "mustpass-gl33.txt")
        for name, value in (("audit", audit), ("receipts", receipts), ("renderers", examples)):
            (stage / f"{name}.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
        with (stage / "cases.csv.gz").open("wb") as stream:
            with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0) as compressed:
                with io.TextIOWrapper(compressed, encoding="utf-8", newline="") as output:
                    writer = csv.writer(output, lineterminator="\n")
                    writer.writerow(("configuration", "case", "status", "message", "receipt"))
                    writer.writerows(rows)
        (stage / "SHA256SUMS").write_text("".join(f"{sha(stage / name)}  {name}\n" for name in names))
        PUBLISHED.verify(stage)
        stage.rename(destination)
    print(f"Exported {len(rows)} audited case results; {len(receipts)} receipts; raw device logs excluded")


if __name__ == "__main__":
    main()
