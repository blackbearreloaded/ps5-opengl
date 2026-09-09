#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Assemble an unlaunched native folder from a verified G26 native build."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from build import ROOT, SDK_CHECKER, digest, run, verify_native_build


def replace_once(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"Native template contract changed: {path}: {old}")
    path.write_text(text.replace(old, new))


def folder_parameters(profile):
    """Called only after verifying the matching SDL build and SDK manifest."""
    param = json.loads((ROOT / "native-app/param.json").read_text())
    language = param["localizedParameters"]["defaultLanguage"]
    param["localizedParameters"][language]["titleName"] = "SDL2 public SDK local candidate"
    if profile["fps"] > 60:
        param["attribute3"] = 0x80040
    return param


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-build", required=True, type=Path)
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--sdk-prefix", "--g19-prefix", dest="sdk_prefix", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    native, template, prefix, out = (p.resolve() for p in
        (args.native_build, args.template, args.sdk_prefix, args.out))
    if (not out.is_relative_to(ROOT / "build") or out == ROOT / "build" or out.exists()
            or any(out.is_relative_to(p) or p.is_relative_to(out) for p in (native, template, prefix))):
        parser.error("--out must be a new directory in this clone's build/, outside inputs")
    receipt = verify_native_build(native, prefix)
    profile = SDK_CHECKER.display_profile(prefix)
    run("sha256sum", "--check", "--strict", "libc.prx.sha256", cwd=template / "runtime")
    template_commit = run("git", "-c", f"safe.directory={template}", "-C", template,
                          "rev-parse", "HEAD", capture_output=True, text=True).stdout.strip()
    out.mkdir(parents=True)
    # Copy every potentially written dependency. No symlinks into canonical caches.
    for directory in ("runtime", "sce_sys", "tooling", "tools", ".deps/native"):
        shutil.copytree(template / directory, out / directory)
    sdk = out / ".deps/native/ps5-payload-sdk"
    for directory in ("src", "vendor", "build/native-imports"):
        (out / directory).mkdir(parents=True)
    for name in ("runtime_shims.c", "app_heap.c"):
        shutil.copy2(ROOT / "native-app" / name, out / "src" / name)
    shutil.copy2(out / "tooling/native/ps5-pie.ld", out / "tooling/native/ps5-pie-base.ld")
    for name in ("ps5-pie.ld", "app-symbols.map"):
        shutil.copy2(ROOT / "native-app" / name, out / "tooling/native" / name)
    param = folder_parameters(profile)
    (out / "sce_sys/param.json").write_text(json.dumps(param, indent=2) + "\n")
    replace_once(out / "tooling/native/sce_module_writer.cpp",
                 "write_u64(result.data, result.heap_size, std::numeric_limits<std::uint64_t>::max());",
                 "write_u64(result.data, result.heap_size, 0x10000000ULL);")
    script = out / "tools/build.sh"
    replace_once(script, 'bash "$root/tools/setup-native-dependencies.sh" >/dev/null',
                 'test -x "$root/.deps/native/ps5-payload-sdk/bin/prospero-lld"')
    replace_once(script, '[[ -f $root/runtime/libc.prx ]] || bash "$root/tools/rebuild-libc.sh"',
                 'test -f "$root/runtime/libc.prx"')
    replace_once(script, "--eh-frame-hdr \\",
                 "--eh-frame-hdr --wrap=malloc --wrap=calloc --wrap=realloc --wrap=free "
                 "--wrap=posix_memalign --wrap=malloc_usable_size \\")
    # Use the verified public SDK's imports, only in the private payload SDK copy.
    env = os.environ.copy()
    env["PS5_PAYLOAD_SDK"] = str(sdk)
    for name in ("libSceAgc.so", "libSceAgcDriver.so"):
        shutil.copy2(prefix / "lib" / name, sdk / "target/lib" / name)
    compiler_rt = Path(run("clang-18", "--print-resource-dir", capture_output=True,
                           text=True).stdout.strip()) / "lib/linux/libclang_rt.builtins-x86_64.a"
    libraries = [native / "sdk/lib/libSDL2.a",
                 native / "cmake/CMakeFiles/g19-example.dir/example.c.obj"]
    libraries += [prefix / "lib/libPS5OpenGLCore33.a"]
    libraries += [sdk / f"target/lib/{name}" for name in ("libunwind.a", "libc++abi.a", "libc++.a")]
    libraries += [compiler_rt]
    for path in libraries:
        if not path.is_file(): raise SystemExit(f"Missing link input: {path}")
    group = (f'SEARCH_DIR("{sdk}/target/lib")\nSEARCH_DIR("{prefix}/lib")\n'
             'EXTERN(ps5_agc_gate2_run)\nGROUP (\n' +
             "".join(f'  "{p}"\n' for p in libraries) + ')\n')
    (out / "vendor/libg26.a").write_text(group)
    env["APP_STATIC_ARCHIVES"] = "vendor/libg26.a"
    for key in ("APP_DEFINITIONS", "APP_INCLUDE_PATHS", "PACBREW_PACKAGES", "PACBREW_INCLUDE_PATHS",
                "PACBREW_STATIC_ARCHIVES", "APP_RUNTIME_MODULES"):
        env[key] = ""
    run("bash", script, "Folder", env=env, cwd=out)
    example = receipt.get("example", "smoke")
    selected = "egl_public_core33_sdl2_input.o" if example == "input-validation" else "egl_public_core33_sdl2.o"
    (out / "selected-test.txt").write_text(selected + "\n")
    run("bash", ROOT / "tools/verify-native-test-app.sh", out)
    if verify_native_build(native, prefix) != receipt:
        raise ValueError("Native build receipt changed during folder assembly")
    candidate = out / "dist" / param["titleId"]
    result = {"hardware_run": False, "template_commit": template_commit,
              "example": example,
              "display_profile": profile,
              "sdk_manifest_sha256": receipt["sdk_manifest_sha256"],
              "sdk_runtime_sha256": receipt["sdk_runtime_sha256"],
              "native_receipt_sha256": digest(native / "receipt.json"),
              "selected_test_sha256": digest(out / "selected-test.txt"),
              "template_libc_sha256": digest(template / "runtime/libc.prx"),
              "files": {str(p.relative_to(candidate)): digest(p)
                        for p in sorted(candidate.rglob("*")) if p.is_file()}}
    (out / "candidate.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"Unlaunched G26 folder: {candidate}; identities: {out / 'candidate.json'}")


if __name__ == "__main__":
    main()
