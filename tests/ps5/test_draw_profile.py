#!/usr/bin/env python3
"""Compile the runtime's actual opt-in timing accumulator and reset logic."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.index("static uint64_t runtime_profile_ns")
body = source[start:source.index("#define PS5_PROFILE_MARK", start)]
code = "#include <stdint.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n" + body + r'''
int main(void) {
    int64_t ticks[8] = {1, 1000001, 3000001, 3000001, 4000001, 4000001, 5000001, 6000001};
    runtime_profile_report(); /* empty reports stay quiet */
    runtime_profile_record(ticks, 0);
    runtime_profile_record(ticks, 0);
    assert(runtime_profile_calls == 2 && !runtime_profile_failures);
    assert(runtime_profile_ns[0] == 2000000 && runtime_profile_ns[1] == 4000000);
    runtime_profile_record(ticks, 1);
    ticks[7] = ticks[6] - 1;
    runtime_profile_record(ticks, 0);
    ticks[7] = 0;
    runtime_profile_record(ticks, 0);
    assert(runtime_profile_calls == 2 && runtime_profile_failures == 3);
    runtime_profile_report();
    assert(!runtime_profile_calls && !runtime_profile_failures);
    for (unsigned i = 0; i < 7; ++i) assert(!runtime_profile_ns[i]);
    runtime_profile_report();
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / "profile.c"
    exe = Path(tmp) / "profile"
    c.write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
    output = subprocess.check_output([str(exe)], text=True)
    assert len(output.splitlines()) == 1
    assert "calls=2 failures=3 warmup_frames=30" in output
    assert "scanout_flush_ms=2.000000" in output and "total_ms=6.000000" in output
print("PASS: draw timing accumulation, invalid/incomplete/failing samples, report/reset")
