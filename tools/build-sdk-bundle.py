#!/usr/bin/env python3
"""Package a frozen sampled SDK or a host-checked CI build; no console access."""
import argparse
import gzip
import importlib
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


def sample_report(repo, candidate, results):
    require(digest(candidate) == CANDIDATE_HASH, "not the frozen sampled candidate")
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
                candidate_sha256=CANDIDATE_HASH, eboot_sha256=manifest["eboot_sha256"],
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--results", type=Path)
    parser.add_argument("--ci-version", help="distinct CI-built, NOT console-validated bundle")
    parser.add_argument("--consumer-report", type=Path, help="CI SDK consumer summary.json")
    parser.add_argument("--runtime-config", type=Path, help="CI runtime-config.txt")
    parser.add_argument("--destination", type=Path, required=True, help="new output directory")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    sdk, output = args.sdk.resolve(), args.destination.resolve()
    require(re.fullmatch(r"[0-9a-f]{40}", args.source_commit), "use a full source commit")
    require(not output.exists(), "refusing to overwrite a bundle directory")
    sdk_hash = CHECK.verify_manifest(sdk)["sha256"]
    runtime_hash = digest(sdk / "lib/libps5_opengl_core33.a")
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
                not args.consumer_report and not args.runtime_config,
                "sampled mode requires the frozen candidate and hardware receipts")
        require(sdk_hash == SDK_HASH and runtime_hash == RUNTIME_HASH,
                "SDK differs from the frozen optimized candidate")
        subprocess.run(["git", "-C", str(repo), "diff", "--exit-code", RUNTIME,
                        args.source_commit, "--", "src", "native-app", "toolchain",
                        "tests/ps5/native-app.mk", "dependencies.json"], check=True)
        sampled = sample_report(repo, args.candidate, args.results)
        version = VERSION
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
    snapshot(repo, args.source_commit, "ps5-opengl", sources / "ps5-opengl.tar")
    selected = {"LICENSE", "THIRD_PARTY_NOTICES.md", "dependencies.json",
                "native-app/app_heap.c", "native-app/agc_link_stub.c",
                "native-app/agc_driver_link_stub.c", "tools/check-sdk-consumers.py"}
    with tarfile.open(sources / "ps5-opengl.tar") as archive:
        for member in archive.getmembers():
            relative = member.name.removeprefix("ps5-opengl/")
            if member.isfile() and (relative in selected or relative.startswith(
                    ("LICENSES/", "docs/", "examples/"))):
                require(".." not in Path(relative).parts and not Path(relative).is_absolute(),
                        "unsafe source path")
                copy_member(archive, member, stage / relative)
    guide = "ci-releases.md" if args.ci_version is not None else "sdk-bundle.md"
    shutil.copyfile(stage / "docs" / guide, stage / "README.md")
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
        version=VERSION, status="local sample-validated distribution candidate; not published",
        runtime_source_commit=RUNTIME, source_snapshot_commit=args.source_commit,
        sdk_manifest_sha256=SDK_HASH, runtime_archive_sha256=RUNTIME_HASH,
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
            version=version, status="CI-built and host-checked; NOT console-validated",
            runtime_source_commit=args.source_commit, sdk_manifest_sha256=sdk_hash,
            runtime_archive_sha256=runtime_hash, build_flags="runtime-config.txt",
            validation="consumer-validation.json: compile/link only; no GPU execution",
            hardware_validation="not performed for this binary",
            payload_sdk=pins["native_boilerplate"]["payload_sdk"],
            payload_sdk_archive_sha256=pins["native_boilerplate"]["payload_sdk_archive_sha256"])
    else:
        write_json(stage / "sample-validation.json", sampled)
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
