# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check flexible batching without weakening the published acceptance checks."""
import contextlib
import csv
import gzip
import hashlib
import importlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

VERIFY = importlib.import_module("verify-published-validation")
FETCH = importlib.import_module("fetch-sources")
EXPORT = importlib.import_module("export-published-validation")
BASELINE = VERIFY.ROOT / "validation/2026-09-06"


class PublishedValidationTest(unittest.TestCase):
    def test_export_and_tamper_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence = Path(directory) / "evidence"
            shutil.copytree(BASELINE, evidence)
            sums = evidence / "SHA256SUMS"
            original = sums.read_bytes()
            with contextlib.redirect_stdout(io.StringIO()):
                VERIFY.verify(evidence)
                for bad, message in [(b"", "incomplete"),
                                     (b"0" * 64 + b"  ../outside\n", "unsafe"),
                                     (original + original, "duplicate")]:
                    sums.write_bytes(bad)
                    with self.assertRaisesRegex(ValueError, message):
                        VERIFY.verify(evidence)
                sums.write_bytes(original)
                (evidence / "cases.csv.gz").write_bytes(b"damaged")
                with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                    VERIFY.verify(evidence)

    def test_compiler_source_identity_rejection(self):
        with patch.object(FETCH, "digest", return_value="wrong"):
            with self.assertRaisesRegex(ValueError, "patch hash mismatch"):
                FETCH.verify_psbc()
        with patch.object(FETCH, "digest", return_value=FETCH.PINS["psbc_patch"]["sha256"]), \
                patch.object(FETCH, "git", return_value="wrong-tree"):
            with self.assertRaisesRegex(ValueError, "source tree mismatch"):
                FETCH.verify_psbc()

    def test_export_refuses_overwrite_and_incomplete_matrix(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "export"
            arguments = ["export", str(BASELINE / "candidate.json"), "--results", directory,
                         "--destination", str(destination), "--mustpass", str(BASELINE / "mustpass-gl33.txt")]
            for name in ("imgui", "nanovg", "sokol"):
                arguments += ["--renderer", f"{name}={directory}/missing-{name}"]
            with patch("sys.argv", arguments), patch.object(EXPORT.AUDITOR, "audit", return_value={"complete": False}):
                with self.assertRaisesRegex(ValueError, "incomplete matrix"):
                    EXPORT.main()
                self.assertFalse(destination.exists())
                destination.mkdir()
                with self.assertRaisesRegex(ValueError, "overwrite"):
                    EXPORT.main()

    def test_rebatched_export_and_rejections(self):
        with tempfile.TemporaryDirectory() as temporary:
            evidence = Path(temporary) / "evidence"
            shutil.copytree(BASELINE, evidence)

            def write_json(name, value):
                (evidence / name).write_text(json.dumps(value), encoding="utf-8")

            def check():
                audit = json.loads((evidence / "audit.json").read_text())
                audit["manifest_sha256"] = hashlib.sha256((evidence / "candidate.json").read_bytes()).hexdigest()
                write_json("audit.json", audit)
                names = [line.split("  ", 1)[1] for line in (evidence / "SHA256SUMS").read_text().splitlines()]
                (evidence / "SHA256SUMS").write_text("".join(
                    f"{hashlib.sha256((evidence / name).read_bytes()).hexdigest()}  {name}\n" for name in names))
                with contextlib.redirect_stdout(io.StringIO()):
                    VERIFY.verify(evidence)

            check()  # Immutable historical export remains supported.
            # Synthetic four-receipt fixture: same exact case results, different batching.
            with gzip.open(evidence / "cases.csv.gz", "rt", newline="") as stream:
                rows = list(csv.DictReader(stream))
            receipts = json.loads((evidence / "receipts.json").read_text())
            selected = [next(dict(r) for r in receipts if r["configuration"] == str(i)) for i in range(4)]
            for receipt in selected:
                receipt["receipt"] = f"config-{receipt['configuration']}/fixture-ps5-opengl-cts.qpa"
                receipt["cases"] = 9886
                receipt["uneventful"] = True
            for row in rows:
                row["receipt"] = selected[int(row["configuration"])]["receipt"]
            with gzip.open(evidence / "cases.csv.gz", "wt", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            write_json("receipts.json", selected)
            candidate = json.loads((evidence / "candidate.json").read_text())
            candidate["receipts"] = [r["receipt"] for r in selected]
            candidate["eventful_receipts"] = {}
            write_json("candidate.json", candidate)
            audit = json.loads((evidence / "audit.json").read_text())
            audit["clean_cycles"] = 4
            write_json("audit.json", audit)
            check()
            for field, bad in (("eboot_sha256", "wrong"), ("post_health", "true"),
                               ("uneventful", False), ("cases", 9885),
                               ("receipt", selected[1]["receipt"])):
                original = selected[0][field]
                selected[0][field] = bad
                write_json("receipts.json", selected)
                with self.assertRaises(ValueError, msg=field):
                    check()
                selected[0][field] = original
            write_json("receipts.json", selected)
            check()
            candidate["renderer_eboot_sha256"] = {name: item["eboot_sha256"] for name, item in
                                                  json.loads((evidence / "renderers.json").read_text()).items()}
            write_json("candidate.json", candidate)
            check()
            candidate["renderer_eboot_sha256"]["imgui"] = "wrong"
            write_json("candidate.json", candidate)
            with self.assertRaisesRegex(ValueError, "renderer binary identity"):
                check()

    def test_renderer_oracles(self):
        renderers = json.loads((BASELINE / "renderers.json").read_text())
        for name, receipt in renderers.items():
            output = receipt["output"]
            VERIFY.verify_renderer(name, output)
            for bad in ([line for line in output if not line.startswith(f"[ps5-{name}] frame=0 ")],
                        [line.replace("font-ink=209", "font-ink=0").replace("dirty-stencil=0", "dirty-stencil=1")
                         .replace("mismatches=0", "mismatches=1") for line in output],
                        [line.replace("close=00000000", "close=00000001") for line in output]):
                with self.assertRaises(ValueError, msg=name):
                    VERIFY.verify_renderer(name, bad)


if __name__ == "__main__":
    unittest.main()
