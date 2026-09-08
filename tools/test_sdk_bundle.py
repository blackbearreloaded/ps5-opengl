# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Bundles must never inherit acceptance from another binary or incomplete run."""
import copy
import importlib
import json
from pathlib import Path
import tarfile
from tempfile import TemporaryDirectory
import unittest
from unittest import mock

BUNDLE = importlib.import_module("build-sdk-bundle")


class SDLBundleTests(unittest.TestCase):
    def setUp(self):
        temporary = TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.native, self.sdk = self.root / "native", self.root / "graphics"
        self.sdk.mkdir()
        self.native.mkdir()
        self.installed = {
            "include/SDL2/SDL.h": b"header", "lib/libSDL2.a": b"!<arch>\n",
            "lib/pkgconfig/sdl2.pc": b"prefix=${pcfiledir}/../..\n",
            "lib/cmake/SDL2/SDL2Config.cmake": b"# relative metadata\n",
            "share/licenses/SDL2/LICENSE.txt": b"SDL license",
            "share/licenses/SDL2-PS5/LICENSE": b"integration license",
        }
        for name, data in self.installed.items():
            path = self.native / "sdk" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (self.native / "integration").mkdir()
        (self.native / "integration/build.py").write_bytes(b"# archived source; never executed\n")
        (self.native / "sdl-source.tar").write_bytes(b"source archive test double")
        self.receipt = dict(
            schema_version=1, mode="native", hardware_run=False,
            sdl_commit="8c56053f13ca13a0c050de613706ff69eb615836",
            sdl_source_tar_sha256=BUNDLE.digest(self.native / "sdl-source.tar"),
            sdk_manifest_sha256="a" * 64, sdk_runtime_sha256="b" * 64,
            integration_inputs={"build.py": BUNDLE.digest(self.native / "integration/build.py")},
            receipt_tool_sha256=BUNDLE.digest(self.native / "integration/build.py"),
            artifacts={"cmake/sdl/libSDL2.a": BUNDLE.digest(self.native / "sdk/lib/libSDL2.a")})
        payload = dict(self.receipt, artifacts={name: BUNDLE.digest(self.native / "sdk" / name)
                                              for name in self.installed})
        payload_path = self.native / "sdk/share/SDL2/receipt.json"
        payload_path.parent.mkdir(parents=True)
        payload_path.write_text(json.dumps(payload))
        self.receipt["payload_receipt_sha256"] = BUNDLE.digest(payload_path)
        manifest = dict(payload["artifacts"], **{
            "share/SDL2/receipt.json": self.receipt["payload_receipt_sha256"]})
        (self.native / "sdk/manifest.sha256").write_text("".join(
            f"{checksum}  {name}\n" for name, checksum in sorted(manifest.items())))
        self.receipt["payload_manifest_sha256"] = BUNDLE.digest(self.native / "sdk/manifest.sha256")
        self.write_receipt()
        # The shared SDL verifier owns source, manifest and artifact validation.
        # These tests isolate the bundler's gates/copying from that separate suite.
        self.builder = mock.Mock()
        self.builder.verify_native_build.side_effect = lambda native, sdk: copy.deepcopy(self.receipt)
        patcher = mock.patch.object(BUNDLE, "sdl_builder", return_value=self.builder)
        patcher.start()
        self.addCleanup(patcher.stop)

    def write_receipt(self):
        (self.native / "receipt.json").write_text(json.dumps(self.receipt))

    def checked(self):
        return BUNDLE.verify_sdl(self.native, self.sdk, "a" * 64, "b" * 64)

    def stage(self, name):
        stage = self.root / name
        (stage / "sources").mkdir(parents=True)
        return stage

    def argv(self, destination, sdl=True):
        return ["build-sdk-bundle.py", "--sdk", str(self.sdk), "--source-commit", "c" * 40,
                "--destination", str(destination),
                *(["--sdl-build", str(self.native)] if sdl else [])]

    def test_sdk_pair_and_native_status_are_independent_packaging_gates(self):
        self.checked()
        self.builder.verify_native_build.assert_called_once_with(self.native, self.sdk)
        for key, bad in [("sdk_manifest_sha256", "d" * 64), ("sdk_runtime_sha256", "d" * 64),
                         ("schema_version", 2), ("mode", "host"), ("hardware_run", True)]:
            with self.subTest(key=key), mock.patch.dict(self.receipt, {key: bad}):
                self.write_receipt()
                with self.assertRaises(ValueError):
                    self.checked()

    def test_changed_receipts_are_rejected(self):
        with mock.patch.dict(self.receipt, {"sdk_files": 999}):
            with self.assertRaisesRegex(ValueError, "receipt changed"):
                self.checked()
        path = self.native / "sdk/share/SDL2/receipt.json"
        path.write_text(path.read_text() + " ")
        with self.assertRaisesRegex(ValueError, "payload receipt changed"):
            self.checked()

    def test_verifier_failure_precedes_any_output(self):
        destination = self.root / "bundle"
        self.builder.verify_native_build.side_effect = ValueError("invalid SDL input")
        with mock.patch("sys.argv", self.argv(destination)), \
                mock.patch.object(BUNDLE.CHECK, "verify_manifest", return_value={"sha256": "a" * 64}), \
                mock.patch.object(BUNDLE, "digest", return_value="b" * 64):
            with self.assertRaisesRegex(ValueError, "invalid SDL input"):
                BUNDLE.main()
        self.assertFalse(destination.exists())

    def test_sdl_overlap_and_symlink_are_rejected_before_verification(self):
        alias = self.root / "alias"
        alias.symlink_to(self.native, target_is_directory=True)
        for destination in (self.native / "bundles", alias / "bundles"):
            with mock.patch("sys.argv", self.argv(destination)):
                with self.assertRaisesRegex(ValueError, "outside the SDL build"):
                    BUNDLE.main()
            self.assertFalse(destination.exists())
        args = self.argv(self.root / "bundle")
        args[-1] = str(alias)
        with mock.patch("sys.argv", args):
            with self.assertRaisesRegex(ValueError, "symlink SDL"):
                BUNDLE.main()
        self.builder.verify_native_build.assert_not_called()

    def test_without_sdl_never_loads_its_verifier(self):
        with mock.patch("sys.argv", self.argv(self.root / "bundle", sdl=False)), \
                mock.patch.object(BUNDLE.CHECK, "verify_manifest", side_effect=ValueError("GL gate")):
            with self.assertRaisesRegex(ValueError, "GL gate"):
                BUNDLE.main()
        BUNDLE.sdl_builder.assert_not_called()

    def test_copy_is_separate_reproducible_and_has_no_hardware_acceptance(self):
        checked = self.checked()
        first, second = self.stage("first"), self.stage("second")
        (first / "sdk").mkdir()
        (first / "sdk/manifest.sha256").write_bytes(b"original graphics manifest\n")
        provenance = BUNDLE.copy_sdl(self.native, first, checked, "123")
        BUNDLE.copy_sdl(self.native, second, checked, "123")
        self.assertEqual((first / "sdk/manifest.sha256").read_bytes(), b"original graphics manifest\n")
        for path in (self.native / "sdk").rglob("*"):
            if path.is_file():
                self.assertEqual(path.read_bytes(), (first / "sdl2" / path.relative_to(self.native / "sdk")).read_bytes())
        for name in ("SDL2.tar", "SDL2-integration.tar"):
            self.assertEqual(BUNDLE.digest(first / "sources" / name), BUNDLE.digest(second / "sources" / name))
        with tarfile.open(first / "sources/SDL2-integration.tar") as archive:
            self.assertEqual(archive.getnames(), ["SDL2-integration/build.py"])
            self.assertEqual(archive.extractfile(archive.getmembers()[0]).read(),
                             (self.native / "integration/build.py").read_bytes())
        self.assertEqual(provenance["build_receipt_sha256"], BUNDLE.digest(first / provenance["build_receipt"]))
        self.assertEqual(provenance["payload_manifest_sha256"], BUNDLE.digest(first / "sdl2/manifest.sha256"))
        self.assertFalse(provenance["hardware_run"])
        self.assertEqual(provenance["mode"], "native")
        self.assertIn("no SDL hardware", provenance["validation"])

    def test_tampering_between_verification_and_copy_is_rejected(self):
        checked = self.checked()
        for index, name in enumerate([
                "sdk/lib/libSDL2.a", "sdk/manifest.sha256", "sdk/share/SDL2/receipt.json",
                "sdl-source.tar", "receipt.json", "integration/build.py"]):
            path = self.native / name
            original = path.read_bytes()
            path.write_bytes(original + b"tampered")
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, "SDL .*changed"):
                BUNDLE.copy_sdl(self.native, self.stage(f"tamper-{index}"), checked, "123")
            path.write_bytes(original)


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

    def test_frozen_versions_require_matching_sdk_and_own_receipts(self):
        profiles = [*BUNDLE.SAMPLES.values(), BUNDLE.TARGETED]
        for profile in profiles:
            BUNDLE.require_frozen_sdk(profile, profile["sdk"], profile["archive"])
            for other in profiles:
                for sdk, archive in [(other["sdk"], profile["archive"]),
                                     (profile["sdk"], other["archive"]),
                                     (other["sdk"], other["archive"])]:
                    # The targeted and sampled G19 packages intentionally share
                    # binaries, but never their distinct receipt manifests.
                    if (sdk, archive) == (profile["sdk"], profile["archive"]):
                        continue
                    with self.assertRaises(ValueError):
                        BUNDLE.require_frozen_sdk(profile, sdk, archive)
        self.assertEqual(len({p["candidate"] for p in profiles}), len(profiles))
        samples = list(BUNDLE.SAMPLES.values())
        for profile in samples:
            for other in profiles:
                if other is profile:
                    continue
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
