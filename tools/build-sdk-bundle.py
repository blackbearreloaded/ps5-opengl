#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Package a frozen validated SDK or a host-checked CI build; no console access."""
import argparse
import gzip
import hashlib
import importlib
import importlib.util
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import g55_release_evidence as G55_EVIDENCE
import g62_release_evidence as G62_EVIDENCE

CHECK = importlib.import_module("check-sdk-consumers")
AUDIT = importlib.import_module("verify-cts-candidate")
digest, require = CHECK.digest, AUDIT.require
VERSION = "0.1.0-perf20260907-sampled"
RUNTIME = "cef6c1b869ba3f0c8acaab5b171bbb33eca07f03"
SDK_HASH = "03e535ec8853475385034759e7bc7e63b37ef275591912e3391669b027bd09fc"
RUNTIME_HASH = "9fe92335c3b18043eea989df009ab3ca21cfba1d7831fc0150bebd1c0b8878d1"
CANDIDATE_HASH = "790e9f56ae71a3153044557c2280644786b5fe7dd3b5c782b8591acaf24f6398"
SAMPLE_HASH = "915d9f79c90d2ff9ec1c900e59f1e34ed3894747beddc8a53419f8b98710c4c9"
SAMPLES = {
    VERSION: dict(runtime=RUNTIME, sdk=SDK_HASH, archive=RUNTIME_HASH,
                  candidate=CANDIDATE_HASH, guide="sdk-bundle.md",
                  native_paths=["native-app"]),
    "0.1.0-perf20260908-sampled": dict(
        runtime="16e651b3d6e871c3986dc5710a9ca897fbb6e4ab",
        sdk="5dcdd41a1e26d714de88e8628e449098055f72dce842bd70106ada73140d9827",
        archive="dd636df0119009173ace9b196b4903fd5da5e515e7e9ae3f1bea5273a871251a",
        candidate="433d1241136532b87985567f663e69a4f421fb33eaa4039c950457c38cf65290",
        guide="sdk-bundle-g13.md",
        # Only these native-app sources enter the SDK. Heap/GPU diagnostics
        # belong to the separately versioned application source companion.
        native_paths=["native-app/agc_link_stub.c", "native-app/agc_driver_link_stub.c"]),
    "0.1.0-perf20260908-g19-sampled": dict(
        runtime="44435a7ccf7e3d30e33169867a5421a5fd86fae2",
        sdk="344673952a789cae7e4a6d4c6670e3cf8c6bbf595fb07ebf27a8ffe3680014be",
        archive="627857a44a8101b0ab0df319293a554143e96405be8a1ec55fc48bbd1830caa5",
        candidate="31d8e1d13bbacad9d87a7ad656612abe04a234a165bd0c82009ed398af70244a",
        guide="sdk-bundle-g19.md",
        native_paths=["native-app/agc_link_stub.c", "native-app/agc_driver_link_stub.c"]),
    "0.1.0-perf20260908-g25-sdl2-sampled": dict(
        runtime="61a0919bff9e6944caef2b8ad6c6ef9fd9a30279",
        sdk="749057e84def5ead284614db5d81a0a7e9c037f474afd95cd88ebceea5d65cda",
        archive="5b6129538328ab8b950dfbdb2f1395774b4a91e7d4030d1acafbabe043829d9e",
        candidate="ff6759b193b5bb9138a72e53214e112c2ddc68aca6338ca3b476c8172ee9bd15",
        sdl_receipt="d5f06b106f9d2de1613113773e1aaa01d30ac38769a4158c8916d72e175a8af7",
        guide="sdk-bundle-g25.md",
        native_paths=["native-app/agc_link_stub.c", "native-app/agc_driver_link_stub.c"]),
}
TARGETED_VERSION = "0.1.0-perf20260908-targeted"
TARGETED = dict(
    runtime="44435a7ccf7e3d30e33169867a5421a5fd86fae2",
    sdk="344673952a789cae7e4a6d4c6670e3cf8c6bbf595fb07ebf27a8ffe3680014be",
    archive="627857a44a8101b0ab0df319293a554143e96405be8a1ec55fc48bbd1830caa5",
    candidate="001ab679f38dfcd9b7466fd333ebc1775e1acdc176fe6e7b2a03f7ca2711b549",
    guide="sdk-bundle-g19.md", receipt="PPSA99005-20260908-105113",
    native_paths=["native-app", "tests/ps5/Makefile",
                  "tests/ps5/egl_public_core33_transfer_workload.c",
                  "tests/ps5/egl_public_core33_render_format_blit.c",
                  "tests/ps5/egl_public_core33_transfer_regressions.c"],
    raw_sha256={
        "-opengl.log": "7aa557920fdf6053c56fdf2f6578a79a5c3f1e1bb5a7a15b688c20113062f2a7",
        "-klog.log": "dee1d2e0b098d133e91a66c6278ffef94793ed57100a8f207c6e2065eb702dd1",
        "-result.json": "db335da69882d23a4802bba327f9ca3570117cff39769ceb161ff733c91fcab8",
        "-runner.json": "8634c6b75db1bfa39eab2595401717e30db39a9f30909b9f90e161034f3a55b0"})

DISPLAY = importlib.import_module("summarize-display")
DISPLAY_PROFILES = {f"{height}p{fps}": dict(width=height * 16 // 9, height=height, fps=fps)
                    for height, fps in ((1080, 60), (1440, 120), (2160, 120))}
HFR_RUNTIME = "3cdc90bbc14def6bc3025b4460fba0892841114a"
HFR_SDL_SOURCE = "ac2a52aa7faac5e7b9bcd6660cee36263f3e5324"
HFR = {
    "1440p120": dict(
        sdk="d2d6169960d379f3987b8b7c3b8f81cf05f97072cdfa67eaba393fc82e327567",
        archive="b7f3ca590d9befa516691b893617d8ae049ce187c47737aad041c5788b93fabe",
        imgui_eboot="1860dc15b22a064136fcf9b9de79c262f339dd1e426a9eb3363b550c09bfcb90",
        sdl_eboot="63baeb465b89a04f8569d04e50f122a42c253df3bd020256e905a61e58184296",
        sdl_receipt="1d970f2268ccef33712ab893464410b9f95e02e19c0d2b6cdf5c45d375285527",
        imgui_record="g39-hdmi4-opengl-1440-20260908/display-report.json",
        imgui_record_sha256="41242867fc376295a0ed89294aaf43b72e85db6f6e4db896f6d332276b5c896c",
        sdl_record="g40-hdmi4-sdl2-1440-20260908/acceptance.json",
        sdl_record_sha256="52c2133faddff14caf48c6e2fa270d6863047541128c0b5c315437c1259faea4"),
    "2160p120": dict(
        sdk="2785038b1020824a882e533b874bb7658e6f41210e689a79ccd363ffc3af1b61",
        archive="83a8f4729bafcb0b61be6e4b96f1603c1d682b26243f47475cff2d2af7353374",
        imgui_eboot="91f2d5cd08d3e7f46e85c8396c1ba5511de874d8a807e77f9c5ab5c2a8905fc0",
        sdl_eboot="5578cbb4b40725dfb21918ebea967209753af4ff34442d45a2e15609674f5ff5",
        sdl_receipt="027bff821586f6baa51822a7a4304b914bd59f84d81f64bf765973313db61c84",
        imgui_record="g37-hdmi4-opengl-2160-20260908/display-report.json",
        imgui_record_sha256="69f4b7b9b773016574055b4e1956e773330d54bc160f25ec7d763a53604bcaee",
        sdl_record="g38-hdmi4-sdl2-2160-20260908/acceptance.json",
        sdl_record_sha256="4e4dfa73bacd5041a81f8d03e4fd69f83911898471723044b2fa2e834b687e76"),
}
for key, value in HFR.items():
    value.update(runtime=HFR_RUNTIME, display=DISPLAY_PROFILES[key], guide="sdk-bundle-hfr.md",
                 native_paths=["native-app"], version=f"0.1.0-perf20260908-g41-{key}-sdl2-focused")

G47_PROVENANCE = "fbb0cbc4b3fb872ceb6c9e265c489e92e57a659288b404bff9d4460a4a74d9ed"
G47_RUNTIME = "23a594c3a5fda4135599d0dea8d2bf4dcef47a39"
G47_SDL_SOURCE = "ad738dca822e0559f8a17b655784dfc72a4fd30a"
# New bytes, new receipts. None means qualification is not yet frozen, never
# permission to fall back to an original-runtime acceptance record.
G47 = {
    "1440p120": dict(
        sdk="5c9da7020167a604400f9a378d20689c78cd8d9d8ec48d889b74aa698e63965c",
        archive="38072f5d0ed30c43b273fac36ebceabbaf943bfe72a956f38e4d0ad157f2ac8a",
        imgui_eboot="4b8b6a4ca0cb426bcce742e3215e8b2aef032c7859228020482ce2770a64e350",
        imgui_candidate="8be4b5d06d8df8d2cd8c09a9a3957c9416b5a7fddbfa37c7866c8396286c1db3",
        sdl_eboot="ce5dfd55bd93040833947bcebd65cb00f29bacd1c7d98de00e8b053015959fc6",
        sdl_receipt="7d430fdeb4ff8262aaf362e74de2e4304d0c12ac5630dc4355e49282dd384f9e",
        date="2026-09-09",
        imgui_record="g47-window-1440-20260909/PPSA99005-20260909-062212-opengl-g47-audit.json",
        imgui_record_sha256="3b04666af2cac957287ef98e140b710482b94c9fbf26a07c51e020704717298a",
        sdl_record="g47-sdl2-1440-20260909/acceptance.json",
        sdl_record_sha256="a1b85e8aca51264927becc7e5c5ab5b5e9a934a03d6e0a69d0706726a26a30af"),
    "2160p120": dict(
        sdk="1585e458b2ce884bc68c3e0b9439955e0e47e1d895e0ce76023ccd5739a7e577",
        archive="fcca06d1701edae0881105c3cdb24eb92a152397e024184c2eabe9254d79a675",
        imgui_eboot="302798b80cc33caff54407e949c18e9b099fe707473d2c714f0d5dd9cfa65b44",
        imgui_candidate="d1918f5e7fd495d54ac8b52d9b8168626cb665fa4595f57b71a939044a6aae46",
        sdl_eboot="e740b584fe28897453ee3e4f9e57b4a9401b2e711efa0781edeb7152807ce3e1",
        sdl_receipt="0646a0792c5ab1278374e4cb0fadc35d732dd373936b1ac36058d6b8a57f1779",
        imgui_record="g47-window-2160-20260908/PPSA99005-20260908-221429-opengl-g47-audit.json",
        imgui_record_sha256="241720ec3328603b819f8c7a6508bda4f28c5672f9a445d75ddd22cd9bedc912",
        sdl_record="g47-sdl2-2160-20260908/acceptance.json",
        sdl_record_sha256="90417420114e0b4fe941c50ddf8bb5c4b8c9783c278a971a0a6f38b7a661facd"),
}
for key, value in G47.items():
    value.update(runtime=G47_RUNTIME, display=DISPLAY_PROFILES[key], guide="sdk-bundle-hfr.md",
                 derivative=True, sdl_source=G47_SDL_SOURCE,
                 version=f"0.1.0-perf20260908-g47-{key}-sdl2-focused")

# Fixed qualification scopes; no CLI override or inherited native acceptance.
# ponytail: owner uses 4K as the hardware gate; add 1440 native checks if a
# resolution-specific regression needs them, not for every shared-code change.
G55 = {name: dict(runtime=None, sdk=None, archive=None, sdl_receipt=None,
                 sdl_source=None, evidence=None, display=DISPLAY_PROFILES[name],
                 guide="sdk-g55-release.md", version=f"0.1.0-perf20260909-g55-{name}-sdl2-focused")
       for name in ("1440p120", "2160p120")}
G55["1440p120"].update(qualification="host-only",
    runtime="9f3b6dda933727dad61175b69438005e6f8abf7b",
    sdk="b46df166e9a89e2ce9300a05eaa908dea59a7f75bd863f15bb34291cba799fde",
    archive="06ad2be2c270a1637c2f73b93de36956d1c268bac0ea4b9816de1a317e4af2b4",
    sdl_receipt="16820788e7268e6c3af5567276dd9936342ad0fc9f59271f202391847578cfe4",
    sdl_source="9f3b6dda933727dad61175b69438005e6f8abf7b",
    evidence="75ef7dc28803be61fc7f6528f15fc0ddea820d7b9d1321da830361400dd9fe46",
    version="0.1.0-perf20260909-g55-1440p120-sdl2-host-checked")
G55["2160p120"].update(
    runtime="9f3b6dda933727dad61175b69438005e6f8abf7b",
    sdk="f9d76590d75e46459c3b93570d794c43000fc6082605eb02d0ac3e13e90ef308",
    archive="7eba855a6cc5d94956c81ea11e3635158de265a772ebf0ac3421a8114121d457",
    sdl_receipt="c806cad89d5fb725fbaa8abee67ad9b84b31488e0555544bd88571805dbc4fd9",
    sdl_source="9f3b6dda933727dad61175b69438005e6f8abf7b",
    evidence="f9dc5cd0682a8f54980799938eede117e5dab307f6b31d21df2d64aca9a8ddca")

# Filled only after exact-binary release qualification; no inherited acceptance.
G62 = {name: dict(runtime=None, sdk=None, archive=None, sdl_receipt=None,
                 sdl_source=None, evidence=None, display=DISPLAY_PROFILES[name],
                 guide="release-g62.md", version=f"0.2.0-{name}-sdl2",
                 qualification="host-only" if name == "1440p120" else "native-sampled")
       for name in ("1440p120", "2160p120")}


def verify_g47_derivative(path, sdk, name):
    """Accept only the reviewed independent build, not arbitrary derivative policies."""
    require(digest(path) == G47_PROVENANCE, "G47 derivative provenance changed")
    record = json.loads(path.read_text())
    profile, original = G47[name], HFR[name]
    derived = record["profiles"][name]
    require(record["format"] == "ps5-opengl-g47-derivative-v1" and
            record["status"] == "HOST_CHECKED_WITH_ADDRSIG_EXCEPTION" and
            record["source_commit"] == G47_RUNTIME and record["original_g31_source_commit"] == HFR_RUNTIME and
            record["hardware_run"] is False and record["inherited_acceptance"] is False and
            record["dependency_rebuilds"] == 0, "G47 derivative scope mismatch")
    require((derived["original_manifest_sha256"], derived["original_runtime_sha256"],
             derived["derived_manifest_sha256"], derived["derived_runtime_sha256"]) ==
            (original["sdk"], original["archive"], profile["sdk"], profile["archive"]),
            "G47 original-to-derived identity mismatch")
    require_frozen_sdk(profile, CHECK.verify_manifest(sdk)["sha256"], digest(sdk / "lib/libps5_opengl_core33.a"))
    require(CHECK.display_profile(sdk) == profile["display"], "G47 SDK display profile mismatch")
    files = {p.relative_to(sdk).as_posix() for p in sdk.rglob("*") if p.is_file()} - {"manifest.sha256"}
    require(files == set(derived["files"]), "G47 derivative file map mismatch")
    for name in files:
        require(digest(sdk / name) == derived["files"][name]["derived_sha256"], "G47 derivative file changed: " + name)
    copies = {"g47-derivative-provenance.json": (path, G47_PROVENANCE)}
    audits = {}
    for key, status in (("dependency_audit", "PASS_WITH_ADDRSIG_EXCEPTION"),
                        ("addrsig_guard", "PASS"), ("privacy_audit", "PASS"), ("consumer_audit", "PASS")):
        filename = key.replace("_", "-") + ".json"
        require(record[key]["path"] == filename, "unexpected G47 audit filename")
        source = path.parent / filename
        require(digest(source) == record[key]["sha256"], "G47 audit changed: " + key)
        audits[key] = json.loads(source.read_text())
        require(audits[key]["status"] == status, "G47 audit failed: " + key)
        copies["g47-" + filename] = (source, record[key]["sha256"])
    consumers = audits["consumer_audit"]["profiles"][f'{profile["display"]["height"]}p120']
    require(consumers["gl33_exports"] == 344 and all(
        row["status"] == "PASS" and row["icf_flags_in_resolved_linker_argv"] == []
        for row in consumers["consumers"].values()), "G47 consumer/ICF scope mismatch")
    require_consumers(dict(consumers, gl33=dict(commands=344, exported=344)), profile["sdk"])
    return copies, consumers


def require_ci_profile(sdk, config, name):
    profile = CHECK.display_profile(sdk)
    require(profile == DISPLAY_PROFILES[name], "CI display profile differs from the SDK")
    for key in ("height", "fps"):
        values = re.findall(r"(?:^|\s)-DPS5_SCANOUT_" + key.upper() + r"=([^\s]+)", config)
        require(values == [str(profile[key])], "CI runtime configuration profile mismatch")
    return profile


def hfr_report(results, profile, sdl, private_hosts=None, window_candidate=None):
    """Re-audit the selected frozen receipts; emit only selected public fields."""
    receipt, receipt_hash, _ = sdl
    expected = profile["display"]
    derivative = profile.get("derivative", False)
    require(all(profile.get(kind + "_record") and profile.get(kind + "_record_sha256")
                for kind in ("imgui", "sdl")), "focused hardware qualification is not yet frozen for this profile")
    require(receipt_hash == profile["sdl_receipt"] and
            receipt["display_profile"] == expected and
            receipt["sdk_manifest_sha256"] == profile["sdk"] and
            receipt["sdk_runtime_sha256"] == profile["archive"], "HFR SDL/profile identity mismatch")
    report = dict(scope="focused ImGui timing/pixels and SDL functional/HDMI checks only",
                  hardware="one recorded firmware-6.02 console; Hisense 55U78N HDMI4",
                  date=profile.get("date", "2026-09-08"), display_profile=expected, clean_cycles=2,
                  sample_complete=False, full_matrix_complete=False,
                  extended_soak=False, independent_per_run_tv_observation=False,
                  runtime_source_commit=profile["runtime"],
                  sdl_source_companion=profile.get("sdl_source", HFR_SDL_SOURCE))
    if derivative:
        report.update(derivative_provenance_sha256=G47_PROVENANCE, inherited_acceptance=False,
                      tv_visual_confirmation="not recorded; native HDMI logs only")
    for kind in ("imgui", "sdl"):
        record_path = results / profile[kind + "_record"]
        require(digest(record_path) == profile[kind + "_record_sha256"], "HFR acceptance record changed: " + kind)
        accepted = json.loads(record_path.read_text())
        logs = list(record_path.parent.glob("*-opengl.log"))
        require(len(logs) == 1, "HFR receipt must identify exactly one native run")
        prefix = str(logs[0]).removesuffix("-opengl.log")
        paths = {key: Path(prefix + suffix) for key, suffix in (
            ("app", "-opengl.log"), ("klog", "-klog.log"),
            ("cycle", "-result.json"), ("runner", "-runner.json"))}
        if derivative and kind == "imgui":
            require(window_candidate is not None and digest(window_candidate) == profile["imgui_candidate"],
                    "G47 window candidate changed")
            paths["candidate"] = window_candidate
            candidate = json.loads(window_candidate.read_text())
            require(candidate == accepted["candidate"] and candidate["hardware_run"] is False and
                    candidate["profile"] == expected and candidate["sdk_manifest_sha256"] == profile["sdk"] and
                    candidate["runtime_sha256"] == profile["archive"] and
                    candidate["files"]["eboot.bin"] == profile["imgui_eboot"] and
                    candidate["source_companion"] == "f0f5bbeb2a5ea8ea96942c653b8085dccdb6c514" and
                    candidate["gate"] == "egl_public_core33_imgui_tv.o" and
                    candidate["build_flags"] == dict(PS5_IMGUI_PROFILE="1", PS5_IMGUI_WINDOW_BENCHMARK="1",
                                                     PS5_IMGUI_WINDOW_TARGET="120"), "G47 window candidate mismatch")
        require(set(accepted["raw_sha256"]) == set(paths), "HFR raw receipt set mismatch")
        for key, path in paths.items():
            require(digest(path) == accepted["raw_sha256"][key], "HFR raw receipt changed: " + kind + "/" + key)
        cycle, runner = [json.loads(paths[key].read_text(encoding="utf-8-sig"))
                         for key in ("cycle", "runner")]
        if private_hosts is not None:
            require(isinstance(runner.get("ps5Host"), str) and runner["ps5Host"], "missing receipt host")
            private_hosts.add(runner["ps5Host"])
        eboot = candidate["files"]["eboot.bin"] if derivative and kind == "imgui" else accepted["eboot_sha256"]
        companion = accepted["runner_companion"] if derivative and kind == "imgui" else accepted["source_companion"]
        require(cycle["titleId"] == "PPSA99005" and cycle["outcome"] == "entered-eboot" and
                cycle["teardownSignal"] == "runtime-layers-released" and
                cycle["ebootSha256"].lower() == eboot == profile[kind + "_eboot"] and
                cycle["libcSha256"].lower() == "e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036" and
                runner["checkoutCommit"] == companion and
                runner["protocolCommit"] == "7195c969e60735f158d46b5034cd53ae62ef0ebc" and
                runner["postHealthChecked"] is True and runner["lockReleased"] is True,
                "HFR native identity/lifecycle mismatch")
        if kind == "imgui":
            if derivative:
                require(runner["gate"] == candidate["gate"], "G47 window workload mismatch")
                text = paths["app"].read_text()
                require(text.count("[pss-opengl-native] gate completed status=0") == 1, "G47 window gate failed")
                audited = importlib.import_module("summarize-imgui-profile").summarize(
                    text, window_target=120, window_height=expected["height"], output_status=True, prepare_profile=True)
                hdmi = DISPLAY.hdmi_report(paths["klog"].read_text(encoding="utf-8-sig"),
                                           "PPSA99005", expected["width"], expected["height"], 119.88)
                require(accepted["classification"] == "pass" and audited == accepted["workload"] and
                        hdmi == accepted["display"], "G47 window acceptance does not reproduce")
            else:
                audited = DISPLAY.summarize(paths["app"], expected["height"])
                require(audited == accepted, "HFR ImGui acceptance does not reproduce")
                hdmi = audited
            benchmark = audited["window_benchmark"]
            require(benchmark["target_met"] is True and benchmark["achieved_fps"] >= 114 and
                    30 <= benchmark["seconds"] <= 31, "HFR ImGui timing failed")
            measurements = dict(frames=round(benchmark["seconds"] * benchmark["achieved_fps"]), pixel_probes=2, **{key: benchmark[key] for key in
                                ("seconds", "achieved_fps", "frame_p50_ms", "frame_p95_ms", "frame_p99_ms")})
        else:
            require(runner["gate"] == "egl_public_core33_sdl2.o", "wrong HFR SDL workload")
            text = paths["app"].read_text()
            require(text.count(f'[sdl2-g19] GL=3.3 (Core Profile) Mesa 26.2.0 drawable={expected["width"]}x{expected["height"]} nominal_refresh=120Hz (not negotiated HDMI)') == 1,
                    "HFR SDL drawable mismatch")
            # Same two exact probes as the recorded G32/G38/G40 local auditor.
            probes = re.findall(r"^\[sdl2-g19\] probe frame=(\d+) rgba=(\d+,\d+,\d+,\d+) expected=(\d+,\d+,\d+,\d+) pass=1$", text, re.M)
            require(probes == [("0", "0,38,102,255", "0,38,102,255"),
                               ("179", "254,38,102,255", "254,38,102,255")] and
                    text.count("[sdl2-g19] probe ") == 2 and
                    text.count("[sdl2-g19] frames=180 probes=2 status=0") == 1 and
                    text.count("[pss-opengl-native] gate completed status=0") == 1,
                    "HFR SDL frame/pixel checks failed")
            hdmi = DISPLAY.hdmi_report(paths["klog"].read_text(encoding="utf-8-sig"),
                                       "PPSA99005", expected["width"], expected["height"], 119.88)
            require(accepted["status"] == "pass" and accepted["frames"] == 180 and
                    accepted["pixel_probes"] == 2 and accepted["measured_fps"] is None and
                    accepted["display_profile"] == expected and accepted["hdmi"] == hdmi and
                    accepted["sdk_manifest_sha256"] == profile["sdk"] and
                    accepted["sdk_runtime_sha256"] == profile["archive"] and
                    accepted["sdl_manifest_sha256"] == receipt["payload_manifest_sha256"] and
                    accepted["native_receipt_sha256"] == receipt_hash and
                    accepted["native_teardown"] == "runtime-layers-released" and
                    accepted["healthy"] is True and accepted["lock_released"] is True and
                    accepted["physical_input_verified"] is False,
                    "HFR SDL acceptance does not reproduce")
            measurements = dict(frames=180, pixel_probes=2, measured_fps=None, physical_input_verified=False)
        require(hdmi["classification"] == "verified-match" and hdmi["restored_60hz"] is True and
                hdmi["render_size"] == {key: expected[key] for key in ("width", "height")} and
                all(hdmi["captured_hdmi_sequence"][-1][key] == expected[key] for key in ("width", "height")),
                "HFR HDMI profile/restoration mismatch")
        report[kind] = dict(
            **measurements, eboot_sha256=profile[kind + "_eboot"],
            source_companion_at_run=companion,
            acceptance_record_sha256=profile[kind + "_record_sha256"], raw_sha256=accepted["raw_sha256"],
            hdmi={label: {key: row[key] for key in ("width", "height", "refresh_hz")}
                  for label, row in (("active", hdmi["negotiated_active"]),
                                     ("restored", hdmi["captured_hdmi_sequence"][-1]))},
            teardown="runtime-layers-released", post_health=True, lock_released=True)
        if derivative and kind == "imgui":
            report[kind]["app_build_source_companion"] = candidate["source_companion"]
    return report


def private_markers(paths, hosts=()):
    """Actual input/user roots and verified receipt hosts, not illustrative paths."""
    roots = {str(Path.home())}
    for path in paths:
        text = str(path).replace("\\", "/")
        match = re.match(r"(/mnt/[a-z]/Users/[^/]+|/Users/[^/]+|/home/[^/]+|[A-Za-z]:/Users/[^/]+)(?:/|$)", text)
        roots.add(match[1] if match else text)
    for root in tuple(roots):
        match = re.fullmatch(r"/mnt/([a-z])/(Users/.+)", root)
        if match:
            roots.add(match[1].upper() + ":/" + match[2])
    values = set(hosts)
    for root in roots:
        values.update((root, root.replace("/", "\\"), root.replace("/", "\\\\")))
    return tuple(sorted(value.encode().lower() for value in values if value))


def require_distributable_tree(root, private=()):
    """Inspect exact bytes, including sources; never redact or strip frozen inputs.

    Public source examples may contain generic home paths, localhost URLs and
    binary fixtures. Only actual private context and native receipt/title paths
    are rejected here; this is not a classifier for every third-party URL/file.
    """
    forbidden = set(private) | set(private_markers([root]))
    overlap = max(map(len, forbidden), default=1)

    def check(stream, name):
        require(Path(name).suffix.lower() not in (".prx", ".sprx") and
                not re.search(r"(?:^|/)(?:PPSA\d{5}-\d{8}-\d{6}-[^/]+\.(?:log|json|qpa)|eboot\.bin)$", name, re.I) and
                not {"sce_sys", "sce_module"}.intersection(part.lower() for part in Path(name).parts),
                "raw receipt or native title asset in bundle input: " + name)
        tail = b""
        while chunk := stream.read(1024 * 1024):
            data = tail + chunk.lower()
            require(not any(marker in data for marker in forbidden), "private build path or host in bundle input: " + name)
            tail = data[-overlap:]

    for path in sorted(root.rglob("*")):
        require(not path.is_symlink() and (path.is_file() or path.is_dir()), "non-regular bundle input")
        if not path.is_file():
            continue
        name = path.relative_to(root).as_posix()
        if path.name.endswith((".tar", ".tar.xz", ".tar.gz")):
            with tarfile.open(path) as archive:
                for member in archive:
                    if member.isfile():
                        with archive.extractfile(member) as stream:
                            check(stream, name + ":" + member.name)
        else:
            with path.open("rb") as stream:
                check(stream, name)


def targeted_report(candidate, results):
    require(digest(candidate) == TARGETED["candidate"], "not the frozen targeted candidate")
    manifest = json.loads(candidate.read_text())
    paths = {suffix: results / (TARGETED["receipt"] + suffix)
             for suffix in TARGETED["raw_sha256"]}
    for suffix, path in paths.items():
        require(digest(path) == TARGETED["raw_sha256"][suffix], "targeted receipt changed: " + suffix)
    log = paths["-opengl.log"].read_text()
    importlib.import_module("test_transfer_workload").check_receipts(log)
    format_rows = [line for line in log.splitlines() if line.startswith("[ps5-egl-render-blit]")]
    require(len(format_rows) == 20 and sum(" case=" in line for line in format_rows) == 17 and
            all(line.endswith("result=0") for line in format_rows) and
            "conversion=R16F-RGBA32F error=0x0 result=0" in log and
            "[ps5-egl-render-blit] matching=18 result=0" in log and
            "[ps5-egl-transfer-regressions] gates=2 result=0" in log and
            "[pss-opengl-native] gate completed status=0" in log,
            "targeted batch is incomplete or failed")
    lifecycle = json.loads(paths["-result.json"].read_text())
    runner = json.loads(paths["-runner.json"].read_text())
    require(lifecycle["titleId"] == manifest["title"] and
            lifecycle["ebootSha256"].lower() == manifest["eboot_sha256"] and
            lifecycle["libcSha256"].lower() == manifest["libc_sha256"] and
            lifecycle["outcome"] == "entered-eboot" and
            lifecycle["teardownSignal"] == "runtime-layers-released" and
            runner["checkoutCommit"] == manifest["source_commit"] == TARGETED["runtime"] and
            runner["gate"] == manifest["gate"] and runner["ps5Host"] == manifest["host"] and
            runner["protocolCommit"] == manifest["protocol_commit"] and
            runner["postHealthChecked"] is True and runner["lockReleased"] is True,
            "targeted artifact or lifecycle mismatch")
    memory = importlib.import_module("summarize-app-heap")
    heap, gpu = memory.summarize(log, sessions=2), memory.summarize_gpu(log, sessions=2)
    require(heap["post_session_growth_bytes"] == 0 and
            all(row["begin_bytes"] == row["end_bytes"] == row["end_blocks"] == 0
                for kind in ("direct", "mapped") for row in gpu[kind]),
            "targeted memory acceptance failed")
    return dict(scope="targeted native regressions only; not CTS or certification",
                targeted_complete=True, full_matrix_complete=False, clean_cycles=1,
                hardware="one recorded firmware-6.02 console; numerical offscreen checks",
                source_commit=TARGETED["runtime"], candidate_sha256=TARGETED["candidate"],
                eboot_sha256=manifest["eboot_sha256"],
                groups=dict(mip_cycles=dict(executed=24, counts={"Pass": 24},
                            checks="copy/upload/draw/sample; exact mip, layer and base guards"),
                            format_checks=dict(executed=18, counts={"Pass": 18}, receipts=format_rows)),
                egl_sessions=2, heap=heap, gpu=gpu, teardown="runtime-layers-released",
                post_health=True, lock_released=True, raw_sha256=TARGETED["raw_sha256"])


def require_frozen_sdk(profile, sdk_hash, runtime_hash):
    require(sdk_hash == profile["sdk"] and runtime_hash == profile["archive"],
            "SDK differs from the selected frozen candidate")


def require_sample(report):
    require(report["complete"] is False and report["clean_cycles"] == 4,
            "expected four sampled cycles, not a full-matrix claim")
    require(set(report["configurations"]) == set(report["render_targets"]) == set("0123"),
            "sample must cover all four reported targets")
    for config in report["configurations"].values():
        require(config["executed"] == 51 and config["counts"] == {"Pass": 51},
                "every selected case must Pass; no exclusions or missing cases")


def require_consumers(report, sdk_hash):
    require(report.get("status") == "PASS" and
            report.get("manifest", {}).get("sha256") == sdk_hash,
            "consumer checks must pass for this exact SDK")
    require(set(report.get("consumers", {})) == {"make", "pkgconfig", "cmake"} and
            report.get("gl33", {}).get("commands") == 344 and
            report.get("gl33", {}).get("exported") == 344 and
            len(report.get("outputs", {})) == 3,
            "expected three linked consumers and 344 Core exports")


def sample_report(repo, candidate, results, profile, third_party=None):
    require(digest(candidate) == profile["candidate"], "not the frozen sampled candidate")
    manifest = json.loads(candidate.read_text())
    mustpass = (third_party or repo / "third_party") / "VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gl/khronos_mustpass/main/gl33-main.txt"
    require(digest(mustpass) == manifest["mustpass_sha256"], "must-pass identity mismatch")
    official = mustpass.read_text().splitlines()
    require(len(official) == len(set(official)) == 9886, "must-pass count mismatch")
    report = AUDIT.audit(results, manifest, official)
    require_sample(report)
    configurations = {}
    # This exact four-receipt manifest is hash-pinned in configuration order.
    for index, relative in enumerate(manifest["receipts"]):
        path = results / relative
        prefix = str(path).removesuffix("-pss-opengl-cts.qpa")
        selected = Path(prefix + "-cts-shard.txt")
        require(digest(selected) == SAMPLE_HASH, "selected sample changed")
        summary = AUDIT.QPA.summarize(path.read_text(), selected.read_text().splitlines())
        configurations[str(index)] = dict(
            target=report["render_targets"][str(index)], cases=summary["cases"],
            executed=summary["executed"], counts=summary["counts"],
            seconds=summary["seconds"], teardown="runtime-layers-released",
            post_health=True, lock_released=True,
            raw_sha256={suffix: digest(Path(prefix + suffix)) for suffix in
                        ("-pss-opengl-cts.qpa", "-pss-opengl-cts.status", "-result.json",
                         "-runner.json", "-klog.log", "-pss-opengl.log",
                         "-cts-args.txt", "-cts-shard.txt")})
    return dict(scope="sample-validated; not full Core 3.3 coverage or certification",
                sample_complete=True, full_matrix_complete=False,
                executed=204, counts={"Pass": 204}, clean_cycles=4,
                hardware="one recorded firmware-6.02 console; numerical checks",
                candidate_sha256=profile["candidate"], eboot_sha256=manifest["eboot_sha256"],
                cts_commit=manifest["cts_commit"], mustpass_sha256=manifest["mustpass_sha256"],
                selected_cases_sha256=SAMPLE_HASH, configurations=configurations)


def snapshot(repo, revision, name, destination):
    with destination.open("xb") as stream:
        subprocess.run(["git", "-C", str(repo), "archive", "--format=tar",
                        "--prefix=" + name + "/", revision], stdout=stream, check=True)


def copy_member(archive, member, destination):
    require(member.isfile(), "source member is not a regular file")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with archive.extractfile(member) as source, destination.open("xb") as output:
        shutil.copyfileobj(source, output)


def write_json(path, value):
    with path.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps(value, indent=2) + "\n")


def sdl_builder():
    # Import the checkout's verifier, never executable code from --sdl-build.
    path = Path(__file__).resolve().parents[1] / "integration/SDL2/build.py"
    spec = importlib.util.spec_from_file_location("sdl_bundle_builder", path)
    builder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(builder)
    require(callable(getattr(builder, "verify_native_build", None)),
            "--sdl-build requires integration/SDL2/build.py with verify_native_build")
    return builder


def verify_sdl(native, sdk, sdk_hash, runtime_hash):
    """Reuse SDL's full source/input/payload checker; pin the bytes to be copied."""
    receipt = sdl_builder().verify_native_build(native, sdk)
    raw = (native / "receipt.json").read_bytes()
    require(json.loads(raw) == receipt, "SDL receipt changed during verification")
    require(receipt.get("schema_version") == 1 and receipt.get("mode") == "native" and
            receipt.get("hardware_run") is False,
            "requires an offline native SDL build, without hardware claims")
    require((receipt.get("sdk_manifest_sha256"), receipt.get("sdk_runtime_sha256")) ==
            (sdk_hash, runtime_hash), "SDL does not match the active graphics SDK pair")
    payload = (native / "sdk/share/SDL2/receipt.json").read_bytes()
    require(hashlib.sha256(payload).hexdigest() == receipt["payload_receipt_sha256"],
            "SDL payload receipt changed during verification")
    return receipt, hashlib.sha256(raw).hexdigest(), json.loads(payload)["artifacts"]


def copy_sdl(native, stage, checked, epoch):
    receipt, receipt_hash, installed = checked
    files = {"sdl2/" + name: (native / "sdk" / name, checksum)
             for name, checksum in installed.items()}
    files.update({
        "sdl2/manifest.sha256": (native / "sdk/manifest.sha256", receipt["payload_manifest_sha256"]),
        "sdl2/share/SDL2/receipt.json": (native / "sdk/share/SDL2/receipt.json", receipt["payload_receipt_sha256"]),
        "sources/SDL2.tar": (native / "sdl-source.tar", receipt["sdl_source_tar_sha256"]),
        "verification/sdl2-build-receipt.json": (native / "receipt.json", receipt_hash),
    })
    for name, (source, checksum) in sorted(files.items()):
        destination = stage / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        with source.open("rb") as stream, destination.open("xb") as output:
            shutil.copyfileobj(stream, output)
        require(digest(destination) == checksum, "SDL copy changed: " + name)
    with tarfile.open(stage / "sources/SDL2-integration.tar", "x") as archive:
        for name, checksum in sorted(receipt["integration_inputs"].items()):
            data = (native / "integration" / name).read_bytes()
            require(hashlib.sha256(data).hexdigest() == checksum, "SDL integration changed: " + name)
            member = tarfile.TarInfo("SDL2-integration/" + name)
            member.size, member.mtime, member.mode = len(data), int(epoch), 0o644
            archive.addfile(member, io.BytesIO(data))
    return dict(
        prefix="sdl2/", mode="native", hardware_run=False,
        validation="native compile only; no SDL hardware, controller or display acceptance inherited",
        build_receipt="verification/sdl2-build-receipt.json", build_receipt_sha256=receipt_hash,
        **{key: receipt[key] for key in (
            "sdl_commit", "sdl_source_tar_sha256", "sdk_manifest_sha256", "sdk_runtime_sha256",
            "payload_manifest_sha256", "payload_receipt_sha256", "integration_inputs",
            "receipt_tool_sha256", "artifacts")})


def archive_bundle(stage, epoch):
    files = sorted(p for p in stage.rglob("*") if p.is_file())
    with (stage / "SHA256SUMS").open("x") as stream:
        stream.writelines(f"{digest(p)}  {p.relative_to(stage).as_posix()}\n" for p in files)
    archive_path = stage.parent / (stage.name + ".tar.gz")
    with archive_path.open("xb") as stream:
        with gzip.GzipFile(filename="", fileobj=stream, mode="wb", mtime=0) as compressed:
            # Standard tar gives stable ordering, ownership, modes and timestamps.
            with subprocess.Popen(["tar", "--sort=name", "--mtime=@" + epoch,
                                   "--owner=0", "--group=0", "--numeric-owner", "--format=gnu",
                                   "--mode=a=rX,u+w", "-C", str(stage.parent), "-cf", "-", stage.name],
                                  stdout=subprocess.PIPE) as process:
                shutil.copyfileobj(process.stdout, compressed)
                require(process.wait() == 0, "tar failed; archive is incomplete")
    with Path(str(archive_path) + ".sha256").open("x") as stream:
        stream.write(f"{digest(archive_path)}  {archive_path.name}\n")
    return archive_path, len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--results", type=Path)
    parser.add_argument("--ci-version", help="distinct CI-built, NOT console-validated bundle")
    parser.add_argument("--ci-profile", choices=DISPLAY_PROFILES,
                        help="fresh-build profile (default: 1080p60); no hardware acceptance")
    parser.add_argument("--sample-version", choices=SAMPLES,
                        help="frozen sampled release (default: September 7)")
    parser.add_argument("--targeted-version", choices=[TARGETED_VERSION],
                        help="frozen G19 native regression bundle; not CTS acceptance")
    parser.add_argument("--hfr-profile", choices=HFR,
                        help="frozen G31 GL + G32 SDL; focused G37/G38/G39/G40 evidence only")
    parser.add_argument("--g47-profile", choices=G47,
                        help="pinned G47 derivative + new SDL payload and focused hardware receipts")
    parser.add_argument("--g55-profile", choices=G55,
                        help="frozen G55 pair (4K native-focused, 1440p host-only); --candidate is its pinned index")
    parser.add_argument("--g62-profile", choices=G62,
                        help="frozen optimized release pair; --candidate is its pinned qualification index")
    parser.add_argument("--derivative-provenance", type=Path, help="reviewed G47 provenance.json; G47 only")
    parser.add_argument("--consumer-report", type=Path, help="CI/targeted/HFR SDK consumer summary.json")
    parser.add_argument("--runtime-config", type=Path, help="CI runtime-config.txt")
    parser.add_argument("--sdl-build", type=Path,
                        help="optional verified native SDL build; packaged separately in sdl2/")
    parser.add_argument("--third-party", type=Path,
                        help="read-only existing dependency source directory; never rebuilds dependencies")
    parser.add_argument("--destination", type=Path, required=True, help="new output directory")
    args = parser.parse_args()
    require(sum(value is not None for value in
                (args.ci_version, args.sample_version, args.targeted_version, args.hfr_profile, args.g47_profile, args.g55_profile, args.g62_profile)) <= 1,
            "CI, sampled, targeted, HFR, G47, G55 and G62 release modes are mutually exclusive")
    require(args.ci_profile is None or args.ci_version is not None, "--ci-profile requires CI mode")
    require(bool(args.derivative_provenance) == bool(args.g47_profile), "--g47-profile requires --derivative-provenance, exclusively")
    frozen_profile = args.g62_profile or args.g55_profile
    evidence_checker = G62_EVIDENCE if args.g62_profile else G55_EVIDENCE
    hfr = args.hfr_profile or args.g47_profile or frozen_profile
    profile = G62[args.g62_profile] if args.g62_profile else G55[args.g55_profile] if args.g55_profile else G47[args.g47_profile] if args.g47_profile else HFR[args.hfr_profile] if args.hfr_profile else TARGETED if args.targeted_version else SAMPLES[args.sample_version or VERSION]
    host_only = bool(frozen_profile and profile.get("qualification") == "host-only")
    if frozen_profile:
        require(args.candidate and args.results and args.consumer_report and args.sdl_build and not args.runtime_config,
                "G55 requires evidence index, local evidence root, consumer report and SDL build; no runtime override")
        g55_evidence = evidence_checker.load_evidence(args.candidate, profile)
    if not args.ci_version and profile.get("sdl_receipt"):
        require(args.sdl_build is not None, "this frozen release requires its accepted SDL build")
    repo = Path(__file__).resolve().parents[1]
    third_party = (args.third_party or repo / "third_party").resolve()
    sdk, output = args.sdk.resolve(), args.destination.resolve()
    require(re.fullmatch(r"[0-9a-f]{40}", args.source_commit), "use a full source commit")
    require(not output.is_relative_to(sdk), "bundle destination must be outside the SDK")
    require(not output.exists(), "refusing to overwrite a bundle directory")
    require(not output.is_relative_to(third_party), "bundle destination must be outside dependency sources")
    if args.derivative_provenance:
        require(not output.is_relative_to(args.derivative_provenance.resolve().parent),
                "bundle destination must be outside derivative inputs")
    if args.sdl_build is not None:
        require(not any(p.is_symlink() for p in
                        (args.sdl_build, *args.sdl_build.absolute().parents)),
                "symlink SDL build directory")
        native = args.sdl_build.resolve()
        require(not output.is_relative_to(native) and not native.is_relative_to(output),
                "bundle destination must be outside the SDL build")
    sdk_hash = CHECK.verify_manifest(sdk)["sha256"]
    runtime_hash = digest(sdk / "lib/libps5_opengl_core33.a")
    if args.sdl_build is not None:
        sdl = verify_sdl(native, sdk, sdk_hash, runtime_hash)
        if not args.ci_version and profile.get("sdl_receipt"):
            require(sdl[1] == profile["sdl_receipt"], "SDL differs from the frozen release receipt")
    if args.ci_version is not None:
        require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+-]{0,95}", args.ci_version),
                "unsafe CI version")
        require(args.consumer_report and args.runtime_config and
                not args.candidate and not args.results,
                "CI mode requires consumer report/config, not hardware receipts")
        require(subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"],
                                        text=True).strip() == args.source_commit,
                "CI source snapshot must be the built checkout")
        subprocess.run(["git", "-C", str(repo), "diff", "--exit-code", "HEAD"], check=True)
        consumers = json.loads(args.consumer_report.read_text())
        require_consumers(consumers, sdk_hash)
        display_profile = require_ci_profile(sdk, args.runtime_config.read_text(), args.ci_profile or "1080p60")
        version = args.ci_version
    elif hfr:
        require(frozen_profile or args.results and not args.runtime_config and
                (args.candidate and not args.consumer_report if args.g47_profile else args.consumer_report and not args.candidate),
                "HFR requires results and consumer report; G47 instead requires its window candidate and derivative provenance")
        require_frozen_sdk(profile, sdk_hash, runtime_hash)
        require(CHECK.display_profile(sdk) == profile["display"], "HFR SDK display profile mismatch")
        require(subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
                == args.source_commit, "HFR source snapshot must own this packaging checkout")
        subprocess.run(["git", "-C", str(repo), "diff", "--exit-code", "HEAD"], check=True)
        subprocess.run(["git", "-C", str(repo), "diff", "--exit-code", profile["runtime"],
                        args.source_commit, "--", "src", "toolchain", "dependencies.json",
                        "tests/ps5/native-app.mk", "native-app"], check=True)
        if args.g47_profile:
            derivative_copies, consumers = verify_g47_derivative(args.derivative_provenance.resolve(), sdk, args.g47_profile)
            consumer_report = derivative_copies["g47-consumer-audit.json"][0]
        else:
            consumer_report = args.consumer_report
            consumers = json.loads(consumer_report.read_text())
            require_consumers(consumers, sdk_hash)
        private_hosts = set()
        if frozen_profile:
            evidence_root = args.results.resolve()
            require(digest(consumer_report) == g55_evidence["consumer_report_sha256"], "G55 consumer report changed")
            require(not subprocess.check_output(["git", "-C", str(repo), "status", "--porcelain", "--untracked-files=normal"],
                                                text=True).strip(), "G55 packaging checkout must be clean, including new files")
            prior_path = G55_EVIDENCE.evidence_path(evidence_root, g55_evidence["g47_provenance"])
            prior_sdk = G55_EVIDENCE.evidence_path(evidence_root, g55_evidence["g47_sdk"], directory=True)
            derivative_copies, _ = verify_g47_derivative(prior_path, prior_sdk, frozen_profile)
            g55_build = evidence_checker.verify_build(g55_evidence, evidence_root, repo, sdk, profile,
                                                 json.loads(prior_path.read_text()))
            focused = evidence_checker.report(evidence_root, profile, sdl, g55_evidence, private_hosts)
            subprocess.run(["git", "-C", str(repo), "diff", "--exit-code", profile["sdl_source"],
                            args.source_commit, "--", "integration/SDL2"], check=True)
            # App builds may precede runner/documentation commits; their actual
            # sources must still be present in the distributed source companion.
            for kind, row in g55_evidence["runs"].items():
                if kind == "cts":
                    continue  # CTS revision and exact executable are checked by its receipt audit.
                subprocess.run(["git", "-C", str(repo), "diff", "--exit-code", row["source_companion"],
                                args.source_commit, "--", "native-app", "tests/ps5", "examples",
                                "integration/SDL2", "tools/build-native-test-app.sh"], check=True)
        else:
            focused = hfr_report(args.results.resolve(), profile, sdl, private_hosts, args.candidate)
        private = private_markers([repo, sdk, native, third_party, args.results.resolve()], private_hosts)
        # Do this before creating any distribution output: path removal would
        # change the frozen library identities and needs a separate decision.
        require_distributable_tree(sdk, private)
        require_distributable_tree(native / "sdk", private)
        version = profile["version"]
    else:
        require(args.candidate and args.results and
                bool(args.consumer_report) == bool(args.targeted_version) and not args.runtime_config,
                "frozen modes require candidate/receipts; targeted mode also requires consumer checks")
        require_frozen_sdk(profile, sdk_hash, runtime_hash)
        subprocess.run(["git", "-C", str(repo), "diff", "--exit-code", profile["runtime"],
                        args.source_commit, "--", "src", "toolchain",
                        "tests/ps5/native-app.mk", "dependencies.json",
                        *profile["native_paths"]], check=True)
        if args.targeted_version:
            sampled = targeted_report(args.candidate, args.results)
            consumers = json.loads(args.consumer_report.read_text())
            require_consumers(consumers, sdk_hash)
        else:
            sampled = sample_report(repo, args.candidate, args.results, profile, third_party)
        version = args.targeted_version or args.sample_version or VERSION
    epoch = subprocess.check_output(["git", "-C", str(repo), "show", "-s", "--format=%ct",
                                     args.source_commit], text=True).strip()
    require(epoch.isdecimal(), "invalid source timestamp")
    name = "ps5-opengl-sdk-" + version
    stage = output / name
    stage.mkdir(parents=True, exist_ok=False)
    shutil.copytree(sdk, stage / "sdk")
    require(CHECK.verify_manifest(stage / "sdk")["sha256"] == sdk_hash, "SDK copy changed")
    sources = stage / "sources"
    sources.mkdir()
    if args.sdl_build is not None:
        sdl_provenance = copy_sdl(native, stage, sdl, epoch)
    snapshot(repo, args.source_commit, "ps5-opengl", sources / "ps5-opengl.tar")
    selected = {"LICENSE", "THIRD_PARTY_NOTICES.md", "dependencies.json",
                "native-app/app_heap.c", "native-app/agc_link_stub.c",
                "native-app/agc_driver_link_stub.c", "tools/check-sdk-consumers.py"}
    with tarfile.open(sources / "ps5-opengl.tar") as archive:
        for member in archive.getmembers():
            relative = member.name.removeprefix("ps5-opengl/")
            if member.isfile() and (relative in selected or relative.startswith(
                    ("LICENSES/", "docs/", "examples/")) or
                    args.sdl_build is not None and relative.startswith("integration/SDL2/")):
                require(".." not in Path(relative).parts and not Path(relative).is_absolute(),
                        "unsafe source path")
                copy_member(archive, member, stage / relative)
    guide = "ci-releases.md" if args.ci_version is not None else profile["guide"]
    # Keep documentation in docs/: copying it to the root breaks relative links.
    (stage / "README.md").write_text(
        f"# PS5 OpenGL SDK {version}\n\n"
        + ("Host-checked only; NOT console-validated.\n\n" if args.ci_version is not None or host_only
           else f"Frozen G62 {hfr} GL + SDL2; 202 sampled CTS executions, 82 focused GPU cases and bounded application/lifecycle checks. Two-minute stability only; not full CTS or universal stability.\n\n" if args.g62_profile
           else f"Frozen G55 {hfr} GL + SDL2; six focused depth/ImGui/SDL checks. Startup diagnostics are not cadence acceptance. No sampled or full CTS acceptance.\n\n" if args.g55_profile
           else f"Frozen {hfr} GL + SDL2; focused timing/pixels/HDMI checks only. No sampled or full CTS acceptance.\n\n" if hfr
           else "Targeted native checks: 24 mip cycles + 18 format checks; NOT a full CTS campaign or certification.\n\n" if args.targeted_version
           else "Sample-validated; NOT a full CTS campaign or certification.\n\n")
        + f"Read [scope, verification and use](docs/{guide}) before using this SDK.\n\n"
        "Compiled libraries and headers are in `sdk/`; sources, examples, licenses,\n"
        "checksums and provenance are included. Nothing is automatically installed.\n"
        + ("\nSDL2 is in `sdl2/`; its offline build receipt remains unchanged. Separate\n"
           "`focused-validation.json` records the exact frozen pair's short hardware checks.\n"
           "SDL and integration sources are in `sources/SDL2*.tar`; licenses are in\n"
           "`sdl2/share/licenses/`.\n" if hfr and not host_only else
           "\nOptional SDL2 is in `sdl2/`, with its own manifest and receipts. Its native\n"
           "compile results do not establish SDL hardware, controller or display acceptance.\n"
           "SDL and integration sources are in `sources/SDL2*.tar`; licenses are in\n"
           "`sdl2/share/licenses/`. See `provenance.json` for exact input and payload identities.\n"
           if args.sdl_build is not None else ""),
        encoding="utf-8")
    pins = json.loads((stage / "dependencies.json").read_text())
    mesa = third_party / "mesa-26.2.0.tar.xz"
    require(digest(mesa) == pins["mesa"]["sha256"], "Mesa source archive hash mismatch")
    shutil.copyfile(mesa, sources / mesa.name)
    for dep in ("opengnm-psbc", "opengnm", "SPIRV-Headers", "Vulkan-Headers",
                "imgui", "nanovg", "sokol", "sokol-samples"):
        snapshot(third_party / dep, pins["repositories"][dep]["revision"],
                 dep, sources / (dep + ".tar"))
    with tarfile.open(mesa) as archive:
        copy_member(archive, archive.getmember("mesa-26.2.0/src/mesa/glapi/glapi/registry/gl.xml"),
                    stage / "verification/gl.xml")
    for dep, license_name in (("SPIRV-Headers", "LICENSE"), ("Vulkan-Headers", "LICENSE.md")):
        with tarfile.open(sources / (dep + ".tar")) as archive:
            copy_member(archive, archive.getmember(dep + "/" + license_name),
                        stage / "LICENSES" / (dep + ".txt"))
    provenance = dict(
        version=version, status="local sample-validated distribution candidate; not published",
        runtime_source_commit=profile["runtime"], source_snapshot_commit=args.source_commit,
        sdk_manifest_sha256=sdk_hash, runtime_archive_sha256=runtime_hash,
        psbc_archive_sha256=digest(stage / "sdk/lib/libpsbc.ps5.a"),
        build_flags=dict(PS5_NATIVE_TITLE_RUNTIME=1, PS5_GPU_PRESENT_BATCH=1, PS5_DRAW_PROFILE=1,
                         PS5_SCANOUT_HEIGHT=1080, PS5_SCANOUT_FPS=60,
                         PS5_MULTIDRAW_BATCH=1, PS5_DEFERRED_DRAW_BATCH=1),
        validation="sample-validation.json; historical full-campaign results apply to another SDK",
        source_archives={p.name: digest(p) for p in sorted(sources.iterdir())})
    if args.ci_version is not None:
        write_json(stage / "consumer-validation.json", consumers)
        shutil.copyfile(args.runtime_config, stage / "runtime-config.txt")
        provenance.update(
            version=version, status="Host-built and host-checked; NOT console-validated",
            runtime_source_commit=args.source_commit, sdk_manifest_sha256=sdk_hash,
            runtime_archive_sha256=runtime_hash, build_flags="runtime-config.txt",
            display_profile=display_profile,
            validation="consumer-validation.json: compile/link only; no GPU execution",
            hardware_validation="not performed for this binary",
            payload_sdk=pins["native_boilerplate"]["payload_sdk"],
            payload_sdk_archive_sha256=pins["native_boilerplate"]["payload_sdk_archive_sha256"])
    elif args.targeted_version or hfr:
        write_json(stage / ("focused-validation.json" if hfr else "targeted-validation.json"),
                   focused if hfr else sampled)
        write_json(stage / "consumer-validation.json", dict(
            scope="installed-SDK compile/link checks, not GPU execution", status="PASS",
            manifest=consumers["manifest"], gl33=dict(commands=344, exported=344),
            consumers={name: "PASS" for name in consumers["consumers"]},
            outputs=consumers["outputs"], raw_report_sha256=digest(consumer_report if hfr else args.consumer_report)))
        provenance.update(status="local targeted-native-validated candidate; not published",
                          validation="targeted-validation.json and consumer-validation.json; no inherited CTS results")
        if hfr:
            provenance["build_flags"].update(PS5_SCANOUT_HEIGHT=profile["display"]["height"], PS5_SCANOUT_FPS=120)
            provenance.update(status="local frozen HFR focused-validation candidate; not published",
                              display_profile=profile["display"], packaging_source_commit=args.source_commit,
                              sdl_source_companion=profile.get("sdl_source", HFR_SDL_SOURCE),
                              validation="focused-validation.json and consumer-validation.json; no sampled/full CTS or extended soak")
            sdl_provenance["focused_hardware_validation"] = "focused-validation.json: SDL functional check; not measured FPS"
        if args.g47_profile or frozen_profile:
            for name, (source, checksum) in derivative_copies.items():
                destination = stage / "verification" / name
                shutil.copyfile(source, destination)
                require(digest(destination) == checksum, "G47 evidence copy changed: " + name)
            provenance.update(derivative_provenance="verification/g47-derivative-provenance.json",
                              derivative_provenance_sha256=G47_PROVENANCE, inherited_acceptance=False)
        if frozen_profile:
            gate = "g62" if args.g62_profile else "g55"
            write_json(stage / f"verification/{gate}-build-validation.json", g55_build)
            provenance.update(status=f"local frozen {gate.upper()} qualified candidate; not published",
                              evidence_index_sha256=profile["evidence"],
                              build_validation=f"verification/{gate}-build-validation.json",
                              derivative_provenance_scope=f"historical G47 dependency lineage; {gate.upper()} replaces runtime and G51 replaces Mesa")
            if args.g62_profile:
                provenance["validation"] = "focused-validation.json and consumer-validation.json; 202 sampled CTS executions, not full CTS; bounded 120-second stability"
            if host_only:
                provenance.update(status=f"local frozen {gate.upper()} host-checked candidate; not published",
                                  hardware_validation="not performed for this binary; owner selected 4K-only hardware gate",
                                  validation=f"consumer-validation.json and verification/{gate}-build-validation.json; no native acceptance")
                sdl_provenance["focused_hardware_validation"] = "not performed for this binary"
    else:
        write_json(stage / "sample-validation.json", sampled)
    if args.sdl_build is not None:
        provenance["sdl2"] = sdl_provenance
    write_json(stage / "provenance.json", provenance)
    if hfr:
        require_distributable_tree(stage, private)
    archive_path, count = archive_bundle(stage, epoch)
    print(f"BUNDLE={archive_path}\nFILES={count}\nSHA256={digest(archive_path)}")


if __name__ == "__main__":
    main()
