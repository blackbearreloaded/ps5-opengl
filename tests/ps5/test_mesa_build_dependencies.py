#!/usr/bin/env python3
"""Exercise the real Make dependency graph without a PS5 compiler or console."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        runtime, mesa = work / "runtime", work / "mesa"
        runtime.mkdir()
        mesa.mkdir()
        makefile = work / "Makefile"
        makefile.write_text(
            f"include {ROOT}/toolchain/ps5-opengl-core33.mk\n"
            "print-mesa:\n\t@printf '%s\\n' $(PS5_OPENGL_MESA_LIBS) $(PS5_OPENGL_GLAPI_BRIDGE)\n"
            "check: $(PS5_OPENGL_RUNTIME)\n")
        command = ["make", "--no-print-directory", "-f", str(makefile),
                   f"PS5_OPENGL_BUILD={runtime}", f"PS5_OPENGL_MESA_BUILD={mesa}",
                   "CC=false", "LD=false", "AR=false"]
        archives = [Path(line) for line in subprocess.check_output(command + ["print-mesa"], text=True).splitlines()]
        assert len(archives) == 15, len(archives)
        (mesa / "source").write_text("original")
        (mesa / "build.ninja").write_text("rule copy\n  command = cp $in $out\n" + "".join(
            f"build {path.relative_to(mesa)}: copy source\n" for path in archives))
        # All native objects are already current. Only Mesa needs dependency checking.
        for name in ("ps5_egl.o", "ps5_screen.o", "ps5_agc_package.o", "ps5_agc_runtime_backend.o",
                     "u_framebuffer.o", "agc_link_stub.o", "agc_driver_link_stub.o",
                     "libSceAgc.so", "libSceAgcDriver.so", "libps5_opengl_core33.a"):
            (runtime / name).write_text("existing native object")
        for contents in ("original", "edited Mesa source"):
            (mesa / "source").write_text(contents)
            result = subprocess.run(command + ["check"], capture_output=True, text=True)
            assert result.returncode == 0, result.stdout + result.stderr
            assert all(path.is_file() and path.read_text() == contents for path in archives), \
                "runtime target linked stale/missing Mesa archives instead of invoking Ninja"
        print("mesa-build-dependencies: PASS (fresh + changed source; native objects not rebuilt)")


if __name__ == "__main__":
    main()
