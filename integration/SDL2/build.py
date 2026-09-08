#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Offline SDL2/G19 build. Inputs are read-only; every output goes in a new directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SDL_REV = "8c56053f13ca13a0c050de613706ff69eb615836"
G19_MANIFEST = "344673952a789cae7e4a6d4c6670e3cf8c6bbf595fb07ebf27a8ffe3680014be"
G19_ARCHIVE = "627857a44a8101b0ab0df319293a554143e96405be8a1ec55fc48bbd1830caa5"


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_g19(prefix):
    for name, expected in (("manifest.sha256", G19_MANIFEST),
                           ("lib/libps5_opengl_core33.a", G19_ARCHIVE)):
        if digest(prefix / name) != expected:
            raise SystemExit(f"Not frozen G19: {prefix / name}")
    run("sha256sum", "--check", "--strict", "manifest.sha256", cwd=prefix,
        stdout=subprocess.DEVNULL)


def record_receipt(out, mode, prefix):
    """Record a completed build (also usable after a local incremental rebuild)."""
    verify_g19(prefix)
    lane = out / "integration"
    artifacts = [out / "cmake/sdl/libSDL2.a",
                 out / "cmake/CMakeFiles/g19-example.dir/example.c.obj"]
    if mode == "host":
        artifacts = [out / "cmake/sdl/libSDL2.a", out / "cmake/g19-contract"]
    receipt = {
        "mode": mode, "hardware_run": False, "sdl_commit": SDL_REV,
        "sdl_source_tar_sha256": digest(out / "sdl-source.tar"),
        "g19_manifest_sha256": G19_MANIFEST, "g19_runtime_sha256": G19_ARCHIVE,
        "integration_inputs": {p.name: digest(p) for p in sorted(lane.iterdir()) if p.is_file()},
        "artifacts": {str(p.relative_to(out)): digest(p) for p in artifacts},
        "receipt_tool_sha256": digest(Path(__file__).resolve()),
    }
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"G26 {mode} build: {out / 'receipt.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("host", "native"))
    parser.add_argument("--sdl-source", required=True, type=Path)
    parser.add_argument("--g19-prefix", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--payload-sdk", type=Path)
    parser.add_argument("--compiler-wrapper", type=Path)
    args = parser.parse_args()
    source, prefix, out = (p.resolve() for p in (args.sdl_source, args.g19_prefix, args.out))
    # Never write into the source cache, SDK, another checkout or an existing stage.
    if not out.is_relative_to(ROOT / "build") or out == ROOT / "build" or out.exists():
        parser.error("--out must be a new directory below this clone's build/")
    if out.is_relative_to(source) or out.is_relative_to(prefix):
        parser.error("output overlaps a read-only input")
    if args.mode == "native" and (not args.payload_sdk or not args.compiler_wrapper):
        parser.error("native requires --payload-sdk and --compiler-wrapper")
    verify_g19(prefix)
    git = ["git", "-c", f"safe.directory={source}", "-C", source]
    run(*git, "cat-file", "-e", f"{SDL_REV}^{{commit}}")
    out.mkdir(parents=True)
    lane = out / "integration"
    shutil.copytree(HERE, lane, ignore=shutil.ignore_patterns("__pycache__"))
    snapshot = out / "SDL"
    snapshot.mkdir()
    archive = out / "sdl-source.tar"
    with archive.open("wb") as stream:
        run(*git, "archive", SDL_REV, stdout=stream)
    with tarfile.open(archive) as tar:
        tar.extractall(snapshot, filter="data")
    # Do not import the cache's dirty dynapi file or any prebuilt SDL archive.
    # Give git apply a repository boundary: otherwise it can silently ignore
    # paths when this exported source lives below the parent clone's worktree.
    run("git", "init", "--quiet", snapshot)
    run("git", "apply", "--check", lane / "static-ps5.patch", cwd=snapshot)
    run("git", "apply", lane / "static-ps5.patch", cwd=snapshot)
    if "Altered for this statically linked PS5 SDL2 build" not in (snapshot / "src/dynapi/SDL_dynapi.h").read_text():
        raise SystemExit("SDL static patch was not applied")
    configure = ["cmake", "-S", lane, "-B", out / "cmake", "-G", "Ninja",
                 f"-DSDL_SOURCE={snapshot}", f"-DG19_PREFIX={prefix}",
                 "-DCMAKE_BUILD_TYPE=Release"]
    env = os.environ.copy()
    if args.mode == "host":
        configure += ["-DG19_HOST_TEST=ON"]
    else:
        env["PS5_PAYLOAD_SDK"] = str(args.payload_sdk.resolve())
        configure += ["-DCMAKE_SYSTEM_NAME=Generic", "-DCMAKE_SYSTEM_PROCESSOR=x86_64",
                      "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                      f"-DCMAKE_C_COMPILER={args.compiler_wrapper.resolve()}",
                      f"-DCMAKE_CXX_COMPILER={args.compiler_wrapper.resolve()}",
                      "-DCMAKE_C_FLAGS=-D__PROSPERO__ -fPIC -ffunction-sections -fdata-sections"]
    run(*configure, env=env)
    run("cmake", "--build", out / "cmake", "--parallel", "8", env=env)
    if args.mode == "host":
        run("ctest", "--test-dir", out / "cmake", "--output-on-failure")
    record_receipt(out, args.mode, prefix)


if __name__ == "__main__":
    main()
