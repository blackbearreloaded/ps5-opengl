"""Changing runtime flags must rebuild objects; identical flags must not."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class RuntimeConfigTest(unittest.TestCase):
    def test_flags_roundtrip(self):
        source = (Path(__file__).resolve().parents[1] / "toolchain/ps5-opengl-core33.mk").read_text()
        start = source.index("# Track command-line configuration")
        end = source.index("\n$(PS5_OPENGL_BUILD)/ps5_egl.o:", start)
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / "Makefile").write_text(".DEFAULT_GOAL := all\nPS5_OPENGL_BUILD := .\n"
                "PS5_OPENGL_RUNTIME_OBJECTS := object\n" + source[start:end] +
                "\nall: object\nobject:\n\t@printf 'rebuilt\\n' >> count\n\t@touch object\n")
            for defines, expected in (("", 1), ("", 1), ("-DPS5_MULTIDRAW_BATCH=1", 2),
                                      ("-DPS5_MULTIDRAW_BATCH=1", 2), ("", 3)):
                subprocess.run(["make", "-s", "PS5_OPENGL_RUNTIME_DEFINES=" + defines], cwd=work, check=True)
                self.assertEqual(len((work / "count").read_text().splitlines()), expected)
