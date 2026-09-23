#!/usr/bin/env python3
"""Reproduce the shared-condition lost wake; qualify the oracle on native Linux futexes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "third_party/opengnm-psbc/src/util/futex.c").read_text()
linux = source.split("#if defined(HAVE_LINUX_FUTEX_H)\n", 1)[1].split("#elif", 1)[0]
fallback = source.split("#elif defined(__ORBIS__) && !defined(__PROSPERO__)\n", 1)[1].split("#elif", 1)[0]
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    for name, implementation, expected in (("fallback", fallback, 1), ("native", linux, 0)):
        backend = work / (name + ".c")
        backend.write_text("#include <stdint.h>\n#include <time.h>\n" + implementation)
        executable = work / name
        subprocess.run(["cc", "-D_GNU_SOURCE", "-O2", "-pthread",
                        str(root / "tests/ps5/egl_public_core33_futex.c"),
                        str(backend), "-o", str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
        print(name + ": " + result.stdout.strip())
        assert result.returncode == expected, result.stderr
print("PASS: shared-condition fallback loses addressed wakes; native futex passes")
