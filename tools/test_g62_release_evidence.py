# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import copy
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
import g62_release_evidence as E


class G62EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name) / "index.json"
        self.profile = dict(runtime="a" * 40, sdk="b" * 64, archive="c" * 64,
            sdl_source="d" * 40, sdl_receipt="e" * 64,
            display=dict(width=3840, height=2160, fps=120), qualification="native-sampled")
        self.record = dict(format="ps5-opengl-g62-release-v1", inherited_acceptance=False,
            qualification="native-sampled", runtime_source_commit=self.profile["runtime"],
            sdk_manifest_sha256=self.profile["sdk"], runtime_archive_sha256=self.profile["archive"],
            sdl_source_companion=self.profile["sdl_source"], sdl_build_receipt_sha256=self.profile["sdl_receipt"],
            display_profile=self.profile["display"], build_provenance_sha256="f" * 64,
            consumer_report_sha256="0" * 64, runs={name: {} for name in set(E.GATES) | {"cts"}})

    def freeze(self, record=None):
        self.path.write_text(json.dumps(self.record if record is None else record))
        self.profile["evidence"] = E.digest(self.path)

    def test_complete_index_and_tampering(self):
        self.freeze()
        self.assertEqual(E.load_evidence(self.path, self.profile), self.record)
        self.path.write_text(self.path.read_text() + " ")
        with self.assertRaises(ValueError):
            E.load_evidence(self.path, self.profile)

    def test_incomplete_or_wrong_identity_rejected(self):
        for change in (lambda r: r["runs"].pop("cts"), lambda r: r["runs"].pop("lifecycle-2"),
                       lambda r: r.update(inherited_acceptance=True),
                       lambda r: r.update(runtime_archive_sha256="1" * 64),
                       lambda r: r.update(qualification="host-only")):
            bad = copy.deepcopy(self.record)
            change(bad)
            self.freeze(bad)
            with self.assertRaises(ValueError):
                E.load_evidence(self.path, self.profile)

    def test_host_only_cannot_inherit_console_results(self):
        self.profile["qualification"] = self.record["qualification"] = "host-only"
        self.freeze()
        with self.assertRaises(ValueError):
            E.load_evidence(self.path, self.profile)
        self.record["runs"] = {}
        self.freeze()
        self.assertEqual(E.load_evidence(self.path, self.profile)["runs"], {})

    def test_unfrozen_and_symlink_rejected(self):
        self.freeze()
        for field in ("runtime", "sdk", "archive", "sdl_receipt", "sdl_source", "evidence"):
            profile = dict(self.profile, **{field: None})
            with self.assertRaises(ValueError):
                E.load_evidence(self.path, profile)
        link = self.path.with_name("link.json")
        link.symlink_to(self.path)
        with self.assertRaises(ValueError):
            E.load_evidence(link, self.profile)

    def test_gpu_completion_count_and_driver_failure(self):
        text = "[pss-opengl-native] gate completed status=0\n"
        accepted = dict(classification="pass", workload=dict(cases=[{}] * 8))
        self.assertEqual(E.workload("gpu-mipmap", text, self.profile["display"], accepted, {}), accepted["workload"])
        for bad in ("", text + text, text.replace("status=0", "status=1"), text + "[ps5-gallium] draw-rejected\n"):
            with self.assertRaises(ValueError):
                E.workload("gpu-mipmap", bad, self.profile["display"], accepted, {})
        accepted["workload"]["cases"].pop()
        with self.assertRaises(ValueError):
            E.workload("gpu-mipmap", text, self.profile["display"], accepted, {})


if __name__ == "__main__":
    unittest.main()
