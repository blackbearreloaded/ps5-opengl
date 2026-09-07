#!/usr/bin/env python3
"""Compile the actual batch retirement code with deterministic failure injection."""
from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile


def audit(text, require_postchecks=False, require_textures=False):
    """Pixel success alone cannot prove that the optimized path ran."""
    matches = list(re.finditer(r"\[ps5-multidraw\] mode=(\d+) serial_ns=(\d+) batch_ns=(\d+) pixels=6912 PASS", text))
    assert len(matches) == text.count("[ps5-multidraw] mode=") == 4, "Missing/duplicate mode results"
    result, start = [], 0
    for mode, match in enumerate(matches):
        actual_mode, serial, batch = map(int, match.groups())
        assert actual_mode == mode and serial > 0 and batch > 0
        section = text[start:match.start()]
        chunks = re.findall(r"\[ps5-multidraw-batch\] draws=(\d+) attempted=(\d+) waits=(\d+) result=(\d+)", section)
        assert len(chunks) == section.count("[ps5-multidraw-batch]") == 2, "Two native chunks required per mode"
        assert sum(int(c[0]) for c in chunks) == 10, "Every nonzero subdraw must be submitted"
        for draws, attempted, waits, status in chunks:
            assert 0 < int(draws) <= 8 and draws == attempted and int(waits) < 2000 and status == "0"
        assert len(re.findall(r"\[ps5-gallium\] multi-draw-batched draws=(?:10|11) result=0", section)) == 1
        assert section.count("multi-draw-batched") == 1
        result.append({"mode": mode, "serial_ms": serial / 1e6, "batch_ms": batch / 1e6,
                       "single_sample_ratio": serial / batch})
        start = match.end()
    assert text.count("[ps5-multidraw-batch]") == 8 and text.count("multi-draw-batched") == 4
    # The original four-mode receipt is retained; the successor adds postchecks.
    if require_postchecks or "[ps5-multidraw] query_samples=" in text:
        assert text.count("[ps5-multidraw] query_samples=2560 fence=1 orphan=1 pixels=4608 PASS") == 1
        assert text.count("[ps5-multidraw] query_samples=") == 1
    if require_textures or "[ps5-multidraw-texture]" in text:
        assert text.count("[ps5-multidraw-texture]") == 2
        assert text.count("[ps5-multidraw-texture] upload-after-batch=1 units=0,7") == 1
        assert text.count("[ps5-multidraw-texture] sampled=2 uploads=1 pixels=32256 PASS") == 1
    for marker in ("[ps5-multidraw] completed=4 cleanup=1 result=0",
                   "[pss-opengl-native] gate completed status=0"):
        assert text.count(marker) == 1, "Missing/duplicate completion"
    return result


def audit_deferred(text, control=False, require_uploads=False):
    """Require real coalescing, complete pixels/hazards and inclusive wait timing."""
    matches = list(re.finditer(r"\[ps5-multidraw\] mode=(\d+) serial_ns=(\d+) batch_ns=(\d+) pixels=6912 PASS", text))
    assert len(matches) == text.count("[ps5-multidraw] mode=") == 4
    start, result = 0, []
    for mode, match in enumerate(matches):
        actual, serial, batched = map(int, match.groups())
        assert actual == mode and serial > 0 and batched > 0
        chunks = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text[start:match.start()])
        assert list(map(int, chunks)) == ([] if control else [1] * 11 + [8, 2, 1]), chunks
        result.append({"mode": mode, "serial_ms": serial / 1e6,
                       "group_ms": batched / 1e6, "single_sample_ratio": serial / batched})
        start = match.end()
    chunks = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text)
    tail = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text[start:])
    assert list(map(int, tail)) == ([] if control else [8, 2] * 4 + [1]), tail
    native = re.findall(r"\[ps5-multidraw-batch\] draws=(\d+) attempted=(\d+) waits=(\d+) result=(\d+)", text)
    assert len(chunks) == text.count("[ps5-deferred-batch]") == len(native) == text.count("[ps5-multidraw-batch]")
    assert not control or not chunks
    for count, (draws, attempted, waits, status) in zip(chunks, native):
        assert count == draws == attempted and 0 < int(count) <= 8 and int(waits) < 2000 and status == "0"
    for marker in (
        "[ps5-deferred] state=uniform,scissor texture-upload=1 buffer-subdata=1 map-write=1 pending-fence=1 pixels=9216 PASS",
        "[ps5-multidraw] query_samples=2560 fence=1 orphan=1 pixels=4608 PASS",
        "[ps5-multidraw] completed=4 cleanup=1 result=0", "[pss-opengl-native] gate completed status=0",
    ):
        assert text.count(marker) == 1, marker
    if require_uploads or "[ps5-deferred] unrelated-buffer" in text:
        assert text.count("[ps5-deferred] unrelated-buffer subdata=1 map=1 explicit-flush=1 unmap=1 read=1 PASS") == 1
        assert text.count("[ps5-deferred] unrelated-buffer") == 1
    return result


if len(sys.argv) > 1:
    assert len(sys.argv) == 2 or (len(sys.argv) == 3 and sys.argv[2] in
        ("--postchecks", "--textures", "--deferred", "--deferred-control", "--deferred-uploads"))
    receipt = Path(sys.argv[1]).read_text()
    result = audit_deferred(receipt, sys.argv[2] == "--deferred-control", sys.argv[2] == "--deferred-uploads") \
        if len(sys.argv) == 3 and sys.argv[2].startswith("--deferred") else \
        audit(receipt, len(sys.argv) == 3, "--textures" in sys.argv)
    print(json.dumps(result, indent=2))
    raise SystemExit
assert len(sys.argv) == 1
sample = "".join(
    "[ps5-multidraw-batch] draws=7 attempted=7 waits=1 result=0\n"
    "[ps5-multidraw-batch] draws=3 attempted=3 waits=1 result=0\n"
    "[ps5-gallium] multi-draw-batched draws=11 result=0\n"
    f"[ps5-multidraw] mode={mode} serial_ns=2000000 batch_ns=1000000 pixels=6912 PASS\n"
    for mode in range(4))
sample += "[ps5-multidraw] completed=4 cleanup=1 result=0\n[pss-opengl-native] gate completed status=0\n"
assert len(audit(sample)) == 4
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

def batch_receipt(count):
    return f"[ps5-multidraw-batch] draws={count} attempted={count} waits=1 result=0\n" \
           f"[ps5-deferred-batch] draws={count} result=0\n"


for control in (False, True):
    deferred_sample = "".join(
        ("" if control else "".join(batch_receipt(n) for n in [1] * 11 + [8, 2, 1])) +
        f"[ps5-multidraw] mode={mode} serial_ns=2000000 batch_ns=1000000 pixels=6912 PASS\n"
        for mode in range(4))
    if not control:
        deferred_sample += "".join(batch_receipt(n) for n in [8, 2] * 4 + [1])
    deferred_sample += "[ps5-deferred] state=uniform,scissor texture-upload=1 buffer-subdata=1 map-write=1 pending-fence=1 pixels=9216 PASS\n" \
        "[ps5-multidraw] query_samples=2560 fence=1 orphan=1 pixels=4608 PASS\n" \
        "[ps5-multidraw] completed=4 cleanup=1 result=0\n[pss-opengl-native] gate completed status=0\n"
    assert len(audit_deferred(deferred_sample, control)) == 4
    upload_marker = "[ps5-deferred] unrelated-buffer subdata=1 map=1 explicit-flush=1 unmap=1 read=1 PASS\n"
    assert len(audit_deferred(deferred_sample + upload_marker, control, True)) == 4
    for bad in (deferred_sample, deferred_sample + upload_marker * 2,
                deferred_sample + upload_marker.replace("unmap=1", "unmap=0")):
        try:
            audit_deferred(bad, control, True)
        except AssertionError:
            continue
        raise AssertionError("Missing or failed unrelated-upload oracle accepted")
    for bad in (deferred_sample.replace("map-write=1", "map-write=0"),
                deferred_sample.replace("cleanup=1", "cleanup=0"),
                deferred_sample + batch_receipt(1)):
        try:
            audit_deferred(bad, control)
        except AssertionError:
            continue
        raise AssertionError("Invalid deferred receipt accepted")
print("PASS: ordinary-draw receipt audit requires matching native chunks and every hazard oracle")

root = Path(__file__).resolve().parents[2]
source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.index("static void runtime_require_retirement(")
guard = source[start:source.index("\n}\n", start) + 3]
start = source.index("static struct runtime_batch_entry {")
body = source[start:source.index("\n#endif\n\n#ifdef PS5_DRAW_PROFILE", start)]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include "ps5_screen.h"
typedef struct { void *words; uint32_t word_count; uint8_t flag, padding[3]; } agc_submit_description_t;
typedef struct { int (*submit)(void *); int (*suspend_point)(void); } agc_api_t;
static uint32_t markers[PS5_MULTIDRAW_BATCH_CAPACITY];
static uint8_t memory[PS5_MULTIDRAW_BATCH_CAPACITY][64];
static unsigned submits, suspends, sleeps, unmaps, releases, delay;
#ifdef PS5_DRAW_PROFILE
static unsigned runtime_present_count = 30;
static int64_t os_time_get_nano(void) { static int64_t ticks = 1; return ticks++; }
static void runtime_batch_profile_record(const int64_t ticks[5], unsigned waits, int result) {
    assert(waits == sleeps);
    if (!result) {
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
static void flush_gpu_data(const void *p, size_t n) { assert(p && n == 4); }
static int sceKernelUsleep(uint32_t us) {
    assert(us == 1000 && !unmaps && !releases);
    if (++sleeps >= delay)
        for (unsigned i = 0; i < submits; ++i) markers[i] = wrong_marker ? 100 : 101 + i;
    return 0;
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
    memset(markers, 0, sizeof(markers));
    runtime_batch_count = runtime_batch_active = runtime_batch_faulted = 0;
    submits = suspends = sleeps = unmaps = releases = delay = 0;
    fail_submit = -1; fail_suspend = fail_unmap = wrong_marker = 0;
}
static void queue(unsigned n) {
    const agc_api_t api = {submit, suspend_point};
    assert(ps5_agc_gate2_batch_begin() == 0);
    assert(ps5_agc_gate2_batch_begin() != 0);
    for (unsigned i = 0; i < n; ++i) {
        agc_submit_description_t d = {memory[i], 1, 0, {0}};
        assert(runtime_batch_queue(&api, &d, memory[i], i * 64, 64, &markers[i], 101 + i) == 0);
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
    assert(runtime_batch_queue(&api, &d, memory[0], 0, 64, &markers[0], 101) != 0);
    assert(ps5_agc_gate2_batch_end() == 0);
    for (int failure = 0; failure < 5; ++failure) {
        reset(); queue(8);
        if (failure == 0) fail_submit = 0;
        if (failure == 1) fail_submit = 3;
        if (failure == 2) fail_suspend = -1;
        if (failure == 3) { delay = 1; wrong_marker = 1; }
        if (failure == 4) fail_unmap = -1;
        int stopped = setjmp(exit_jump);
        if (!stopped) {
            assert(ps5_agc_gate2_batch_end() != 0);
            assert(failure == 4); /* Only a post-retirement unmap error returns. */
        } else {
            assert(failure < 4);
        }
        assert(!releases && (runtime_batch_faulted != 0) == (failure == 4));
        assert(unmaps == (failure == 4 ? 1u : 0u));
        assert(submits == (failure == 0 ? 1u : failure == 1 ? 4u : 8u));
        assert(suspends == 1 && sleeps <= 2000);
        if (failure == 3) assert(sleeps == 2000); /* One shared timeout, not eight. */
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
print("PASS: staged ownership, 1..8 draws, all-marker retirement, shared timeout, fail-stop before cleanup")

# Exercise the real Gallium wrapper too: ownership must survive command staging.
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()
cache_start = source.index("struct ps5_depth_flush_cache {")
depth_cache_type = source[cache_start:source.index("\n};", cache_start) + 3]
start = source.index("static bool\nps5_multidraw_eligible(")
body = source[start:source.index("\n#endif", start)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
#include "ps5_screen.h"
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define PS5_RENDER_ARENA_OFFSET (2u * 0xa00000u)
enum { PIPE_MAX_ATTRIBS=16, PS5_MAX_CONSTANT_BUFFERS=13, PS5_MAX_TEXTURE_UNITS=16, PIPE_BUFFER=1,
       PIPE_TEXTURE_2D=2, PIPE_FORMAT_R8G8B8A8_UNORM=1, MESA_PRIM_TRIANGLES=4, MESA_PRIM_TRIANGLE_FAN=5, PIPE_BIND_DISPLAY_TARGET=1,
       PIPE_FORMAT_Z32_FLOAT=77, PIPE_FORMAT_Z32_FLOAT_S8X24_UINT=78 };
struct pipe_resource { unsigned target, format, nr_samples, nr_storage_samples, refs, last_level, bind; };
struct pipe_sampler_view { struct pipe_resource *texture; unsigned target, format;
    union { struct { unsigned first_level, last_level, first_layer, last_layer; } tex; } u; };
struct ps5_resource { struct pipe_resource base; unsigned render_staging_size, depth_staging_size;
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
struct ps5_context {
    struct pipe_context base;
    struct { bool running; } *blitter;
    bool deferred_color_clear;
    struct { struct pipe_surface cbufs[1], zsbuf; unsigned nr_cbufs; } framebuffer;
    bool framebuffer_valid; unsigned *vs, *fs, *gs;
    unsigned stream_output_target_count, render_condition_query, active_occlusion_query,
        active_primitives_generated_query, active_primitives_emitted_query, vertex_buffer_count;
    struct { bool is_user_buffer; struct { struct pipe_resource *resource; } buffer; } vertex_buffers[PIPE_MAX_ATTRIBS];
    struct { struct pipe_resource *buffer; } constants[2][PS5_MAX_CONSTANT_BUFFERS];
    struct pipe_resource *vertex_descriptor_table, *descriptor_storage[2], *border_color_storage;
    struct pipe_sampler_view *sampler_views[2][PS5_MAX_TEXTURE_UNITS];
    const struct pipe_depth_stencil_alpha_state *depth_stencil_alpha;
    int last_draw_status;
};
static struct ps5_resource original[3], copies[128], borrowed, depth_buffer;
static uint8_t original_bytes[3][64], copy_bytes[128][64];
static uint8_t borrowed_bytes[64], depth_bytes[64];
static struct ps5_context context;
static struct ps5_screen screen;
static unsigned shader_textures, allocated, freed, begun, ended, staged, calls, locked;
static int fail_alloc, fail_begin, fail_end, fail_draw;
static struct ps5_resource *pending[8][3];
static unsigned expected_start[8], expected_id[8];
static bool deferred_mode;
static unsigned retained_draws;
static unsigned unindexed_draws;
static uint8_t expected_uniform[8][3];
static unsigned ps5_shader_texture_count(const unsigned *s) { return *s; }
static bool ps5_texture_used(const struct ps5_context *c, const unsigned *s, const void *metadata, unsigned unit) {
    (void)c; (void)metadata; return (*s & (1u << unit)) != 0;
}
static bool ps5_linear_sampled_layout(const struct pipe_resource *r) { return !(r->bind & 2); }
static unsigned fragment_textures;
static struct ps5_resource textures[PS5_MAX_TEXTURE_UNITS];
static struct pipe_sampler_view views[PS5_MAX_TEXTURE_UNITS];
static uint8_t texels[PS5_MAX_TEXTURE_UNITS][64];
static struct pipe_resource *create(struct pipe_screen *s, const struct pipe_resource *r) {
    assert(s == &screen.base && r->target == PIPE_BUFFER && (deferred_mode || !locked));
    if ((int)allocated == fail_alloc) return NULL;
    unsigned i = allocated++;
    assert(i < 128);
    copies[i] = (struct ps5_resource){.base=*r, .data=copy_bytes[i], .size=64, .allocation_size=64};
    copies[i].base.refs=1;
    return &copies[i].base;
}
static void pipe_resource_reference(struct pipe_resource **dst, struct pipe_resource *src) {
    if (src) ++src->refs;
    if (*dst) { assert((*dst)->refs && !staged); if (!--(*dst)->refs) ++freed; }
    *dst=src;
}
void ps5_screen_submit_lock(struct pipe_screen *s) { assert(s == &screen.base && !locked); locked=1; }
void ps5_screen_submit_unlock(struct pipe_screen *s) { assert(s == &screen.base && locked); locked=0; }
static int begin(void) { assert(locked && !staged); ++begun; return fail_begin; }
static int end(void) {
    assert(locked && context.vertex_descriptor_table == &original[0].base &&
        context.descriptor_storage[0] == &original[1].base && context.descriptor_storage[1] == &original[2].base);
    ++ended;
    for (unsigned i=0; i<staged; ++i) {
        for (unsigned stage=0; stage<3; ++stage) {
            assert(pending[i][stage]->base.refs == 1);
            assert(pending[i][stage]->data[0] == expected_start[i]);
            assert(pending[i][stage]->data[1] == expected_id[i]);
            for (unsigned byte=16; byte<64; ++byte)
                assert(pending[i][stage]->data[byte] == expected_uniform[i][stage]);
        }
    }
    unsigned factor = deferred_mode ? retained_draws : 1;
    assert(borrowed.base.refs == 1+factor*(4+PIPE_MAX_ATTRIBS+2*PS5_MAX_CONSTANT_BUFFERS+
        (context.framebuffer.zsbuf.texture == &borrowed.base))-unindexed_draws);
    if (context.framebuffer.zsbuf.texture == &depth_buffer.base)
        assert(depth_buffer.base.refs == 1+factor);
    for (unsigned unit=0; unit<PS5_MAX_TEXTURE_UNITS; ++unit)
        if (fragment_textures & (1u << unit)) assert(textures[unit].base.refs == 1+factor);
    if (fail_end) return -1;
    staged=retained_draws=unindexed_draws=0;
    return 0;
}
static int (*ps5_agc_gate2_batch_begin)(void)=begin;
static int (*ps5_agc_gate2_batch_end)(void)=end;
''' + depth_cache_type + r'''
static void ps5_draw_vbo_locked(struct pipe_context *b, const struct pipe_draw_info *info, unsigned id,
    const struct pipe_draw_indirect_info *indirect, const struct pipe_draw_start_count_bias *draw, unsigned n,
    struct ps5_depth_flush_cache *depth_cache) {
    struct ps5_context *drawing=(struct ps5_context *)b;
    assert((b == &context.base || deferred_mode) && info && !indirect && n==1 && draw->count && locked);
    assert(id == 20 + (info->increment_draw_id ? draw->start : 0));
    ++retained_draws;
    if (!info->index_size) ++unindexed_draws;
    if ((int)calls++ == fail_draw) { drawing->last_draw_status=-9; return; }
    assert(staged < 8);
    /* Model a backing flush: cache ownership ends at EVERY batch boundary. */
    assert(depth_cache);
    if (!staged) assert(!depth_cache->data[0] && !depth_cache->data[1] &&
                        !depth_cache->size[0] && !depth_cache->size[1]);
    else assert(depth_cache->data[0] == &borrowed && depth_cache->size[0] == begun);
    depth_cache->data[0] = &borrowed;
    depth_cache->size[0] = begun;
    pending[staged][0]=(struct ps5_resource *)drawing->vertex_descriptor_table;
    pending[staged][1]=(struct ps5_resource *)drawing->descriptor_storage[0];
    pending[staged][2]=(struct ps5_resource *)drawing->descriptor_storage[1];
    for (unsigned stage=0; stage<3; ++stage) {
        assert(pending[staged][stage] != &original[stage]);
        pending[staged][stage]->data[0]=draw->start;
        pending[staged][stage]->data[1]=id;
        expected_uniform[staged][stage]=pending[staged][stage]->data[16];
        for (unsigned i=0; i<staged; ++i) assert(pending[i][stage] != pending[staged][stage]);
    }
    expected_start[staged]=draw->start; expected_id[staged++]=id;
}
''' + body + r'''
static void reset(void) {
    allocated=freed=begun=ended=staged=calls=locked=shader_textures=fragment_textures=retained_draws=unindexed_draws=0;
    fail_alloc=fail_draw=-1; fail_begin=fail_end=0;
    borrowed=(struct ps5_resource){.base={.target=PIPE_TEXTURE_2D, .format=1, .refs=1},
        .data=borrowed_bytes, .size=64, .allocation_size=64};
    depth_buffer=(struct ps5_resource){.base={.target=PIPE_TEXTURE_2D, .format=PIPE_FORMAT_Z32_FLOAT, .refs=1},
        .data=depth_bytes, .size=64, .allocation_size=64};
    screen=(struct ps5_screen){.base={create}, .render_pool=&borrowed.base};
    context=(struct ps5_context){.base={&screen.base}, .framebuffer={.cbufs={{.texture=&borrowed.base, .format=1}},
        .nr_cbufs=1}, .framebuffer_valid=true, .vs=&shader_textures, .fs=&shader_textures,
        .vertex_buffer_count=PIPE_MAX_ATTRIBS, .border_color_storage=&borrowed.base};
    for (unsigned i=0; i<3; ++i) {
        memset(original_bytes[i], 0xa0+i, 64);
        original[i]=(struct ps5_resource){.base={.target=PIPE_BUFFER, .refs=1}, .data=original_bytes[i], .size=64, .allocation_size=64};
    }
    context.vertex_descriptor_table=&original[0].base;
    context.descriptor_storage[0]=&original[1].base; context.descriptor_storage[1]=&original[2].base;
    for (unsigned i=0; i<PIPE_MAX_ATTRIBS; ++i) context.vertex_buffers[i].buffer.resource=&borrowed.base;
    for (unsigned s=0; s<2; ++s) for (unsigned i=0; i<PS5_MAX_CONSTANT_BUFFERS; ++i)
        context.constants[s][i].buffer=&borrowed.base;
    for (unsigned unit=0; unit<PS5_MAX_TEXTURE_UNITS; ++unit) {
        textures[unit]=(struct ps5_resource){.base={.target=PIPE_TEXTURE_2D, .format=1, .refs=1},
            .data=texels[unit], .size=64, .allocation_size=64, .render_staging_size=64};
        views[unit]=(struct pipe_sampler_view){.texture=&textures[unit].base, .target=PIPE_TEXTURE_2D, .format=1};
    }
}
int main(void) {
    struct pipe_draw_info info={.mode=4, .instance_count=1, .index_size=2, .index={&borrowed.base}};
    struct pipe_draw_start_count_bias draws[19];
    for (unsigned i=0; i<19; ++i) draws[i]=(struct pipe_draw_start_count_bias){i, i==3 ? 0 : 6, 0};
    for (unsigned increment=0; increment<2; ++increment) {
        reset(); info.increment_draw_id=increment;
        assert(ps5_try_multi_draw_batch(&context.base, &info, 20, NULL, draws, 19));
        assert(allocated==24 && freed==24 && begun==3 && ended==3 && calls==18 && !locked);
        assert(borrowed.base.refs==1 && !context.last_draw_status);
        for (unsigned s=0; s<3; ++s) for (unsigned byte=0; byte<64; ++byte)
            assert(original_bytes[s][byte]==0xa0+s);
    }
    for (int i=0; i<24; ++i) {
        reset(); fail_alloc=i;
        assert(!ps5_try_multi_draw_batch(&context.base, &info, 20, NULL, draws, 19));
        assert(freed==allocated && !begun && borrowed.base.refs==1);
    }
    for (int failure=0; failure<3; ++failure) {
        reset(); fail_begin=failure==0 ? -1:0; fail_draw=failure==1 ? 4:-1; fail_end=failure==2;
        assert(ps5_try_multi_draw_batch(&context.base, &info, 20, NULL, draws, 19));
        assert(context.last_draw_status && !locked && begun==1);
        if (failure==1) assert(calls==5 && ended==1); /* No replay after partial preparation. */
        assert(freed==(failure==2 ? 0:24));
        assert((borrowed.base.refs==1)==(failure!=2));
    }
    reset();
#define REJECT(field,value) do { struct ps5_context c=context; c.field=value; \
    assert(!ps5_multidraw_eligible(&c,&info,NULL,draws,19)); } while(0)
    REJECT(gs,&shader_textures); REJECT(active_occlusion_query,1); REJECT(render_condition_query,1);
    REJECT(active_primitives_generated_query,1); REJECT(active_primitives_emitted_query,1);
    REJECT(stream_output_target_count,1); REJECT(framebuffer.zsbuf.texture,&borrowed.base);
    REJECT(framebuffer.cbufs[0].format,2); REJECT(framebuffer.cbufs[0].level,1);
    REJECT(framebuffer.cbufs[0].first_layer,1); REJECT(framebuffer.cbufs[0].last_layer,1);
    REJECT(framebuffer.nr_cbufs,2); REJECT(framebuffer_valid,false);
    REJECT(vertex_buffers[0].is_user_buffer,true); REJECT(vertex_buffer_count,PIPE_MAX_ATTRIBS+1);
    shader_textures=1; assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,19));
    reset();
    struct pipe_depth_stencil_alpha_state dsa={0};
    for (unsigned format=77; format<=78; ++format) for (unsigned enabled=0; enabled<2; ++enabled) {
        reset(); dsa.depth_enabled=enabled;
        context.depth_stencil_alpha=&dsa;
        depth_buffer.base.format=format;
        context.framebuffer.zsbuf=(struct pipe_surface){.texture=&depth_buffer.base, .format=format};
        assert(ps5_try_multi_draw_batch(&context.base,&info,20,NULL,draws,19));
        assert(borrowed.base.refs==1 && depth_buffer.base.refs==1 && freed==24 && !context.last_draw_status);
    }
    for (unsigned face=0; face<2; ++face) {
        dsa.stencil[face].enabled=true; assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,19));
        dsa.stencil[face].enabled=false;
    }
#define REJECT_DEPTH(field,value) do { struct ps5_resource saved=depth_buffer; depth_buffer.field=value; \
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,19)); depth_buffer=saved; } while(0)
    REJECT_DEPTH(depth_staging_size,1); REJECT_DEPTH(base.target,PIPE_BUFFER);
    REJECT_DEPTH(base.format,1); REJECT_DEPTH(base.nr_samples,4); REJECT_DEPTH(base.nr_storage_samples,4);
    REJECT(framebuffer.zsbuf.format,1); REJECT(framebuffer.zsbuf.level,1);
    REJECT(framebuffer.zsbuf.first_layer,1); REJECT(framebuffer.zsbuf.last_layer,1);
    for (unsigned failure=0; failure<2; ++failure) {
        reset(); fragment_textures=0xffff; context.fs=&fragment_textures; fail_end=failure;
        for (unsigned unit=0; unit<PS5_MAX_TEXTURE_UNITS; ++unit) context.sampler_views[1][unit]=&views[unit];
        assert(ps5_try_multi_draw_batch(&context.base,&info,20,NULL,draws,19));
        for (unsigned unit=0; unit<PS5_MAX_TEXTURE_UNITS; ++unit) assert(textures[unit].base.refs == 1+failure);
    }
    reset(); context.fs=&fragment_textures; fragment_textures=1u << 7;
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,19)); /* Used sparse unit missing. */
    context.sampler_views[1][7]=&views[7];
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,19));
    REJECT(sampler_views[1][7],NULL);
#define REJECT_TEX(field,value) do { struct ps5_resource saved=textures[7]; textures[7].field=value; \
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,19)); textures[7]=saved; } while(0)
    REJECT_TEX(base.target,PIPE_BUFFER); REJECT_TEX(base.format,2); REJECT_TEX(base.nr_samples,4);
    REJECT_TEX(base.nr_storage_samples,4); REJECT_TEX(base.last_level,1); REJECT_TEX(base.bind,1);
    REJECT_TEX(base.bind,2); REJECT_TEX(depth_staging_size,1); REJECT_TEX(data,NULL); REJECT_TEX(size,0);
#define REJECT_VIEW(field,value) do { struct pipe_sampler_view saved=views[7]; views[7].field=value; \
    assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,19)); views[7]=saved; } while(0)
    REJECT_VIEW(texture,&borrowed.base); REJECT_VIEW(texture,NULL); REJECT_VIEW(target,PIPE_BUFFER);
    REJECT_VIEW(format,2); REJECT_VIEW(u.tex.first_level,1); REJECT_VIEW(u.tex.last_level,1);
    REJECT_VIEW(u.tex.first_layer,1); REJECT_VIEW(u.tex.last_layer,1);
    views[0].format=2; context.sampler_views[1][0]=&views[0]; /* Unused state cannot veto a batch. */
    assert(ps5_multidraw_eligible(&context,&info,NULL,draws,19));
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "ownership"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                   input=code, text=True, check=True)
    subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
    mutations = (
        ("   for (unsigned first = 0; first < num_draws;) {\n      struct ps5_depth_flush_cache depth_cache = {0};",
         "   struct ps5_depth_flush_cache depth_cache = {0};\n   for (unsigned first = 0; first < num_draws;) {"),
        ("ps5_shader_texture_count(context->vs) ||",
         "ps5_shader_texture_count(context->vs) || ps5_shader_texture_count(context->fs) ||"),
        ("pipe_resource_reference(&retained[retained_count++], context->sampler_views[1][unit]->texture);",
         "(void)0;"),
        ("pipe_resource_reference(&retained[retained_count++], context->framebuffer.zsbuf.texture);",
         "(void)0;"),
    )
    for before, after in mutations:
        assert code.count(before) == 1
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(root / "src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                       input=code.replace(before, after), text=True, check=True)
        failed = subprocess.run([str(exe)], cwd=tmp, text=True, capture_output=True)
        assert failed.returncode != 0 and "Assertion" in failed.stderr
print("PASS: descriptors/uniforms, chunk retirement, draw IDs, rollback, all 16 fragment texture refs and narrow eligibility")

# Reuse the same Gallium mocks, but exercise the actual cross-call queue too.
start = source.index("static struct {\n   struct ps5_context *owner;")
deferred = source[start:source.index("\n#endif\n\nstatic void\nps5_draw_vbo(", start)]
deferred_code = code[:code.index("int main(void) {")] + r'''
static unsigned ps5_deferred_mutex;
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
    context.deferred_color_clear=true;
    assert(!ps5_try_deferred_draw(&context.base,&fan,20,NULL,&quad,1));
    __typeof__(*context.blitter) blitter={.running=true};
    context.blitter=&blitter;
    for (unsigned invalid=0;invalid<7;++invalid) {
        struct pipe_draw_info f=fan;
        struct pipe_draw_start_count_bias q=quad;
        if (invalid==0) context.deferred_color_clear=false;
        if (invalid==1) f.index_size=2;
        if (invalid==2) f.instance_count=2;
        if (invalid==3) f.start_instance=1;
        if (invalid==4) q.count=3;
        if (invalid==5) q.start=1;
        if (invalid==6) blitter.running=false;
        assert(!ps5_try_deferred_draw(&context.base,&f,20,NULL,&q,1));
        context.deferred_color_clear=true; blitter.running=true;
    }
    assert(ps5_try_deferred_draw(&context.base,&fan,20,NULL,&quad,1));
    context.deferred_color_clear=false; blitter.running=false;
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
    assert(!ps5_memory_overlaps((void *)100, 10, (void *)110, 10));
    assert(!ps5_memory_overlaps((void *)110, 10, (void *)100, 10));
    assert(ps5_memory_overlaps((void *)100, 10, (void *)109, 10));
    assert(ps5_memory_overlaps((void *)109, 10, (void *)100, 10));
    assert(ps5_memory_overlaps((void *)100, 0, (void *)110, 10));
    assert(ps5_memory_overlaps(NULL, 10, (void *)110, 10));
    assert(ps5_memory_overlaps((void *)(UINTPTR_MAX-1), 4, (void *)100, 10));
    for (unsigned kind=0; kind<10; ++kind) {
        reset();
        uint8_t unrelated[64];
        struct ps5_resource cpu={.base={.target=PIPE_BUFFER}, .data=unrelated, .allocation_size=64};
        assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        ps5_draw_batch_drain_buffer(&cpu.base);
        assert(!locked && !ended && staged==1); /* Unrelated uploads leave the batch queued. */
        if (kind<3) cpu.data=copies[kind].data+1; /* Each private descriptor allocation. */
        if (kind==3) cpu.data=borrowed.data+63; /* Distinct object, last-byte overlap. */
        if (kind==4) cpu.base.target=PIPE_TEXTURE_2D; /* Textures remain conservative. */
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
    for (unsigned depth=0; depth<2; ++depth) for (unsigned n=1;n<=8;++n) {
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
            assert(ended==(i==7)); /* CPU staging, not a hidden wait per draw. */
            for (unsigned s=0;s<3;++s) assert(original_bytes[s][0]==0x40+i+s);
        }
        drain(); idle(); assert(ended==1 && calls==n);
    }
    reset(); fragment_textures=0xffff; context.fs=&fragment_textures;
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
    for (unsigned boundary=0;boundary<3;++boundary) {
        reset();
        assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        if (!boundary) context.active_occlusion_query=1;
        if (boundary==1) context.gs=&shader_textures;
        assert(!ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,boundary==2?2:1));
        idle(); assert(ended==1 && calls==1);
    }
    for (int fail=0;fail<3;++fail) {
        reset();
        assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        fail_alloc=3+fail;
        assert(!ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
        idle(); assert(ended==1 && calls==1); /* No staged draw replay on OOM. */
    }
    reset(); fail_begin=-1;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    idle(); assert(context.last_draw_status==-30 && !calls && !ended);
    reset();
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    fail_draw=1;
    assert(ps5_try_deferred_draw(&context.base,&info,20,NULL,&draw,1));
    idle(); assert(context.last_draw_status==-9 && calls==2 && ended==1);
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
            candidate = candidate.replace("   memset(&ps5_deferred, 0, sizeof(ps5_deferred));",
                "   struct ps5_depth_flush_cache stale = ps5_deferred.depth_cache;\n"
                "   memset(&ps5_deferred, 0, sizeof(ps5_deferred));\n"
                "   ps5_deferred.depth_cache = stale;")
            assert candidate != deferred_code
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", *flags,
                        "-I" + str(root / "src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                       input=candidate, text=True, check=True)
        run = subprocess.run([str(exe)], cwd=tmp, capture_output=True, text=True)
        assert (run.returncode == 0) == (mutate == 0), run.stderr

# The queue test alone cannot prove that CPU access / lifecycle entry points drain.
for name in ("ps5_resource_info", "ps5_resource_stencil_info", "ps5_blit",
             "ps5_generate_mipmap", "ps5_get_timestamp", "ps5_begin_query", "ps5_end_query",
             "ps5_flush", "ps5_clear", "ps5_context_last_draw_status",
             "ps5_context_destroy", "ps5_screen_destroy"):
    start = source.index("\n" + name + "(")
    function = source[start:source.index("\n}\n", start)]
    assert function.count("ps5_draw_batch_drain();") == 1, name
for name, argument in (("ps5_transfer_map", "base"),
                       ("ps5_transfer_flush_region", "transfer ? transfer->resource : NULL"),
                       ("ps5_transfer_unmap", "transfer->resource"),
                       ("ps5_buffer_subdata", "resource")):
    start = source.index("\n" + name + "(")
    function = source[start:source.index("\n}\n", start)]
    assert function.count("ps5_draw_batch_drain_buffer(" + argument + ");") == 1, name
    assert "ps5_draw_batch_drain();" not in function, name
start = source.index("\nps5_flush(")
function = source[start:source.index("\n}\n", start)]
assert function.index("ps5_draw_batch_drain();") < function.index("if (!out_fence)")
start = source.index("\nps5_screen_submit_lock(")
assert "ps5_draw_batch_flush_locked();" in source[start:source.index("\n}\n", start)]
print("PASS: deferred 1..8 draw snapshots, resource pins, owner/fallback drains, OOM/no replay, fail-stop and boundary wiring")
