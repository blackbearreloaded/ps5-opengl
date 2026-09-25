#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compile the actual batch retirement code with deterministic failure injection."""
from pathlib import Path
import argparse
import json
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from opengl_receipts import LEGACY_NAME, normalize_log_tags


def audit(text, require_postchecks=False, require_textures=False, capacity=8):
    """Pixel success alone cannot prove that the optimized path ran."""
    text = normalize_log_tags(text)
    matches = list(re.finditer(r"\[ps5-multidraw\] mode=(\d+) serial_ns=(\d+) batch_ns=(\d+) pixels=6912 PASS", text))
    assert len(matches) == text.count("[ps5-multidraw] mode=") == 4, "Missing/duplicate mode results"
    result, start = [], 0
    for mode, match in enumerate(matches):
        actual_mode, serial, batch = map(int, match.groups())
        assert actual_mode == mode and serial > 0 and batch > 0
        section = text[start:match.start()]
        chunks = re.findall(r"\[ps5-multidraw-batch\] draws=(\d+) attempted=(\d+) waits=(\d+) result=(\d+)", section)
        assert len(chunks) == section.count("[ps5-multidraw-batch]") == (11 + capacity - 1) // capacity, "Wrong native chunk count"
        assert sum(int(c[0]) for c in chunks) == 10, "Every nonzero subdraw must be submitted"
        for draws, attempted, waits, status in chunks:
            assert 0 < int(draws) <= capacity and draws == attempted and int(waits) < 2000 and status == "0"
        assert len(re.findall(r"\[ps5-gallium\] multi-draw-batched draws=(?:10|11) result=0", section)) == 1
        assert section.count("multi-draw-batched") == 1
        result.append({"mode": mode, "serial_ms": serial / 1e6, "batch_ms": batch / 1e6,
                       "single_sample_ratio": serial / batch})
        start = match.end()
    assert text.count("[ps5-multidraw-batch]") == 4 * ((11 + capacity - 1) // capacity) and text.count("multi-draw-batched") == 4
    # The original four-mode receipt is retained; the successor adds postchecks.
    if require_postchecks or "[ps5-multidraw] query_samples=" in text:
        assert text.count("[ps5-multidraw] query_samples=2560 fence=1 orphan=1 pixels=4608 PASS") == 1
        assert text.count("[ps5-multidraw] query_samples=") == 1
    if require_textures or "[ps5-multidraw-texture]" in text:
        assert text.count("[ps5-multidraw-texture]") == 2
        assert text.count("[ps5-multidraw-texture] upload-after-batch=1 units=0,7") == 1
        assert text.count("[ps5-multidraw-texture] sampled=2 uploads=1 pixels=32256 PASS") == 1
    for marker in ("[ps5-multidraw] completed=4 cleanup=1 result=0",
                   "[ps5-opengl-native] gate completed status=0"):
        assert text.count(marker) == 1, "Missing/duplicate completion"
    return result


def audit_deferred(text, control=False, require_uploads=False, capacity=8):
    """Require real coalescing, complete pixels/hazards and inclusive wait timing."""
    matches = list(re.finditer(r"\[ps5-multidraw\] mode=(\d+) serial_ns=(\d+) batch_ns=(\d+) pixels=6912 PASS", text))
    assert len(matches) == text.count("[ps5-multidraw] mode=") == 4
    start, result = 0, []
    group = [min(capacity, 10 - i) for i in range(0, 10, capacity)]
    for mode, match in enumerate(matches):
        actual, serial, batched = map(int, match.groups())
        assert actual == mode and serial > 0 and batched > 0
        chunks = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text[start:match.start()])
        assert list(map(int, chunks)) == ([] if control else [1] * 11 + group + [1]), chunks
        result.append({"mode": mode, "serial_ms": serial / 1e6,
                       "group_ms": batched / 1e6, "single_sample_ratio": serial / batched})
        start = match.end()
    chunks = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text)
    tail = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text[start:])
    assert list(map(int, tail)) == ([] if control else group * 4 + [1]), tail
    native = re.findall(r"\[ps5-multidraw-batch\] draws=(\d+) attempted=(\d+) waits=(\d+) result=(\d+)", text)
    assert len(chunks) == text.count("[ps5-deferred-batch]") == len(native) == text.count("[ps5-multidraw-batch]")
    assert not control or not chunks
    for count, (draws, attempted, waits, status) in zip(chunks, native):
        assert count == draws == attempted and 0 < int(count) <= capacity and int(waits) < 2000 and status == "0"
    for marker in (
        "[ps5-deferred] state=uniform,scissor texture-upload=1 buffer-subdata=1 map-write=1 pending-fence=1 pixels=9216 PASS",
        "[ps5-multidraw] query_samples=2560 fence=1 orphan=1 pixels=4608 PASS",
        "[ps5-multidraw] completed=4 cleanup=1 result=0", "[ps5-opengl-native] gate completed status=0",
    ):
        assert text.count(marker) == 1, marker
    if require_uploads or "[ps5-deferred] unrelated-buffer" in text:
        assert text.count("[ps5-deferred] unrelated-buffer subdata=1 map=1 explicit-flush=1 unmap=1 read=1 PASS") == 1
        assert text.count("[ps5-deferred] unrelated-buffer") == 1
    return result


root = Path(__file__).resolve().parents[2]
capacity = int(re.search(r"#define PS5_MULTIDRAW_BATCH_CAPACITY (\d+)u",
                        (root / "src/gallium/ps5/ps5_screen.h").read_text())[1])
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("receipt", nargs="?")
parser.add_argument("--capacity", type=int, choices=sorted({8, 32, 128, capacity}), default=8,
                    help="Frozen runtime capacity; default preserves historical receipts")
modes = parser.add_mutually_exclusive_group()
for mode in ("postchecks", "textures", "deferred", "deferred-control", "deferred-uploads"):
    modes.add_argument("--" + mode, dest="mode", action="store_const", const=mode)
args = parser.parse_args()
if args.receipt:
    receipt = Path(args.receipt).read_text()
    result = audit_deferred(receipt, args.mode == "deferred-control",
                            args.mode == "deferred-uploads", args.capacity) \
        if args.mode and args.mode.startswith("deferred") else \
        audit(receipt, bool(args.mode), args.mode == "textures", args.capacity)
    print(json.dumps(result, indent=2))
    raise SystemExit
if args.mode or args.capacity != 8:
    parser.error("receipt required with audit options")
sample = "".join(
    "[ps5-multidraw-batch] draws=7 attempted=7 waits=1 result=0\n"
    "[ps5-multidraw-batch] draws=3 attempted=3 waits=1 result=0\n"
    "[ps5-gallium] multi-draw-batched draws=11 result=0\n"
    f"[ps5-multidraw] mode={mode} serial_ns=2000000 batch_ns=1000000 pixels=6912 PASS\n"
    for mode in range(4))
sample += "[ps5-multidraw] completed=4 cleanup=1 result=0\n[ps5-opengl-native] gate completed status=0\n"
assert len(audit(sample)) == 4
assert audit(sample.replace("ps5-opengl", LEGACY_NAME)) == audit(sample)
assert len(audit(sample + "[ps5-multidraw] query_samples=2560 fence=1 orphan=1 pixels=4608 PASS\n", True)) == 4
texture_markers = "[ps5-multidraw-texture] upload-after-batch=1 units=0,7\n" \
                  "[ps5-multidraw-texture] sampled=2 uploads=1 pixels=32256 PASS\n"
assert len(audit(sample + texture_markers, require_textures=True)) == 4
try:
    audit(sample, True)
except AssertionError:
    pass
else:
    raise AssertionError("Missing required postchecks accepted")
for bad in (sample.replace("[ps5-multidraw-batch]", "[unused]"), sample.replace("attempted=7", "attempted=6", 1),
            sample.replace("waits=1", "waits=2000", 1), sample.replace("result=0", "result=1", 1),
            sample.replace("cleanup=1", "cleanup=0"), sample + "[ps5-multidraw-batch] malformed",
            sample + "[ps5-multidraw] query_samples=2559 fence=1 orphan=1 pixels=4608 PASS",
            sample + texture_markers.replace("units=0,7", "units=0,0"),
            sample + texture_markers.replace("uploads=1", "uploads=0")):
    try:
        audit(bad)
    except AssertionError:
        continue
    raise AssertionError("Invalid or unbatched receipt accepted")
print("PASS: receipt audit rejects unbatched, incomplete and failed runs")
if capacity > 11:
    wide_sample = sample.replace(
        "[ps5-multidraw-batch] draws=7 attempted=7 waits=1 result=0\n"
        "[ps5-multidraw-batch] draws=3 attempted=3 waits=1 result=0\n",
        "[ps5-multidraw-batch] draws=10 attempted=10 waits=1 result=0\n")
    assert len(audit(wide_sample, capacity=capacity)) == 4
    try:
        audit(wide_sample)
    except AssertionError:
        pass
    else:
        raise AssertionError("Wide receipt accepted under historical capacity")

def batch_receipt(count):
    return f"[ps5-multidraw-batch] draws={count} attempted={count} waits=1 result=0\n" \
           f"[ps5-deferred-batch] draws={count} result=0\n"


for receipt_capacity, control in ((8, False), (8, True), (capacity, False), (capacity, True)):
    group = [min(receipt_capacity, 10 - i) for i in range(0, 10, receipt_capacity)]
    deferred_sample = "".join(
        ("" if control else "".join(batch_receipt(n) for n in [1] * 11 + group + [1])) +
        f"[ps5-multidraw] mode={mode} serial_ns=2000000 batch_ns=1000000 pixels=6912 PASS\n"
        for mode in range(4))
    if not control:
        deferred_sample += "".join(batch_receipt(n) for n in group * 4 + [1])
    deferred_sample += "[ps5-deferred] state=uniform,scissor texture-upload=1 buffer-subdata=1 map-write=1 pending-fence=1 pixels=9216 PASS\n" \
        "[ps5-multidraw] query_samples=2560 fence=1 orphan=1 pixels=4608 PASS\n" \
        "[ps5-multidraw] completed=4 cleanup=1 result=0\n[ps5-opengl-native] gate completed status=0\n"
    assert len(audit_deferred(deferred_sample, control, capacity=receipt_capacity)) == 4
    upload_marker = "[ps5-deferred] unrelated-buffer subdata=1 map=1 explicit-flush=1 unmap=1 read=1 PASS\n"
    assert len(audit_deferred(deferred_sample + upload_marker, control, True, receipt_capacity)) == 4
    for bad in (deferred_sample, deferred_sample + upload_marker * 2,
                deferred_sample + upload_marker.replace("unmap=1", "unmap=0")):
        try:
            audit_deferred(bad, control, True, receipt_capacity)
        except AssertionError:
            continue
        raise AssertionError("Missing or failed unrelated-upload oracle accepted")
    for bad in (deferred_sample.replace("map-write=1", "map-write=0"),
                deferred_sample.replace("cleanup=1", "cleanup=0"),
                deferred_sample + batch_receipt(1)):
        try:
            audit_deferred(bad, control, capacity=receipt_capacity)
        except AssertionError:
            continue
        raise AssertionError("Invalid deferred receipt accepted")
print("PASS: ordinary-draw receipt audit requires matching native chunks and every hazard oracle")

source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.index("static void runtime_require_retirement(")
guard = source[start:source.index("\n}\n", start) + 3]
start = source.index("static struct runtime_batch_entry {")
body = source[start:source.index("\n#endif\n\n#ifdef PS5_DRAW_PROFILE", start)]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <inttypes.h>
#include <string.h>
#include "ps5_screen.h"
#include <inttypes.h>
typedef struct { void *words; uint32_t word_count; uint8_t flag, padding[3]; } agc_submit_description_t;
typedef struct { int (*submit)(void *); int (*suspend_point)(void); } agc_api_t;
static uint64_t markers[PS5_MULTIDRAW_BATCH_CAPACITY];
static uint8_t memory[PS5_MULTIDRAW_BATCH_CAPACITY][64];
static unsigned submits, suspends, sleeps, unmaps, releases, delay;
#ifdef PS5_DRAW_PROFILE
static unsigned runtime_present_count = 30;
static int64_t os_time_get_nano(void) { static int64_t ticks = 1; return ticks++; }
static void runtime_batch_profile_record(const int64_t ticks[5], unsigned waits, int result) {
    assert(waits == sleeps);
    if (!result) {
        assert(ticks[0] > 0); /* The production accumulator rejects zero origin. */
        for (unsigned i = 1; i < 5; ++i) assert(ticks[i] > ticks[i - 1]);
        assert(unmaps == submits && releases == submits);
    }
}
#endif
static int fail_submit, fail_suspend, fail_unmap, wrong_marker;
static int submit(void *p) {
    agc_submit_description_t *d = p;
    unsigned i = submits++;
    assert(d->words == memory[i] && d->word_count == 1 && !unmaps && !releases);
    if ((int)i == fail_submit) return -1;
    if (!delay) markers[i] = 101 + i;
    return 0;
}
static int suspend_point(void) { ++suspends; return fail_suspend; }
static void flush_gpu_data(const void *p, size_t n) { assert(p && n == 8); }
static int sceKernelUsleep(uint32_t us) {
    assert(us == 1000 && !unmaps && !releases);
    if (++sleeps >= delay)
        for (unsigned i = 0; i < submits; ++i) markers[i] = wrong_marker ? 100 : 101 + i;
    return 0;
}
static unsigned runtime_completion_pause(int64_t *deadline) {
    (void)deadline; sceKernelUsleep(1000); return 1;
}
static int munmap(void *p, size_t n) {
    assert(p && n == 64);
    for (unsigned i = 0; i < submits; ++i) assert(markers[i] == 101 + i);
    ++unmaps;
    return fail_unmap;
}
static int sceKernelReleaseDirectMemory(int64_t p, size_t n) {
    assert(p >= 0 && n == 64 && unmaps > releases); ++releases; return 0;
}
static jmp_buf exit_jump;
static _Noreturn void check_exit(int status) {
    assert(status == EXIT_FAILURE && !unmaps && !releases);
    longjmp(exit_jump, 1);
}
#define _Exit check_exit
''' + guard + r'''
#undef _Exit
''' + body + r'''
static void reset(void) {
    /* Only resets the host mock. Production intentionally has no reset API. */
    memset(runtime_batch_entries, 0, sizeof(runtime_batch_entries));
    memset(runtime_pending, 0, sizeof(runtime_pending));
    memset(markers, 0, sizeof(markers));
    runtime_batch_count = runtime_batch_active = runtime_batch_faulted = 0;
    runtime_pending_head = runtime_pending_batches = 0;
#ifdef PS5_DRAW_PROFILE

#endif
    submits = suspends = sleeps = unmaps = releases = delay = 0;
    fail_submit = -1; fail_suspend = fail_unmap = wrong_marker = 0;
}
static void queue(unsigned n) {
    const agc_api_t api = {submit, suspend_point};
    assert(ps5_agc_gate2_batch_begin() == 0);
    assert(ps5_agc_gate2_batch_begin() != 0);
    for (unsigned i = 0; i < n; ++i) {
        agc_submit_description_t d = {memory[i], 1, 0, {0}};
        const struct runtime_batch_entry entry={d,memory[i],i*64,64,&markers[i],101+i,0,0,0};
        assert(runtime_batch_queue(&api, &entry) == 0);
    }
    assert(!submits && !unmaps && !releases); /* Staging is not execution. */
}
int main(void) {
    reset(); assert(ps5_agc_gate2_batch_end() != 0);
    queue(0); assert(ps5_agc_gate2_batch_end() == 0 && !submits && !suspends);
    for (unsigned n = 1; n <= PS5_MULTIDRAW_BATCH_CAPACITY; ++n) {
        reset(); delay = 5; queue(n);
        assert(ps5_agc_gate2_batch_end() == 0);
        assert(submits == n && suspends == 1 && sleeps == 5 && unmaps == n && releases == n);
        assert(!runtime_batch_count && !runtime_batch_active && !runtime_batch_faulted);
    }
    reset(); queue(PS5_MULTIDRAW_BATCH_CAPACITY);
    const agc_api_t api = {submit, suspend_point};
    agc_submit_description_t d = {memory[0], 1, 0, {0}};
    const struct runtime_batch_entry entry={d,memory[0],0,64,&markers[0],101,0,0,0};
    assert(runtime_batch_queue(&api, &entry) != 0);
    assert(ps5_agc_gate2_batch_end() == 0);
    reset(); delay=5; queue(3);
    assert(ps5_agc_gate2_batch_submit() == 0 && runtime_pending[runtime_pending_head].count == 3);
    assert(ps5_agc_gate2_batch_begin() == 0); /* CPU may stage the next batch. */
    assert(ps5_agc_gate2_batch_retire(0) == 0 && !sleeps && !unmaps);
    assert(ps5_agc_gate2_batch_retire(1) == 1 && sleeps == 5 && unmaps == 3 && releases == 3);
    assert(ps5_agc_gate2_batch_end() == 0); /* Empty staged successor. */
    for (int failure = 0; failure < 6; ++failure) {
        reset(); queue(PS5_MULTIDRAW_BATCH_CAPACITY);
        if (failure == 0) fail_submit = 0;
        if (failure == 1) fail_submit = 3;
        if (failure == 2) fail_submit = PS5_MULTIDRAW_BATCH_CAPACITY - 1;
        if (failure == 3) fail_suspend = -1;
        if (failure == 4) { delay = 1; wrong_marker = 1; }
        if (failure == 5) fail_unmap = -1;
        int stopped = setjmp(exit_jump);
        if (!stopped) {
            assert(ps5_agc_gate2_batch_end() != 0);
            assert(failure == 5); /* Only a post-retirement unmap error returns. */
        } else {
            assert(failure < 5);
        }
        assert(!releases && (runtime_batch_faulted != 0) == (failure == 5));
        assert(unmaps == (failure == 5 ? 1u : 0u));
        assert(submits == (failure == 0 ? 1u : failure == 1 ? 4u : PS5_MULTIDRAW_BATCH_CAPACITY));
        assert(suspends == 1 && sleeps <= 2000);
        if (failure == 4) assert(sleeps == 2000); /* One shared timeout for the whole batch. */
        if (failure == 5)
            assert(ps5_agc_gate2_batch_begin() != 0 && ps5_agc_gate2_batch_end() != 0);
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c, exe = Path(tmp) / "lifetime.c", Path(tmp) / "lifetime"
    c.write_text(code)
    for flags in ([], ["-DPS5_DRAW_PROFILE=1"]):
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                        "-I" + str(root / "src/gallium/ps5"), str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
print(f"PASS: staged ownership, 1..{capacity} draws, all-marker retirement, shared timeout, fail-stop before cleanup")

# Native FIFO: later completion cannot release older memory; full submission
# preserves the staged batch until the caller retires an entry and retries.
native_fifo = code[:code.index("int main(void) {")]
native_fifo = native_fifo.replace(" && !unmaps && !releases", "")
native_fifo = native_fifo.replace("for (unsigned i = 0; i < submits; ++i) assert(markers[i] == 101 + i);",
    "unsigned i=((uint8_t *)p-&memory[0][0])/64; assert(i<submits && markers[i]==101+i);")
native_fifo += r'''
static void queue_one(unsigned i) {
    const agc_api_t api={submit,suspend_point};
    assert(ps5_agc_gate2_batch_begin()==0);
    agc_submit_description_t d={memory[i],1,0,{0}};
    const struct runtime_batch_entry entry={d,memory[i],i*64,64,&markers[i],101+i,0,0,0};
    assert(runtime_batch_queue(&api,&entry)==0);
}
int main(void) {
    reset(); delay=100000;
    for (unsigned round=0;round<3;++round) {
        unsigned first=submits;
        for (unsigned i=0;i<PS5_INFLIGHT_BATCH_CAPACITY;++i) {
            queue_one(submits);
            assert(ps5_agc_gate2_batch_submit()==0);
        }
        assert(runtime_pending_batches==PS5_INFLIGHT_BATCH_CAPACITY && !sleeps);
        queue_one(submits);
        assert(ps5_agc_gate2_batch_submit()==-1);
        assert(runtime_batch_active && runtime_batch_count==1); // Still caller-owned.
        unsigned freed=unmaps;
        markers[submits-1]=101+submits-1;
        assert(ps5_agc_gate2_batch_retire(0)==0 && unmaps==freed);
        markers[first]=(UINT64_C(1)<<32)|(101+first);
        assert(ps5_agc_gate2_batch_retire(0)==0 && unmaps==freed); // Reject partial/stale upper word.
        markers[first]=101+first;
        assert(ps5_agc_gate2_batch_retire(0)==1 && unmaps==freed+1);
        assert(ps5_agc_gate2_batch_submit()==0 && runtime_pending_batches==PS5_INFLIGHT_BATCH_CAPACITY);
        for (unsigned i=first;i<submits;++i) markers[i]=101+i;
        while (runtime_pending_batches) assert(ps5_agc_gate2_batch_retire(0)==1);
        assert(unmaps==submits && releases==submits && !sleeps);
    }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe=Path(tmp)/"native-fifo"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-I"+str(root/"src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                   input=native_fifo, text=True, check=True)
    subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
print("PASS: native FIFO capacity, full-queue ownership/retry, out-of-order markers and wraparound")

# Repeat native FIFO ownership with one suspend callback per frame.
start = source.index("#ifdef PS5_FRAME_SUSPEND\nstatic int (*runtime_frame_suspend)")
policy = source[start:source.index("\n#endif", start) + 7]
frame_fifo = native_fifo.replace(guard, guard + "\n" + policy)
frame_fifo = frame_fifo.replace("reset(); delay=100000;", """reset(); delay=100000;
    assert(runtime_end_submit_period()==0 && !suspends);
    assert(runtime_defer_suspend(NULL)==-1);""")
frame_fifo = frame_fifo.replace("assert(unmaps==submits && releases==submits && !sleeps);", """
        assert(unmaps==submits && releases==submits && !sleeps);
        assert(suspends==round); // No suspend during any submission or retirement.
        assert(runtime_end_submit_period()==0 && suspends==round+1);
        assert(runtime_end_submit_period()==0 && suspends==round+1);
    """)
frame_fifo = frame_fifo.replace("int main(void) {", "static int other_suspend(void) { return 0; }\nint main(void) {")
frame_fifo = frame_fifo.replace("    }\n}\n", """    }
    assert(runtime_defer_suspend(suspend_point)==0);
    assert(runtime_defer_suspend(other_suspend)==-1);
    fail_suspend=-1;
    assert(runtime_end_submit_period()==-1 && runtime_frame_suspend==suspend_point);
    fail_suspend=0;
    assert(runtime_end_submit_period()==0 && !runtime_frame_suspend);
}
""")
with tempfile.TemporaryDirectory() as tmp:
    exe=Path(tmp)/"frame-fifo"
    subprocess.run(["cc", "-DPS5_FRAME_SUSPEND=1", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-I"+str(root/"src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                   input=frame_fifo, text=True, check=True)
    subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
print("PASS: frame suspend batches retain FIFO ownership, coalesce callbacks, retain failed boundary")

# Exercise the real Gallium wrapper too: ownership must survive command staging.
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()
cache_start = source.index("struct ps5_batch_flush_cache {")
flush_cache_type = source[cache_start:source.index("\n};", cache_start) + 3]
start = source.index("enum ps5_batch_eligibility {")
body = source[start:source.index("\n#endif\n\n#ifdef PS5_DEFERRED_DRAW_BATCH", start)]
code = r'''
#include <inttypes.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "ps5_screen.h"
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define PS5_DIRECT_ALIGNMENT 64 /* Small backing in this ownership-only fixture. */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define BITFIELD_BIT(b) (1u << (b))
#define PS5_RENDER_ARENA_OFFSET (2u * 0xa00000u)
enum { PIPE_MAX_ATTRIBS=16, PS5_MAX_CONSTANT_BUFFERS=13, PS5_MAX_TEXTURE_UNITS=16, PS5_MAX_RENDER_TARGETS=8, PIPE_BUFFER=1,
       PIPE_TEXTURE_2D=2, PIPE_TEXTURE_2D_ARRAY=3, PIPE_FORMAT_R8G8B8A8_UNORM=1, MESA_PRIM_TRIANGLES=4, MESA_PRIM_TRIANGLE_FAN=5, PIPE_BIND_DISPLAY_TARGET=1,
       PIPE_FORMAT_Z32_FLOAT=77, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT=78, PIPE_MAX_VERTEX_STREAMS=4,
       PIPE_FORMAT_X32_S8X24_UINT=79, PIPE_BIND_RENDER_TARGET=2, PIPE_BIND_DEPTH_STENCIL=4 };
struct pipe_resource { unsigned target, format, nr_samples, nr_storage_samples, refs, last_level, bind, array_size, width0; };
struct pipe_sampler_view { struct pipe_resource *texture; unsigned target, format;
    union { struct { unsigned first_level, last_level, first_layer, last_layer; } tex; } u; };
struct ps5_resource { struct pipe_resource base; uint64_t texture_publication_epoch, stencil_publication_epoch;
    int64_t direct_start; unsigned render_arena_slot_count; bool external_cpu_access; unsigned render_staging_size, depth_staging_size;
    unsigned level_stride[16];
    uint8_t *data, *stencil_data; size_t size, allocation_size, stencil_allocation_size; };
struct pipe_screen { struct pipe_resource *(*resource_create)(struct pipe_screen *, const struct pipe_resource *); };
struct ps5_screen { struct pipe_screen base; struct pipe_resource *render_pool; };
struct pipe_context { struct pipe_screen *screen; };
struct pipe_surface { struct pipe_resource *texture; unsigned level, first_layer, last_layer, format; };
struct pipe_draw_info { unsigned mode, instance_count, start_instance, index_size; bool primitive_restart, has_user_indices,
    increment_draw_id; struct { struct pipe_resource *resource; } index; };
struct pipe_draw_indirect_info { int unused; };
struct pipe_draw_start_count_bias { unsigned start, count; int index_bias; };
struct pipe_depth_stencil_alpha_state { bool depth_enabled; struct { bool enabled; } stencil[2]; };
struct ps5_query { uint64_t value, start, end; unsigned type, index; bool ready, active; struct pipe_resource *buffer; };
struct test_nir { struct { unsigned num_ubos; } info; };
struct ps5_shader { struct test_nir *nir; unsigned *textures; };
struct test_elements { unsigned count; struct { unsigned vertex_buffer_index; } elements[PIPE_MAX_ATTRIBS]; };
struct ps5_context {
    struct pipe_context base;
    struct { bool running; } *blitter;
    bool deferred_blitter_draw;
    struct { struct pipe_surface cbufs[PS5_MAX_RENDER_TARGETS], zsbuf; unsigned nr_cbufs; } framebuffer;
    bool framebuffer_valid; struct ps5_shader *vs, *fs, *gs, *tcs, *tes;
    struct test_elements *vertex_elements;
    unsigned stream_output_target_count, render_condition_query, vertex_buffer_count;
    bool queries_enabled;
    struct ps5_query *active_streamout_overflow_query[PIPE_MAX_VERTEX_STREAMS+1];
    struct ps5_query *active_occlusion_query;
    struct ps5_query *active_primitives_generated_query[PIPE_MAX_VERTEX_STREAMS];
    struct ps5_query *active_primitives_emitted_query[PIPE_MAX_VERTEX_STREAMS];
    struct { bool is_user_buffer; struct { struct pipe_resource *resource; } buffer; } vertex_buffers[PIPE_MAX_ATTRIBS];
    struct { struct pipe_resource *buffer; bool valid, copied; } constants[2][PS5_MAX_CONSTANT_BUFFERS];
    struct pipe_resource *vertex_descriptor_table, *descriptor_storage[2], *border_color_storage;
    struct pipe_sampler_view *sampler_views[2][PS5_MAX_TEXTURE_UNITS];
    const struct pipe_depth_stencil_alpha_state *depth_stencil_alpha;
    int last_draw_status;
    uint64_t batch_checks, batch_eligible, batch_reject[7];
    struct { unsigned key[16]; uint64_t count; } framebuffer_fallbacks[32];
    uint64_t framebuffer_fallback_overflow;
};
/* Snapshot sizing is exercised with real ranges in test_descriptor_snapshot.py. */
static size_t ps5_descriptor_snapshot_size(const struct ps5_context *c, unsigned slot) { (void)c; (void)slot; return 64; }
static bool ps5_any_primitive_query(const struct ps5_context *c)
{ for (unsigned i=0;i<PIPE_MAX_VERTEX_STREAMS;++i) {
      if (c->active_primitives_generated_query[i] || c->active_primitives_emitted_query[i]) return true;
  } return false; }
enum { TEST_DRAWS = 2 * PS5_MULTIDRAW_BATCH_CAPACITY + 3,
       TEST_COPIES = 3 * TEST_DRAWS, SNAPSHOTS = 3 * PS5_MULTIDRAW_BATCH_CAPACITY };
static struct ps5_resource original[3], copies[TEST_COPIES], borrowed, depth_buffer, query_backing;
static uint8_t original_bytes[3][64], copy_bytes[TEST_COPIES][64];
static uint8_t borrowed_bytes[64], depth_bytes[64];
static struct ps5_context context;
static struct ps5_screen screen;
static unsigned shader_textures, allocated, freed, begun, ended, staged, calls, locked;
static int fail_alloc, fail_begin, fail_end, fail_draw, bad_high_alloc;
static struct ps5_resource *pending[PS5_MULTIDRAW_BATCH_CAPACITY][3];
static unsigned expected_start[PS5_MULTIDRAW_BATCH_CAPACITY], expected_id[PS5_MULTIDRAW_BATCH_CAPACITY];
static bool deferred_mode;
static unsigned retained_draws;
static unsigned unindexed_draws;
static uint8_t expected_uniform[PS5_MULTIDRAW_BATCH_CAPACITY][3];
static unsigned ps5_constant_state_binding(const struct ps5_shader *s, unsigned i) { (void)s;return i; }
static unsigned shader_storage;
static unsigned ps5_shader_uses_storage(const struct ps5_shader *s) { (void)s; return shader_storage; }
static bool ps5_texture_used(const struct ps5_context *c, const struct ps5_shader *s, const void *metadata, unsigned unit) {
    (void)c; (void)metadata; return (*s->textures & (1u << unit)) != 0;
}
static unsigned ps5_linear_color_pitch(const struct pipe_surface *s) {
    return s && s->texture && s->first_layer == s->last_layer &&
        ((struct ps5_resource *)s->texture)->render_staging_size ? 256 : 0;
}
static unsigned ps5_surface_width(const struct pipe_surface *s) { (void)s; return 16; }
static unsigned ps5_surface_height(const struct pipe_surface *s) { (void)s; return 16; }
static bool ps5_depth_render_target(unsigned target) {
    return target == PIPE_TEXTURE_2D || target == PIPE_TEXTURE_2D_ARRAY;
}
static unsigned ps5_texture_level_layers(const struct pipe_resource *r, unsigned level) {
    (void)level; return r->array_size ? r->array_size : 1;
}
static unsigned fragment_textures;
static struct test_nir all_ubos={{PS5_MAX_CONSTANT_BUFFERS}};
static struct ps5_shader vertex_shader={&all_ubos,&shader_textures},fragment_shader __attribute__((unused))={&all_ubos,&fragment_textures};
static struct test_elements all_elements;
static struct ps5_resource textures[PS5_MAX_TEXTURE_UNITS];
static struct pipe_sampler_view views[PS5_MAX_TEXTURE_UNITS];
static uint8_t texels[PS5_MAX_TEXTURE_UNITS][64];
static bool __attribute__((unused)) ps5_collect_occlusion_query_resource(
    struct ps5_query *query, struct pipe_resource *base) {
    uint64_t value;
    assert(query && base && base != &query_backing.base);
    memcpy(&value, ((struct ps5_resource *)base)->data, sizeof(value));
    query->value += value;
    return true;
}
static struct pipe_resource *create(struct pipe_screen *s, const struct pipe_resource *r) {
    assert(s == &screen.base && r->target == PIPE_BUFFER && (deferred_mode || !locked));
    if ((int)allocated == fail_alloc) return NULL;
    unsigned i = allocated++;
    assert(i < TEST_COPIES);
    copies[i] = (struct ps5_resource){.base=*r, .data=copy_bytes[i], .size=64, .allocation_size=64};
    if ((int)i == bad_high_alloc)
        copies[i].data=(uint8_t *)((uintptr_t)copies[i].data ^ (UINT64_C(1)<<32));
    copies[i].base.refs=1;
    return &copies[i].base;
}
static void pipe_resource_reference(struct pipe_resource **dst, struct pipe_resource *src) {
    if (src) ++src->refs;
    if (*dst) {
        bool pending_resource=false;
        for (unsigned i=0;i<staged;++i) for (unsigned stage=0;stage<3;++stage)
            pending_resource |= *dst==&pending[i][stage]->base;
        assert((*dst)->refs);
        if (!--(*dst)->refs) { assert(!pending_resource); ++freed; }
    }
    *dst=src;
}
void ps5_screen_submit_lock(struct pipe_screen *s) { assert(s == &screen.base && !locked); locked=1; }
void ps5_screen_submit_unlock(struct pipe_screen *s) { assert(s == &screen.base && locked); locked=0; }
static int begin(void) { assert(locked && !staged); ++begun; return fail_begin; }
static unsigned async_retained_draws;
static int end(void) {
    assert(locked && context.vertex_descriptor_table == &original[0].base &&
        context.descriptor_storage[0] == &original[1].base && context.descriptor_storage[1] == &original[2].base);
    ++ended;
    for (unsigned i=0; i<staged; ++i) {
        for (unsigned stage=0; stage<3; ++stage) {
            assert(pending[i][stage]->base.refs == 1);
            unsigned start, id;
            memcpy(&start, pending[i][stage]->data, sizeof(start));
            memcpy(&id, pending[i][stage]->data + sizeof(start), sizeof(id));
            assert(start == expected_start[i] && id == expected_id[i]);
            for (unsigned byte=16; byte<64; ++byte)
                assert(pending[i][stage]->data[byte] == expected_uniform[i][stage]);
        }
    }
    unsigned factor = deferred_mode ? retained_draws + async_retained_draws : 1;
    assert(borrowed.base.refs == 1+factor*(4+PIPE_MAX_ATTRIBS+2*PS5_MAX_CONSTANT_BUFFERS+
        (context.framebuffer.zsbuf.texture == &borrowed.base))-unindexed_draws);
    if (context.framebuffer.zsbuf.texture == &depth_buffer.base)
        assert(depth_buffer.base.refs == 1+factor);
    for (unsigned unit=0; unit<PS5_MAX_TEXTURE_UNITS; ++unit) {
        unsigned bindings = ((shader_textures & (1u << unit)) && context.sampler_views[0][unit]) +
                            ((fragment_textures & (1u << unit)) && context.sampler_views[1][unit]);
        if (bindings) assert(textures[unit].base.refs == 1+factor*bindings);
    }
    if (fail_end) return -1;
    staged=retained_draws=unindexed_draws=0;
    return 0;
}
static int (*ps5_agc_gate2_batch_begin)(void)=begin;
static int (*ps5_agc_gate2_batch_begin_framebuffer)(void)=begin;
static int (*ps5_agc_gate2_batch_end)(void)=end;
static int (*ps5_agc_gate2_batch_submit)(void) __attribute__((unused));
static int (*ps5_agc_gate2_batch_retire)(int) __attribute__((unused));
''' + flush_cache_type + r'''
static void ps5_draw_vbo_locked(struct pipe_context *b, const struct pipe_draw_info *info, unsigned id,
    const struct pipe_draw_indirect_info *indirect, const struct pipe_draw_start_count_bias *draw, unsigned n,
    struct ps5_batch_flush_cache *flush_cache, bool *submitted) {
    struct ps5_context *drawing=(struct ps5_context *)b;
    if (submitted) *submitted=false;
    assert((b == &context.base || deferred_mode) && info && !indirect && n==1 && draw->count && locked);
    assert(id == 20 + (info->increment_draw_id ? draw->start : 0));
    if ((int)calls++ == fail_draw) { drawing->last_draw_status=-15; return; }
    if (submitted) *submitted=true;
    if (drawing->queries_enabled && drawing->active_occlusion_query) {
        struct ps5_query *query=drawing->active_occlusion_query;
        uint64_t value=draw->start+1;
        assert(query->buffer);
        if (flush_cache) {
            assert(query->buffer != &query_backing.base);
            memcpy(((struct ps5_resource *)query->buffer)->data,&value,sizeof(value));
        } else query->value += value;
    }
    if (drawing->queries_enabled && drawing->active_primitives_generated_query[0])
        drawing->active_primitives_generated_query[0]->value += draw->count/3;
    if (!flush_cache) {
        assert(drawing->vertex_descriptor_table == &original[0].base &&
            drawing->descriptor_storage[0] == &original[1].base &&
            drawing->descriptor_storage[1] == &original[2].base);
        return;
    }
    ++retained_draws;
    if (!info->index_size) ++unindexed_draws;
    assert(staged < PS5_MULTIDRAW_BATCH_CAPACITY);
    /* Model a backing flush: cache ownership ends at EVERY batch boundary. */
    assert(flush_cache);
    for (unsigned unit = 0; unit < 3 + PS5_MAX_TEXTURE_UNITS + PIPE_MAX_ATTRIBS; ++unit) {
        if (!staged) assert(!flush_cache->data[unit] && !flush_cache->size[unit]);
        else assert(flush_cache->data[unit] == &borrowed && flush_cache->size[unit] == begun);
        flush_cache->data[unit] = &borrowed;
        flush_cache->size[unit] = begun;
    }
    pending[staged][0]=(struct ps5_resource *)drawing->vertex_descriptor_table;
    pending[staged][1]=(struct ps5_resource *)drawing->descriptor_storage[0];
    pending[staged][2]=(struct ps5_resource *)drawing->descriptor_storage[1];
    for (unsigned stage=0; stage<3; ++stage) {
        assert(pending[staged][stage] != &original[stage]);
        memcpy(pending[staged][stage]->data, &draw->start, sizeof(draw->start));
        memcpy(pending[staged][stage]->data + sizeof(draw->start), &id, sizeof(id));
        expected_uniform[staged][stage]=pending[staged][stage]->data[16];
        for (unsigned i=0; i<staged; ++i) assert(pending[i][stage] != pending[staged][stage]);
    }
    expected_start[staged]=draw->start; expected_id[staged++]=id;
}
''' + body + r'''
static void reset(void) {
    all_elements.count=PIPE_MAX_ATTRIBS;
    for(unsigned i=0;i<PIPE_MAX_ATTRIBS;i++)all_elements.elements[i].vertex_buffer_index=i;
    allocated=freed=begun=ended=staged=calls=locked=shader_textures=fragment_textures=retained_draws=unindexed_draws=0;
    fail_alloc=fail_draw=bad_high_alloc=-1; fail_begin=fail_end=0;
    borrowed=(struct ps5_resource){.base={.target=PIPE_TEXTURE_2D, .format=1, .refs=1},
        .data=borrowed_bytes, .size=64, .allocation_size=64};
    depth_buffer=(struct ps5_resource){.base={.target=PIPE_TEXTURE_2D, .format=PIPE_FORMAT_Z32_FLOAT, .refs=1},
        .data=depth_bytes, .size=64, .allocation_size=64};
    query_backing=(struct ps5_resource){.base={.target=PIPE_BUFFER, .refs=1},
        .data=depth_bytes, .size=64, .allocation_size=64};
    screen=(struct ps5_screen){.base={create}, .render_pool=&borrowed.base};
    context=(struct ps5_context){.base={&screen.base}, .framebuffer={.cbufs={{.texture=&borrowed.base, .format=1}},
        .nr_cbufs=1}, .framebuffer_valid=true, .vs=&vertex_shader, .fs=&vertex_shader,
        .vertex_elements=&all_elements, .vertex_buffer_count=PIPE_MAX_ATTRIBS, .border_color_storage=&borrowed.base};
    for (unsigned i=0; i<3; ++i) {
        memset(original_bytes[i], 0xa0+i, 64);
        original[i]=(struct ps5_resource){.base={.target=PIPE_BUFFER, .refs=1, .width0=64}, .data=original_bytes[i], .size=64, .allocation_size=64};
    }
    context.vertex_descriptor_table=&original[0].base;
    context.descriptor_storage[0]=&original[1].base; context.descriptor_storage[1]=&original[2].base;
    for (unsigned i=0; i<PIPE_MAX_ATTRIBS; ++i) context.vertex_buffers[i].buffer.resource=&borrowed.base;
    for (unsigned s=0; s<2; ++s) for (unsigned i=0; i<PS5_MAX_CONSTANT_BUFFERS; ++i)
        context.constants[s][i].buffer=&borrowed.base, context.constants[s][i].valid=true;
    for (unsigned unit=0; unit<PS5_MAX_TEXTURE_UNITS; ++unit) {
        textures[unit]=(struct ps5_resource){.base={.target=PIPE_TEXTURE_2D, .format=1, .refs=1},
            .data=texels[unit], .size=64, .allocation_size=64, .render_staging_size=64};
        views[unit]=(struct pipe_sampler_view){.texture=&textures[unit].base, .target=PIPE_TEXTURE_2D, .format=1};
    }
}
int main(void) {
    struct pipe_draw_info info={.mode=4, .instance_count=1, .index_size=2, .index={&borrowed.base}};
    struct pipe_draw_start_count_bias draws[TEST_DRAWS];
    for (unsigned i=0; i<TEST_DRAWS; ++i) draws[i]=(struct pipe_draw_start_count_bias){i, i==3 ? 0 : 6, 0};
    for (unsigned increment=0; increment<2; ++increment) {
        reset(); info.increment_draw_id=increment;
        assert(ps5_try_multi_draw_batch(&context.base, &info, 20, NULL, draws, TEST_DRAWS));
        assert(allocated==SNAPSHOTS && freed==SNAPSHOTS && begun==3 && ended==3 && calls==TEST_DRAWS-1 && !locked);
        assert(borrowed.base.refs==1 && !context.last_draw_status);
        for (unsigned s=0; s<3; ++s) for (unsigned byte=0; byte<64; ++byte)
            assert(original_bytes[s][byte]==0xa0+s);
    }
    for (int i=0; i<SNAPSHOTS; ++i) {
        reset(); fail_alloc=i;
        assert(!ps5_try_multi_draw_batch(&context.base, &info, 20, NULL, draws, TEST_DRAWS));
        assert(freed==allocated && !begun && borrowed.base.refs==1);
    }
    for (int failure=0; failure<2; ++failure) {
        reset(); fail_begin=failure==0 ? -1:0; fail_end=failure==1;
        assert(ps5_try_multi_draw_batch(&context.base, &info, 20, NULL, draws, TEST_DRAWS));
        assert(context.last_draw_status && !locked && begun==1);
        assert(freed==(failure ? 0:SNAPSHOTS));
        assert((borrowed.base.refs==1)==!failure);
    }
    reset(); fail_draw=4;
    assert(ps5_try_multi_draw_batch(&context.base, &info, 20, NULL, draws, TEST_DRAWS));
    assert(!context.last_draw_status && !locked && begun==3 && ended==3 &&
           calls==TEST_DRAWS && freed==SNAPSHOTS && borrowed.base.refs==1);
    reset();
#define REJECT(field,value) do { struct ps5_context c=context; c.field=value; \
    assert(!ps5_multidraw_eligible(&c,&info,NULL,draws,TEST_DRAWS)); } while(0)
    REJECT(gs,&vertex_shader); REJECT(render_condition_query,1);
    REJECT(stream_output_target_count,1); REJECT(framebuffer.zsbuf.texture,&borrowed.base);
    REJECT(framebuffer.nr_cbufs,PS5_MAX_RENDER_TARGETS+1); REJECT(framebuffer_valid,false);
    REJECT(vertex_buffers[0].is_user_buffer,true); REJECT(vertex_buffer_count,PIPE_MAX_ATTRIBS+1);
    context.framebuffer.cbufs[0].format=2;
    context.framebuffer.cbufs[0].level=1;
    context.framebuffer.cbufs[0].first_layer=2;
    context.framebuffer.cbufs[0].last_layer=2;
    context.framebuffer.cbufs[1]=(struct pipe_surface){.texture=&textures[0].base,
        .format=2, .level=1, .first_layer=1, .last_layer=1};
    textures[0].render_staging_size=0;
    context.framebuffer.nr_cbufs=2;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    borrowed.render_staging_size=64;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    context.framebuffer.cbufs[0].last_layer=3;
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    reset();
    shader_textures=1; context.fs=&fragment_shader; context.sampler_views[0][0]=&views[0];
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    assert(ps5_try_multi_draw_batch(&context.base,&info,20,NULL,draws,TEST_DRAWS));
    assert(textures[0].base.refs==1 && !context.last_draw_status);
    reset();
    shader_storage=16; assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    shader_storage=0;
    struct pipe_depth_stencil_alpha_state dsa={0};
    for (unsigned format=77; format<=78; ++format) for (unsigned enabled=0; enabled<2; ++enabled) {
        reset(); dsa.depth_enabled=enabled;
        context.depth_stencil_alpha=&dsa;
        depth_buffer.base.format=format;
        context.framebuffer.zsbuf=(struct pipe_surface){.texture=&depth_buffer.base, .format=format};
        assert(ps5_try_multi_draw_batch(&context.base,&info,20,NULL,draws,TEST_DRAWS));
        assert(borrowed.base.refs==1 && depth_buffer.base.refs==1 && freed==SNAPSHOTS && !context.last_draw_status);
    }
    depth_buffer.base.target=PIPE_TEXTURE_2D_ARRAY;
    depth_buffer.base.array_size=4;
    depth_buffer.base.last_level=1;
    depth_buffer.base.format=PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
    context.framebuffer.zsbuf=(struct pipe_surface){.texture=&depth_buffer.base,
        .format=PIPE_FORMAT_Z32_FLOAT_S8X24_UINT, .level=1, .first_layer=1, .last_layer=2};
    dsa.stencil[0].enabled=true;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    dsa.stencil[0].enabled=false;
#define REJECT_DEPTH(field,value) do { struct ps5_resource saved=depth_buffer; depth_buffer.field=value; \
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS)); depth_buffer=saved; } while(0)
    REJECT_DEPTH(depth_staging_size,1); REJECT_DEPTH(base.target,PIPE_BUFFER);
    REJECT_DEPTH(base.format,1); REJECT(framebuffer.zsbuf.format,1);
    REJECT(framebuffer.zsbuf.level,2); REJECT(framebuffer.zsbuf.first_layer,3);
    REJECT(framebuffer.zsbuf.last_layer,4);
    for (unsigned failure=0; failure<2; ++failure) {
        reset(); fragment_textures=0xffff; context.fs=&fragment_shader; fail_end=failure;
        for (unsigned unit=0; unit<PS5_MAX_TEXTURE_UNITS; ++unit) context.sampler_views[1][unit]=&views[unit];
        assert(ps5_try_multi_draw_batch(&context.base,&info,20,NULL,draws,TEST_DRAWS));
        for (unsigned unit=0; unit<PS5_MAX_TEXTURE_UNITS; ++unit) assert(textures[unit].base.refs == 1+failure);
    }
    reset(); context.fs=&fragment_shader; fragment_textures=1u << 7;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS)); /* Null descriptor is legal. */
    assert(ps5_try_multi_draw_batch(&context.base,&info,20,NULL,draws,TEST_DRAWS));
    context.sampler_views[1][7]=&views[7];
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
#define REJECT_TEX(field,value) do { struct ps5_resource saved=textures[7]; textures[7].field=value; \
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS)); textures[7]=saved; } while(0)
    REJECT_TEX(depth_staging_size,1); REJECT_TEX(data,NULL); REJECT_TEX(size,0);
    textures[7].base.target=views[7].target=PIPE_TEXTURE_2D_ARRAY;
    textures[7].base.format=views[7].format=2;
    textures[7].base.nr_samples=textures[7].base.nr_storage_samples=4;
    textures[7].base.last_level=3;
    views[7].u.tex.first_level=1; views[7].u.tex.last_level=3;
    views[7].u.tex.first_layer=2; views[7].u.tex.last_layer=5;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    memset(&views[7].u,0,sizeof(views[7].u));
    textures[7].base.target=views[7].target=PIPE_TEXTURE_2D;
    textures[7].base.nr_samples=textures[7].base.nr_storage_samples=0;
    textures[7].base.last_level=0;
    textures[7].base.bind=PIPE_BIND_RENDER_TARGET;
    textures[7].render_staging_size=0;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    textures[7].base.bind=0;
    textures[7].base.format=views[7].format=PIPE_FORMAT_Z32_FLOAT;
    textures[7].base.bind=PIPE_BIND_DEPTH_STENCIL;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    textures[7].depth_staging_size=1;
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    textures[7].depth_staging_size=0;
    textures[7].base.bind=0;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    textures[7].base.format=views[7].format=1;
#define REJECT_VIEW(field,value) do { struct pipe_sampler_view saved=views[7]; views[7].field=value; \
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS)); views[7]=saved; } while(0)
    REJECT_VIEW(texture,&borrowed.base); REJECT_VIEW(texture,NULL);
    textures[7].base.format=PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
    views[7].format=PIPE_FORMAT_X32_S8X24_UINT;
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    textures[7].base.format=views[7].format=1;
    views[0].format=2; context.sampler_views[1][0]=&views[0]; /* Unused state cannot veto a batch. */
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    struct ps5_query query={.buffer=&query_backing.base};
    context.active_occlusion_query=&query;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    assert(!ps5_try_multi_draw_batch(&context.base,&info,20,NULL,draws,TEST_DRAWS));
    context.active_occlusion_query=NULL;
    context.active_primitives_generated_query[0]=&query;
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    assert(!ps5_try_multi_draw_batch(&context.base,&info,20,NULL,draws,TEST_DRAWS));
    uint64_t shader_rejects=context.batch_reject[PS5_BATCH_REJECT_SHADER-1];
    uint64_t framebuffer_rejects=context.batch_reject[PS5_BATCH_REJECT_FRAMEBUFFER-1];
    context.gs=&vertex_shader; context.framebuffer_valid=false;
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,TEST_DRAWS));
    assert(context.batch_reject[PS5_BATCH_REJECT_SHADER-1]==shader_rejects+1 &&
           context.batch_reject[PS5_BATCH_REJECT_FRAMEBUFFER-1]==framebuffer_rejects+1);
    assert(context.batch_eligible && context.batch_reject[PS5_BATCH_REJECT_TEXTURE-1]);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "ownership"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-DPS5_DRAW_PROFILE=1",
                    "-I" + str(root / "src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                   input=code, text=True, check=True)
    subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
    mutations = (
        ("   for (unsigned first = 0; first < num_draws;) {\n      struct ps5_batch_flush_cache flush_cache = {0};",
         "   struct ps5_batch_flush_cache flush_cache = {0};\n   for (unsigned first = 0; first < num_draws;) {"),
        ("pipe_resource_reference(&retained[retained_count++],\n"
         "                                    context->sampler_views[stage][unit]->texture);",
         "(void)0;"),
        ("pipe_resource_reference(&retained[retained_count++], context->framebuffer.zsbuf.texture);",
         "(void)0;"),
    )
    for before, after in mutations:
        assert code.count(before) == 1
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-DPS5_DRAW_PROFILE=1",
                        "-I" + str(root / "src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                       input=code.replace(before, after), text=True, check=True)
        failed = subprocess.run([str(exe)], cwd=tmp, text=True, capture_output=True)
        assert failed.returncode != 0 and "Assertion" in failed.stderr
print("PASS: descriptors/uniforms, chunk retirement, draw IDs, rollback, vertex/fragment texture refs and native batching eligibility")

# Reuse the same Gallium mocks, but exercise the actual cross-call queue too.
start = source.index("struct ps5_deferred_slot {")
deferred = source[start:source.index(
    "\n#endif\n\nstatic bool\nps5_lower_default_tess_levels(", start)]
fence_helpers = source[source.index("static uint64_t\nps5_draw_batch_fence_submit("):source.index("static bool\nps5_memory_overlaps(")]
deferred = deferred.replace(fence_helpers, "")
deferred_code = code[:code.index("int main(void) {")] + r'''
static unsigned scanout_waits;
static int scanout_wait(void) { assert(locked); ++scanout_waits; return 0; }
static int (*ps5_agc_gate2_wait_present)(void) = scanout_wait;
static unsigned ps5_deferred_mutex;
static uint64_t ps5_texture_publication_epoch=1;
static void simple_mtx_lock(unsigned *m) { assert(m == &ps5_deferred_mutex && !locked); locked=1; }
static void simple_mtx_unlock(unsigned *m) { assert(m == &ps5_deferred_mutex && locked); locked=0; }
#ifdef PS5_GPU_PRESENT_BATCH
static unsigned gpu_present_requests;
static int gpu_present_error;
int ps5_agc_gate2_batch_present(unsigned index) {
    assert(locked && staged && index == 1); ++gpu_present_requests; return gpu_present_error;
}
#endif
static jmp_buf exit_jump;
static _Noreturn void check_exit(int status) {
    assert(status == EXIT_FAILURE && locked && staged && !freed);
    longjmp(exit_jump,1);
}
#define _Exit check_exit
''' + deferred + r'''
#undef _Exit
static void drain(void) {
    simple_mtx_lock(&ps5_deferred_mutex);
    ps5_draw_batch_flush_locked();
    ps5_draw_batch_retire_locked(true);
    simple_mtx_unlock(&ps5_deferred_mutex);
}
static void idle(void) {
    assert(!locked && !staged && !ps5_deferred.owner && !ps5_deferred.count);
    assert(allocated == freed && borrowed.base.refs == 1 && depth_buffer.base.refs == 1);
}
int main(void) {
    struct pipe_draw_info info={.mode=4,.instance_count=1,.index_size=2,.index={&borrowed.base}};
    struct pipe_draw_start_count_bias draw={0,6,0};
    deferred_mode=true;
    reset();
    ps5_context_queue_present(&context.base, 1); /* Empty queue: unchanged CPU fallback. */
    struct pipe_draw_info fan={.mode=MESA_PRIM_TRIANGLE_FAN,.instance_count=1};
    struct pipe_draw_start_count_bias quad={0,4,0};
    assert(!ps5_try_deferred_draw(&context.base,&fan,20,NULL,&quad,1));
    context.deferred_blitter_draw=true;
    assert(!ps5_try_deferred_draw(&context.base,&fan,20,NULL,&quad,1));
    __typeof__(*context.blitter) blitter={.running=true};
    context.blitter=&blitter;
    for (unsigned invalid=0;invalid<7;++invalid) {
        struct pipe_draw_info f=fan;
        struct pipe_draw_start_count_bias q=quad;
        if (invalid==0) context.deferred_blitter_draw=false;
        if (invalid==1) f.index_size=2;
        if (invalid==2) f.instance_count=2;
        if (invalid==3) f.start_instance=1;
        if (invalid==4) q.count=3;
        if (invalid==5) q.start=1;
        if (invalid==6) blitter.running=false;
        assert(!ps5_try_deferred_draw(&context.base,&f,20,NULL,&q,1));
        context.deferred_blitter_draw=true; blitter.running=true;
    }
    assert(ps5_try_deferred_draw(&context.base,&fan,20,NULL,&quad,1));
    context.deferred_blitter_draw=false; blitter.running=false;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    assert(staged==2 && !ended); /* Internal clear and ordinary draw share one retirement. */
#ifdef PS5_GPU_PRESENT_BATCH
    struct ps5_context other_present = context;
    ps5_context_queue_present(&other_present.base, 1);
    assert(!gpu_present_requests && !locked && !ended);
    ps5_context_queue_present(&context.base, 1);
    assert(gpu_present_requests == 1 && !locked && !ended && staged == 2);
#endif
    drain(); idle(); assert(ended==1);
    struct ps5_resource display = {.base={.target=PIPE_TEXTURE_2D, .bind=PIPE_BIND_DISPLAY_TARGET}};
    unsigned waits_before = scanout_waits;
    ps5_draw_batch_drain_buffer(&display.base);
    assert(scanout_waits == waits_before + 1);
    display.base.bind = 0;
    ps5_draw_batch_drain_buffer(&display.base);
    assert(scanout_waits == waits_before + 1);
    assert(!ps5_memory_overlaps((void *)100, 10, (void *)110, 10));
    assert(!ps5_memory_overlaps((void *)110, 10, (void *)100, 10));
    assert(ps5_memory_overlaps((void *)100, 10, (void *)109, 10));
    assert(ps5_memory_overlaps((void *)109, 10, (void *)100, 10));
    assert(ps5_memory_overlaps((void *)100, 0, (void *)110, 10));
    assert(ps5_memory_overlaps(NULL, 10, (void *)110, 10));
    assert(ps5_memory_overlaps((void *)(UINTPTR_MAX-1), 4, (void *)100, 10));
    for (unsigned kind=0; kind<12; ++kind) {
        reset();
        uint8_t unrelated[64];
        struct ps5_resource cpu={.base={.target=PIPE_BUFFER}, .data=unrelated, .allocation_size=64};
        assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        cpu.texture_publication_epoch=1;
        ps5_draw_batch_drain_buffer(&cpu.base);
        assert(!cpu.texture_publication_epoch);
        assert(!locked && !ended && staged==1); /* Unrelated uploads leave the batch queued. */
        if (kind<3) cpu.data=copies[kind].data+1; /* Each private descriptor allocation. */
        if (kind==3) cpu.data=borrowed.data+63; /* Distinct object, last-byte overlap. */
        if (kind==4) {
            cpu.base.target=PIPE_TEXTURE_2D;
            uint64_t epoch_before=ps5_texture_publication_epoch;
            cpu.stencil_publication_epoch=epoch_before;
            ps5_draw_batch_drain_buffer(&cpu.base);
            assert(ps5_texture_publication_epoch==epoch_before && !cpu.stencil_publication_epoch);
            cpu.direct_start=-1; /* Unowned alias stays conservative. */
            ps5_draw_batch_drain_buffer(&cpu.base);
            assert(ps5_texture_publication_epoch==epoch_before+1);
            cpu.render_arena_slot_count=1;
            ps5_draw_batch_drain_buffer(&cpu.base);
            assert(ps5_texture_publication_epoch==epoch_before+1);
            cpu.base.bind=PIPE_BIND_DISPLAY_TARGET;
            ps5_draw_batch_drain_buffer(&cpu.base);
            assert(ps5_texture_publication_epoch==epoch_before+2);
            cpu.base.bind=0;cpu.external_cpu_access=true;
            ps5_draw_batch_drain_buffer(&cpu.base);
            assert(ps5_texture_publication_epoch==epoch_before+3);
            assert(!ended && staged==1); /* Unrelated texture upload stays asynchronous. */
            cpu.data=borrowed.data;
        }
        if (kind==10) { cpu.stencil_data=borrowed.data; cpu.stencil_allocation_size=64; }
        if (kind==11) {
            borrowed.stencil_data=unrelated; borrowed.stencil_allocation_size=64;
            cpu.data=(uint8_t *)1; cpu.stencil_data=unrelated; cpu.stencil_allocation_size=64;
        }
        if (kind==5) cpu.data=NULL;
        if (kind==6) cpu.allocation_size=0;
        if (kind==7) { borrowed.stencil_data=cpu.data; borrowed.stencil_allocation_size=64; }
        if (kind==8) { borrowed.stencil_data=cpu.data; borrowed.stencil_allocation_size=0; }
        ps5_draw_batch_drain_buffer(kind==9 ? NULL : &cpu.base);
        idle(); assert(ended==1);
    }
    reset();
    borrowed.base.bind=PIPE_BIND_DISPLAY_TARGET;
    borrowed.data=(uint8_t *)4096; borrowed.allocation_size=0x4000000;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    struct ps5_resource arena={.base={.target=PIPE_BUFFER, .refs=1},
        .data=(uint8_t *)(4096u+PS5_RENDER_ARENA_OFFSET), .allocation_size=16384};
    ps5_draw_batch_drain_buffer(&arena.base);
    assert(!ended && staged==1); /* Parent lifetime reference is not access to all arena bytes. */
    pipe_resource_reference(&ps5_deferred.slots[0].retained[ps5_deferred.slots[0].retained_count++], &arena.base);
    ps5_draw_batch_drain_buffer(&arena.base);
    idle(); assert(ended==1 && arena.base.refs==1); /* A used suballocation still drains. */
    reset();
    borrowed.base.bind=PIPE_BIND_DISPLAY_TARGET;
    borrowed.data=(uint8_t *)4096; borrowed.allocation_size=0x4000000;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    arena.data=(uint8_t *)(4096u+PS5_RENDER_ARENA_OFFSET-1);
    ps5_draw_batch_drain_buffer(&arena.base);
    idle(); assert(ended==1); /* Overlap with either scanout slot still drains. */
    for (unsigned depth=0; depth<2; ++depth) for (unsigned n=1;n<=PS5_MULTIDRAW_BATCH_CAPACITY;++n) {
        reset();
        struct pipe_depth_stencil_alpha_state dsa={.depth_enabled=true};
        if (depth) {
            context.depth_stencil_alpha=&dsa;
            context.framebuffer.zsbuf=(struct pipe_surface){.texture=&depth_buffer.base, .format=77};
        }
        for (unsigned i=0;i<n;++i) {
            for (unsigned s=0;s<3;++s) memset(original_bytes[s],0x40+i+s,64);
            draw.start=i;
            assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
            assert(!locked && !context.last_draw_status && begun==1);
            assert(ended==(i+1==PS5_MULTIDRAW_BATCH_CAPACITY)); /* CPU staging, not a hidden wait per draw. */
            for (unsigned s=0;s<3;++s) assert(original_bytes[s][0]==(uint8_t)(0x40+i+s));
        }
        drain(); idle(); assert(ended==1 && calls==n);
    }
    reset();
    for (unsigned i=0;i<TEST_DRAWS;++i) {
        draw.start=i;
        assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        assert(ended==(i+1)/PS5_MULTIDRAW_BATCH_CAPACITY);
    }
    drain(); idle(); assert(ended==3 && calls==TEST_DRAWS);
    reset(); fragment_textures=0xffff; context.fs=&fragment_shader;
    for (unsigned u=0;u<16;++u) context.sampler_views[1][u]=&views[u];
    for (unsigned i=0;i<3;++i) assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    drain(); idle();
    for (unsigned u=0;u<16;++u) assert(textures[u].base.refs==1);
    reset();
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    struct ps5_context other=context;
    assert(ps5_try_deferred_draw(&other.base,&info,20,NULL,&draw,1));
    assert(ended==1 && ps5_deferred.owner==&other); /* Owner switches retire first. */
    drain(); idle(); assert(ended==2);
    for (unsigned boundary=0;boundary<2;++boundary) {
        reset();
        assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        if (!boundary) context.gs=&vertex_shader;
        assert(!ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,boundary?2:1));
        idle(); assert(ended==1 && calls==1);
    }
    reset();
    struct ps5_query occlusion={.buffer=&query_backing.base}, primitives={0};
    context.queries_enabled=true;
    context.active_occlusion_query=&occlusion;
    context.active_primitives_generated_query[0]=&primitives;
    draw.start=2;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    assert(occlusion.buffer==&query_backing.base && !occlusion.value && primitives.value==2);
    draw.start=7;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    assert(occlusion.buffer==&query_backing.base && !occlusion.value && primitives.value==4);
    drain(); idle();
    assert(occlusion.value==11 && query_backing.base.refs==1);
    for (int fail=0;fail<3;++fail) {
        reset();
        assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        fail_alloc=3+fail;
        assert(!ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        idle(); assert(ended==1 && calls==1); /* No staged draw replay on OOM. */
    }
    reset();
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    bad_high_alloc=allocated;
    assert(!ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    idle(); assert(ended==1 && calls==1); /* Unusable direct fallback drains before retry. */
    reset(); fail_begin=-1;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    idle(); assert(context.last_draw_status==-30 && !calls && !ended);
    reset();
    occlusion=(struct ps5_query){.buffer=&query_backing.base};
    primitives=(struct ps5_query){0};
    context.queries_enabled=true;
    context.active_occlusion_query=&occlusion;
    context.active_primitives_generated_query[0]=&primitives;
    draw.start=2;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    fail_draw=1;
    draw.start=7;
    assert(!ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    assert(!context.last_draw_status && calls==2 && ended==1 &&
           occlusion.value==3 && primitives.value==2);
    bool submitted=false;
    ps5_screen_submit_lock(&screen.base);
    ps5_draw_vbo_locked(&context.base,&info,20,NULL,&draw,1,NULL,&submitted);
    ps5_screen_submit_unlock(&screen.base);
    idle(); assert(submitted && !context.last_draw_status && calls==3 &&
                   occlusion.value==11 && primitives.value==4);
    reset();
    occlusion=(struct ps5_query){.buffer=&query_backing.base};
    primitives=(struct ps5_query){0};
    context.active_occlusion_query=&occlusion;
    context.active_primitives_generated_query[0]=&primitives;
    context.queries_enabled=true;
    draw.start=2;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    drain(); assert(occlusion.value==3 && primitives.value==2);
    context.queries_enabled=false; /* u_blitter internal clear */
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    assert(!ps5_deferred.slots[0].occlusion_buffer && !ps5_deferred.slots[0].occlusion_query);
    assert(occlusion.value==3 && primitives.value==2);
    context.queries_enabled=true; /* Restore must preserve the uncounted queued clear. */
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    drain(); assert(occlusion.value==6 && primitives.value==4 && query_backing.base.refs==1);
    reset(); draw.count=0;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    idle(); assert(!begun && !allocated);
    reset(); draw.count=6;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    fail_end=1;
#ifdef PS5_GPU_PRESENT_BATCH
    gpu_present_error=1;
    if (!setjmp(exit_jump)) { ps5_context_queue_present(&context.base, 1); assert(!"queue error returned"); }
#else
    if (!setjmp(exit_jump)) { drain(); assert(!"cleanup failure returned"); }
#endif
    assert(!freed && borrowed.base.refs>1); /* Simulated process exit retains ownership. */
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "deferred"
    for mutate, flags in ((0, []), (1, []), (2, []), (0, ["-DPS5_GPU_PRESENT_BATCH=1"])):
        candidate = deferred_code
        if mutate == 1:
            candidate = candidate.replace("if (ps5_deferred.owner && ps5_deferred.owner != context)", "if (false)")
            assert candidate != deferred_code
        if mutate == 2:
            candidate = candidate.replace("   memset(batch, 0, offsetof(struct ps5_deferred_batch, slots));",
                "   struct ps5_batch_flush_cache stale = batch->flush_cache;\n"
                "   memset(batch, 0, offsetof(struct ps5_deferred_batch, slots));\n"
                "   batch->flush_cache = stale;")
            assert candidate != deferred_code
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", *flags,
                        "-I" + str(root / "src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                       input=candidate, text=True, check=True)
        run = subprocess.run([str(exe)], cwd=tmp, capture_output=True, text=True)
        assert (run.returncode == 0) == (mutate == 0), f"mutate={mutate} flags={flags}\n{run.stderr}"

# Exercise the new runtime entry points, not only the synchronous fallback above.
submit_start = source.index("static void\nps5_draw_batch_submit(")
submit_helper = source[submit_start:source.index("\n}\n", submit_start) + 3]
def query_function(name):
    start = source.index("\n" + name + "(")
    start = source.rfind("static ", 0, start)
    return source[start:source.index("\n}\n", start) + 3]
query_code = r'''
struct pipe_query;
enum { PIPE_QUERY_PRIMITIVES_GENERATED=1, PIPE_QUERY_PRIMITIVES_EMITTED,
       PIPE_QUERY_SO_OVERFLOW_PREDICATE, PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE,
       PIPE_QUERY_OCCLUSION_COUNTER, PIPE_QUERY_OCCLUSION_PREDICATE,
       PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE, PIPE_QUERY_TIME_ELAPSED };
#define PS5_ENABLE_TRANSFORM_FEEDBACK_CANDIDATE 1
#define PS5_ENABLE_GLSL_460_CANDIDATE 1
#define PS5_ENABLE_OCCLUSION_QUERY_CANDIDATE 1
static void ps5_draw_batch_drain(void) { drain(); }
static uint64_t os_time_get_nano(void) { return 1; }
''' + "\n".join(query_function(name) for name in (
    "ps5_active_primitive_query", "ps5_active_streamout_overflow_query",
    "ps5_begin_query", "ps5_end_query", "ps5_set_active_query_state"))
async_code = deferred_code[:deferred_code.index("int main(void) {")] + submit_helper + query_code + r'''
static unsigned in_flight;
static unsigned blocking_waits, probes;
static int async_submit(void) {
    unsigned submitted=retained_draws;
    int result=end();
    async_retained_draws+=submitted;
    ++in_flight;
    return result;
}
static int async_retire(int wait) {
    assert(in_flight);
    if (!wait) { ++probes; return 0; }
    ++blocking_waits;
    --in_flight;
    --async_retained_draws;
    return 1;
}
int main(void) {
    struct pipe_draw_info info={.mode=4,.instance_count=1,.index_size=2,.index={&borrowed.base}};
    struct pipe_draw_start_count_bias draw={0,6,0};
    deferred_mode=true;
    reset();
    ps5_agc_gate2_batch_submit=async_submit;
    ps5_agc_gate2_batch_retire=async_retire;
    struct ps5_query occlusion={.buffer=&query_backing.base,.type=PIPE_QUERY_OCCLUSION_COUNTER};
    assert(ps5_begin_query(&context.base,(struct pipe_query *)&occlusion));
    context.queries_enabled=true;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    assert(ps5_end_query(&context.base,(struct pipe_query *)&occlusion));
    assert(!ended && !freed && ps5_deferred.owner);
    /* Unoccupied slots must not be copied or cleared at submission. */
    ps5_deferred.slots[PS5_MULTIDRAW_BATCH_CAPACITY-1].retained_count=0xabc;
    ps5_inflight[ps5_inflight_head].slots[PS5_MULTIDRAW_BATCH_CAPACITY-1].retained_count=0xdef;
    ps5_draw_batch_submit();
    assert(in_flight && probes==1 && !blocking_waits && !freed);
    assert(ps5_deferred.slots[PS5_MULTIDRAW_BATCH_CAPACITY-1].retained_count==0xabc);
    assert(ps5_inflight[ps5_inflight_head].slots[PS5_MULTIDRAW_BATCH_CAPACITY-1].retained_count==0xdef);
    ps5_deferred.slots[PS5_MULTIDRAW_BATCH_CAPACITY-1].retained_count=0;
    ps5_inflight[ps5_inflight_head].slots[PS5_MULTIDRAW_BATCH_CAPACITY-1].retained_count=0;
    assert(ps5_inflight[ps5_inflight_head].owner && !ps5_deferred.owner && !occlusion.value);
    ps5_draw_batch_submit(); /* An empty flush must not retire the prior batch. */
    assert(in_flight && !blocking_waits && !freed);
    struct ps5_resource independent={.base={.target=PIPE_BUFFER},.data=(void *)1,.allocation_size=1};
    ps5_draw_batch_drain_buffer(&independent.base);
    assert(in_flight && !blocking_waits && !freed);
    struct ps5_query successor={.buffer=&query_backing.base,.type=PIPE_QUERY_OCCLUSION_COUNTER};
    assert(ps5_begin_query(&context.base,(struct pipe_query *)&successor));
    assert(in_flight && !blocking_waits && !freed);
    ps5_set_active_query_state(&context.base,false);
    ps5_set_active_query_state(&context.base,true);
    assert(in_flight && !blocking_waits);
    /* CPU can build a second batch while the first still owns its resources. */
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    assert(ps5_deferred.owner && ps5_inflight[ps5_inflight_head].owner && !freed);
    assert(ps5_end_query(&context.base,(struct pipe_query *)&successor));
    assert(!blocking_waits && !freed);
    /* Reusing the first query must collect both batches before resetting it. */
    assert(ps5_begin_query(&context.base,(struct pipe_query *)&occlusion));
    assert(!in_flight && blocking_waits==2 && probes>=2);
    assert(occlusion.value==0 && successor.value==1);
    assert(ps5_end_query(&context.base,(struct pipe_query *)&occlusion));
    idle();
    assert(ps5_begin_query(&context.base,(struct pipe_query *)&occlusion));
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    assert(ps5_end_query(&context.base,(struct pipe_query *)&occlusion));
    assert(ps5_begin_query(&context.base,(struct pipe_query *)&occlusion));
    assert(blocking_waits==3 && !occlusion.value); /* Reuse of an unsubmitted slot. */
    assert(ps5_end_query(&context.base,(struct pipe_query *)&occlusion));
    idle();
    reset();
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    ps5_draw_batch_submit();
    struct pipe_resource *first_storage=ps5_inflight[ps5_inflight_head].slots[0].storage[0];
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    ps5_draw_batch_drain_buffer(first_storage);
    assert(!in_flight && blocking_waits==4 && ps5_deferred.owner && staged==1);
    drain();
    assert(blocking_waits==5);
    idle();
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "async"
    for before, after in ((None, None),
        ("pending |= batches[i]->slots[slot].occlusion_query == query;",
         "pending |= (false && query);"),
        ("ps5_draw_batch_drain_query(query);", "ps5_draw_batch_drain();")):
        candidate = async_code if before is None else async_code.replace(before, after)
        assert before is None or candidate != async_code
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-DPS5_DEFERRED_DRAW_BATCH=1",
                    "-I" + str(root / "src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                       input=candidate, text=True, check=True)
        run = subprocess.run([str(exe)], cwd=tmp, capture_output=True, text=True)
        assert (run.returncode == 0) == (before is None), run.stderr
print("PASS: asynchronous flush, query scope changes, queued/in-flight query reuse, and delayed collection; both unsafe reuse and global-wait mutations rejected")

# Exercise real fence helpers against a delayed FIFO, including ring wraparound.
fence_code = async_code[:async_code.index("int main(void) {")]
fence_code = fence_code.replace("static uint64_t os_time_get_nano(void) { return 1; }", """
static uint64_t now_ns;
static unsigned ready_batches, clock_reads;
static uint64_t os_time_get_nano(void) { ++clock_reads; return now_ns; }
static void os_time_sleep(int64_t us) { assert(!locked); now_ns += us ? (uint64_t)us*1000 : 1; }
static void mock_pause(void) { assert(!locked); now_ns += 1000; }
#define __builtin_ia32_pause() mock_pause()

""")
fence_code = fence_code.replace("if (!wait) { ++probes; return 0; }", "if (!wait) { ++probes; if (!ready_batches) return 0; }")
fence_code = fence_code.replace("    ++blocking_waits;", "    if (wait) ++blocking_waits;\n    if (ready_batches) --ready_batches;")
fence_code += fence_helpers + r'''
int main(void) {
    struct pipe_draw_info info={.mode=4,.instance_count=1,.index_size=2,.index={&borrowed.base}};
    struct pipe_draw_start_count_bias draw={0,6,0};
    deferred_mode=true;
    reset();
    ps5_agc_gate2_batch_submit=async_submit;
    ps5_agc_gate2_batch_retire=async_retire;
    assert(ps5_draw_batch_fence_submit()==0);
    assert(ps5_draw_batch_fence_finish(0,0));
    assert(!clock_reads);
    for (unsigned round=0;round<3;++round) {
        uint64_t first=0, last=0;
        unsigned waits_before=blocking_waits;
        for (unsigned i=0;i<PS5_INFLIGHT_BATCH_CAPACITY;++i) {
            assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
            last=ps5_draw_batch_fence_submit();
            if (!i) first=last;
            assert(ps5_inflight_count==i+1 && in_flight==i+1);
            assert(blocking_waits==waits_before);
        }
        unsigned freed_before=freed;
        uint64_t time_before=now_ns;
        unsigned clocks_before=clock_reads;
        assert(!ps5_draw_batch_fence_finish(last,0));
        assert(clock_reads==clocks_before);
        assert(now_ns==time_before && freed==freed_before);
        assert(!ps5_draw_batch_fence_finish(last,250000));
        assert(now_ns-time_before==250000 && freed==freed_before);
        ready_batches=1;
        clocks_before=clock_reads;
        assert(ps5_draw_batch_fence_finish(first,0));
        assert(clock_reads==clocks_before);
        assert(ps5_completed_sequence==first && ps5_inflight_count==PS5_INFLIGHT_BATCH_CAPACITY-1);
        assert(!ps5_draw_batch_fence_finish(last,0));
        // Fill again, then only the oldest batch blocks to make room.
        for (unsigned i=0;i<2;++i) {
            assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
            last=ps5_draw_batch_fence_submit();
        }
        assert(blocking_waits==waits_before+1 && ps5_inflight_count==PS5_INFLIGHT_BATCH_CAPACITY);
        ready_batches=PS5_INFLIGHT_BATCH_CAPACITY;
        assert(ps5_draw_batch_fence_finish(last,UINT64_MAX));
        assert(!ps5_inflight_count && !in_flight && ps5_completed_sequence==last);
        assert(ps5_draw_batch_fence_finish(first,0)); // Old fence survives slot reuse.
        idle();
    }
    // An older buffer hazard must leave younger unrelated batches in flight.
    for (unsigned pass=0;pass<PS5_INFLIGHT_BATCH_CAPACITY+1;++pass) {
        for (unsigned i=0;i<3;++i) {
            assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
            ps5_draw_batch_fence_submit();
        }
        struct pipe_resource *old_storage=ps5_inflight[ps5_inflight_head].slots[0].storage[0];
        unsigned waits_before=blocking_waits;
        ps5_draw_batch_drain_buffer(old_storage);
        assert(blocking_waits==waits_before+1 && ps5_inflight_count==2);
        drain(); idle();
    }
    // CPU writes must find retained resources even in a non-head FIFO slot.
    for (unsigned i=0;i<3;++i) {
        assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        ps5_draw_batch_fence_submit();
    }
    struct pipe_resource *late=ps5_inflight[(ps5_inflight_head+2)%PS5_INFLIGHT_BATCH_CAPACITY].slots[0].storage[0];
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    unsigned waits_before=blocking_waits;
    ps5_draw_batch_drain_buffer(late);
    assert(blocking_waits==waits_before+3 && !ps5_inflight_count && ps5_deferred.owner);
    drain(); idle();
    // Unsubmitted successor work must not be executed by an old fence wait.
    uint64_t old=ps5_draw_batch_fence_submit();
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    assert(ps5_draw_batch_fence_finish(old,0) && ps5_deferred.owner && staged==1);
    drain(); idle();
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe=Path(tmp)/"fences"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    "-DPS5_DEFERRED_DRAW_BATCH=1", "-I"+str(root/"src/gallium/ps5"),
                    "-x", "c", "-o", str(exe), "-"], input=fence_code, text=True, check=True)
    subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
print("PASS: eight in-flight resource snapshots, FIFO wraparound, oldest-only backpressure, zero/finite/infinite fence waits and old-fence isolation")

# The queue test alone cannot prove that CPU access / lifecycle entry points drain.
for name in ("ps5_resource_get_info", "ps5_resource_stencil_info",
             "ps5_flush", "ps5_context_last_draw_status", "ps5_context_destroy", "ps5_screen_destroy"):
    start = source.index("\n" + name + "(")
    function = source[start:source.index("\n}\n", start)]
    assert function.count("ps5_draw_batch_drain();") == 1, name
for name, calls in {
    "ps5_get_timestamp": ["ps5_screen_submit_lock(NULL);", "ps5_screen_submit_unlock(NULL);"],
    "ps5_blit": ["ps5_draw_batch_drain_buffer(info ? info->src.resource : NULL);", "ps5_draw_batch_drain_buffer(info ? info->dst.resource : NULL);"],
    "ps5_generate_mipmap": ["ps5_draw_batch_drain_buffer(base);"],
    "ps5_destroy_query": ["ps5_draw_batch_drain_query(query);"],
    "ps5_begin_query": ["ps5_draw_batch_drain_query(query);"],
    "ps5_render_condition": ["ps5_draw_batch_drain_query(query);"],
    "ps5_clear": ["ps5_draw_batch_drain_query(context->render_condition_query);", "ps5_draw_batch_drain_buffer(resource ? &resource->base : NULL);", "ps5_draw_batch_drain_buffer(context->framebuffer.cbufs[i].texture);"],
    "ps5_transfer_map": ["ps5_draw_batch_drain_buffer(base);"],
    "ps5_transfer_flush_region": ["ps5_draw_batch_drain_buffer(transfer ? transfer->resource : NULL);"],
    "ps5_transfer_unmap": ["ps5_draw_batch_drain_buffer(transfer->resource);"],
    "ps5_buffer_subdata": ["ps5_draw_batch_drain_buffer(resource);"],
}.items():
    start = source.index("\n" + name + "(")
    function = source[start:source.index("\n}\n", start)]
    # Mip generation also retires its GPU blit before CPU tail filtering;
    # test_generate_mipmap.py exercises that dependency with pending output.
    for call in calls: assert function.count(call) == (2 if name == "ps5_generate_mipmap" else 1), (name, call)
    assert "ps5_draw_batch_drain();" not in function, name
# End-query does not wait; the queued-query lifetime cases above verify collection.
start = source.index("\nps5_end_query(")
assert "ps5_draw_batch_drain" not in source[start:source.index("\n}\n", start)]
start = source.index("\nps5_flush(")
function = source[start:source.index("\n}\n", start)]
assert "fence->sequence = ps5_draw_batch_fence_submit();" in function
start = source.index("\nps5_screen_submit_lock(")
assert "ps5_draw_batch_flush_locked();" in source[start:source.index("\n}\n", start)]
print(f"PASS: deferred 1..{capacity} snapshots and {2 * capacity + 3} cross-boundary draws, resource pins, owner/fallback drains, OOM/no replay and fail-stop")
