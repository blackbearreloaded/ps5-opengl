# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Schedule checks: full coverage, bounded batches, and no inherited passes."""
from collections import Counter
import contextlib
import importlib
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

PLANNER = importlib.import_module("plan-cts-campaign")


class CampaignTests(unittest.TestCase):
    def test_native_dependency_copy_resumes_without_nesting(self):
        script = (Path(__file__).parent / "build-native-cts-app.sh").read_text()
        start = script.index('if [[ ! -f "$app/.deps/native/.cts-copy-complete" ]]')
        block = script[start:script.index('\nfi', start) + 3]
        with tempfile.TemporaryDirectory() as tmp:
            app, template = Path(tmp) / "partial app", Path(tmp) / "template"
            native = app / ".deps/native"
            native.mkdir(parents=True)
            (native / "linker").write_text("partial")
            env = dict(os.environ, app=str(app), template=str(template))
            failed = subprocess.run(["bash", "-ec", block], env=env, capture_output=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertFalse((native / ".cts-copy-complete").exists())
            source = template / ".deps/native"
            source.mkdir(parents=True)
            (source / "linker").write_text("complete")
            (source / "header").write_text("required")
            subprocess.run(["bash", "-ec", block], env=env, check=True)
            self.assertEqual((native / "header").read_text(), "required")
            self.assertEqual((native / "linker").read_text(), "complete")
            self.assertTrue((native / ".cts-copy-complete").is_file())
            self.assertFalse((native / "native").exists())
            (native / "linker").write_text("stage-specific")
            subprocess.run(["bash", "-ec", block], env=env, check=True)
            self.assertEqual((native / "linker").read_text(), "stage-specific")

    def test_every_profile_selects_its_surface_explicitly(self):
        for config, surface in enumerate(("pbuffer", "pbuffer", "fbo", "fbo")):
            args = PLANNER.PREPARE.encode_arguments(config).decode().splitlines()
            self.assertEqual([arg for arg in args if arg.startswith("--deqp-surface-type=")],
                             [f"--deqp-surface-type={surface}"])
        root = Path(__file__).resolve().parent.parent
        self.assertIn("--deqp-surface-type=pbuffer",
                      (root / "conformance/vk-gl-cts/native/cts-args.txt").read_text().splitlines())

    def test_release_shard_uses_selected_inventory(self):
        with tempfile.TemporaryDirectory() as tmp:
            mustpass = Path(tmp) / "release.txt"
            mustpass.write_bytes(b"KHR-GL33.release.b\r\nKHR-GL33.release.a\r\n")
            argv = ["prepare", "--mustpass", str(mustpass), "--count", "2", "--dry-run"]
            output = io.StringIO()
            with patch("sys.argv", argv), contextlib.redirect_stdout(output):
                self.assertEqual(PLANNER.PREPARE.main(), 0)
            self.assertIn("first=KHR-GL33.release.b", output.getvalue())
            self.assertIn("last=KHR-GL33.release.a", output.getvalue())
            self.assertIn("mustpass_sha256=" + PLANNER.hashlib.sha256(mustpass.read_bytes()).hexdigest(), output.getvalue())
            mustpass.write_text("KHR-GL33.release.a\nKHR-GL33.release.a\n")
            with patch("sys.argv", argv), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                PLANNER.PREPARE.main()

    def test_coverage_budgets_order_and_config_specific_timings(self):
        cases = ["KHR-GL33.info.version", "KHR-GL33.a", "KHR-GL33.b", "KHR-GL33.slow"]
        history = {str(c): {name: dict(status="Pass", seconds=1) for name in cases} for c in range(4)}
        history["2"][cases[-1]]["seconds"] = 8000
        rows = PLANNER.plan(cases, history, cases[:1])
        self.assertEqual(rows, PLANNER.plan(cases, history, cases[:1]))
        self.assertEqual([r["configuration"] for r in rows[:4]], list(range(4)))
        for config in range(4):
            self.assertEqual(Counter(n for r in rows if r["configuration"] == config for n in r["cases"]), Counter(cases))
        long = [r for r in rows if r["needs_duration_approval"]]
        self.assertEqual(len(long), 1)
        self.assertEqual(long[0]["cases"], cases[-1:])
        self.assertTrue(long[0]["exceeds_runner_limit"])
        self.assertTrue(all(r["observation_seconds"] <= 120 for r in rows if r["lane"] != "long"))

    def test_unknown_and_failed_timings_do_not_become_fast_passes(self):
        rows = PLANNER.plan(["a", "b", "c"], {"0": {"a": dict(status="Fail", seconds=0)}}, [])
        row = next(r for r in rows if r["lane"] == "previous-failure")
        self.assertEqual((row["cases"], row["estimated_test_seconds"], row["unknown_timings"]), (["a"], 30, 1))
        self.assertTrue(all(r["estimated_test_seconds"] == 30 * len(r["cases"]) for r in rows))

    def test_rejects_bad_inputs(self):
        for value in (-1, float("nan"), float("inf"), True, "1"):
            with self.assertRaises(ValueError):
                PLANNER.plan(["a"], {"0": {"a": dict(status="Pass", seconds=value)}}, [])
        for cases, smoke in (([], []), (["a", "a"], []), (["a"], ["typo"])):
            with self.assertRaises(ValueError):
                PLANNER.plan(cases, {}, smoke)
        for options in (dict(budget=0), dict(startup=120), dict(margin=.5), dict(unknown=0)):
            with self.assertRaises(ValueError):
                PLANNER.plan(["a"], {}, [], **options)

    def test_export_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            cases = root / "mustpass.txt"
            cases.write_text("KHR-GL33.info.version\nKHR-GL33.a\n")
            (root / "tests/ps5").mkdir(parents=True)
            (root / "tests/ps5/cts-regressions.json").write_text(json.dumps(dict(smoke=["*.info.*"])))
            (root / "dependencies.json").write_text(json.dumps(dict(repositories={"VK-GL-CTS": dict(revision="a"*40)})))
            timings = root / "timings.json"
            timings.write_text(json.dumps(dict(scope="development-history-not-release-certification", cases={})))
            output = root / "campaign"
            argv = ["plan", "--timings", str(timings), "--output", str(output)]
            with patch.object(PLANNER, "ROOT", root), patch.object(PLANNER, "MUSTPASS", cases), patch("sys.argv", argv):
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(PLANNER.main(), 0)
                plan = json.loads((output / "plan.json").read_text())
                self.assertEqual(plan["completed_executions"], 0)
                self.assertEqual(plan["planned_executions"], 8)
                self.assertFalse(plan["ready_for_console"])
                for row in plan["shards"]:
                    for name, checksum in row["input_sha256"].items():
                        data = (output / "shards" / row["id"] / name).read_bytes()
                        self.assertEqual(PLANNER.hashlib.sha256(data).hexdigest(), checksum)
                    self.assertEqual((output / "shards" / row["id"] / "cts-args.txt").read_bytes(),
                                     PLANNER.PREPARE.encode_arguments(row["configuration"]))
                before = (output / "plan.json").read_bytes()
                with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    PLANNER.main()
                self.assertEqual((output / "plan.json").read_bytes(), before)
