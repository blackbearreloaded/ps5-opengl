# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Verify the fixed G62 release index and its raw, local receipts; no console I/O."""
import importlib
import json
from pathlib import Path
import re
import g55_release_evidence as BASE

digest, require, evidence_path, verify_build = BASE.digest, BASE.require, BASE.evidence_path, BASE.verify_build
MEMORY = importlib.import_module("summarize-app-heap")
CTS = importlib.import_module("verify-cts-candidate")
GATES = {
    **{name: "egl_public_core33_gpu_transfer_regression.o" for name in
       ("gpu-blit-extended", "gpu-clear-extended", "native-color-formats")},
    "gpu-mipmap": "egl_public_core33_gpu_mipmap.o",
    "window": "egl_public_core33_imgui_tv.o",
    "offscreen": "egl_public_core33_imgui_benchmark.o",
    "cubes": "egl_public_core33_cubes_profile.o",
    "soak": "egl_public_core33_imgui_tv.o",
    **{f"lifecycle-{n}": "egl_public_core33_imgui_lifecycle.o" for n in range(3)},
    "sdl": "egl_public_core33_sdl2.o",
}
GPU_CASES = {"gpu-blit-extended": 22, "gpu-clear-extended": 46, "native-color-formats": 6, "gpu-mipmap": 8}
DEFERRED = "KHR-GL33.framebuffer_blit.multisampled_to_singlesampled_blit_color_config_test"


def app_source_paths(kind):
    require(kind in GATES, "unknown G62 app gate")
    paths = ["native-app", "tests/ps5", "examples", "integration/SDL2", "tools/build-native-test-app.sh"]
    if not kind.startswith("lifecycle-"):
        # Only the lifecycle target compiles this standalone translation unit.
        # Its paced successor does not change the other frozen applications.
        paths.append(":(exclude)examples/core33-imgui/lifecycle.cpp")
    return paths


def load_evidence(path, profile):
    for key in ("runtime", "sdk", "archive", "sdl_receipt", "sdl_source", "evidence"):
        require(BASE.is_hash(profile.get(key), 40 if key in ("runtime", "sdl_source") else 64),
                "G62 qualification is not frozen: " + key)
    require(path.is_file() and not any(p.is_symlink() for p in (path, *path.absolute().parents)) and
            digest(path) == profile["evidence"], "G62 evidence index changed")
    record = json.loads(path.read_text())
    require(record["format"] == "ps5-opengl-g62-release-v1" and record["inherited_acceptance"] is False,
            "G62 evidence format/scope mismatch")
    for field, pin in (("runtime_source_commit", "runtime"), ("sdk_manifest_sha256", "sdk"),
                       ("runtime_archive_sha256", "archive"), ("sdl_source_companion", "sdl_source"),
                       ("sdl_build_receipt_sha256", "sdl_receipt"), ("display_profile", "display")):
        require(record[field] == profile[pin], "G62 identity mismatch: " + field)
    host_only = profile.get("qualification") == "host-only"
    require(record["qualification"] == ("host-only" if host_only else "native-sampled") and
            set(record["runs"]) == (set() if host_only else set(GATES) | {"cts"}), "G62 required gates/scope differ")
    for field in ("build_provenance_sha256", "consumer_report_sha256"):
        require(BASE.is_hash(record[field]), "G62 missing " + field)
    return record


def workload(kind, text, display, accepted, flags):
    require(re.findall(r"\[pss-opengl-native\] gate completed status=(-?\d+)", text) == ["0"] and
            not re.search(r"^\[ps5-imgui\] FAIL|^\[ps5-gallium\] (?:draw-rejected|reject-)", text, re.M),
            "G62 native operation failed")
    if kind in GPU_CASES:
        # These exact audits and their raw inputs are hash-pinned in the reviewed
        # release index. Do not duplicate their pixel/counter parser here.
        require(accepted["classification"] == "pass" and
                len(accepted["workload"]["cases"]) == GPU_CASES[kind], "G62 focused GPU gate incomplete")
        return accepted["workload"]
    if kind in ("window", "sdl"):
        return BASE.summarize_workload("imgui" if kind == "window" else "sdl", text, display, "window")
    if kind == "offscreen":
        return importlib.import_module("summarize-imgui-benchmark").summarize(text, case=11)
    if kind == "cubes":
        return importlib.import_module("summarize-cubes-profile").summarize(text, height=2160, budget_hz=119.88)
    soak = kind == "soak"
    sessions, samples = (1, 4) if soak else (3, 0)
    heap, gpu = MEMORY.summarize(text, sessions, samples), MEMORY.summarize_gpu(text, sessions, samples)
    require(all(r["begin_bytes"] == r["end_bytes"] == r["end_blocks"] == 0
                for name in ("direct", "mapped") for r in gpu[name]), "G62 GPU allocations not balanced")
    measured = dict(heap=heap, gpu=gpu)
    if soak:
        require(flags["PS5_IMGUI_SOAK_SECONDS"] == "120", "G62 stability duration differs")
        measured["profile"] = BASE.IMGUI.summarize(text, soak=True, soak_seconds=120, soak_target=120,
            strict_soak_fps=False, deferred_batches=True, prepare_profile=True)
        require(all(r["steady_growth_bytes"] == r["steady_range_bytes"] == 0 for rows in
                    (heap["sessions"], gpu["direct"], gpu["mapped"]) for r in rows), "G62 tracked memory grew")
    else:
        require(heap["post_session_growth_bytes"] == 0 and
                re.findall(r"^\[ps5-imgui-lifecycle\] session=(\d+) (begin|PASS)$", text, re.M) ==
                [(str(n), phase) for n in range(3) for phase in ("begin", "PASS")] and
                text.count("[ps5-imgui-lifecycle] finished sessions=3 status=0") == 1 and
                re.findall(r"^\[ps5-imgui\] finished status=(\d+)$", text, re.M) == ["0"] * 3 and
                len(re.findall(r"^\[ps5-imgui\] frame=\d+ .* PASS$", text, re.M)) == 18,
                "G62 lifecycle sessions/pixels/heap failed")
        require(re.findall(r"^\[ps5-imgui-lifecycle\] settle_after=(\d+) seconds=(\d+)$", text, re.M) ==
                [("0", "5"), ("1", "5")], "G62 paced lifecycle intervals differ")
        measured["inter_session_settle_seconds"] = 5
    return measured


def report(root, profile, sdl, evidence, private_hosts):
    receipt, receipt_hash, _ = sdl
    require(receipt_hash == profile["sdl_receipt"] and receipt["display_profile"] == profile["display"] and
            receipt["sdk_manifest_sha256"] == profile["sdk"] and receipt["sdk_runtime_sha256"] == profile["archive"],
            "G62 SDL/GL pair mismatch")
    result = dict(scope="bounded exact-binary release checks; not full CTS or universal stability",
        display_profile=profile["display"], runtime_source_commit=profile["runtime"],
        sdl_source_companion=profile["sdl_source"], evidence_index_sha256=profile["evidence"],
        inherited_acceptance=False, full_matrix_complete=False, extended_soak=False,
        independent_per_run_tv_observation=False, runs={})
    if profile.get("qualification") == "host-only":
        require(evidence["runs"] == {}, "G62 host-only profile cannot claim console tests")
        return dict(result, scope="host-checked only; NOT console-validated", hardware_validation=False,
                    clean_cycles=0, sample_complete=False)
    require(set(evidence["runs"]) == set(GATES) | {"cts"}, "G62 required runs differ")
    cycles = set()
    for kind in GATES:
        entry = evidence["runs"][kind]
        audit = evidence_path(root, entry["audit"])
        require(digest(audit) == entry["audit_sha256"], "G62 audit changed: " + kind)
        accepted = json.loads(audit.read_text())
        prefix = entry["app"].removesuffix("-opengl.log")
        require(prefix != entry["app"], "G62 app receipt suffix differs")
        paths = {key: evidence_path(root, prefix + suffix) for key, suffix in
            (("app", "-opengl.log"), ("klog", "-klog.log"), ("cycle", "-result.json"), ("runner", "-runner.json"))}
        paths["candidate"] = evidence_path(root, entry["candidate"])
        require(set(accepted["raw_sha256"]) == set(paths) and all(digest(p) == accepted["raw_sha256"][k]
                for k, p in paths.items()), "G62 raw receipt changed: " + kind)
        candidate, cycle, runner = (json.loads(paths[k].read_text(encoding="utf-8-sig")) for k in ("candidate", "cycle", "runner"))
        require(accepted["candidate"] == candidate and candidate["hardware_run"] is False and
                candidate["sdk_manifest_sha256"] == profile["sdk"] and
                candidate.get("runtime_sha256", candidate.get("sdk_runtime_sha256")) == profile["archive"] and
                candidate.get("profile", candidate.get("display_profile")) == profile["display"], "G62 candidate mismatch")
        require(cycle["titleId"] == "PPSA99005" and cycle["outcome"] == "entered-eboot" and
                cycle["teardownSignal"] == "runtime-layers-released" and
                cycle["ebootSha256"].lower() == candidate["files"]["eboot.bin"] and
                cycle["libcSha256"].lower() == candidate["files"]["sce_module/libc.prx"] == BASE.LIBC and
                runner["gate"] == GATES[kind] and runner["checkoutCommit"] == accepted["runner_companion"] and
                runner["protocolCommit"] == BASE.PROTOCOL and runner["postHealthChecked"] is True and
                runner["lockReleased"] is True and set(cycle.get("evidenceWarnings", [])) <=
                {"Chiaki video readiness skipped by caller"}, "G62 lifecycle/identity failed")
        if kind == "sdl":
            require(candidate["native_receipt_sha256"] == receipt_hash and candidate["example"] == "smoke" and
                    entry["source_companion"] == profile["sdl_source"],
                    "G62 SDL app receipt differs")
        else:
            require(candidate["source_companion"] == entry["source_companion"] and candidate["gate"] == GATES[kind],
                    "G62 app source/gate differs")
        private_hosts.add(runner["ps5Host"])
        measured = workload(kind, paths["app"].read_text(), profile["display"], accepted, candidate.get("build_flags", {}))
        require(measured == accepted["workload"], "G62 workload audit does not reproduce: " + kind)
        allowed = {"partial-pass"} if kind == "soak" and not measured["profile"]["soak"]["target_met"] else {"pass"}
        require(accepted["classification"] in allowed, "G62 workload failed: " + kind)
        hdmi = BASE.DISPLAY.hdmi_report(paths["klog"].read_text(encoding="utf-8-sig"), "PPSA99005", 3840, 2160, 119.88)
        if kind in GPU_CASES:
            hdmi["acceptance_scope"] = "not-a-display-test"
        elif kind.startswith("lifecycle-"):
            require([(r["height"], r["refresh_hz"]) for r in hdmi["captured_hdmi_sequence"]] ==
                    [(2160, hz) for _ in range(3) for hz in (119.88, 59.94)], "G62 lifecycle HDMI sequence differs")
        else:
            require(hdmi["classification"] == "verified-match" and hdmi["restored_60hz"], "G62 HDMI/restoration failed")
        require(hdmi == accepted["display"], "G62 HDMI audit differs")
        cycles.add(prefix)
        result["runs"][kind] = dict(workload=measured, classification=accepted["classification"],
            eboot_sha256=candidate["files"]["eboot.bin"], raw_sha256=accepted["raw_sha256"],
            acceptance_sha256=entry["audit_sha256"], source_companion=entry["source_companion"],
            clean_teardown=True, post_health=True, lock_released=True)
    entry = evidence["runs"]["cts"]
    manifest_path = evidence_path(root, entry["manifest"])
    require(digest(manifest_path) == entry["manifest_sha256"], "G62 CTS manifest changed")
    manifest = json.loads(manifest_path.read_text())
    require(manifest["runtime_archive_sha256"] == profile["archive"] and manifest["sdk_manifest_sha256"] == profile["sdk"],
            "G62 CTS runtime mismatch")
    cts_root = evidence_path(root, entry["results"], directory=True)
    require(entry["raw_sha256"] and all(digest(evidence_path(cts_root, name)) == checksum
            for name, checksum in entry["raw_sha256"].items()), "G62 CTS raw receipts changed")
    official_path = evidence_path(root, entry["mustpass"])
    require(digest(official_path) == manifest["mustpass_sha256"], "G62 mustpass changed")
    official = official_path.read_text().splitlines()
    summary = CTS.audit(cts_root, manifest, official)
    require(summary["complete"] is False and summary["clean_cycles"] == 4 and
            set(summary["configurations"]) == set(summary["render_targets"]) == set("0123"), "G62 CTS scope differs")
    smoke = evidence["cts_smoke_cases"]
    require(len(smoke) == len(set(smoke)) == 51 and
            digest(evidence_path(root, entry["smoke_list"])) == "915d9f79c90d2ff9ec1c900e59f1e34ed3894747beddc8a53419f8b98710c4c9" and
            evidence_path(root, entry["smoke_list"]).read_text().splitlines() == smoke, "G62 smoke selection differs")
    compact = {}
    for n, relative in enumerate(manifest["receipts"]):
        qpa = evidence_path(cts_root, relative)
        prefix = str(qpa).removesuffix("-pss-opengl-cts.qpa")
        runner = json.loads(Path(prefix + "-runner.json").read_text(encoding="utf-8-sig"))
        require(runner["checkoutCommit"] == manifest["source_commit"], "G62 CTS source differs")
        private_hosts.add(runner["ps5Host"])
        wanted = [name for name in smoke if n < 2 or name != DEFERRED]
        selected = evidence_path(cts_root, relative.removesuffix("-pss-opengl-cts.qpa") + "-cts-shard.txt")
        require(selected.read_text().splitlines() == wanted and summary["configurations"][str(n)]["counts"] == {"Pass": len(wanted)},
                "G62 CTS selected cases/results differ")
        row = CTS.QPA.summarize(qpa.read_text(), wanted)
        compact[str(n)] = dict(target=summary["render_targets"][str(n)], executed=len(wanted), counts=row["counts"],
            seconds=row["seconds"], cases=row["cases"], raw_sha256={suffix: digest(Path(prefix + suffix)) for suffix in
            ("-pss-opengl-cts.qpa", "-pss-opengl-cts.status", "-cts-shard.txt", "-cts-args.txt", "-result.json", "-runner.json", "-pss-opengl.log", "-klog.log")})
    result.update(hardware_validation=True, sample_complete=True, sample_executions=202, clean_cycles=len(cycles) + 4,
                  stability_seconds=120, sample_deferred=[dict(configuration=n, case=DEFERRED, reason="prior execution exceeded 120 seconds") for n in (2, 3)])
    result["runs"]["cts"] = compact
    return result
