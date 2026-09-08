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


def sample_report(repo, candidate, results, profile):
    require(digest(candidate) == profile["candidate"], "not the frozen sampled candidate")
    manifest = json.loads(candidate.read_text())
    mustpass = repo / "third_party/VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gl/khronos_mustpass/main/gl33-main.txt"
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--results", type=Path)
    parser.add_argument("--ci-version", help="distinct CI-built, NOT console-validated bundle")
    parser.add_argument("--sample-version", choices=SAMPLES,
                        help="frozen sampled release (default: September 7)")
    parser.add_argument("--targeted-version", choices=[TARGETED_VERSION],
                        help="frozen G19 native regression bundle; not CTS acceptance")
    parser.add_argument("--consumer-report", type=Path, help="CI/targeted SDK consumer summary.json")
    parser.add_argument("--runtime-config", type=Path, help="CI runtime-config.txt")
    parser.add_argument("--sdl-build", type=Path,
                        help="optional verified native SDL build; packaged separately in sdl2/")
    parser.add_argument("--destination", type=Path, required=True, help="new output directory")
    args = parser.parse_args()
    require(sum(value is not None for value in
                (args.ci_version, args.sample_version, args.targeted_version)) <= 1,
            "CI, sampled and targeted release modes are mutually exclusive")
    profile = TARGETED if args.targeted_version else SAMPLES[args.sample_version or VERSION]
    repo = Path(__file__).resolve().parents[1]
    sdk, output = args.sdk.resolve(), args.destination.resolve()
    require(re.fullmatch(r"[0-9a-f]{40}", args.source_commit), "use a full source commit")
    require(not output.is_relative_to(sdk), "bundle destination must be outside the SDK")
    require(not output.exists(), "refusing to overwrite a bundle directory")
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
        version = args.ci_version
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
            sampled = sample_report(repo, args.candidate, args.results, profile)
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
        + ("Host-checked only; NOT console-validated.\n\n" if args.ci_version is not None
           else "Targeted native checks: 24 mip cycles + 18 format checks; NOT a full CTS campaign or certification.\n\n" if args.targeted_version
           else "Sample-validated; NOT a full CTS campaign or certification.\n\n")
        + f"Read [scope, verification and use](docs/{guide}) before using this SDK.\n\n"
        "Compiled libraries and headers are in `sdk/`; sources, examples, licenses,\n"
        "checksums and provenance are included. Nothing is automatically installed.\n"
        + ("\nOptional SDL2 is in `sdl2/`, with its own manifest and receipts. Its native\n"
           "compile results do not establish SDL hardware, controller or display acceptance.\n"
           "SDL and integration sources are in `sources/SDL2*.tar`; licenses are in\n"
           "`sdl2/share/licenses/`. See `provenance.json` for exact input and payload identities.\n"
           if args.sdl_build is not None else ""),
        encoding="utf-8")
    pins = json.loads((stage / "dependencies.json").read_text())
    mesa = repo / "third_party/mesa-26.2.0.tar.xz"
    require(digest(mesa) == pins["mesa"]["sha256"], "Mesa source archive hash mismatch")
    shutil.copyfile(mesa, sources / mesa.name)
    for dep in ("opengnm-psbc", "opengnm", "SPIRV-Headers", "Vulkan-Headers",
                "imgui", "nanovg", "sokol", "sokol-samples"):
        snapshot(repo / "third_party" / dep, pins["repositories"][dep]["revision"],
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
            validation="consumer-validation.json: compile/link only; no GPU execution",
            hardware_validation="not performed for this binary",
            payload_sdk=pins["native_boilerplate"]["payload_sdk"],
            payload_sdk_archive_sha256=pins["native_boilerplate"]["payload_sdk_archive_sha256"])
    elif args.targeted_version:
        write_json(stage / "targeted-validation.json", sampled)
        write_json(stage / "consumer-validation.json", dict(
            scope="installed-SDK compile/link checks, not GPU execution", status="PASS",
            manifest=consumers["manifest"], gl33=dict(commands=344, exported=344),
            consumers={name: "PASS" for name in consumers["consumers"]},
            outputs=consumers["outputs"], raw_report_sha256=digest(args.consumer_report)))
        provenance.update(status="local targeted-native-validated candidate; not published",
                          validation="targeted-validation.json and consumer-validation.json; no inherited CTS results")
    else:
        write_json(stage / "sample-validation.json", sampled)
    if args.sdl_build is not None:
        provenance["sdl2"] = sdl_provenance
    write_json(stage / "provenance.json", provenance)
    files = sorted(p for p in stage.rglob("*") if p.is_file())
    with (stage / "SHA256SUMS").open("x") as stream:
        stream.writelines(f"{digest(p)}  {p.relative_to(stage).as_posix()}\n" for p in files)
    archive_path = output / (name + ".tar.gz")
    with archive_path.open("xb") as stream:
        with gzip.GzipFile(filename="", fileobj=stream, mode="wb", mtime=0) as compressed:
            # Standard tar gives stable ordering, ownership, modes and timestamps.
            with subprocess.Popen(["tar", "--sort=name", "--mtime=@" + epoch,
                                   "--owner=0", "--group=0", "--numeric-owner", "--format=gnu",
                                   "--mode=a=rX,u+w", "-C", str(output), "-cf", "-", name],
                                  stdout=subprocess.PIPE) as process:
                shutil.copyfileobj(process.stdout, compressed)
                require(process.wait() == 0, "tar failed; archive is incomplete")
    with Path(str(archive_path) + ".sha256").open("x") as stream:
        stream.write(f"{digest(archive_path)}  {archive_path.name}\n")
    print(f"BUNDLE={archive_path}\nFILES={len(files)}\nSHA256={digest(archive_path)}")


if __name__ == "__main__":
    main()
