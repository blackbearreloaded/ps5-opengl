#!/usr/bin/env python3
"""Assemble an unlaunched native folder from a verified G26 native build."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from build import ROOT, digest, run, verify_g19


def replace_once(path, old, new):
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"Native template contract changed: {path}: {old}")
    path.write_text(text.replace(old, new))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native-build", required=True, type=Path)
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--g19-prefix", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    native, template, prefix, out = (p.resolve() for p in
        (args.native_build, args.template, args.g19_prefix, args.out))
    if (not out.is_relative_to(ROOT / "build") or out == ROOT / "build" or out.exists()
            or any(out.is_relative_to(p) for p in (native, template, prefix))):
        parser.error("--out must be a new directory in this clone's build/, outside inputs")
    receipt = json.loads((native / "receipt.json").read_text())
    if receipt["mode"] != "native": parser.error("requires a native build receipt")
    for name, expected in receipt["artifacts"].items():
        path = (native / name).resolve()
        if not path.is_relative_to(native) or digest(path) != expected:
            parser.error(f"native artifact identity mismatch: {name}")
    verify_g19(prefix)
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
    shutil.copy2(ROOT / "native-app/param.json", out / "sce_sys/param.json")
    param = json.loads((out / "sce_sys/param.json").read_text())
    language = param["localizedParameters"]["defaultLanguage"]
    param["localizedParameters"][language]["titleName"] = "SDL2 G19 local candidate"
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
    # Public import stubs, only in the private SDK copy, as in the existing gate builder.
    env = os.environ.copy()
    env["PS5_PAYLOAD_SDK"] = str(sdk)
    for name, soname in (("agc_link_stub", "SceAgc"), ("agc_driver_link_stub", "SceAgcDriver")):
        obj = out / f"build/native-imports/{name}.o"
        run("sh", out / "tooling/prospero-clang18", "-std=c11", "-O2", "-fPIC", "-c",
            ROOT / f"native-app/{name}.c", "-o", obj, env=env)
        run(sdk / "bin/prospero-lld", "--shared", "-soname", f"lib{soname}.prx",
            "-o", sdk / f"target/lib/lib{soname}.so", obj)
    compiler_rt = Path(run("clang-18", "--print-resource-dir", capture_output=True,
                           text=True).stdout.strip()) / "lib/linux/libclang_rt.builtins-x86_64.a"
    libraries = [native / name for name in receipt["artifacts"]]
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
    (out / "selected-test.txt").write_text("egl_public_core33_sdl2.o\n")
    run("bash", ROOT / "tools/verify-native-test-app.sh", out)
    verify_g19(prefix)
    candidate = out / "dist" / param["titleId"]
    result = {"hardware_run": False, "template_commit": template_commit,
              "native_receipt_sha256": digest(native / "receipt.json"),
              "selected_test_sha256": digest(out / "selected-test.txt"),
              "template_libc_sha256": digest(template / "runtime/libc.prx"),
              "files": {str(p.relative_to(candidate)): digest(p)
                        for p in sorted(candidate.rglob("*")) if p.is_file()}}
    (out / "candidate.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"Unlaunched G26 folder: {candidate}; identities: {out / 'candidate.json'}")


if __name__ == "__main__":
    main()
