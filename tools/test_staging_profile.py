#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Parser checks; --include SDK/include also compiles/runs actual software-Mesa pixels.

No native runner. All generated outputs stay in this clone's .local/staging-host.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
API = runpy.run_path(str(ROOT / "tools/summarize-staging-profile.py"))
TAG, CASES, summarize = API["TAG"], API["CASES"], API["summarize"]


def parser_check():
    records = [API["CONFIG"].format(host=1), "renderer=fixture version=3.3", "session_setup_ns=100"]
    records += [f"case={name} framebuffer_srgb={int(name == 'srgb8-2d')} "
                f"encoding={'srgb' if name.startswith('srgb8-') else 'linear'} "
                f"setup_ns=100 warmup_ns=100 before={probes} cycles=3 "
                f"previous_ns=2000000000 measured_ns=3000000000 after={probes} cleanup_ns=100 result=0"
                for name, probes in CASES]
    records += ["finished cases=7 session_cleanup_ns=100 cleanup=1 result=0"]
    valid = "\n".join(TAG + " " + record for record in records)
    assert len(summarize(valid, host=True)["cases"]) == 7
    bad = [valid.replace("host=1", "host=0"), valid.rsplit("\n", 1)[0], valid + "\n" + valid,
           valid.replace("before=24", "before=16"), valid.replace("after=32", "after=0"),
           valid.replace("result=0", "result=1", 1), valid.replace("cycles=3", "cycles=0", 1),
           valid.replace("previous_ns=2000000000", "previous_ns=3000000000", 1),
           valid.replace("measured_ns=3000000000", "measured_ns=2999999999", 1),
           valid.replace("measured_ns=3000000000", "measured_ns=5000000001", 1),
           valid.replace("setup_ns=100", "setup_ns=nan", 1), valid.replace("cleanup=1", "cleanup=0"),
           valid.replace("version=2", "version=1"), valid.replace("encoding=srgb", "encoding=linear", 1),
           valid.replace("framebuffer_srgb=1", "framebuffer_srgb=0"),
           "\n".join(line for line in valid.splitlines() if "case=srgb8-" not in line)]
    for log in bad:
        try:
            summarize(log, host=True)
        except ValueError:
            continue
        raise AssertionError("Invalid receipt accepted")
    print(f"Parser PASS: valid fixture and {len(bad)} negative checks (synthetic, not execution evidence)")


def host_check(include, cc):
    include = include.resolve(strict=True)
    assert (include / "EGL/egl.h").is_file() and (include / "GL/gl.h").is_file()
    out = ROOT / ".local/staging-host"
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT / "tests/ps5/egl_public_core33_staging_profile.c"
    executable = out / "staging-reference"
    command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-DPS5_STAGING_HOST_REFERENCE", "-I" + str(include), str(source),
               "-l:libEGL.so.1", "-l:libGL.so.1", "-o", str(executable)]
    build = subprocess.run(command, text=True, capture_output=True, timeout=60)
    (out / "build.log").write_text(shlex.join(command) + "\n" + build.stdout + build.stderr)
    assert build.returncode == 0, build.stderr
    settings = dict(EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1", GALLIUM_DRIVER="llvmpipe",
                    LP_NUM_THREADS="2", MESA_GL_VERSION_OVERRIDE="3.3", MESA_GLSL_VERSION_OVERRIDE="330",
                    MESA_SHADER_CACHE_DISABLE="true", XDG_CACHE_HOME=str(out / "cache"))
    run = subprocess.run([str(executable)], env=dict(os.environ, **settings),
                         text=True, capture_output=True, timeout=60)
    log = run.stdout + run.stderr
    (out / "reference.log").write_text(log)
    assert run.returncode == 0, log
    report = summarize(log, host=True)
    assert "llvmpipe" in report["renderer"], log
    report.update(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                  receipt_sha256=hashlib.sha256((out / "reference.log").read_bytes()).hexdigest(),
                  compiler=subprocess.check_output([cc, "--version"], text=True),
                  command=command, environment=settings, native_execution=False)
    (out / "evidence.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(f"Actual host pixels PASS: 7 cases; evidence: {out / 'evidence.json'}; PS5 NOT RUN")
    # Ensure the fractional-color oracle catches a missing framebuffer encoding.
    original = source.read_text()
    enable = "if (c->framebuffer_srgb) glEnable(GL_FRAMEBUFFER_SRGB);"
    assert original.count(enable) == 1
    mutation = original.replace(enable, "if (0) glEnable(GL_FRAMEBUFFER_SRGB);")
    executable = out / "srgb-disabled-mutation"
    command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-DPS5_STAGING_HOST_REFERENCE", "-I" + str(include), "-x", "c", "-",
               "-l:libEGL.so.1", "-l:libGL.so.1", "-o", str(executable)]
    build = subprocess.run(command, input=mutation, text=True, capture_output=True, timeout=60)
    (out / "srgb-disabled-build.log").write_text(shlex.join(command) + "\n" + build.stdout + build.stderr)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(executable)], env=dict(os.environ, **settings),
                         text=True, capture_output=True, timeout=30)
    log = run.stdout + run.stderr
    (out / "srgb-disabled.log").write_text(log)
    assert run.returncode == 1 and "mismatch case=srgb8-2d phase=before image=sample" in log, log
    assert "finished cases=1 " in log and "cleanup=1 result=1" in log, log
    report["srgb_disabled_mutation"] = dict(source_sha256=hashlib.sha256(mutation.encode()).hexdigest(),
        executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
        receipt_sha256=hashlib.sha256((out / "srgb-disabled.log").read_bytes()).hexdigest(),
        returncode=run.returncode, command=command)
    (out / "evidence.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print("Framebuffer-sRGB negative pixel check PASS: disabled encoding rejected")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include", type=Path)
    parser.add_argument("--cc", default="clang-18")
    args = parser.parse_args()
    parser_check()
    if args.include:
        host_check(args.include, args.cc)
