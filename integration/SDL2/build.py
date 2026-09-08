#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Offline SDL2 build against a verified public SDK; all outputs are isolated."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
SDL_REV = "8c56053f13ca13a0c050de613706ff69eb615836"
# Integrity pin for `git archive SDL_REV`; this is not source authentication.
SDL_SOURCE_SHA256 = "9dc445a5add6a6abccbad323d173881fe489ec5fe196c26bc08f127c47f00ee4"
# These two in-tree Android links are part of the exact pinned upstream archive.
SDL_SOURCE_LINKS = {
    "android-project-ant/AndroidManifest.xml": "../android-project/app/src/main/AndroidManifest.xml",
    "android-project-ant/src": "../android-project/app/src/main/java",
}
spec = importlib.util.spec_from_file_location("sdk_checker", ROOT / "tools/check-sdk-consumers.py")
SDK_CHECKER = importlib.util.module_from_spec(spec)
spec.loader.exec_module(SDK_CHECKER)
NATIVE_ARTIFACTS = {"cmake/sdl/libSDL2.a", "cmake/CMakeFiles/g19-example.dir/example.c.obj"}
PAYLOAD_RECEIPT = "share/SDL2/receipt.json"
PAYLOAD_REQUIRED = {
    "include/SDL2/SDL.h", "include/SDL2/SDL_config.h", "include/SDL2/SDL_revision.h",
    "lib/libSDL2.a", "lib/pkgconfig/sdl2.pc", "lib/cmake/SDL2/SDL2Config.cmake",
    "lib/cmake/SDL2/SDL2staticTargets.cmake", "share/licenses/SDL2/LICENSE.txt",
    "share/licenses/SDL2-PS5/LICENSE", "share/SDL2/README.md",
}
INTEGRATION_REQUIRED = {"build.py", "folder.py", "CMakeLists.txt", "README.md", "sdl2.pc.in",
                        "static-ps5.patch", "SDL_ps5g19.c", "example.c", "test_contract.c",
                        "ps5g19_display.h"}


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_sdk(prefix):
    """Reuse the public verifier: complete file set, confined paths, no symlinks."""
    for path in prefix.rglob("*"):
        if not path.is_file() and not path.is_dir():
            raise ValueError(f"Special file in SDK: {path}")
    checked = SDK_CHECKER.verify_manifest(prefix)
    SDK_CHECKER.display_profile(prefix)
    required = {
        "include/EGL/egl.h", "include/EGL/eglext.h", "include/EGL/eglplatform.h",
        "include/GL/glcorearb.h", "include/KHR/khrplatform.h",
        "lib/libPS5OpenGLCore33.a", "lib/libSceAgc.so", "lib/libSceAgcDriver.so",
        "lib/pkgconfig/ps5-opengl-core33.pc",
        "lib/cmake/PS5OpenGLCore33/PS5OpenGLCore33Config.cmake",
    }
    required.update(f"lib/lib{name}.a" for name in (
        "ps5_opengl_core33", "glapi_bridge", "psbc.ps5", "mesa", "mesa_sse41",
        "gallium", "glapi", "glsl", "glcpp", "glsl_util", "vtn", "nir", "compiler",
        "mesa_util", "mesa_util_simd", "blake3", "mesa_util_c11"))
    for name in required:
        if not (prefix / name).is_file() or not (prefix / name).stat().st_size:
            raise ValueError(f"Missing required SDK artifact: {name}")
    return {"sdk_manifest_sha256": checked["sha256"], "sdk_files": checked["files"],
            "sdk_runtime_sha256": digest(prefix / "lib/libps5_opengl_core33.a")}


def relative_path(name):
    path = PurePosixPath(name)
    if (not path.parts or path.is_absolute() or ".." in path.parts or "\\" in name
            or ":" in name or str(path) != name):
        raise ValueError(f"Unsafe relative path: {name}")
    return path


def verify_files(root, expected, required, complete=False, excluded=()):
    if not isinstance(expected, dict) or not required <= expected.keys():
        raise ValueError("Missing required receipt artifacts")
    for name, checksum in expected.items():
        relative = relative_path(name)
        path = root / name
        if (not path.resolve().is_relative_to(root)
                or any((root / part).is_symlink() for part in (relative, *relative.parents))):
            raise ValueError(f"Unsafe receipt path: {name}")
        if not isinstance(checksum, str) or not re.fullmatch(r"[0-9a-f]{64}", checksum):
            raise ValueError(f"Invalid receipt hash: {name}")
        if not path.is_file() or digest(path) != checksum:
            raise ValueError(f"Artifact identity mismatch: {name}")
    if complete:
        actual = set()
        for path in root.rglob("*"):
            if path.is_symlink() or not (path.is_file() or path.is_dir()):
                raise ValueError(f"Symlink or special file in receipt tree: {path}")
            if path.is_file():
                actual.add(path.relative_to(root).as_posix())
        if actual != expected.keys() | set(excluded):
            raise ValueError("Receipt file-set mismatch")


def verify_native_build(native, prefix):
    if native != native.resolve() or prefix != prefix.resolve():
        raise ValueError("Use resolved native-build and SDK paths")
    for path in (native / "receipt.json", native / "sdl-source.tar",
                 native / "sdk" / PAYLOAD_RECEIPT, native / "sdk/manifest.sha256"):
        if not path.is_file() or any(p.is_symlink() for p in (path, *path.parents)):
            raise ValueError(f"Non-regular file or symlink in build receipt inputs: {path}")
    receipt = json.loads((native / "receipt.json").read_text())
    if (type(receipt.get("schema_version")) is not int or receipt["schema_version"] != 1
            or receipt.get("mode") != "native" or receipt.get("hardware_run") is not False
            or receipt.get("sdl_commit") != SDL_REV):
        raise ValueError("Requires an offline native SDL build receipt")
    if any(receipt.get(key) != value for key, value in verify_sdk(prefix).items()):
        raise ValueError("Receipt-to-SDK identity mismatch")
    profile = SDK_CHECKER.display_profile(prefix)
    if (receipt.get("display_profile", dict(width=1920, height=1080, fps=60)) != profile
            or ("display_profile" not in receipt and
                (prefix / "include/ps5_opengl_display.h").exists())):
        raise ValueError("Receipt-to-SDK display profile mismatch")
    if digest(native / "sdl-source.tar") != receipt.get("sdl_source_tar_sha256") or \
            receipt.get("sdl_source_tar_sha256") != SDL_SOURCE_SHA256:
        raise ValueError("SDL source tar identity mismatch")
    with tarfile.open(native / "sdl-source.tar", "r:") as tar:
        seen = set()
        for member in tar:
            name = str(relative_path(member.name))
            pinned_link = member.issym() and SDL_SOURCE_LINKS.get(name) == member.linkname
            if name in seen or not (member.isfile() or member.isdir() or pinned_link):
                raise ValueError(f"Unsafe or duplicate SDL tar member: {name}")
            seen.add(name)
        if tar.pax_headers.get("comment") != SDL_REV:
            raise ValueError("SDL tar git-archive revision mismatch")
        license_file = tar.getmember("LICENSE.txt")
        if not license_file.isfile() or license_file.size > 65536:
            raise ValueError("Invalid SDL source license")
        source_license = tar.extractfile(license_file).read()
    # Pre-profile 1080p60 receipts retain their original integration file set.
    required = INTEGRATION_REQUIRED if "display_profile" in receipt else \
        INTEGRATION_REQUIRED - {"ps5g19_display.h"}
    verify_files(native / "integration", receipt.get("integration_inputs"), required,
                 complete=True)
    if receipt.get("receipt_tool_sha256") != receipt["integration_inputs"]["build.py"]:
        raise ValueError("Receipt tool differs from integration snapshot")
    verify_files(native, receipt.get("artifacts"), NATIVE_ARTIFACTS)
    if receipt["artifacts"].keys() != NATIVE_ARTIFACTS:
        raise ValueError("Unexpected native receipt artifacts")
    payload_path = native / "sdk" / PAYLOAD_RECEIPT
    if payload_path.is_symlink() or digest(payload_path) != receipt.get("payload_receipt_sha256"):
        raise ValueError("SDL payload receipt identity mismatch")
    manifest = native / "sdk/manifest.sha256"
    if manifest.is_symlink() or digest(manifest) != receipt.get("payload_manifest_sha256"):
        raise ValueError("SDL payload manifest identity mismatch")
    payload = json.loads(payload_path.read_text())
    if any(payload.get(key) != value for key, value in receipt.items()
           if key not in ("artifacts", "payload_receipt_sha256", "payload_manifest_sha256")):
        raise ValueError("SDL payload provenance mismatch")
    verify_files(native / "sdk", payload.get("artifacts"), PAYLOAD_REQUIRED, complete=True,
                 excluded=(PAYLOAD_RECEIPT, "manifest.sha256"))
    if source_license != (native / "sdk/share/licenses/SDL2/LICENSE.txt").read_bytes():
        raise ValueError("Installed SDL license differs from pinned source")
    if digest(native / "sdk/share/licenses/SDL2-PS5/LICENSE") != digest(ROOT / "LICENSE"):
        raise ValueError("Installed integration license differs from repository license")
    if digest(native / "sdk/share/SDL2/README.md") != receipt["integration_inputs"]["README.md"]:
        raise ValueError("Installed README differs from integration snapshot")
    expected_manifest = dict(payload["artifacts"], **{PAYLOAD_RECEIPT: digest(payload_path)})
    if manifest.read_text() != "".join(f"{checksum}  {name}\n"
                                      for name, checksum in sorted(expected_manifest.items())):
        raise ValueError("SDL payload manifest contents mismatch")
    if payload["artifacts"]["lib/libSDL2.a"] != receipt["artifacts"]["cmake/sdl/libSDL2.a"]:
        raise ValueError("Installed SDL archive differs from native build")
    return receipt


def record_receipt(out, mode, prefix, sdk_identity):
    if verify_sdk(prefix) != sdk_identity:
        raise ValueError("SDK changed during SDL build")
    lane = out / "integration"
    artifacts = [out / "cmake/sdl/libSDL2.a",
                 out / "cmake/CMakeFiles/g19-example.dir/example.c.obj"]
    if mode == "host":
        artifacts = [out / "cmake/sdl/libSDL2.a", out / "cmake/g19-contract"]
    receipt = {
        "schema_version": 1, "mode": mode, "hardware_run": False, "sdl_commit": SDL_REV,
        "sdl_source_tar_sha256": digest(out / "sdl-source.tar"),
        **sdk_identity,
        "display_profile": SDK_CHECKER.display_profile(prefix),
        "integration_inputs": {p.name: digest(p) for p in sorted(lane.iterdir()) if p.is_file()},
        "artifacts": {str(p.relative_to(out)): digest(p) for p in artifacts},
        "receipt_tool_sha256": digest(lane / "build.py"),
    }
    if mode == "native":
        sdk = out / "sdk"
        # Upstream installs these, but their flags do not describe this adapter.
        # The supported consumer interfaces are pkg-config and CMake below.
        (sdk / "bin/sdl2-config").unlink()
        (sdk / "share/aclocal/sdl2.m4").unlink()
        (sdk / "share/licenses/SDL2-PS5").mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / "LICENSE", sdk / "share/licenses/SDL2-PS5/LICENSE")
        (sdk / "share/SDL2").mkdir(parents=True, exist_ok=True)
        shutil.copy2(lane / "README.md", sdk / "share/SDL2/README.md")
        payload = dict(receipt, artifacts={p.relative_to(sdk).as_posix(): digest(p)
                       for p in sorted(sdk.rglob("*")) if p.is_file()})
        (sdk / PAYLOAD_RECEIPT).write_text(json.dumps(payload, indent=2) + "\n")
        receipt["payload_receipt_sha256"] = digest(sdk / PAYLOAD_RECEIPT)
        hashes = dict(payload["artifacts"], **{PAYLOAD_RECEIPT: receipt["payload_receipt_sha256"]})
        (sdk / "manifest.sha256").write_text("".join(
            f"{checksum}  {name}\n" for name, checksum in sorted(hashes.items())))
        receipt["payload_manifest_sha256"] = digest(sdk / "manifest.sha256")
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    if mode == "native":
        verify_native_build(out, prefix)
    print(f"SDL2 {mode} build: {out / 'receipt.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("host", "native"))
    parser.add_argument("--sdl-source", required=True, type=Path)
    parser.add_argument("--sdk-prefix", "--g19-prefix", dest="sdk_prefix", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--payload-sdk", type=Path)
    parser.add_argument("--compiler-wrapper", type=Path)
    args = parser.parse_args()
    source, prefix, out = (p.resolve() for p in (args.sdl_source, args.sdk_prefix, args.out))
    # Never write into the source cache, SDK, another checkout or an existing stage.
    if not out.is_relative_to(ROOT / "build") or out == ROOT / "build" or out.exists():
        parser.error("--out must be a new directory below this clone's build/")
    inputs = [source, prefix, *(p.resolve() for p in (args.payload_sdk, args.compiler_wrapper) if p)]
    if any(out.is_relative_to(p) or p.is_relative_to(out) for p in inputs):
        parser.error("output overlaps a read-only input")
    if args.mode == "native" and (not args.payload_sdk or not args.compiler_wrapper):
        parser.error("native requires --payload-sdk and --compiler-wrapper")
    sdk_identity = verify_sdk(prefix)
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
    if digest(archive) != SDL_SOURCE_SHA256:
        raise ValueError("Pinned SDL git archive checksum mismatch")
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
                 f"-DSDL_SOURCE={snapshot}", f"-DPS5_OPENGL_PREFIX={prefix}",
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
                      f"-DCMAKE_INSTALL_PREFIX={out / 'sdk'}", "-DCMAKE_INSTALL_LIBDIR=lib",
                      "-DCMAKE_C_FLAGS=-D__PROSPERO__ -fPIC -ffunction-sections -fdata-sections "
                      f"-ffile-prefix-map={out}=."]
    run(*configure, env=env)
    run("cmake", "--build", out / "cmake", "--parallel", "8", env=env)
    if args.mode == "host":
        run("ctest", "--test-dir", out / "cmake", "--output-on-failure",
            env={**env, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
                 "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
    else:
        run("cmake", "--install", out / "cmake", env=env)
    record_receipt(out, args.mode, prefix, sdk_identity)


if __name__ == "__main__":
    main()
