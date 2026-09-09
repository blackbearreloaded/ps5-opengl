# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Bundles must never inherit acceptance from another binary or incomplete run."""
import copy
import importlib
import io
import json
from pathlib import Path
import shutil
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

    def test_named_frozen_sdl_releases_cannot_omit_or_substitute_sdl(self):
        version = "0.1.0-perf20260908-g25-sdl2-sampled"
        destination = self.root / "frozen-g25"
        for mode in (["--sample-version", version], ["--hfr-profile", "1440p120"], ["--hfr-profile", "2160p120"],
                     ["--g47-profile", "1440p120", "--derivative-provenance", "unused"],
                     ["--g47-profile", "2160p120", "--derivative-provenance", "unused"]):
            with mock.patch("sys.argv", self.argv(destination, sdl=False) + mode):
                with self.assertRaisesRegex(ValueError, "requires its accepted SDL build"):
                    BUNDLE.main()
            with mock.patch("sys.argv", self.argv(destination) + mode), \
                    mock.patch.object(BUNDLE.CHECK, "verify_manifest", return_value={"sha256": "a" * 64}), \
                    mock.patch.object(BUNDLE, "digest", return_value="b" * 64), \
                    mock.patch.object(BUNDLE, "verify_sdl", return_value=({}, "changed", {})):
                with self.assertRaisesRegex(ValueError, "SDL differs from the frozen release receipt"):
                    BUNDLE.main()
            self.assertFalse(destination.exists())

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

    def test_tar_gzip_extraction_integrity_and_relocation(self):
        stage = self.stage("archive-a/bundle")
        BUNDLE.copy_sdl(self.native, stage, self.checked(), "123")
        BUNDLE.write_json(stage / "provenance.json", dict(scope="format fixture; no hardware evidence"))
        second = self.root / "archive-b/bundle"
        shutil.copytree(stage, second)
        first_archive, count = BUNDLE.archive_bundle(stage, "123")
        second_archive, _ = BUNDLE.archive_bundle(second, "123")
        self.assertEqual(BUNDLE.digest(first_archive), BUNDLE.digest(second_archive))
        self.assertEqual(Path(str(first_archive) + ".sha256").read_text(),
                         f"{BUNDLE.digest(first_archive)}  bundle.tar.gz\n")
        relocated = self.root / "relocated"
        with tarfile.open(first_archive) as archive:
            self.assertTrue(all(m.uid == m.gid == 0 and m.mtime == 123 for m in archive))
            archive.extractall(relocated, filter="data")
        extracted = relocated / "bundle"
        for manifest, root in ((extracted / "SHA256SUMS", extracted),
                               (extracted / "sdl2/manifest.sha256", extracted / "sdl2")):
            for line in manifest.read_text().splitlines():
                checksum, name = line.split("  ", 1)
                self.assertEqual(BUNDLE.digest(root / name), checksum)
        self.assertEqual(len((extracted / "SHA256SUMS").read_text().splitlines()), count)
        payload = extracted / "sdl2/lib/libSDL2.a"
        original = BUNDLE.digest(payload)
        payload.write_bytes(payload.read_bytes() + b"tampered")
        self.assertNotEqual(BUNDLE.digest(payload), original)
        with self.assertRaises(FileExistsError):
            BUNDLE.archive_bundle(stage, "123")

    def test_current_sdl_instructions_are_extracted_only_with_option(self):
        current = {"docs/consumer-build.md": b"See ../integration/SDL2/README.md\n",
                   "examples/main.c": b"/* current example */\n",
                   "integration/SDL2/README.md": b"Current SDL consumer instructions\n",
                   "integration/SDL2/build.py": b"# current companion, distinct from frozen build\n",
                   "integration/other/README.md": b"Not selected\n"}

        def snapshot(repo, revision, name, destination):
            with tarfile.open(destination, "w") as archive:
                for relative, data in current.items():
                    member = tarfile.TarInfo(name + "/" + relative)
                    member.size = len(data)
                    archive.addfile(member, io.BytesIO(data))

        digest = BUNDLE.digest
        for enabled in (False, True):
            destination = self.root / f"instructions-{enabled}"
            stage = destination / ("ps5-opengl-sdk-" + BUNDLE.VERSION)
            with mock.patch("sys.argv", self.argv(destination, sdl=enabled) +
                            ["--candidate", "unused", "--results", "unused"]), \
                    mock.patch.object(BUNDLE.CHECK, "verify_manifest", return_value={"sha256": "a" * 64}), \
                    mock.patch.object(BUNDLE, "digest", side_effect=lambda p:
                                      "b" * 64 if p == self.sdk / "lib/libps5_opengl_core33.a" else digest(p)), \
                    mock.patch.object(BUNDLE, "require_frozen_sdk"), \
                    mock.patch.object(BUNDLE, "sample_report", return_value={}), \
                    mock.patch.object(BUNDLE, "snapshot", side_effect=snapshot), \
                    mock.patch.object(BUNDLE.subprocess, "run"), \
                    mock.patch.object(BUNDLE.subprocess, "check_output", return_value="123"):
                # Stop after extraction/README: the fixture omits dependencies.json.
                # No runtime build or final release archive is needed for this check.
                with self.assertRaises(FileNotFoundError) as stopped:
                    BUNDLE.main()
                self.assertEqual(Path(stopped.exception.filename), stage / "dependencies.json")
            for relative, data in current.items():
                included = relative.startswith(("docs/", "examples/")) or \
                    enabled and relative.startswith("integration/SDL2/")
                self.assertEqual((stage / relative).exists(), included)
                if included:
                    self.assertEqual((stage / relative).read_bytes(), data)
            if enabled:
                with tarfile.open(stage / "sources/SDL2-integration.tar") as archive:
                    self.assertEqual(archive.extractfile("SDL2-integration/build.py").read(),
                                     (self.native / "integration/build.py").read_bytes())


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
                 ("--targeted-version", BUNDLE.TARGETED_VERSION), ("--hfr-profile", "1440p120"),
                 ("--g47-profile", "1440p120")]
        for index, mode in enumerate(modes):
            for other in modes[index + 1:]:
                with mock.patch("sys.argv", ["build-sdk-bundle.py", "--sdk", "unused",
                        "--source-commit", "a" * 40, "--destination", "unused", *mode, *other]):
                    with self.assertRaisesRegex(ValueError, "mutually exclusive"):
                        BUNDLE.main()

    def test_derivative_contract_argument_is_exclusive_and_required(self):
        with TemporaryDirectory() as temporary:
            destination = Path(temporary) / "must-not-be-created"
            for mode in (["--derivative-provenance", "unused"], ["--g47-profile", "2160p120"]):
                with mock.patch("sys.argv", ["build-sdk-bundle.py", "--sdk", "unused",
                        "--source-commit", "a" * 40, "--destination", str(destination), *mode]), \
                        self.assertRaisesRegex(ValueError, "requires --derivative-provenance"):
                    BUNDLE.main()
                self.assertFalse(destination.exists())

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


class HFRBundleTests(unittest.TestCase):
    def setUp(self):
        temporary = TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.profile = copy.deepcopy(BUNDLE.HFR["1440p120"])
        self.sdl = (dict(display_profile=self.profile["display"],
                         sdk_manifest_sha256=self.profile["sdk"], sdk_runtime_sha256=self.profile["archive"],
                         payload_manifest_sha256="d" * 64), self.profile["sdl_receipt"], {})
        self.paths, self.records = {}, {}
        for kind in ("imgui", "sdl"):
            record_path = self.root / self.profile[kind + "_record"]
            record_path.parent.mkdir(parents=True)
            self.paths[kind] = {key: record_path.parent / ("PPSA99005-test" + suffix)
                               for key, suffix in (("app", "-opengl.log"), ("klog", "-klog.log"),
                                                   ("cycle", "-result.json"), ("runner", "-runner.json"))}
            app = "\n".join([
                "[sdl2-g19] GL=3.3 (Core Profile) Mesa 26.2.0 drawable=2560x1440 nominal_refresh=120Hz (not negotiated HDMI)",
                "[sdl2-g19] probe frame=0 rgba=0,38,102,255 expected=0,38,102,255 pass=1",
                "[sdl2-g19] probe frame=179 rgba=254,38,102,255 expected=254,38,102,255 pass=1",
                "[sdl2-g19] frames=180 probes=2 status=0", "[pss-opengl-native] gate completed status=0"])
            klog = "launchApp(PPSA99005)\nEXEC /app0/eboot.bin\n" + "".join(
                "[AvControl] video: port:HDMI " + mode + "\n" for mode in ("1440P_11988", "1440P_5994"))
            cycle = dict(titleId="PPSA99005", outcome="entered-eboot", teardownSignal="runtime-layers-released",
                         ebootSha256=self.profile[kind + "_eboot"],
                         libcSha256="e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036",
                         appDirectory="private fixture path; must never be copied")
            runner = dict(checkoutCommit="c" * 40, protocolCommit="7195c969e60735f158d46b5034cd53ae62ef0ebc",
                          postHealthChecked=True, lockReleased=True,
                          gate="egl_public_core33_sdl2.o" if kind == "sdl" else "egl_public_core33_imgui_tv.o")
            for key, data in (("app", app), ("klog", klog), ("cycle", json.dumps(cycle)), ("runner", json.dumps(runner))):
                self.paths[kind][key].write_text(data)
            hdmi = BUNDLE.DISPLAY.hdmi_report(klog, "PPSA99005", 2560, 1440, 119.88)
            accepted = dict(eboot_sha256=cycle["ebootSha256"], source_companion=runner["checkoutCommit"])
            if kind == "imgui":
                accepted.update(hdmi, window_benchmark=dict(target_met=True, seconds=30.004589,
                    achieved_fps=119.881660, frame_p50_ms=8.52, frame_p95_ms=8.69, frame_p99_ms=8.77))
            else:
                accepted.update(status="pass", frames=180, pixel_probes=2, measured_fps=None,
                    display_profile=self.profile["display"], hdmi=hdmi, sdk_manifest_sha256=self.profile["sdk"],
                    sdk_runtime_sha256=self.profile["archive"], sdl_manifest_sha256=self.sdl[0]["payload_manifest_sha256"],
                    native_receipt_sha256=self.sdl[1], native_teardown="runtime-layers-released",
                    healthy=True, lock_released=True, physical_input_verified=False)
            self.records[kind] = accepted
            self.repin(kind)
        # Numerical ImGui auditing is exercised by summarize-imgui-profile's
        # self-test and the real frozen-receipt check. Isolate the packaging gate.
        patcher = mock.patch.object(BUNDLE.DISPLAY, "summarize", side_effect=lambda *_: copy.deepcopy(self.records["imgui"]))
        patcher.start()
        self.addCleanup(patcher.stop)

    def repin(self, kind):
        record = self.records[kind]
        record["raw_sha256"] = {key: BUNDLE.digest(path) for key, path in self.paths[kind].items()}
        path = self.root / self.profile[kind + "_record"]
        path.write_text(json.dumps(record))
        self.profile[kind + "_record_sha256"] = BUNDLE.digest(path)

    def report(self):
        return BUNDLE.hfr_report(self.root, self.profile, self.sdl)

    def test_focused_scope_and_sanitized_identity(self):
        report = self.report()
        self.assertFalse(report["sample_complete"])
        self.assertFalse(report["full_matrix_complete"])
        self.assertFalse(report["extended_soak"])
        self.assertIsNone(report["sdl"]["measured_fps"])
        self.assertEqual(report["imgui"]["frames"], 3597)
        self.assertEqual(report["sdl"]["hdmi"]["active"]["height"], 1440)
        self.assertNotIn("private fixture", json.dumps(report))
        self.assertNotIn("details", json.dumps(report))
        self.assertNotIn("appDirectory", json.dumps(report))

    def test_receipt_host_is_a_private_marker_only(self):
        host = "private-console.example.invalid"
        for kind in self.records:
            path = self.paths[kind]["runner"]
            runner = json.loads(path.read_text())
            runner["ps5Host"] = host
            path.write_text(json.dumps(runner))
            self.repin(kind)
        private_hosts = set()
        report = BUNDLE.hfr_report(self.root, self.profile, self.sdl, private_hosts)
        self.assertEqual(private_hosts, {host})
        self.assertNotIn(host, json.dumps(report))

    def test_records_and_every_raw_receipt_are_pinned(self):
        for kind in self.records:
            record = self.root / self.profile[kind + "_record"]
            for path in (record, *self.paths[kind].values()):
                original = path.read_bytes()
                path.write_bytes(original + b"changed")
                with self.subTest(path=path.name), self.assertRaisesRegex(ValueError, "changed"):
                    self.report()
                path.write_bytes(original)
            record.write_text("{")
            self.profile[kind + "_record_sha256"] = BUNDLE.digest(record)
            with self.assertRaises(json.JSONDecodeError):
                self.report()
            self.repin(kind)

    def test_mixed_profiles_payloads_and_acceptance_are_rejected(self):
        for profile in BUNDLE.HFR.values():
            BUNDLE.require_frozen_sdk(profile, profile["sdk"], profile["archive"])
        with self.assertRaises(ValueError):
            BUNDLE.require_frozen_sdk(BUNDLE.HFR["1440p120"], BUNDLE.HFR["2160p120"]["sdk"], self.profile["archive"])
        for key, value in (("display_profile", BUNDLE.DISPLAY_PROFILES["2160p120"]),
                           ("sdk_manifest_sha256", "x" * 64), ("sdk_runtime_sha256", "x" * 64)):
            with mock.patch.dict(self.sdl[0], {key: value}), self.assertRaises(ValueError):
                self.report()
        with self.assertRaises(ValueError):
            BUNDLE.hfr_report(self.root, self.profile, (self.sdl[0], "changed", {}))
        for kind, key, value in (("sdl", "display_profile", BUNDLE.DISPLAY_PROFILES["2160p120"]),
                                 ("sdl", "measured_fps", 120), ("sdl", "frames", 179),
                                 ("sdl", "sdl_manifest_sha256", "wrong"), ("sdl", "healthy", False),
                                 ("sdl", "lock_released", False), ("sdl", "physical_input_verified", True),
                                 ("imgui", "eboot_sha256", "wrong")):
            with mock.patch.dict(self.records[kind], {key: value}):
                self.repin(kind)
                with self.subTest(key=key), self.assertRaises(ValueError):
                    self.report()
            self.repin(kind)

    def test_rehashed_bad_pixels_hdmi_cleanup_and_timing_still_fail(self):
        changes = (("app", "pass=1", "pass=0"), ("app", "drawable=2560x1440", "drawable=3840x2160"),
                   ("app", "frames=180", "frames=179"), ("klog", "1440P_11988", "1080P_11988"),
                   ("klog", "1440P_5994", "2160P_5994"), ("klog", "1440P_5994", "unknown"),
                   ("cycle", "runtime-layers-released", "timeout"), ("runner", '"lockReleased": true', '"lockReleased": false'),
                   ("runner", '"postHealthChecked": true', '"postHealthChecked": false'))
        for key, before, after in changes:
            path = self.paths["sdl"][key]
            original = path.read_text()
            path.write_text(original.replace(before, after))
            self.repin("sdl")
            with self.subTest(key=key, after=after), self.assertRaises(ValueError):
                self.report()
            path.write_text(original)
            self.repin("sdl")
        for key, value in (("target_met", False), ("seconds", 29), ("achieved_fps", 113)):
            with mock.patch.dict(self.records["imgui"]["window_benchmark"], {key: value}):
                self.repin("imgui")
                with self.assertRaises(ValueError):
                    self.report()
            self.repin("imgui")

    def test_ci_profiles_and_runtime_defines_must_match(self):
        fixture = importlib.import_module("test_sdl_sdk")
        sdk = self.root / "profile-sdk"
        for name, profile in BUNDLE.DISPLAY_PROFILES.items():
            fixture.profile_header(sdk, **profile)
            flags = f'-DPS5_SCANOUT_HEIGHT={profile["height"]} -DPS5_SCANOUT_FPS={profile["fps"]}\n'
            self.assertEqual(BUNDLE.require_ci_profile(sdk, flags, name), profile)
            for bad in ("", flags + flags, flags.replace("FPS=", "FPS=bad"), flags.replace("HEIGHT=", "HEIGHT=0")):
                with self.assertRaises(ValueError):
                    BUNDLE.require_ci_profile(sdk, bad, name)
            with self.assertRaises(ValueError):
                BUNDLE.require_ci_profile(sdk, flags, "2160p120" if name != "2160p120" else "1080p60")
        header = sdk / "include/ps5_opengl_display.h"
        header.write_text(header.read_text() + "#define PS5_OPENGL_NATIVE_FPS 60\n")
        with self.assertRaises(ValueError):
            BUNDLE.require_ci_profile(sdk, flags, "2160p120")

    def test_private_bytes_are_rejected_without_rewriting_even_inside_sources(self):
        root = self.root / "distribution"
        root.mkdir()
        path = root / "lib.a"
        # Split literals so the tests themselves contain no personal path/URL.
        private = [b"/mnt/" + b"c/Users/person/build", b"C:" + b"\\Users\\person\\build",
                   b"/home/" + b"person/build", b"https://" + b"192.0.2.99/private"]
        markers = BUNDLE.private_markers([Path(private[0].decode()), Path(private[2].decode())],
                                          hosts=["192.0." + "2.99"])
        for value in private:
            path.write_bytes(b"!<arch>\n" + value)
            with self.assertRaisesRegex(ValueError, "private build path or host"):
                BUNDLE.require_distributable_tree(root, markers)
            self.assertTrue(path.read_bytes().endswith(value))
        path.write_bytes(b"!<arch>\n/user/home/%04x/\n")
        BUNDLE.require_distributable_tree(root)
        for suffix in (".tar", ".tar.gz", ".tar.xz"):
            archive_path = root / ("source" + suffix)
            mode = {".tar": "w", ".tar.gz": "w:gz", ".tar.xz": "w:xz"}[suffix]
            with tarfile.open(archive_path, mode) as archive:
                member = tarfile.TarInfo("project/file.c")
                member.size = len(private[0])
                archive.addfile(member, io.BytesIO(private[0]))
            with self.assertRaisesRegex(ValueError, "private build path or host"):
                BUNDLE.require_distributable_tree(root, markers)
            archive_path.unlink()
        for name in ("PPSA99005-20260908-200944-klog.log", "eboot.bin", "EBOOT.BIN", "libc.prx", "libSceExample.sprx"):
            extra = root / name
            extra.write_text("must not ship")
            with self.assertRaisesRegex(ValueError, "raw receipt or native title asset"):
                BUNDLE.require_distributable_tree(root)
            extra.unlink()
        for name in ("project/PPSA99005-20260908-200944-opengl.log", "project/sce_sys/icon0.png"):
            with tarfile.open(root / "title.tar", "w") as archive:
                member = tarfile.TarInfo(name)
                member.size = 4
                archive.addfile(member, io.BytesIO(b"data"))
            with self.assertRaisesRegex(ValueError, "raw receipt or native title asset"):
                BUNDLE.require_distributable_tree(root)

    def test_self_source_and_public_source_fixtures_pass_without_redaction(self):
        root = self.root / "public-distribution"
        (root / "sources").mkdir(parents=True)
        public = {
            "tools/build-sdk-bundle.py": Path(BUNDLE.__file__).read_bytes(),
            "tools/test_sdk_bundle.py": Path(__file__).read_bytes(),
            "docs/example.md": b"Example: /home/user/project or /Users/example/project; http://localhost:8080\n",
            "tests/fixtures/data.bin": bytes(range(256)),
            "tests/fixtures/parser.log": b"public parser input\n",
        }
        for name, data in public.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        source_tar = root / "sources/project.tar.xz"
        with tarfile.open(source_tar, "w:xz") as archive:
            for name, data in public.items():
                member = tarfile.TarInfo("project/" + name)
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
        before = {path: BUNDLE.digest(path) for path in root.rglob("*") if path.is_file()}
        BUNDLE.require_distributable_tree(root)
        self.assertEqual(before, {path: BUNDLE.digest(path) for path in before})
        host = "192.0." + "2.99"
        (root / "docs/example.md").write_text("actual receipt host: " + host)
        with self.assertRaisesRegex(ValueError, "private build path or host"):
            BUNDLE.require_distributable_tree(root, BUNDLE.private_markers([], [host]))


class G47BundleTests(unittest.TestCase):
    repin = HFRBundleTests.repin

    def setUp(self):
        # Reuse the old receipt fixture, then replace every runtime-bound input.
        HFRBundleTests.setUp(self)
        records = {key: value for key, value in self.profile.items() if "_record" in key}
        self.profile.update(copy.deepcopy(BUNDLE.G47["1440p120"]), **records)
        self.sdl = (dict(self.sdl[0], sdk_manifest_sha256=self.profile["sdk"],
                         sdk_runtime_sha256=self.profile["archive"]), self.profile["sdl_receipt"], {})
        self.candidate_path = self.root / "window-candidate.json"
        self.candidate = dict(source_companion="f0f5bbeb2a5ea8ea96942c653b8085dccdb6c514",
            gate="egl_public_core33_imgui_tv.o", hardware_run=False, profile=self.profile["display"],
            sdk_manifest_sha256=self.profile["sdk"], runtime_sha256=self.profile["archive"],
            files={"eboot.bin": self.profile["imgui_eboot"]},
            build_flags=dict(PS5_IMGUI_PROFILE="1", PS5_IMGUI_WINDOW_BENCHMARK="1", PS5_IMGUI_WINDOW_TARGET="120"))
        old = self.records["imgui"]
        self.records["imgui"] = dict(classification="pass", candidate=self.candidate,
            runner_companion=self.candidate["source_companion"],
            workload=dict(frames=3597, window_benchmark=dict(old["window_benchmark"], output_mode_verified=False)),
            display={key: value for key, value in old.items() if key not in
                     ("eboot_sha256", "source_companion", "window_benchmark", "raw_sha256")})
        self.paths["imgui"]["candidate"] = self.candidate_path
        self.records["sdl"].update(sdk_manifest_sha256=self.profile["sdk"],
            sdk_runtime_sha256=self.profile["archive"], native_receipt_sha256=self.sdl[1],
            eboot_sha256=self.profile["sdl_eboot"])
        for kind in self.records:
            path = self.paths[kind]["cycle"]
            path.write_text(json.dumps(dict(json.loads(path.read_text()), ebootSha256=self.profile[kind + "_eboot"])))
        path = self.paths["imgui"]["runner"]
        path.write_text(json.dumps(dict(json.loads(path.read_text()), checkoutCommit=self.candidate["source_companion"])))
        self.repin_candidate()
        self.repin("sdl")
        patcher = mock.patch.object(importlib.import_module("summarize-imgui-profile"), "summarize",
                                    side_effect=lambda *a, **kw: copy.deepcopy(self.records["imgui"]["workload"]))
        patcher.start()
        self.addCleanup(patcher.stop)

    def repin_candidate(self):
        self.candidate_path.write_text(json.dumps(self.candidate))
        self.profile["imgui_candidate"] = BUNDLE.digest(self.candidate_path)
        self.repin("imgui")

    def report(self):
        return BUNDLE.hfr_report(self.root, self.profile, self.sdl, window_candidate=self.candidate_path)

    def test_new_identity_and_top_level_hdmi_not_nested_output_flag(self):
        report = self.report()
        self.assertEqual(report["runtime_source_commit"], BUNDLE.G47_RUNTIME)
        self.assertFalse(report["inherited_acceptance"])
        self.assertFalse(report["independent_per_run_tv_observation"])
        self.assertFalse(report["sample_complete"])
        self.assertFalse(report["full_matrix_complete"])
        self.assertEqual(report["imgui"]["hdmi"]["active"]["refresh_hz"], 119.88)
        self.assertIn("not recorded", report["tv_visual_confirmation"])
        self.assertNotIn("private fixture", json.dumps(report))

    def test_all_new_raw_inputs_including_candidate_are_pinned(self):
        HFRBundleTests.test_records_and_every_raw_receipt_are_pinned(self)

    def test_runner_checkout_is_bound_separately_from_app_build_source(self):
        path = self.paths["imgui"]["runner"]
        runner = json.loads(path.read_text())
        runner["checkoutCommit"] = "b" * 40
        path.write_text(json.dumps(runner))
        self.repin("imgui")
        with self.assertRaisesRegex(ValueError, "identity/lifecycle"):
            self.report()
        self.records["imgui"]["runner_companion"] = runner["checkoutCommit"]
        self.repin("imgui")
        report = self.report()["imgui"]
        self.assertEqual(report["source_companion_at_run"], "b" * 40)
        self.assertEqual(report["app_build_source_companion"], self.candidate["source_companion"])
        self.records["imgui"]["candidate"] = dict(self.candidate, source_companion="b" * 40)
        self.repin("imgui")
        with self.assertRaisesRegex(ValueError, "candidate mismatch"):
            self.report()

    def test_original_acceptance_and_pending_qualification_cannot_transfer(self):
        with self.assertRaises(ValueError):
            BUNDLE.hfr_report(self.root, self.profile,
                              (self.sdl[0], BUNDLE.HFR["1440p120"]["sdl_receipt"], {}))
        for key in ("sdl_record", "sdl_record_sha256", "imgui_record", "imgui_record_sha256"):
            with mock.patch.dict(self.profile, {key: None}), self.assertRaisesRegex(ValueError, "not yet frozen"):
                self.report()

    def test_rehashed_wrong_candidate_and_native_hdmi_still_fail(self):
        for key, bad in (("sdk_manifest_sha256", BUNDLE.HFR["1440p120"]["sdk"]),
                         ("runtime_sha256", BUNDLE.G47["2160p120"]["archive"]),
                         ("profile", BUNDLE.DISPLAY_PROFILES["2160p120"]),
                         ("source_companion", "a" * 40), ("hardware_run", True),
                         ("gate", "offscreen.o"), ("build_flags", {})):
            with mock.patch.dict(self.candidate, {key: bad}):
                self.repin_candidate()
                with self.subTest(key=key), self.assertRaisesRegex(ValueError, "candidate mismatch"):
                    self.report()
            self.repin_candidate()
        path = self.paths["imgui"]["klog"]
        text, display = path.read_text(), self.records["imgui"]["display"]
        for bad in ("launchApp(PPSA99005)\nEXEC /app0/eboot.bin\n",
                    text.replace("1440P_11988", "2160P_11988"), text.replace("1440P_5994", "unknown")):
            path.write_text(bad)
            self.records["imgui"]["display"] = BUNDLE.DISPLAY.hdmi_report(bad, "PPSA99005", 2560, 1440, 119.88)
            self.repin("imgui")
            with self.assertRaisesRegex(ValueError, "HDMI"):
                self.report()
        path.write_text(text)
        self.records["imgui"]["display"] = display
        self.repin("imgui")


class G47DerivativeTests(unittest.TestCase):
    def test_pinned_contract_audits_file_mapping_and_scope(self):
        with TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / "sdk"
            (sdk / "lib").mkdir(parents=True)
            runtime = sdk / "lib/libps5_opengl_core33.a"
            runtime.write_bytes(b"!<arch>\n")
            (sdk / "manifest.sha256").write_text("fixture")
            profile = dict(BUNDLE.G47["1440p120"], archive=BUNDLE.digest(runtime))
            consumers = dict(status="PASS", manifest=dict(sha256=profile["sdk"]), gl33_exports=344,
                consumers={key: dict(status="PASS", icf_flags_in_resolved_linker_argv=[])
                           for key in ("make", "pkgconfig", "cmake")}, outputs=dict.fromkeys(("a", "b", "c"), "hash"))
            record = dict(format="ps5-opengl-g47-derivative-v1", status="HOST_CHECKED_WITH_ADDRSIG_EXCEPTION",
                source_commit=BUNDLE.G47_RUNTIME, original_g31_source_commit=BUNDLE.HFR_RUNTIME,
                hardware_run=False, inherited_acceptance=False, dependency_rebuilds=0,
                profiles={"1440p120": dict(original_manifest_sha256=BUNDLE.HFR["1440p120"]["sdk"],
                    original_runtime_sha256=BUNDLE.HFR["1440p120"]["archive"],
                    derived_manifest_sha256=profile["sdk"], derived_runtime_sha256=profile["archive"],
                    files={"lib/libps5_opengl_core33.a": dict(derived_sha256=profile["archive"])})})
            for key in ("dependency_audit", "addrsig_guard", "privacy_audit", "consumer_audit"):
                path = root / (key.replace("_", "-") + ".json")
                value = dict(status="PASS_WITH_ADDRSIG_EXCEPTION" if key == "dependency_audit" else "PASS")
                if key == "consumer_audit":
                    value["profiles"] = {"1440p120": consumers}
                path.write_text(json.dumps(value))
                record[key] = dict(path=path.name, sha256=BUNDLE.digest(path))
            provenance = root / "provenance.json"
            provenance.write_text(json.dumps(record))
            with mock.patch.dict(BUNDLE.G47, {"1440p120": profile}), \
                    mock.patch.object(BUNDLE, "G47_PROVENANCE", BUNDLE.digest(provenance)), \
                    mock.patch.object(BUNDLE.CHECK, "verify_manifest", return_value=dict(sha256=profile["sdk"])), \
                    mock.patch.object(BUNDLE.CHECK, "display_profile", return_value=profile["display"]):
                def verify():
                    return BUNDLE.verify_g47_derivative(provenance, sdk, "1440p120")
                copies, checked = verify()
                self.assertEqual(len(copies), 5)
                self.assertEqual(checked, consumers)
                for path in (provenance, *(root / record[key]["path"] for key in
                             ("dependency_audit", "addrsig_guard", "privacy_audit", "consumer_audit")), runtime):
                    original = path.read_bytes()
                    path.write_bytes(original + b"changed")
                    with self.subTest(path=path.name), self.assertRaises(ValueError):
                        verify()
                    path.write_bytes(original)
                for key, bad in (("hardware_run", True), ("inherited_acceptance", True),
                                 ("dependency_rebuilds", 1), ("source_commit", BUNDLE.HFR_RUNTIME)):
                    with mock.patch.dict(record, {key: bad}):
                        provenance.write_text(json.dumps(record))
                        with mock.patch.object(BUNDLE, "G47_PROVENANCE", BUNDLE.digest(provenance)), \
                                self.assertRaisesRegex(ValueError, "scope mismatch"):
                            verify()
                provenance.write_text(json.dumps(record))
                record["profiles"]["1440p120"]["original_manifest_sha256"] = BUNDLE.HFR["2160p120"]["sdk"]
                provenance.write_text(json.dumps(record))
                with mock.patch.object(BUNDLE, "G47_PROVENANCE", BUNDLE.digest(provenance)), \
                        self.assertRaisesRegex(ValueError, "original-to-derived"):
                    verify()


if __name__ == "__main__":
    unittest.main()
