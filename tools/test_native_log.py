# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""The shared native logger must retain interleaved stdout and stderr."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


class NativeLogTest(unittest.TestCase):
    def test_routine_trace_policy(self):
        source = (Path(__file__).resolve().parents[1] / "src/gallium/ps5/ps5_screen.c").read_text()
        blocks = re.findall(r"#ifdef AGC_RUNTIME_DIAGNOSTICS\n.*?#endif", source, re.S)
        self.assertEqual(len(blocks), 3)
        for block in blocks:
            self.assertEqual(block.count("printf("), 1)
            self.assertNotIn("ps5_flush_gpu_data", block)
            self.assertNotIn("last_draw_status", block)
            source = source.replace(block, "")
        for marker in ("shared-resource exhausted", "reject-clear",
                       "queued presentation failed", "deferred batch cleanup failed"):
            self.assertIn(marker, source)
        for diagnostics in (False, True):
            flags = ["-DAGC_RUNTIME_DIAGNOSTICS=1"] if diagnostics else []
            result = subprocess.run(["cc", "-E", "-P", "-x", "c", *flags, "-"],
                                    input="\n".join(blocks), text=True, capture_output=True, check=True)
            for marker in ("shared-resource offset=", "depth-state format=", "stencil-state format="):
                self.assertEqual(marker in result.stdout, diagnostics)

    def test_interleaved_streams(self):
        source = (Path(__file__).resolve().parents[1] / "native-app/runtime_shims.c").read_text()
        start = source.index("__attribute__((constructor))")
        function = source[start:source.index("\n}", start) + 2].replace("/download0/pss-opengl.log", "receipt.log")
        program = '\nint main(void) { puts("out1"); fputs("diagnostic-long-line\\n", stderr); puts("out2"); fputs("error2\\n", stderr); return 0; }\n'
        expected = "out1\ndiagnostic-long-line\nout2\nerror2\n"
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            for broken in (False, True):
                logger = function
                if broken:
                    logger = logger.replace('    stream = freopen("receipt.log", "a", stdout);', '    (void)stream;')
                subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-", "-o", str(work / "check")],
                               input="#include <stdio.h>\n" + logger + program, text=True, check=True)
                (work / "receipt.log").write_text("stale receipt must disappear\n")
                subprocess.run([str(work / "check")], cwd=work, check=True)
                actual = (work / "receipt.log").read_text()
                if broken:
                    self.assertNotEqual(actual, expected)
                else:
                    self.assertEqual(actual, expected)
