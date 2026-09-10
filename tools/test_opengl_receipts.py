# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Current branding and read-only compatibility with frozen validation logs."""
import importlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from opengl_receipts import LEGACY_NAME, cts_receipt_parts
from test_cubes_profile import receipt as cubes_receipt, summarize as cubes
from test_imgui_benchmark import receipt as imgui_receipt, summarize as imgui

CTS = importlib.import_module("verify-cts-candidate")
HEAP = importlib.import_module("summarize-app-heap")
ROOT = Path(__file__).resolve().parents[1]


class ReceiptNamingTests(unittest.TestCase):
    def test_cts_surface_contract_and_historical_default(self):
        prepare = importlib.import_module("prepare-cts-shard")
        release = "067e8832315e79817ede1c4863804e440f5d1c80"
        development = "cf7edb26d3be2d8763595ed08fdc41f3c1b1966f"
        for i in range(4):
            options = dict(arg.split("=", 1) for arg in
                           prepare.encode_arguments(i).decode().splitlines() if arg.startswith("--"))
            signature = (int(options["--deqp-surface-width"]), int(options["--deqp-surface-height"]),
                         int(options["--deqp-base-seed"]), options["--deqp-surface-type"],
                         options.get("--deqp-gl-config-name", "default"))
            self.assertEqual(CTS.configuration_index(signature, release, True), str(i))
        implicit = (64, 64, 1, "default", "default")
        self.assertEqual(CTS.configuration_index(implicit, development), "0")
        for signature, commit, tagged in ((implicit, release, True), (implicit, development, True),
                                         ((64, 64, 1, "window", "default"), release, True)):
            with self.assertRaises(ValueError):
                CTS.configuration_index(signature, commit, tagged)

    def test_current_project_names(self):
        names = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
        pattern = re.compile(r"\b" + LEGACY_NAME.split("-")[0] + r"(?:[-_ ]|OPENGL)", re.I)
        for name in names:
            path = ROOT / name
            if name.startswith("validation/") or not path.is_file():
                continue
            if name == "tools/opengl_receipts.py" or path.suffix not in {
                ".c", ".cpp", ".h", ".py", ".ps1", ".sh", ".md", ".json", ".txt"
            }:
                continue
            self.assertIsNone(pattern.search(path.read_text()), name)
        metadata = json.loads((ROOT / "native-app/param.json").read_text())
        self.assertEqual(metadata["localizedParameters"]["en-US"]["titleName"], "PS5 OpenGL 3.3 Tests")
        self.assertEqual(metadata["contentId"], "UP9000-PPSA99005_00-PS5OPENGLTEST001")

    def test_legacy_logs_and_duplicate_rejection(self):
        heap = "".join(f"[ps5-opengl-heap] phase={phase} sample=0 state=2 live_bytes=0 "
                       "peak_bytes=16 blocks=0 failures=0 ambiguous_zero_reallocs=0\n"
                       for phase in ("begin", "end"))
        gpu = "".join(f"[ps5-opengl-gpu-memory] phase={phase} sample=0 direct_bytes=0 "
                      "direct_peak=16384 allocations=0 mapped_bytes=0 mapped_peak=16384 "
                      "mappings=0 failures=0 invalid=0\n" for phase in ("begin", "end"))
        for parser, text, options in (
            (cubes, cubes_receipt(), {"seconds": 1}), (imgui, imgui_receipt(), {}),
            (HEAP.summarize, heap, {}), (HEAP.summarize_gpu, gpu, {})
        ):
            legacy = text.replace("ps5-opengl", LEGACY_NAME)
            self.assertEqual(parser(text, **options), parser(legacy, **options))
            with self.assertRaises(ValueError):
                parser(text + legacy, **options)

    def test_cts_namespace_and_original_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for namespace in ("ps5-opengl", LEGACY_NAME):
                with self.subTest(namespace=namespace):
                    prefix = root / namespace
                    qpa = Path(f"{prefix}-{namespace}-cts.qpa")
                    self.assertEqual(cts_receipt_parts(qpa), (str(prefix), namespace))
                    args = ["--deqp-surface-width=64", "--deqp-surface-height=64",
                            "--deqp-base-seed=1", "--deqp-surface-type=pbuffer",
                            f"--deqp-log-filename=/download0/{namespace}-cts.qpa"]
                    inputs = {"-cts-args.txt": "\n".join(args) + "\n",
                              "-cts-shard.txt": "KHR-GL33.a\n",
                              f"-{namespace}-cts.qpa": '#sessionInfo commandLineParameters "' + " ".join(args) +
                                  '"\n#sessionInfo releaseName fixture\n#beginTestCaseResult KHR-GL33.a\n'
                                  '<TestCaseResult><Result StatusCode="Pass">Pass</Result></TestCaseResult>\n'
                                  '#endTestCaseResult\n#endSession\n',
                              f"-{namespace}-cts.status": "state=passed complete=1 executed=1 passed=1 "
                                  "not_supported=0 failed=0 warnings=0 waived=0 device_lost=0\n",
                              "-klog.log": f"[{namespace}-cts] finished\n",
                              f"-{namespace}.log": f"[{namespace}-cts] starting GL33 CTS runner\n"
                                  f"[{namespace}-cts] finished state=passed executed=1 failed=0 device_lost=0\n",
                              "-result.json": json.dumps(dict(titleId="PPSA99005", ebootSha256="a"*64,
                                  libcSha256="b"*64, outcome="entered-eboot", teardownSignal="runtime-layers-released"))}
                    for suffix, text in inputs.items():
                        Path(str(prefix) + suffix).write_text(text)
                    Path(str(prefix) + "-runner.json").write_text(json.dumps(dict(
                        postHealthChecked=True, lockReleased=True,
                        argumentsSha256=CTS.digest(Path(str(prefix) + "-cts-args.txt")),
                        caseListSha256=CTS.digest(Path(str(prefix) + "-cts-shard.txt")))))
                    manifest = dict(receipts=[qpa.name], eboot_sha256="a"*64, libc_sha256="b"*64,
                                    cts_commit="fixture", not_supported={}, expected_render_targets={})
                    before = {p: CTS.digest(p) for p in root.iterdir()}
                    report = CTS.audit(root, manifest, ["KHR-GL33.a"])
                    self.assertEqual(report["configurations"]["0"]["counts"], {"Pass": 1})
                    self.assertFalse(report["complete"])
                    self.assertEqual(before, {p: CTS.digest(p) for p in root.iterdir()})
                    original = qpa.read_text()
                    release = "opengl-cts-4.6.8.1-0-g" + "a" * 40
                    qpa.write_text(original.replace("releaseName fixture", "releaseName " + release))
                    tagged = dict(manifest, cts_commit="a" * 40, cts_release_name=release)
                    self.assertEqual(CTS.audit(root, tagged, ["KHR-GL33.a"])["clean_cycles"], 1)
                    with self.assertRaises(ValueError):
                        CTS.audit(root, dict(tagged, cts_commit="b" * 40), ["KHR-GL33.a"])
                    with self.assertRaises(ValueError):
                        CTS.audit(root, manifest, ["KHR-GL33.a"])
                    qpa.write_text(original)
                    # Never borrow a sibling from the other namespace.
                    status = Path(f"{prefix}-{namespace}-cts.status")
                    status.rename(Path(f"{prefix}-wrong-cts.status"))
                    with self.assertRaises(FileNotFoundError):
                        CTS.audit(root, manifest, ["KHR-GL33.a"])
            with self.assertRaises(ValueError):
                cts_receipt_parts(root / "unrelated.qpa")
