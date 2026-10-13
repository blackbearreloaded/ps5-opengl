#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Mesa's thread-local variables must not need a C++ initializer on the PS5.

A C++ file that reads an extern thread_local variable calls the variable's
initializer when one exists. The current-context pointer is plain C data, so
nothing defines it: an application that links only the SDK kept an unresolved
reference, and the console ended the process at the first query that reached
it (issue 9). With __thread there is no initializer to look for."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
header = "src/util/u_thread.h"
parts = re.split(r"(?m)^(?=diff --git )", (root / "toolchain/mesa-ps5.patch").read_text())
hunk = [part for part in parts if part.startswith(f"diff --git a/{header} ")]
assert len(hunk) == 1, "the Mesa patch must record one change to " + header


def macro(text):
    """The block that chooses how thread-local variables are declared."""
    start = text.index("#if DETECT_OS_APPLE")
    return text[start:text.index("#endif\n", start) + 7]


with tempfile.TemporaryDirectory() as tmp:
    work = Path(tmp)
    # The fetched tree is the archive's file or already the patched one: derive both.
    variants = {}
    for name in ("as-fetched", "other"):
        (work / name / "src/util").mkdir(parents=True)
        shutil.copy(root / "third_party/mesa-26.2.0" / header, work / name / header)
    forward = subprocess.run(["patch", "--batch", "--forward", "-p1", "-d", str(work / "other")], input=hunk[0],
                             text=True, capture_output=True)
    if forward.returncode == 0:
        variants = {"archive": work / "as-fetched", "patched": work / "other"}
    else:
        shutil.copy(work / "as-fetched" / header, work / "other" / header)
        subprocess.run(["patch", "--batch", "-R", "-p1", "-d", str(work / "other")], input=hunk[0],
                       text=True, capture_output=True, check=True)
        variants = {"archive": work / "other", "patched": work / "as-fetched"}
    blocks = {name: macro((folder / header).read_text()) for name, folder in variants.items()}
    assert blocks["archive"] != blocks["patched"]

    def initializers(block, *defines):
        """The initializers a C++ file is left looking for when it reads the current context."""
        source = ("#define DETECT_OS_APPLE 0\n" + block +
                  'extern "C" { extern __THREAD_INITIAL_EXEC void *_mesa_glapi_tls_Context; }\n'
                  "void *current(void) { return _mesa_glapi_tls_Context; }\n")
        subprocess.run(["c++", "-std=c++17", "-O2", *defines, "-x", "c++", "-c", "-", "-o", str(work / "probe.o")],
                       input=source, text=True, check=True)
        undefined = subprocess.run(["nm", "-u", str(work / "probe.o")], text=True, capture_output=True, check=True).stdout
        return sorted(set(re.findall(r"\b_ZTH\w+", undefined)))

    wanted = ["_ZTH23_mesa_glapi_tls_Context"]
    assert initializers(blocks["archive"], "-D__PROSPERO__=1") == wanted   # what the SDK shipped with
    assert initializers(blocks["patched"], "-D__PROSPERO__=1") == []      # the PS5 build after the patch
    assert initializers(blocks["patched"]) == wanted                      # other platforms keep Mesa's choice
print("PASS: with the recorded patch a PS5 C++ file reads the current context without an initializer")

# The SDK's own applications no longer define the initializer, so their verifiers and the
# consumer check are what keep a new reference from going unnoticed.
assert "_ZTH" not in (root / "native-app/runtime_shims.c").read_text()
for script in ("tools/verify-native-test-app.sh", "tools/verify-native-cts-app.sh"):
    assert "_ZTH23_mesa_glapi_tls_Context" in (root / script).read_text(), script
assert "_ZTH" in (root / "tools/check-sdk-consumers.py").read_text()
print("PASS: applications supply no initializer; native app verifiers and the consumer check reject an unresolved one")
