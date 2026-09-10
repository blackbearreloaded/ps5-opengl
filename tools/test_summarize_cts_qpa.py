#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Tests for strict GL CTS QPA receipt validation."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import runpy
from opengl_receipts import LEGACY_NAME


ROOT = Path(__file__).resolve().parent
PARSER = ROOT / "summarize-cts-qpa.py"
SUMMARY = runpy.run_path(str(PARSER))
SELECTOR = runpy.run_path(str(ROOT / "prepare-cts-shard.py"))


def qpa(cases: list[tuple[str, str]], newline: str = "\n", end: bool = True) -> str:
    blocks = [
        newline.join(
            (
                f"#beginTestCaseResult {name}",
                f'<Result StatusCode="{status}"/>',
                "#endTestCaseResult",
            )
        )
        for name, status in cases
    ]
    if end:
        blocks.append("#endSession")
    return newline.join(blocks) + newline


class QpaSummaryTest(unittest.TestCase):
    def run_parser(
        self, text: str, expected: list[str] | None = None
    ) -> tuple[subprocess.CompletedProcess[str], dict[str, object]]:
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            receipt = work / "receipt.qpa"
            receipt.write_bytes(text.encode())
            command = ["python3", str(PARSER), str(receipt), "--json"]
            if expected is not None:
                case_list = work / "cases.txt"
                case_list.write_text("\n".join(expected) + "\n")
                command.extend(("--expected-list", str(case_list)))
            result = subprocess.run(command, text=True, capture_output=True)
            return result, json.loads(result.stdout)

    def test_accepts_complete_ordered_lf_receipt(self) -> None:
        result, summary = self.run_parser(
            qpa([("KHR-GL33.a", "Pass"), ("KHR-GL33.b", "NotSupported")]),
            ["KHR-GL33.a", "KHR-GL33.b"],
        )
        self.assertEqual(result.returncode, 0)
        self.assertTrue(summary["complete"])
        self.assertEqual(summary["executed"], 2)

    def test_accepts_crlf_receipt(self) -> None:
        result, summary = self.run_parser(
            qpa([("KHR-GL33.a", "Pass")], newline="\r\n"), ["KHR-GL33.a"]
        )
        self.assertEqual(result.returncode, 0)
        self.assertTrue(summary["complete"])

    def test_rejects_incomplete_receipt(self) -> None:
        result, summary = self.run_parser(
            qpa([("KHR-GL33.a", "Pass")], end=False), ["KHR-GL33.a"]
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(summary["complete"])

    def test_rejects_reordered_receipt(self) -> None:
        result, summary = self.run_parser(
            qpa([("KHR-GL33.b", "Pass"), ("KHR-GL33.a", "Pass")]),
            ["KHR-GL33.a", "KHR-GL33.b"],
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(summary["complete"])

    def test_rejects_unknown_status(self) -> None:
        result, summary = self.run_parser(
            qpa([("KHR-GL33.a", "FutureStatus")]), ["KHR-GL33.a"]
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(summary["failed"], ["KHR-GL33.a"])

    def test_timing_and_duplicate_guard(self) -> None:
        text = qpa([("KHR-GL33.a", "Pass")]).replace(
            '<Result', '<Number Name="TestDuration" Unit="us">1250000</Number>\n<Result')
        self.assertEqual(SUMMARY["summarize"](text)["seconds"], 1.25)
        self.assertFalse(SUMMARY["summarize"](qpa([
            ("KHR-GL33.a", "Pass"), ("KHR-GL33.a", "Pass")]))["complete"])
        self.assertFalse(SUMMARY["summarize"]("#endSession\n")["complete"])

    def test_named_selection_and_timed_prefix(self) -> None:
        names = ["KHR-GL33.a.one", "KHR-GL33.a.two", "KHR-GL33.b.one"]
        self.assertEqual(SELECTOR["select_cases"](names, ["*.a.*", "*.a.one"]), names[:2])
        with self.assertRaises(ValueError):
            SELECTOR["select_cases"](names, ["typo"])
        selected, seconds = SELECTOR["timed_prefix"](names, {names[0]: 3, names[1]: 10}, 5)
        self.assertEqual((selected, seconds), (names[:1], 3))
        self.assertEqual(SELECTOR["timed_prefix"](names, {}, 5), (names[:1], 30))
        with self.assertRaises(ValueError):
            SELECTOR["timed_prefix"](names, {names[0]: float("nan")}, 5)

    def test_inventory_keeps_build_configuration_and_failure_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            command = ('#sessionInfo commandLineParameters "--deqp-surface-width=64 '
                       '--deqp-surface-height=64 --deqp-base-seed=1"\n')
            for index, status in enumerate(("Pass", "Fail")):
                prefix = work / f"PPSA99005-{index}"
                text = qpa([("KHR-GL33.a", status), ("KHR-GL33.optional", "NotSupported")]).replace(
                    '<Result', '<Number Name="TestDuration" Unit="us">2000</Number>\n<Result')
                namespace = LEGACY_NAME if index == 0 else "ps5-opengl"
                selected_command = command if index == 0 else command.replace(
                    '--deqp-base-seed=1', '--deqp-base-seed=1 --deqp-surface-type=pbuffer')
                Path(f"{prefix}-{namespace}-cts.qpa").write_text(selected_command + text)
                Path(f"{prefix}-cts-shard.txt").write_text("KHR-GL33.a\nKHR-GL33.optional\n")
                Path(f"{prefix}-result.json").write_text(json.dumps(dict(
                    ebootSha256=str(index)*64, outcome="entered-eboot",
                    teardownSignal="runtime-layers-released")))
            output = work / "ledger.json"
            result = subprocess.run(["python3", str(PARSER), "--inventory", str(work),
                                     "--output", str(output), "--current-eboot-sha256", "1"*64],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            ledger = json.loads(output.read_text())
            self.assertEqual(len(ledger["receipts"]), 2)
            case = ledger["cases"]["0"]["KHR-GL33.a"]
            self.assertEqual(case["status"], "Fail")
            self.assertEqual(ledger["timings"], {"KHR-GL33.optional": .002})
            self.assertEqual(ledger["cases"]["0"]["KHR-GL33.optional"]["status"], "NotSupported")
            self.assertTrue(ledger["receipts"][case["receipt"]]["current_binary"])
            self.assertIsNone(ledger["receipts"][case["receipt"]]["post_health"])
            self.assertEqual({row["requested_surface"] for row in ledger["receipts"].values()},
                             {"default", "pbuffer"})


if __name__ == "__main__":
    unittest.main()
