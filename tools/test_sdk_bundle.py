# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Bundles must never inherit acceptance from another binary or incomplete run."""
import copy
import importlib
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

BUNDLE = importlib.import_module("build-sdk-bundle")


class SampleGateTests(unittest.TestCase):
    def test_destination_inside_sdk_is_rejected_before_copying(self):
        with TemporaryDirectory() as temporary:
            sdk = Path(temporary) / "sdk"
            sdk.mkdir()
            alias = Path(temporary) / "alias"
            alias.symlink_to(sdk, target_is_directory=True)
            for destination in (sdk / "bundles", alias / "bundles"):
                with mock.patch("sys.argv", ["build-sdk-bundle.py", "--sdk", str(sdk),
                        "--source-commit", "a" * 40, "--destination", str(destination)]):
                    with self.assertRaisesRegex(ValueError, "outside the SDK"):
                        BUNDLE.main()
                self.assertFalse(destination.exists())

    def test_frozen_versions_cannot_share_sdk_acceptance(self):
        profiles = [*BUNDLE.SAMPLES.values(), BUNDLE.TARGETED]
        for index, profile in enumerate(profiles):
            BUNDLE.require_frozen_sdk(profile, profile["sdk"], profile["archive"])
            other = profiles[(index + 1) % len(profiles)]
            for sdk, archive in [(other["sdk"], profile["archive"]),
                                 (profile["sdk"], other["archive"]),
                                 (other["sdk"], other["archive"])]:
                with self.assertRaises(ValueError):
                    BUNDLE.require_frozen_sdk(profile, sdk, archive)
        self.assertNotEqual(profiles[0]["candidate"], profiles[1]["candidate"])
        samples = list(BUNDLE.SAMPLES.values())
        for profile, other in (samples, samples[::-1]):
            with mock.patch.object(BUNDLE, "digest", return_value=other["candidate"]):
                with self.assertRaises(ValueError):
                    BUNDLE.sample_report(None, None, None, profile)

    def test_bundle_modes_are_exclusive(self):
        modes = [("--ci-version", "local"), ("--sample-version", BUNDLE.VERSION),
                 ("--targeted-version", BUNDLE.TARGETED_VERSION)]
        for index, mode in enumerate(modes):
            with mock.patch("sys.argv", ["build-sdk-bundle.py", "--sdk", "unused",
                    "--source-commit", "a" * 40, "--destination", "unused",
                    *mode, *modes[(index + 1) % len(modes)]]):
                with self.assertRaisesRegex(ValueError, "mutually exclusive"):
                    BUNDLE.main()

    def test_targeted_batch_requires_identity_completion_and_cleanup(self):
        # Existing transfer/memory auditors have their own numerical regressions.
        # Here exercise the packaging gate without requiring private console logs.
        manifest = dict(title="test", eboot_sha256="eboot", libc_sha256="libc",
                        source_commit=BUNDLE.TARGETED["runtime"], gate="batch", host="test",
                        protocol_commit="protocol")
        lifecycle = dict(titleId="test", ebootSha256="EBOOT", libcSha256="LIBC",
                         outcome="entered-eboot", teardownSignal="runtime-layers-released")
        runner = dict(checkoutCommit=manifest["source_commit"], gate="batch", ps5Host="test",
                      protocolCommit="protocol", postHealthChecked=True, lockReleased=True)
        log = "\n".join([f"[ps5-egl-render-blit] case={i} result=0" for i in range(17)] + [
            "[ps5-egl-render-blit] conversion=R16F-RGBA32F error=0x0 result=0",
            "[ps5-egl-render-blit] matching=18 result=0",
            "[ps5-egl-render-blit] cleanup=1 result=0",
            "[ps5-egl-transfer-regressions] gates=2 result=0",
            "[pss-opengl-native] gate completed status=0"])
        heap = dict(post_session_growth_bytes=0)
        gpu = {kind: [dict(begin_bytes=0, end_bytes=0, end_blocks=0)] * 2
               for kind in ("direct", "mapped")}
        memory = importlib.import_module("summarize-app-heap")
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = root / "candidate.json"
            candidate.write_text(json.dumps(manifest))
            paths = {suffix: root / (BUNDLE.TARGETED["receipt"] + suffix)
                     for suffix in BUNDLE.TARGETED["raw_sha256"]}
            payloads = {"-opengl.log": log, "-klog.log": "lifecycle",
                        "-result.json": json.dumps(lifecycle), "-runner.json": json.dumps(runner)}
            for suffix, text in payloads.items():
                paths[suffix].write_text(text)
            pins = dict(candidate=BUNDLE.digest(candidate),
                        raw_sha256={suffix: BUNDLE.digest(path) for suffix, path in paths.items()})
            with mock.patch.dict(BUNDLE.TARGETED, pins), \
                    mock.patch("test_transfer_workload.check_receipts"), \
                    mock.patch.object(memory, "summarize", return_value=heap), \
                    mock.patch.object(memory, "summarize_gpu", return_value=gpu):
                report = BUNDLE.targeted_report(candidate, root)
                self.assertFalse(report["full_matrix_complete"])
                self.assertEqual(report["groups"]["format_checks"]["executed"], 18)
                candidate.write_text("{}")
                with self.assertRaisesRegex(ValueError, "frozen targeted candidate"):
                    BUNDLE.targeted_report(candidate, root)
                candidate.write_text(json.dumps(manifest))
                for suffix, original in payloads.items():
                    paths[suffix].write_text(original + "changed")
                    with self.assertRaisesRegex(ValueError, "receipt changed"):
                        BUNDLE.targeted_report(candidate, root)
                    paths[suffix].write_text(original)
                for suffix, bad in [
                        ("-opengl.log", log.replace("matching=18", "matching=17")),
                        ("-opengl.log", log.replace("gates=2 result=0", "gates=2 result=1")),
                        ("-result.json", json.dumps(dict(lifecycle, teardownSignal="timeout"))),
                        ("-result.json", json.dumps(dict(lifecycle, ebootSha256="other"))),
                        ("-runner.json", json.dumps(dict(runner, lockReleased=False))),
                        ("-runner.json", json.dumps(dict(runner, postHealthChecked=False)))]:
                    paths[suffix].write_text(bad)
                    with mock.patch.dict(BUNDLE.TARGETED["raw_sha256"],
                                         {suffix: BUNDLE.digest(paths[suffix])}):
                        with self.assertRaises(ValueError):
                            BUNDLE.targeted_report(candidate, root)
                    paths[suffix].write_text(payloads[suffix])
                for counters, key in [(heap, "post_session_growth_bytes"),
                                      (gpu["mapped"][0], "end_bytes")]:
                    with mock.patch.dict(counters, {key: 1}):
                        with self.assertRaisesRegex(ValueError, "memory acceptance"):
                            BUNDLE.targeted_report(candidate, root)

    def test_ci_requires_matching_complete_host_checks(self):
        report = dict(status="PASS", manifest={"sha256": "sdk-hash"},
                      consumers=dict.fromkeys(("make", "pkgconfig", "cmake"), {}),
                      gl33=dict(commands=344, exported=344),
                      outputs=dict.fromkeys(("make.elf", "pkgconfig.elf", "cmake.elf"), "hash"))
        BUNDLE.require_consumers(report, "sdk-hash")
        mutations = [lambda r: r.update(status="FAIL"),
                     lambda r: r["manifest"].update(sha256="other-sdk"),
                     lambda r: r["consumers"].pop("cmake"),
                     lambda r: r["gl33"].update(exported=343),
                     lambda r: r["outputs"].pop("make.elf")]
        for mutate in mutations:
            bad = copy.deepcopy(report)
            mutate(bad)
            with self.assertRaises(ValueError):
                BUNDLE.require_consumers(bad, "sdk-hash")

    def test_accept_only_four_all_pass_samples(self):
        report = dict(complete=False, clean_cycles=4,
                      render_targets={str(i): {} for i in range(4)},
                      configurations={str(i): dict(executed=51, counts={"Pass": 51})
                                      for i in range(4)})
        BUNDLE.require_sample(report)
        mutations = [
            lambda r: r.update(complete=True),
            lambda r: r.update(clean_cycles=3),
            lambda r: r["configurations"].pop("3"),
            lambda r: r["render_targets"].pop("3"),
            lambda r: r["configurations"]["0"].update(executed=50),
            lambda r: r["configurations"]["0"].update(counts={"Pass": 50, "NotSupported": 1}),
            lambda r: r["configurations"]["0"].update(counts={"Pass": 50, "Fail": 1}),
        ]
        for mutate in mutations:
            bad = copy.deepcopy(report)
            mutate(bad)
            with self.assertRaises(ValueError):
                BUNDLE.require_sample(bad)


if __name__ == "__main__":
    unittest.main()
