#!/usr/bin/env python3
"""Check the actual diagnostic draw loop; optionally audit its native receipt."""
from pathlib import Path
import json
import re
import statistics
import subprocess
import sys
import tempfile


def audit(text):
    rows = re.findall(r"^\[ps5-batch-probe\] repeats=(\d+) wait_ns=(-?\d+) result=(\d+)$", text, re.M)
    assert len(rows) == text.count("[ps5-batch-probe]") == 36, "Missing/extra/malformed GPU records"
    for i, (count, ns, result) in enumerate(rows):
        assert int(count) == (1, 2, 8)[i % 3] and int(ns) > 0 and result == "0", "Failed batch sample"
    # Historical 64x32 probe and the 128x128 GPU-clear-boundary successor.
    sweeps = re.findall(r"^\[ps5-gpu-clear\] rgba8-sweep=36 pixels=(\d+) PASS$", text, re.M)
    assert sweeps in (["73728"], ["589824"]), "Missing/duplicate/wrong-size pixel oracle"
    for marker in (
        "[ps5-gpu-clear] completed=36 cleanup=1 result=0",
        "[pss-opengl-native] gate completed status=0",
    ):
        assert text.count(marker) == 1, f"Missing/duplicate oracle: {marker}"
    # Discard three complete warm-up cycles, preserving nine samples per size.
    return {count: {"samples": 9, "median_wait_ms": statistics.median(
        int(ns) / 1e6 for c, ns, _ in rows[9:] if int(c) == count)} for count in (1, 2, 8)}


if len(sys.argv) == 2:
    print(json.dumps(audit(Path(sys.argv[1]).read_text()), indent=2))
    raise SystemExit
assert len(sys.argv) == 1
root = Path(__file__).resolve().parents[2]
source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.index("static unsigned runtime_batch_probe_sequence;")
helper = source[start:source.index("#endif", start)]
start = source.index("#ifdef PS5_DRAW_BATCH_PROBE\n    for (unsigned repetition")
loop = source[start:source.index("#else\n    agc.draw_auto(&command, 3, 2);", start)]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
enum { AGC_INDEX_SIZE_16 = 0, AGC_INDEX_SIZE_32 = 1 };
static unsigned calls, setup_calls, command, runtime_draw_count = 3, runtime_index_count = 6;
static unsigned runtime_index_size;
static const void *runtime_index_buffer;
static void auto_draw(void *p, unsigned count, uint64_t flags) {
    assert(p == &command && count == runtime_draw_count && flags == 2); ++calls;
}
static void index_size(void *p, uint8_t size, uint8_t flags) {
    assert(p == &command && size == (runtime_index_size == 4) && !flags); ++setup_calls;
}
static void index_buffer(void *p, void *buffer) { assert(p == &command && buffer == runtime_index_buffer); }
static void index_count(void *p, unsigned count) { assert(p == &command && count == runtime_index_count); }
static void index_draw(void *p, unsigned count, void *buffer, uint64_t flags) {
    assert(p == &command && count == runtime_index_count && buffer == runtime_index_buffer && !flags); ++calls;
}
static const struct {
    void (*draw_auto)(void *, unsigned, uint64_t);
    void (*set_index_size)(void *, uint8_t, uint8_t);
    void (*set_index_buffer)(void *, void *);
    void (*set_index_count)(void *, unsigned);
    void (*draw_index)(void *, unsigned, void *, uint64_t);
} agc = {auto_draw, index_size, index_buffer, index_count, index_draw};
''' + helper + r'''
static void emit(unsigned batch_repeats) {
    (void)batch_repeats;
''' + loop + r'''
}
int main(void) {
    for (unsigned i = 0; i < 12; ++i) {
        const unsigned expected[] = {1, 2, 8};
        unsigned repeats = runtime_batch_probe_repeats();
        assert(repeats == expected[i % 3]);
        for (unsigned indexed = 0; indexed < 3; ++indexed) {
            runtime_index_buffer = indexed ? &command : NULL;
            runtime_index_size = indexed == 2 ? 4 : 2;
            calls = setup_calls = 0;
            emit(repeats);
#ifndef PS5_DRAW_BATCH_PROBE
            repeats = 1;
#endif
            assert(calls == repeats && setup_calls == (indexed ? repeats : 0));
        }
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c, exe = Path(tmp) / "batch.c", Path(tmp) / "batch"
    c.write_text(code)
    for flags in ([], ["-DPS5_DRAW_BATCH_PROBE=1"]):
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags, str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
sample = "\n".join(f"[ps5-batch-probe] repeats={c} wait_ns=1000000 result=0" for c in (1, 2, 8) * 12)
sample += "\n[ps5-gpu-clear] rgba8-sweep=36 pixels=73728 PASS\n[ps5-gpu-clear] completed=36 cleanup=1 result=0\n[pss-opengl-native] gate completed status=0\n"
assert audit(sample)[8]["median_wait_ms"] == 1
assert audit(sample.replace("pixels=73728", "pixels=589824"))[8]["median_wait_ms"] == 1
for bad in (sample.replace("wait_ns=1000000", "wait_ns=0", 1), sample.replace("repeats=8", "repeats=1", 1),
            sample.replace("result=0", "result=1", 1), sample.replace("cleanup=1", "cleanup=0"),
            sample.replace("pixels=73728", "pixels=100"),
            sample + "[ps5-batch-probe] malformed", sample.replace("status=0", "status=1")):
    try:
        audit(bad)
    except AssertionError:
        continue
    raise AssertionError("Bad diagnostic receipt accepted")
print("PASS: batch counts/indexed/nonindexed emission, default single draw, strict receipt audit")
