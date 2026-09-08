#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Offline software-Mesa oracle and two source mutations; no native runner.

python3 tools/test_transfer_workload.py --include /read-only/sdk/include
Outputs stay in this worktree's ignored .local/results/transfer-host directory.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess


TAG = "[ps5-egl-transfer-workload]"
ROOT = Path(__file__).resolve().parents[1]


def check_receipts(log):
    receipts = [line for line in log.splitlines()
                if line.startswith(TAG) and not line.startswith(TAG + " renderer=")]
    expected = [TAG + " begin cases=3 cycles_per_case=8 timing=cpu-wall exact=rgba8"]
    for name, base, mip in (("2d-mip", "512/1", "256/1"),
                            ("array-mip", "256/3", "128/3"),
                            ("volume-mip", "256/4", "128/2")):
        expected.append(f"{TAG} case={name} cycles=8 base={base} mip={mip} "
                        "sample=8 mip_guard=8 base_guard=8 deletes=8 "
                        "cpu_wall_ms=TIME result=0")
    expected += [TAG + " cleanup=1 result=0", TAG + " finished cases=3 cycles=24 result=0"]
    normalized = [re.sub(r"cpu_wall_ms=\d+\.\d{3}", "cpu_wall_ms=TIME", line)
                  for line in receipts]
    assert normalized == expected, (normalized, expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include", type=Path, required=True)
    parser.add_argument("--cc", default="clang-18")
    args = parser.parse_args()
    include = args.include.resolve(strict=True)
    assert (include / "EGL/egl.h").is_file() and (include / "GL/gl.h").is_file()
    out = ROOT / ".local/results/transfer-host"
    out.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "tests/ps5/egl_public_core33_transfer_workload.c").read_text()
    upload = "glTexSubImage3D(target, 1, 3, 5, layer, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);"
    assert source.count("GL_NEAREST_MIPMAP_NEAREST") == source.count(upload) == 1
    variants = {
        "reference": (source, None, 3),
        "wrong-filter": (source.replace("GL_NEAREST_MIPMAP_NEAREST", "GL_NEAREST"), "sample", 0),
        "extra-layer-write": (source.replace(upload, upload + "\n      " +
                                             upload.replace("5, layer,", "5, 0,")), "mip", 1),
    }
    environment = dict(os.environ, EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1",
                       GALLIUM_DRIVER="llvmpipe", LP_NUM_THREADS="2",
                       MESA_GL_VERSION_OVERRIDE="3.3", MESA_GLSL_VERSION_OVERRIDE="330",
                       MESA_SHADER_CACHE_DISABLE="true", XDG_CACHE_HOME=str(out / "cache"))
    evidence = {"compiler": subprocess.check_output([args.cc, "--version"], text=True),
                "environment": {key: environment[key] for key in (
                    "EGL_PLATFORM", "LIBGL_ALWAYS_SOFTWARE", "GALLIUM_DRIVER", "LP_NUM_THREADS",
                    "MESA_GL_VERSION_OVERRIDE", "MESA_GLSL_VERSION_OVERRIDE",
                    "MESA_SHADER_CACHE_DISABLE", "XDG_CACHE_HOME")}, "variants": {}}
    for name, (text, failure_phase, completed) in variants.items():
        executable = out / name
        command = [args.cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-DPS5_TRANSFER_HOST_REFERENCE", "-I" + str(include), "-x", "c", "-",
                   "-l:libEGL.so.1", "-l:libGL.so.1", "-o", str(executable)]
        build = subprocess.run(command, input=text, text=True, capture_output=True, timeout=60)
        (out / f"{name}-build.log").write_text(shlex.join(command) + "\n" + build.stdout + build.stderr)
        assert build.returncode == 0, build.stderr
        run = subprocess.run([str(executable)], env=environment, text=True,
                             capture_output=True, timeout=60)
        log = run.stdout + run.stderr
        (out / f"{name}.log").write_text(log)
        if failure_phase is None:
            assert run.returncode == 0, log
            assert "llvmpipe" in log, log
            check_receipts(log)
        else:
            assert run.returncode == 1, log
            assert f"{TAG} mismatch={failure_phase} " in log, log
            assert TAG + " cleanup=1 result=1" in log, log
            assert f"{TAG} finished cases={completed} cycles={completed * 8} result=1" in log, log
        evidence["variants"][name] = {
            "command": command, "returncode": run.returncode,
            "source_sha256": hashlib.sha256(text.encode()).hexdigest(),
            "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "log_sha256": hashlib.sha256(log.encode()).hexdigest(),
        }
        print(f"{name}: PASS ({'exact ordered receipts' if failure_phase is None else 'mutation rejected'})")
    (out / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"Host reference PASS; evidence: {out / 'evidence.json'}; hardware NOT RUN")


if __name__ == "__main__":
    main()
