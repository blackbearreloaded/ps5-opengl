# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Changing runtime flags must rebuild objects; identical flags must not."""
from pathlib import Path
import os
import hashlib
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest


class RuntimeConfigTest(unittest.TestCase):
    def test_cts_runtime_flags(self):
        root = Path(__file__).resolve().parents[1]
        builder = (root / "tools/build-native-cts-app.sh").read_text().replace("\\\n", " ")
        match = re.search(r'^[ \t]*make -C .*? runtime$', builder, re.M)
        self.assertIsNotNone(match, "CTS source-runtime build command is missing")
        command = shlex.split(match[0])
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            for name in ("tests/ps5", "toolchain", "sdk/toolchain"):
                (work / name).mkdir(parents=True)
            shutil.copyfile(root / "tests/ps5/native-app.mk", work / "tests/ps5/native-app.mk")
            for name in ("toolchain/ps5-opengl-core33.mk", "sdk/toolchain/prospero.mk"):
                (work / name).touch()
            command = [arg.replace("$root", str(work)).replace("$sdk", str(work / "sdk"))
                       for arg in command[:-1]]
            command += ["--eval=probe:;@echo $(PS5_OPENGL_RUNTIME_DEFINES)", "probe"]
            for enabled in (None, "0", "1", "0"):
                environment = dict(os.environ, PS5_MULTIDRAW_BATCH="0")
                environment.pop("PS5_DEFERRED_DRAW_BATCH", None)
                if enabled is not None:
                    environment["PS5_DEFERRED_DRAW_BATCH"] = enabled
                output = subprocess.check_output(command, text=True, env=environment)
                self.assertEqual(output.split(), ["-DPS5_NATIVE_TITLE_RUNTIME=1"] +
                    (["-DPS5_MULTIDRAW_BATCH=1", "-DPS5_DEFERRED_DRAW_BATCH=1"] if enabled != "0" else []))

    def test_cts_frozen_sdk(self):
        root = Path(__file__).resolve().parents[1]
        builder = (root / "tools/build-native-cts-app.sh").read_text()
        start = builder.index('if [[ -n ${PS5_OPENGL_PREFIX:-} ]]')
        branch = builder[start:builder.index('\nelse\n', start)] + '\nfi\nprintf "%s\\n" "${opengl_libraries[@]}"\n'
        with tempfile.TemporaryDirectory() as tmp:
            sdk = Path(tmp) / "frozen sdk"
            (sdk / "lib").mkdir(parents=True)
            library = sdk / "lib/libPS5OpenGLCore33.a"
            library.write_bytes(b"GROUP (fixture.a)\n")
            (sdk / "manifest.sha256").write_text(
                hashlib.sha256(library.read_bytes()).hexdigest() + "  lib/libPS5OpenGLCore33.a\n")
            env = dict(os.environ, PS5_OPENGL_PREFIX=str(sdk))
            result = subprocess.run(["bash", "-eu", "-c", branch], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), str(library))
            library.write_bytes(b"changed\n")
            result = subprocess.run(["bash", "-eu", "-c", branch], env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_native_compiler_identity(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            for name in ("toolchain", "tools", "src/platform", "third_party/opengnm-psbc", "sdk/bin", "bin"):
                (work / name).mkdir(parents=True)
            script = work / "toolchain/build-opengnm-psbc-ps5.sh"
            shutil.copyfile(root / "toolchain/build-opengnm-psbc-ps5.sh", script)
            for name in ("dependencies.json", "toolchain/Makefile.opengnm-psbc-ps5",
                         "toolchain/opengnm-psbc-ps5.mak", "src/platform/ps5_mesa_shims.c"):
                (work / name).write_text("original\n")
            (work / "tools/fetch-sources.py").write_text(
                "import os,sys\nassert sys.argv[1:] == ['--verify-psbc']\n"
                "raise SystemExit(2 if os.environ.get('BAD_SOURCE') else 0)\n")
            for compiler in ("prospero-clang", "prospero-clang++"):
                path = work / "sdk/bin" / compiler
                path.write_text("#!/bin/sh\nprintf 'compiler version 1\\n'\n")
                path.chmod(0o755)
            make = work / "bin/make"
            make.write_text("#!/bin/sh\n[ -z \"${FAIL_MAKE:-}\" ] || exit 9\n"
                "printf 'build\\n' >> \"$2/count\"\nprintf 'archive\\n' > \"$2/libpsbc.ps5.a\"\n")
            make.chmod(0o755)
            source = work / "third_party/opengnm-psbc"
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            def change_source(value):
                (source / "input.c").write_text(value)
                subprocess.run(["git", "-C", str(source), "add", "input.c"], check=True)
            change_source("first\n")
            subprocess.run(["git", "-C", str(source), "-c", "user.name=Test",
                            "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture"], check=True)
            head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"])
            env = dict(os.environ, PATH=str(work / "bin") + os.pathsep + os.environ["PATH"],
                       PS5_PAYLOAD_SDK=str(work / "sdk"))
            def run(expected, *args, overrides=None, success=True):
                result = subprocess.run(["bash", str(script), *args], env=env | (overrides or {}),
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
                self.assertEqual(len((source / "count").read_text().splitlines()), expected)
            run(1)
            run(1, "--if-needed")
            change_source("changed patch, same HEAD\n")
            self.assertEqual(subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"]), head)
            run(2, "--if-needed")
            run(2, "--if-needed")
            (source / "libpsbc.ps5.a").write_text("old or damaged archive\n")
            run(3, "--if-needed")
            (work / "toolchain/opengnm-psbc-ps5.mak").write_text("new flags\n")
            run(4, "--if-needed")
            (work / "sdk/bin/prospero-clang").write_text("#!/bin/sh\nprintf 'compiler version 2\\n'\n")
            run(5, "--if-needed")
            stamp = (source / "libpsbc.ps5.identity").read_bytes()
            change_source("pending patch\n")
            run(5, "--if-needed", overrides={"FAIL_MAKE": "1"}, success=False)
            self.assertEqual((source / "libpsbc.ps5.identity").read_bytes(), stamp)
            run(5, "--if-needed", overrides={"BAD_SOURCE": "1"}, success=False)
            run(6, "--if-needed")
            run(6, "--invalid", success=False)

    def test_flags_roundtrip(self):
        source = (Path(__file__).resolve().parents[1] / "toolchain/ps5-opengl-core33.mk").read_text()
        start = source.index("# Track command-line configuration")
        end = source.index("\n$(PS5_OPENGL_BUILD)/ps5_egl.o:", start)
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / "ps5_scanout.h").touch()
            (work / "Makefile").write_text(".DEFAULT_GOAL := all\nPS5_OPENGL_BUILD := .\n"
                "PS5_OPENGL_PLATFORM := .\nPS5_OPENGL_RUNTIME_OBJECTS := object\n" + source[start:end] +
                "\nall: object\nobject:\n\t@printf 'rebuilt\\n' >> count\n\t@touch object\n")
            for defines, expected in (("", 1), ("", 1), ("-DPS5_MULTIDRAW_BATCH=1", 2),
                                      ("-DPS5_MULTIDRAW_BATCH=1", 2), ("", 3),
                                      ("-DPS5_SCANOUT_HEIGHT=2160", 4),
                                      ("-DPS5_SCANOUT_HEIGHT=2160", 4), ("", 5),
                                      ("-DPS5_SCANOUT_FPS=120", 6),
                                      ("-DPS5_SCANOUT_FPS=120", 6), ("-DPS5_SCANOUT_FPS=60", 7), ("", 8)):
                subprocess.run(["make", "-s", "PS5_OPENGL_RUNTIME_DEFINES=" + defines], cwd=work, check=True)
                self.assertEqual(len((work / "count").read_text().splitlines()), expected)
            (work / "ps5_scanout.h").write_text("/* changed layout */\n")
            subprocess.run(["make", "-s", "PS5_OPENGL_RUNTIME_DEFINES="], cwd=work, check=True)
            self.assertEqual(len((work / "count").read_text().splitlines()), 9)
