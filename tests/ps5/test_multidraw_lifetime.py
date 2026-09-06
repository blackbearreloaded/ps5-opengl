#!/usr/bin/env python3
"""Compile the actual batch retirement code with deterministic failure injection."""
from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile


def audit(text, require_postchecks=False):
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
    for marker in ("[ps5-multidraw] completed=4 cleanup=1 result=0",
                   "[pss-opengl-native] gate completed status=0"):
        assert text.count(marker) == 1, "Missing/duplicate completion"
    return result


if len(sys.argv) > 1:
    assert len(sys.argv) == 2 or (len(sys.argv) == 3 and sys.argv[2] == "--postchecks")
    print(json.dumps(audit(Path(sys.argv[1]).read_text(), len(sys.argv) == 3), indent=2))
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
try:
    audit(sample, True)
except AssertionError:
    pass
else:
    raise AssertionError("Missing required postchecks accepted")
for bad in (sample.replace("[ps5-multidraw-batch]", "[unused]"), sample.replace("attempted=7", "attempted=6", 1),
            sample.replace("waits=1", "waits=2000", 1), sample.replace("result=0", "result=1", 1),
            sample.replace("cleanup=1", "cleanup=0"), sample + "[ps5-multidraw-batch] malformed",
            sample + "[ps5-multidraw] query_samples=2559 fence=1 orphan=1 pixels=4608 PASS"):
    try:
        audit(bad)
    except AssertionError:
        continue
    raise AssertionError("Invalid or unbatched receipt accepted")
print("PASS: receipt audit rejects unbatched, incomplete and failed runs")

root = Path(__file__).resolve().parents[2]
source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.index("static struct runtime_batch_entry {")
body = source[start:source.index("\n#endif", start)]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ps5_screen.h"
typedef struct { void *words; uint32_t word_count; uint8_t flag, padding[3]; } agc_submit_description_t;
typedef struct { int (*submit)(void *); int (*suspend_point)(void); } agc_api_t;
static uint32_t markers[PS5_MULTIDRAW_BATCH_CAPACITY];
static uint8_t memory[PS5_MULTIDRAW_BATCH_CAPACITY][64];
static unsigned submits, suspends, sleeps, unmaps, releases, delay;
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
        assert(ps5_agc_gate2_batch_end() != 0);
        assert(runtime_batch_faulted && !releases);
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
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "src/gallium/ps5"), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
print("PASS: staged ownership, 1..8 draws, all-marker retirement, shared timeout, failure quarantine")

# Exercise the real Gallium wrapper too: ownership must survive command staging.
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()
start = source.index("static bool\nps5_multidraw_eligible(")
body = source[start:source.index("\n#endif", start)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ps5_screen.h"
#define MIN2(a,b) ((a)<(b)?(a):(b))
enum { PIPE_MAX_ATTRIBS=16, PS5_MAX_CONSTANT_BUFFERS=13, PIPE_BUFFER=1,
       PIPE_TEXTURE_2D=2, PIPE_FORMAT_R8G8B8A8_UNORM=1, MESA_PRIM_TRIANGLES=4 };
struct pipe_resource { unsigned target, format, nr_samples, nr_storage_samples, refs; };
struct ps5_resource { struct pipe_resource base; unsigned render_staging_size, depth_staging_size; uint8_t *data; size_t size; };
struct pipe_screen { struct pipe_resource *(*resource_create)(struct pipe_screen *, const struct pipe_resource *); };
struct ps5_screen { struct pipe_screen base; struct pipe_resource *render_pool; };
struct pipe_context { struct pipe_screen *screen; };
struct pipe_surface { struct pipe_resource *texture; unsigned level, first_layer, last_layer, format; };
struct pipe_draw_info { unsigned mode, instance_count, index_size; bool primitive_restart, has_user_indices,
    increment_draw_id; struct { struct pipe_resource *resource; } index; };
struct pipe_draw_indirect_info { int unused; };
struct pipe_draw_start_count_bias { unsigned start, count; int index_bias; };
struct pipe_depth_stencil_alpha_state { bool depth_enabled; struct { bool enabled; } stencil[2]; };
struct ps5_context {
    struct pipe_context base;
    struct { struct pipe_surface cbufs[1], zsbuf; unsigned nr_cbufs; } framebuffer;
    bool framebuffer_valid; unsigned *vs, *fs, *gs;
    unsigned stream_output_target_count, render_condition_query, active_occlusion_query,
        active_primitives_generated_query, active_primitives_emitted_query, vertex_buffer_count;
    struct { bool is_user_buffer; struct { struct pipe_resource *resource; } buffer; } vertex_buffers[PIPE_MAX_ATTRIBS];
    struct { struct pipe_resource *buffer; } constants[2][PS5_MAX_CONSTANT_BUFFERS];
    struct pipe_resource *vertex_descriptor_table, *descriptor_storage[2], *border_color_storage;
    const struct pipe_depth_stencil_alpha_state *depth_stencil_alpha;
    int last_draw_status;
};
static struct ps5_resource original[3], copies[24], borrowed;
static uint8_t original_bytes[3][64], copy_bytes[24][64];
static struct ps5_context context;
static struct ps5_screen screen;
static unsigned shader_textures, allocated, freed, begun, ended, staged, calls, locked;
static int fail_alloc, fail_begin, fail_end, fail_draw;
static struct ps5_resource *pending[8][3];
static unsigned expected_start[8], expected_id[8];
static unsigned ps5_shader_texture_count(const unsigned *s) { return *s; }
static struct pipe_resource *create(struct pipe_screen *s, const struct pipe_resource *r) {
    assert(s == &screen.base && r->target == PIPE_BUFFER && !locked);
    if ((int)allocated == fail_alloc) return NULL;
    unsigned i = allocated++;
    assert(i < 24);
    copies[i] = (struct ps5_resource){.base=*r, .data=copy_bytes[i], .size=64};
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
                assert(pending[i][stage]->data[byte] == 0xa0+stage);
        }
    }
    assert(borrowed.base.refs == 1+4+PIPE_MAX_ATTRIBS+2*PS5_MAX_CONSTANT_BUFFERS+
        !!context.framebuffer.zsbuf.texture);
    if (fail_end) return -1;
    staged=0;
    return 0;
}
static int (*ps5_agc_gate2_batch_begin)(void)=begin;
static int (*ps5_agc_gate2_batch_end)(void)=end;
static void ps5_draw_vbo_locked(struct pipe_context *b, const struct pipe_draw_info *info, unsigned id,
    const struct pipe_draw_indirect_info *indirect, const struct pipe_draw_start_count_bias *draw, unsigned n) {
    assert(b == &context.base && info && !indirect && n==1 && draw->count && locked);
    assert(id == 20 + (info->increment_draw_id ? draw->start : 0));
    if ((int)calls++ == fail_draw) { context.last_draw_status=-9; return; }
    assert(staged < 8);
    pending[staged][0]=(struct ps5_resource *)context.vertex_descriptor_table;
    pending[staged][1]=(struct ps5_resource *)context.descriptor_storage[0];
    pending[staged][2]=(struct ps5_resource *)context.descriptor_storage[1];
    for (unsigned stage=0; stage<3; ++stage) {
        assert(pending[staged][stage] != &original[stage]);
        pending[staged][stage]->data[0]=draw->start;
        pending[staged][stage]->data[1]=id;
        for (unsigned i=0; i<staged; ++i) assert(pending[i][stage] != pending[staged][stage]);
    }
    expected_start[staged]=draw->start; expected_id[staged++]=id;
}
''' + body + r'''
static void reset(void) {
    allocated=freed=begun=ended=staged=calls=locked=shader_textures=0;
    fail_alloc=fail_draw=-1; fail_begin=fail_end=0;
    borrowed=(struct ps5_resource){.base={.target=PIPE_TEXTURE_2D, .format=1, .refs=1}};
    screen=(struct ps5_screen){.base={create}, .render_pool=&borrowed.base};
    context=(struct ps5_context){.base={&screen.base}, .framebuffer={.cbufs={{.texture=&borrowed.base, .format=1}},
        .nr_cbufs=1}, .framebuffer_valid=true, .vs=&shader_textures, .fs=&shader_textures,
        .vertex_buffer_count=PIPE_MAX_ATTRIBS, .border_color_storage=&borrowed.base};
    for (unsigned i=0; i<3; ++i) {
        memset(original_bytes[i], 0xa0+i, 64);
        original[i]=(struct ps5_resource){.base={.target=PIPE_BUFFER, .refs=1}, .data=original_bytes[i], .size=64};
    }
    context.vertex_descriptor_table=&original[0].base;
    context.descriptor_storage[0]=&original[1].base; context.descriptor_storage[1]=&original[2].base;
    for (unsigned i=0; i<PIPE_MAX_ATTRIBS; ++i) context.vertex_buffers[i].buffer.resource=&borrowed.base;
    for (unsigned s=0; s<2; ++s) for (unsigned i=0; i<PS5_MAX_CONSTANT_BUFFERS; ++i)
        context.constants[s][i].buffer=&borrowed.base;
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
    context.depth_stencil_alpha=&dsa; context.framebuffer.zsbuf.texture=&borrowed.base;
    assert(ps5_try_multi_draw_batch(&context.base,&info,20,NULL,draws,19));
    assert(borrowed.base.refs==1 && freed==24 && !context.last_draw_status);
    dsa.depth_enabled=true; assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,19));
    dsa.depth_enabled=false;
    for (unsigned face=0; face<2; ++face) {
        dsa.stencil[face].enabled=true; assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,19));
        dsa.stencil[face].enabled=false;
    }
    borrowed.depth_staging_size=1; assert(!ps5_multidraw_eligible(&context,&info,NULL,draws,19));
}
'''
with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "ownership"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "src/gallium/ps5"), "-x", "c", "-o", str(exe), "-"],
                   input=code, text=True, check=True)
    subprocess.run([str(exe)], check=True, stdout=subprocess.DEVNULL)
print("PASS: descriptor isolation/uniform copy, chunk retirement, draw IDs, allocation rollback, failure pinning")
