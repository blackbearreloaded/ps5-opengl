# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Offline, bounded G55 receipt checks. No builds, launches or receipt writes."""
import importlib
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
from opengl_receipts import normalize_log_tags
import re

CHECK = importlib.import_module("check-sdk-consumers")
DISPLAY = importlib.import_module("summarize-display")
IMGUI = importlib.import_module("summarize-imgui-profile")
digest, require = CHECK.digest, DISPLAY.require
PROTOCOL = "7195c969e60735f158d46b5034cd53ae62ef0ebc"
LIBC = "e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036"
# Existing G51 provenance, not a pin or acceptance for the pending G55 runtime.
G51_CANDIDATE = "83db921f072745a4aebfec2403ac44c90b708cef4c5a200b7e25db6f0cacbc6e"
GATES = {
    "mip-blit": "egl_public_core33_depth_mip_blit.o",
    "depth-array-samples": "egl_public_core33_depth_array_samples.o",
    "depth-array-fetch": "egl_public_core33_msaa4_depth_array_texture.o",
    "depth-mip": "egl_public_core33_depth_mip_target.o",
    "imgui": "egl_public_core33_imgui_tv.o",
    "sdl": "egl_public_core33_sdl2.o",
}
MIP_STAGES = (
    ("mip0-mip1", "0:0:2", "1:1:3"), ("mip1-mip2", "0:1:3", "1:2:2"),
    ("mip2-mip0", "0:2:2", "1:0:3"), ("same-chain-0-1", "0:0:2", "0:1:3"),
    ("same-chain-1-0", "0:1:3", "0:0:2"), ("single-mip", "2:0:2", "1:2:3"),
    ("mip-single", "0:1:3", "2:0:2"), ("flip-source-x", "0:1:2", "1:1:3"),
    ("flip-dest-y", "0:1:3", "1:1:2"), ("scale-up", "0:2:2", "1:1:3"),
    ("scale-down", "0:0:3", "1:2:2"), ("flip-scale-scissor", "0:1:2", "1:2:3"),
    ("msaa4-mip", "3:0:3", "1:1:2"), ("mip-msaa4", "0:2:2", "3:0:3"),
)


def is_hash(value, length=64):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{%d}" % length, value) is not None


def evidence_path(root, name, directory=False):
    """Every index reference stays inside the explicitly supplied local root."""
    require(isinstance(name, str), "G55 evidence path must be a string")
    relative = PurePosixPath(name)
    require(relative.parts and str(relative) == name and not relative.is_absolute() and
            ".." not in relative.parts and "\\" not in name and ":" not in name,
            "unsafe G55 evidence path")
    path = root / name
    require(path.resolve().is_relative_to(root.resolve()) and
            not any(p.is_symlink() for p in (path, *path.parents)) and
            (path.is_dir() if directory else path.is_file()), "non-regular G55 evidence input")
    return path


def load_evidence(path, profile):
    for key in ("runtime", "sdk", "archive", "sdl_receipt", "sdl_source", "evidence"):
        require(is_hash(profile.get(key), 40 if key in ("runtime", "sdl_source") else 64),
                "G55 qualification is not yet frozen: " + key)
    require(path.is_file() and not any(p.is_symlink() for p in (path, *path.absolute().parents)),
            "non-regular G55 evidence index")
    require(digest(path) == profile["evidence"], "G55 evidence index changed")
    record = json.loads(path.read_text(encoding="utf-8"))
    require(record["format"] == "ps5-opengl-g55-release-v1" and
            record["display_profile"] == profile["display"] and
            record["runtime_source_commit"] == profile["runtime"] and
            record["sdk_manifest_sha256"] == profile["sdk"] and
            record["runtime_archive_sha256"] == profile["archive"] and
            record["sdl_source_companion"] == profile["sdl_source"] and
            record["sdl_build_receipt_sha256"] == profile["sdl_receipt"] and
            record["inherited_acceptance"] is False, "G55 source/SDK/index identity mismatch")
    host_only = profile.get("qualification") == "host-only"
    require(record.get("qualification", "native-focused") == ("host-only" if host_only else "native-focused"),
            "G55 qualification scope mismatch")
    require(set(record["runs"]) == (set() if host_only else set(GATES)),
            "G55 requires no native receipts for host-only or exactly six focused workloads")
    if not host_only:
        require(record["runs"]["imgui"]["mode"] in ("startup", "window"), "unknown G55 ImGui mode")
    require(is_hash(record["build_provenance_sha256"]), "missing G55 build provenance hash")
    require(is_hash(record["consumer_report_sha256"]), "missing G55 consumer report hash")
    return record


def verify_build(record, root, repo, sdk, profile, original):
    """Preserve G47 dependency lineage and G51's one-member Mesa replacement."""
    prior = original["profiles"][f'{profile["display"]["height"]}p120']["files"]
    actual = {p.relative_to(sdk).as_posix() for p in sdk.rglob("*") if p.is_file()} - {"manifest.sha256"}
    require(actual == set(prior), "G55 SDK file set differs from reviewed dependency lineage")
    for name in actual - {"lib/libps5_opengl_core33.a", "lib/libmesa.a"}:
        require(digest(sdk / name) == prior[name]["derived_sha256"], "G55 dependency changed: " + name)
    mesa_path = evidence_path(root, record["g51_candidate"])
    require(digest(mesa_path) == G51_CANDIDATE, "G51 Mesa provenance changed")
    mesa = json.loads(mesa_path.read_text())
    require(mesa["status"] == "PASS" and mesa["hardware_run"] is False and
            digest(repo / "toolchain/mesa-ps5.patch") == mesa["patch_sha256"],
            "G55 Mesa patch source differs from the reviewed G51 build")
    selected = mesa["profiles"][str(profile["display"]["height"])]
    require(digest(sdk / "lib/libmesa.a") == selected["mesa_archive_sha256"] and
            selected["changed_member"] == "main_fbobject.c.o" and selected["unchanged_mesa_members"] == 219,
            "G55 Mesa archive differs from reviewed G51 replacement")
    build_path = evidence_path(root, record["build_provenance"])
    require(digest(build_path) == record["build_provenance_sha256"], "G55 build provenance changed")
    build = json.loads(build_path.read_text())
    require(build["source_companion"] == profile["runtime"] and build["hardware_run"] is False and
            build["new_manifest"] == dict(sha256=profile["sdk"], files=len(actual)) and
            build["runtime_sha256"] == profile["archive"], "G55 build/source/SDK provenance mismatch")
    return dict(runtime_source_commit=profile["runtime"], sdk_manifest_sha256=profile["sdk"],
                runtime_archive_sha256=profile["archive"],
                build_provenance_sha256=record["build_provenance_sha256"],
                unchanged_g47_files=len(actual) - 2, inherited_acceptance=False,
                mesa=dict(candidate_sha256=G51_CANDIDATE, archive_sha256=selected["mesa_archive_sha256"],
                          **{key: mesa[key] for key in ("source_companion", "patch_sha256",
                             "raw_object_sha256", "installed_object_sha256", "strip_addrsig_exceptions")}))


def summarize_mip_blit(text):
    rows = re.findall(r"^\[depth-mip-blit\] (.*)$", text, re.M)
    require(rows and re.fullmatch(r"mode=native renderer=.+ version=.+ size=32 levels=0/1/2 "
                                 r"array_layers=2/3 uniform_samples=1 sample_isolation=0", rows[0]),
            "missing native mip-blit identity")
    expected, case = [], 0
    for fmt in ("D32", "D32S8"):
        for target, pixels in (("2D", 4736), ("2D-array", 18944)):
            expected.append(f"format={fmt} target={target} stage=initial pixels={pixels} result=0")
            for mask in (("depth",) if fmt == "D32" else ("depth", "stencil", "both")):
                for stage, source, destination in MIP_STAGES:
                    case += 1
                    if target == "2D":
                        source, destination = source[:-1] + "0", destination[:-1] + "0"
                    samples = f'{4 if source.startswith("3:") else 1}->{4 if destination.startswith("3:") else 1}'
                    expected.append(f"case={case} format={fmt} target={target} stage={stage} mask={mask} "
                                    f"src={source} dst={destination} samples={samples} pixels={pixels} "
                                    "depth_errors=0 stencil_errors=0 result=0")
            expected.append(f"format={fmt} target={target} cleanup=1 result=0")
    expected.append("summary cases=112 passed=112 pixels=1373440 depth_pixels=1373440 stencil_pixels=1018240 "
                    "errors=0 depth_errors=0 stencil_errors=0 gl_errors=0 fbo_errors=0 "
                    "driver_errors=0 egl_error=0x3000 cleanup=1 result=0")
    require(rows[1:] == expected, "mip-blit case/pixel/error/cleanup receipts differ")
    return dict(cases=112, depth_pixels=1373440, stencil_pixels=1018240,
                formats=["D32", "D32S8"], samples=[1, 4], levels=[0, 1, 2],
                cleanup=True, individual_sample_isolation=False)


def summarize_workload(kind, text, display, mode=None):
    """Numerical contract shared with the maintainer's offline raw auditor."""
    require(kind in GATES, "unknown G55 workload")
    require(re.findall(r"\[ps5-opengl-native\] gate completed status=(-?\d+)", normalize_log_tags(text)) == ["0"],
            "G55 native gate missing, duplicated or failed")
    require(not re.search(r"^\[ps5-imgui\] FAIL|^\[ps5-gallium\] (?:draw-rejected|reject-|clear-gpu-color status=(?!0\b))",
                          text, re.M), "G55 driver reported a failed operation")
    if kind == "mip-blit":
        return summarize_mip_blit(text)
    if kind == "depth-array-samples":
        tag = "[ps5-egl-core33-depth-array-samples]"
        require(len([row for row in text.splitlines() if row.startswith(tag)]) == 33,
                "array sample receipt count differs")
        require(text.splitlines().count(tag + " draw_counter=native-driver") == 1, "missing native draw counter")
        rows = re.findall(re.escape(tag) + r" samples=(\d+) phase=(\d+) layer=(\d+) pixels=(\d+)/(\d+)", text)
        require(rows == [(str(s), str(p), str(l), "1024", "1024")
                         for s in (1, 4) for p in range(3) for l in range(4)], "array sample pixels incomplete")
        draws = re.findall(re.escape(tag) + r" samples=(\d+) layer=(\d+) draw=0/(\d+)->0/(\d+)", text)
        require([(s, l) for s, l, _, _ in draws] == [("1", "2"), ("1", "3"), ("4", "2"), ("4", "3")] and
                all(int(b) == int(a) + 1 for _, _, a, b in draws), "array sample draw counts differ")
        for samples in (1, 4):
            require(text.splitlines().count(f"{tag} samples={samples} explicit_draws=2 cleanup=1 result=0") == 1,
                    "array sample variant failed")
        require(len(re.findall(re.escape(tag) + r" final_draw=0/[1-9][0-9]* result=0", text)) == 1 and
                text.splitlines().count(tag + " cleanup=1 result=0") == 1 and tag + " mismatch" not in text,
                "array sample final status failed")
        return dict(samples=[1, 4], layers=4, modified_layers=[2, 3], phases=3,
                    pixel_tuples=24576, explicit_draws=4, cleanup=True, individual_sample_isolation=False)
    if kind == "depth-array-fetch":
        tag = "[ps5-egl-msaa4-depth-array-texture]"
        require(len([row for row in text.splitlines() if row.startswith(tag)]) == 5 and
                len(re.findall(r"^" + re.escape(tag) + r" draw_counter=native-driver renderer=.+ version=.+ "
                               r"uniform_samples=1 sample_isolation=0$", text, re.M)) == 1,
                "missing native fetch identity")
        for fmt, stencil in (("D32", "0/0"), ("D32S8", "1024/1024")):
            row = (f"{tag} format={fmt} samples=4 fixed=1 layers=2/3 resolve=1024/1024 stencil={stencil} "
                   "sampled=1024/1024 rgba=255/255/255/255 draw_delta=1 cleanup=1 result=0")
            require(text.splitlines().count(row) == 1, "array fetch pixels incomplete")
        require(len(re.findall(re.escape(tag) + r" final_draw=0/[1-9][0-9]* result=0", text)) == 1 and
                text.splitlines().count(tag + " cleanup=1 result=0") == 1, "array fetch cleanup failed")
        return dict(formats=["D32", "D32S8"], samples=4, layers=[2, 3], resolved_depth_pixels=4096,
                    resolved_stencil_pixels=2048, sampled_pixels=2048, explicit_draws=2,
                    cleanup=True, individual_sample_isolation=False)
    if kind == "depth-mip":
        rows = ["invalid_3d_error=0x502 expected=0x502 result=0",
                "matching=5 depths=0.500000/0.500000/0.500000/0.500000/0.500000 draw=0/5 rejected_3d=1 error=0x0 result=0",
                "cleanup=1 result=0"]
        require(re.findall(r"^\[ps5-egl-core33-depth-mip-target\] (.*)$", text, re.M) == rows,
                "depth mip target receipt failed")
        return dict(strict_receipts=rows, cleanup=True)
    if kind == "sdl":
        rows = re.findall(r"^\[sdl2-g19\] (.*)$", text, re.M)
        require(rows == [f'GL=3.3 (Core Profile) Mesa 26.2.0 drawable={display["width"]}x{display["height"]} nominal_refresh=120Hz (not negotiated HDMI)',
                         "probe frame=0 rgba=0,38,102,255 expected=0,38,102,255 pass=1",
                         "probe frame=179 rgba=254,38,102,255 expected=254,38,102,255 pass=1",
                         "frames=180 probes=2 status=0"], "SDL frame/pixel/profile receipt failed")
        return dict(frames=180, pixel_probes=2, measured_fps=None, physical_input_verified=False)
    require(mode in ("startup", "window"), "unknown G55 ImGui mode")
    if mode == "window":
        report = IMGUI.summarize(text, window_target=120, window_height=display["height"],
                                 output_status=True, prepare_profile=True)
        window = report["window_benchmark"]
        require(window["target_met"] is True and window["achieved_fps"] >= 114 and
                30 <= window["seconds"] <= 31, "G55 ImGui window timing failed")
        return report
    report = IMGUI.summarize(text, deferred_batches=True, prepare_profile=True)
    rows = re.findall(r"^\[ps5-imgui-startup\] (.*)$", text, re.M)
    require(len(rows) == 1, "missing or duplicate startup profile")
    pairs = [token.split("=", 1) for token in rows[0].split()]
    require(all(len(pair) == 2 for pair in pairs), "malformed startup field")
    fields = dict(pairs)
    expected = {"frame30_seconds", "window_seconds", "window_complete", "clock_valid",
                "window_stage_ms", "window_outside_stages_ms", "window_snapshot_log_ms"}
    expected.update(group + "_" + item for group in ("frame0", "first30", "window")
                    for item in ("frames", "ui_ms", "clear_ms", "draw_ms", "readback_ms", "swap_ms"))
    require(len(fields) == len(pairs) and set(fields) == expected, "unexpected startup fields")
    require(all(math.isfinite(float(v)) and float(v) >= 0 for v in fields.values()), "invalid startup timing")
    require(fields["clock_valid"] == fields["window_complete"] == fields["frame0_frames"] == "1" and
            fields["first30_frames"] == "30" and int(fields["window_frames"]) == report["frames"] + report["warmup"] and
            30 <= float(fields["window_seconds"]) < 31 and
            abs(float(fields["window_stage_ms"]) + float(fields["window_outside_stages_ms"]) -
                float(fields["window_seconds"]) * 1000) < .001, "startup count/clock/accounting mismatch")
    stages = ("ui", "clear", "draw", "readback", "swap")
    require(float(fields["frame30_seconds"]) <= float(fields["window_seconds"]) and
            float(fields["window_snapshot_log_ms"]) <= float(fields["window_outside_stages_ms"]) + .001 and
            abs(sum(float(fields[f"window_{s}_ms"]) for s in stages) - float(fields["window_stage_ms"])) < .001 and
            all(float(fields[f"frame0_{s}_ms"]) <= float(fields[f"first30_{s}_ms"]) + .001 and
                float(fields[f"first30_{s}_ms"]) <= float(fields[f"window_{s}_ms"]) + .001 for s in stages),
            "startup stage totals or intervals differ")
    report.update(startup_diagnostic=fields, startup_inclusive_fps=int(fields["window_frames"]) / float(fields["window_seconds"]),
                  not_soak_acceptance=True)
    return report


def report(root, profile, sdl, evidence, private_hosts):
    receipt, receipt_hash, _ = sdl
    require(receipt_hash == profile["sdl_receipt"] and receipt["display_profile"] == profile["display"] and
            receipt["sdk_manifest_sha256"] == profile["sdk"] and receipt["sdk_runtime_sha256"] == profile["archive"],
            "G55 SDL/GL identity mismatch")
    if profile.get("qualification") == "host-only":
        require(evidence.get("qualification") == "host-only" and evidence["runs"] == {},
                "G55 host-only profile cannot claim native receipts")
        return dict(scope="host-checked only; NOT console-validated", clean_cycles=0,
                    display_profile=profile["display"], runtime_source_commit=profile["runtime"],
                    sdl_source_companion=profile["sdl_source"], inherited_acceptance=False,
                    hardware_validation=False, sample_complete=False, full_matrix_complete=False,
                    extended_soak=False, independent_per_run_tv_observation=False,
                    evidence_index_sha256=profile["evidence"], runs={})
    require(set(evidence["runs"]) == set(GATES), "G55 requires exactly six focused workloads")
    result = dict(scope="six focused exact-binary checks; not CTS or certification", clean_cycles=6,
                  display_profile=profile["display"], runtime_source_commit=profile["runtime"],
                  sdl_source_companion=profile["sdl_source"], inherited_acceptance=False,
                  sample_complete=False, full_matrix_complete=False, extended_soak=False,
                  independent_per_run_tv_observation=False, evidence_index_sha256=profile["evidence"], runs={})
    for kind, entry in evidence["runs"].items():
        audit_path = evidence_path(root, entry["audit"])
        require(is_hash(entry["audit_sha256"]) and digest(audit_path) == entry["audit_sha256"], "G55 audit changed: " + kind)
        accepted = json.loads(audit_path.read_text(encoding="utf-8"))
        app = evidence_path(root, entry["app"])
        require(app.name.endswith("-opengl.log"), "G55 requires a native app receipt")
        prefix = entry["app"].removesuffix("-opengl.log")
        paths = {key: evidence_path(root, prefix + suffix) for key, suffix in
                 (("app", "-opengl.log"), ("klog", "-klog.log"), ("cycle", "-result.json"), ("runner", "-runner.json"))}
        paths["candidate"] = evidence_path(root, entry["candidate"])
        require(set(accepted["raw_sha256"]) == set(paths), "G55 raw receipt set mismatch")
        for key, path in paths.items():
            require(digest(path) == accepted["raw_sha256"][key], "G55 raw receipt changed: " + kind + "/" + key)
        candidate, cycle, runner = [json.loads(paths[key].read_text(encoding="utf-8-sig"))
                                    for key in ("candidate", "cycle", "runner")]
        require(accepted["candidate"] == candidate and candidate["hardware_run"] is False and
                candidate["sdk_manifest_sha256"] == profile["sdk"], "G55 candidate/SDK mismatch")
        require(is_hash(entry["source_companion"], 40) and is_hash(accepted["runner_companion"], 40), "invalid G55 source commit")
        if kind == "sdl":
            require(candidate["display_profile"] == profile["display"] and candidate["example"] == "smoke" and
                    candidate["sdk_runtime_sha256"] == profile["archive"] and candidate["native_receipt_sha256"] == receipt_hash and
                    candidate["template_libc_sha256"] == LIBC and
                    candidate["selected_test_sha256"] == hashlib.sha256((GATES[kind] + "\n").encode()).hexdigest() and
                    entry["source_companion"] == profile["sdl_source"], "G55 SDL relink candidate mismatch")
        else:
            require(candidate["profile"] == profile["display"] and candidate["runtime_sha256"] == profile["archive"] and
                    candidate["source_companion"] == entry["source_companion"] and candidate["gate"] == GATES[kind],
                    "G55 native candidate mismatch")
            flags = (dict(PS5_IMGUI_PROFILE="1", PS5_GPU_MEMORY_PROFILE="1") if entry.get("mode") == "startup" else
                     dict(PS5_IMGUI_PROFILE="1", PS5_IMGUI_WINDOW_BENCHMARK="1", PS5_IMGUI_WINDOW_TARGET="120")) if kind == "imgui" else {}
            require(candidate["build_flags"] == flags, "G55 app flags mismatch")
        require(is_hash(candidate["files"]["eboot.bin"]) and candidate["files"]["sce_module/libc.prx"] == LIBC and
                cycle["titleId"] == "PPSA99005" and cycle["outcome"] == "entered-eboot" and
                cycle["teardownSignal"] == "runtime-layers-released" and cycle["ebootSha256"].lower() == candidate["files"]["eboot.bin"] and
                cycle["libcSha256"].lower() == LIBC and runner["gate"] == GATES[kind] and
                runner["checkoutCommit"] == accepted["runner_companion"] and runner["protocolCommit"] == PROTOCOL and
                runner["postHealthChecked"] is True and runner["lockReleased"] is True,
                "G55 native identity/lifecycle mismatch")
        require(isinstance(runner["ps5Host"], str) and runner["ps5Host"], "missing G55 receipt host")
        private_hosts.add(runner["ps5Host"])
        workload = summarize_workload(kind, app.read_text(encoding="utf-8-sig"), profile["display"], entry.get("mode"))
        hdmi = DISPLAY.hdmi_report(paths["klog"].read_text(encoding="utf-8-sig"), "PPSA99005",
                                   profile["display"]["width"], profile["display"]["height"], 119.88)
        if kind not in ("imgui", "sdl"):
            hdmi["acceptance_scope"] = "not-a-display-test"
        else:
            require(hdmi["classification"] == "verified-match" and hdmi["restored_60hz"] is True and
                    all(hdmi["captured_hdmi_sequence"][-1][key] == profile["display"][key] for key in ("width", "height")),
                    "G55 native HDMI/restoration failed")
        require(accepted["classification"] == "pass" and accepted["workload"] == workload and accepted["display"] == hdmi,
                "G55 acceptance does not reproduce: " + kind)
        result["runs"][kind] = dict(workload=workload, app_build_source_companion=entry["source_companion"],
            runner_source_companion=runner["checkoutCommit"], eboot_sha256=candidate["files"]["eboot.bin"],
            acceptance_sha256=entry["audit_sha256"], raw_sha256=accepted["raw_sha256"],
            teardown="runtime-layers-released", post_health=True, lock_released=True,
            display_scope="native HDMI logs" if kind in ("imgui", "sdl") else "not-a-display-test",
            hdmi={label: {key: row[key] for key in ("width", "height", "refresh_hz")} if row else None
                  for label, row in (("active", hdmi["negotiated_active"]),
                                     ("restored", hdmi["captured_hdmi_sequence"][-1] if hdmi["captured_hdmi_sequence"] else None))})
        if kind == "imgui":
            result["runs"][kind]["mode"] = entry["mode"]
            result["runs"][kind]["startup_is_cadence_acceptance"] = False
    return result
