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
import xml.etree.ElementTree as ET

AUDITOR = importlib.import_module("verify-cts-candidate")
ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("research_checkout", type=Path)
    args = parser.parse_args()
    research = args.research_checkout.resolve()
    raw = research / "results/native-app-cts"
    manifest_path = raw / "candidate-cbb5b718.json"
    manifest = json.loads(manifest_path.read_text())
    mustpass = research / "third_party/VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gl/khronos_mustpass/main/gl33-main.txt"
    AUDITOR.require(sha(mustpass) == manifest["mustpass_sha256"], "must-pass identity mismatch")
    audit = AUDITOR.audit(raw, manifest, mustpass.read_text().splitlines())
    AUDITOR.require(audit["complete"], "refusing to export an incomplete matrix")
    audit["manifest_sha256"] = sha(manifest_path)
    destination = ROOT / "validation/2026-09-06"
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(manifest_path, destination / "candidate.json")
    shutil.copyfile(mustpass, destination / "mustpass-gl33.txt")
    (destination / "audit.json").write_text(json.dumps(audit, indent=2) + "\n")
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
            uneventful="20260905-234856" not in relative,
            raw_sha256={suffix: sha(Path(prefix + suffix)) for suffix in
                        ("-pss-opengl-cts.qpa", "-pss-opengl-cts.status", "-result.json",
                         "-runner.json", "-klog.log", "-pss-opengl.log", "-cts-args.txt", "-cts-shard.txt")}))
    with (destination / "cases.csv.gz").open("wb") as stream:
        with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0) as compressed:
            with io.TextIOWrapper(compressed, encoding="utf-8", newline="") as output:
                writer = csv.writer(output, lineterminator="\n")
                writer.writerow(("configuration", "case", "status", "message", "receipt"))
                writer.writerows(rows)
    (destination / "receipts.json").write_text(json.dumps(receipts, indent=2) + "\n")
    # These allowlisted prefixes contain renderer results, not raw kernel or device state.
    examples = {}
    for name, stamp in (("imgui", "023854"), ("nanovg", "024217"), ("sokol", "024513")):
        prefix = research / f"results/native-app/cbb5-consumers/PPSA99005-20260906-{stamp}"
        log = Path(str(prefix) + "-opengl.log")
        lifecycle = json.loads(Path(str(prefix) + "-result.json").read_text(encoding="utf-8-sig"))
        runner = json.loads(Path(str(prefix) + "-runner.json").read_text(encoding="utf-8-sig"))
        text = log.read_text()
        AUDITOR.require("gate completed status=0" in text and lifecycle["teardownSignal"] ==
                        "runtime-layers-released" and runner["postHealthChecked"] and runner["lockReleased"],
                        f"unclean external renderer: {name}")
        examples[name] = dict(eboot_sha256=lifecycle["ebootSha256"].lower(),
                             raw_log_sha256=sha(log), teardown=lifecycle["teardownSignal"],
                             post_health=True, lock_released=True,
                             output=[line for line in text.splitlines() if line.startswith(
                                 (f"[ps5-{name}]", "[ps5-agc] present-shutdown", "[pss-opengl-native] gate completed"))])
    (destination / "renderers.json").write_text(json.dumps(examples, indent=2) + "\n")
    names = ("candidate.json", "audit.json", "mustpass-gl33.txt", "cases.csv.gz", "receipts.json", "renderers.json")
    (destination / "SHA256SUMS").write_text("".join(f"{sha(destination / name)}  {name}\n" for name in names))
    print(f"Exported {len(rows)} audited case results; {len(receipts)} receipts; raw device logs excluded")


if __name__ == "__main__":
    main()
