#!/usr/bin/env python3
"""Compile the actual queued-presentation tail and completion checks with mocks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.index("int ps5_agc_gate2_batch_present(")
body = source[start:source.index("\n#endif", start)]
start = source.index("int ps5_agc_gate2_present(unsigned buffer_index)")
present = source[start:source.index("\n#endif", source.index("    return result;", start))]
code = r'''
#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#define PS5_GPU_PRESENT_BATCH 1
#define PS5_MULTIDRAW_BATCH 1
#define PS5_PROFILE_MARK(i) ((void)0)
#define COMMAND_BYTES 0x4000u
typedef struct { uint32_t *bottom, *top, *up, *down; uintptr_t callback; } agc_command_buffer_t;
static uint8_t memory[COMMAND_BYTES + 64];
static uint32_t completion;
static struct runtime_batch_entry { struct { void *words; uint32_t word_count; } submit;
    void *memory; size_t bytes; uint32_t *marker; uint32_t expected;
} runtime_batch_entries[1];
static unsigned runtime_batch_count, runtime_batch_active, runtime_batch_faulted;
static int runtime_video_registered, runtime_video_handle, runtime_gpu_present_buffer;
static uint64_t runtime_gpu_present_marker;
static unsigned runtime_gpu_present_count, runtime_present_count, out_of_space;
static unsigned tails, releases, flushes, cpu_flips, waits, idle_calls;
static int idle_error, flip_error, status_error, pending_error, wait_error, tail_error;
static unsigned finish_after;
static int64_t runtime_next_render_marker(void) { return 101; }
static uint8_t command_out_of_space(agc_command_buffer_t *c, uint32_t n, void *p) {
    (void)c; (void)n; (void)p; out_of_space = 1; return 0;
}
static void flush_gpu_data(const void *p, size_t bytes) {
    assert(p == memory && bytes == 12 * sizeof(uint32_t)); ++flushes;
}
static uint32_t *set_flip(void *p, uint32_t handle, int index, uint32_t mode, int64_t marker) {
    agc_command_buffer_t *c = p;
    assert(handle == 7 && index == 1 && mode == 1 && marker == 101);
    assert(c->up == c->bottom + 4 && c->top - c->bottom == COMMAND_BYTES / 4);
    assert(completion == 17 && runtime_batch_entries[0].expected == 17);
    ++tails; c->up += 4;
    return tail_error == 1 ? NULL : c->up;
}
static uint32_t *release_mem(void *p, uint8_t event, int16_t cache, uint64_t gcr,
    int8_t dst, void *address, uint32_t selection, uint64_t marker,
    uint16_t x, uint16_t y, int8_t z, int32_t w) {
    agc_command_buffer_t *c = p;
    assert(tails == 1 && c->up == c->bottom + 8 && event == 40 && cache == 0x30c);
    assert(!gcr && !dst && address == &completion && selection == 1 && marker == 101);
    assert(!x && !y && !z && !w);
    ++releases; c->up += 4;
    if (tail_error == 3) out_of_space = 1;
    return tail_error == 2 ? NULL : c->up;
}
static struct {
    uint32_t *(*set_flip)(void *, uint32_t, int, uint32_t, int64_t);
    uint32_t *(*release_mem)(void *, uint8_t, int16_t, uint64_t, int8_t, void *, uint32_t,
        uint64_t, uint16_t, uint16_t, int8_t, int32_t);
} runtime_batch_api = {set_flip, release_mem};
static int status(int handle, void *p) {
    assert(handle == 7); ((uint64_t *)p)[3] = waits >= finish_after ? 101 : 99;
    return status_error;
}
static int pending(int handle) { assert(handle == 7); return pending_error ? pending_error : waits < finish_after; }
static int vblank(int handle) { assert(handle == 7); ++waits; return wait_error; }
static int cpu_flip(int handle, int index, uint32_t mode, int64_t marker) {
    assert(handle == 7 && index == 1 && mode == 1 && marker == 101); ++cpu_flips; return flip_error;
}
static struct {
    int (*get_flip_status)(int, void *);
    int (*is_flip_pending)(int);
    int (*wait_vblank)(int);
    int (*submit_flip)(int, int, uint32_t, int64_t);
} runtime_video_api = {status, pending, vblank, cpu_flip};
static int runtime_video_wait_idle(void) { ++idle_calls; return idle_error; }
''' + body + present + r'''
static void reset(void) {
    runtime_video_registered = 1; runtime_video_handle = 7;
    runtime_gpu_present_buffer = -1; runtime_gpu_present_marker = 0;
    runtime_batch_active = runtime_batch_count = 1; runtime_batch_faulted = 0;
    runtime_batch_entries[0].submit.words = memory;
    runtime_batch_entries[0].submit.word_count = 4;
    runtime_batch_entries[0].memory = memory;
    runtime_batch_entries[0].bytes = sizeof(memory);
    runtime_batch_entries[0].marker = &completion;
    runtime_batch_entries[0].expected = completion = 17;
    tails = releases = flushes = cpu_flips = waits = idle_calls = 0;
    runtime_present_count = runtime_gpu_present_count = 0;
    idle_error = flip_error = status_error = pending_error = wait_error = tail_error = 0;
    finish_after = 0;
}
int main(void) {
    for (unsigned n = 0; n <= 121; ++n) {
        reset(); assert(ps5_agc_gate2_batch_present(1) == 0);
        assert(tails == 1 && releases == 1 && flushes == 1);
        assert(runtime_batch_entries[0].expected == 101 && completion == 17);
        assert(ps5_agc_gate2_present(1) != 0); /* Unretired command tail. */
        completion = 101; runtime_batch_count = runtime_batch_active = 0;
        finish_after = n;
        assert((ps5_agc_gate2_present(1) == 0) == (n <= 120));
        assert(!cpu_flips && waits == (n <= 120 ? n : 120));
        assert(runtime_present_count == (n <= 120) && runtime_gpu_present_count == (n <= 120));
        assert(runtime_gpu_present_buffer == (n <= 120 ? -1 : 1));
    }
    for (int error = 0; error < 10; ++error) {
        reset();
        if (error == 0) runtime_batch_entries[0].submit.word_count = COMMAND_BYTES / 4;
        if (error == 1) runtime_batch_entries[0].bytes = COMMAND_BYTES - 1;
        if (error == 2) runtime_gpu_present_buffer = 0;
        if (error == 3) idle_error = -1;
        if (error == 4) runtime_batch_faulted = 1;
        if (error == 5) runtime_video_registered = 0;
        if (error == 6) runtime_batch_count = 0;
        if (error >= 7) tail_error = error - 6;
        assert(ps5_agc_gate2_batch_present(1) != 0 && !flushes);
        assert(runtime_batch_entries[0].expected == 17);
    }
    for (int error = 0; error < 4; ++error) {
        reset(); assert(ps5_agc_gate2_batch_present(1) == 0);
        runtime_batch_count = runtime_batch_active = 0;
        if (error == 0) status_error = -21;
        if (error == 1) pending_error = -22;
        if (error == 2) { wait_error = -23; finish_after = 1; }
        assert(ps5_agc_gate2_present(error == 3 ? 0 : 1) != 0);
        assert(runtime_gpu_present_buffer == 1 && !cpu_flips && !runtime_present_count);
    }
    reset(); runtime_batch_count = runtime_batch_active = 0;
    assert(ps5_agc_gate2_present(1) == 0 && cpu_flips == 1 && waits == 1);
    assert(!runtime_gpu_present_count); /* Empty/readback-drained batch uses original path. */
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c, exe = Path(tmp) / "present.c", Path(tmp) / "present"
    c.write_text(code)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
egl = (root / "src/egl/ps5_egl.c").read_text()
assert "&fence, surface->window ? ps5_before_swap_flush : NULL, surface)" in egl
assert "ps5_context_queue_present(ps5_current_context->st->pipe, surface->buffer_index)" in egl
make = (root / "toolchain/ps5-opengl-core33.mk").read_text()
egl_rule = make[make.index("$(PS5_OPENGL_BUILD)/ps5_egl.o:"):make.index("$(PS5_OPENGL_BUILD)/ps5_screen.o:")]
assert "$(PS5_OPENGL_RUNTIME_DEFINES)" in egl_rule
assert "if (runtime_gpu_present_buffer >= 0)\n        return -1; /* Unconfirmed presentation" in source
print("PASS: bounded GPU-flip tail, post-tail marker, exact flip/idle completion, failures, CPU fallback")

# Compile the actual scanout-flush gate, including the unchanged default path.
start = source.rindex("    PS5_PROFILE_MARK(1);", 0, source.index("    /* The first queued draw flushes"))
flush_gate = source[start:source.index("    PS5_PROFILE_MARK(2);", start)]
code = r'''
#include <assert.h>
#include <stddef.h>
#define PS5_PROFILE_MARK(i) ((void)0)
static int flushes;
static void flush_gpu_data(const void *p, size_t n) { assert(p && n); ++flushes; }
static void run(int runtime_batch_active, unsigned runtime_batch_count,
                int runtime_video_registered, void *framebuffer, size_t framebuffer_pool_bytes,
                void *runtime_video_framebuffer, size_t runtime_video_framebuffer_size) {
    (void)runtime_batch_active; (void)runtime_batch_count; (void)runtime_video_registered;
    (void)runtime_video_framebuffer; (void)runtime_video_framebuffer_size;
''' + flush_gate + r'''
}
int main(void) {
    char pools[2];
    for (unsigned state = 0; state < 32; ++state) {
        int active = state & 1, queued = state & 2, registered = state & 4;
        int same_pointer = state & 8, same_size = state & 16;
        flushes = 0;
        run(active, queued ? 2 : 0, registered, pools, 64,
            pools + !same_pointer, same_size ? 64 : 32);
#ifdef PS5_GPU_PRESENT_BATCH
        assert(flushes == !(active && queued && registered && same_pointer && same_size));
#else
        assert(flushes == 1);
#endif
    }
    /* CPU access drains/reset the queue: the first resumed draw flushes again. */
    flushes = 0;
    run(1, 0, 1, pools, 64, pools, 64);
    run(1, 1, 1, pools, 64, pools, 64);
    run(1, 2, 1, pools, 64, pools, 64);
    run(1, 0, 1, pools, 64, pools, 64);
#ifdef PS5_GPU_PRESENT_BATCH
    assert(flushes == 2);
#else
    assert(flushes == 4);
#endif
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c, exe = Path(tmp) / "flush.c", Path(tmp) / "flush"
    c.write_text(code)
    for flags in ([], ["-DPS5_GPU_PRESENT_BATCH=1"]):
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                        str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print("PASS: scanout flush retained at batch/CPU-access/pool boundaries; default path unchanged")

screen = (root / "src/gallium/ps5/ps5_screen.c").read_text()
start = screen.index("#ifdef PS5_GPU_PRESENT_BATCH\n      /* Disabled depth AND stencil")
policy = screen[start:screen.index("#endif", start) + len("#endif")] + "\n"
start = screen.index("      if (flush_depth_stencil)")
depth_flush = screen[start:screen.index("      if (packed)", start)]
start = screen.index("         if (flush_depth_stencil)")
stencil_flush = screen[start:screen.index(';', start) + 1] + "\n"
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
static unsigned flushes;
static void ps5_flush_gpu_data(const void *p, size_t n) { assert(p && n); ++flushes; }
static void run(uint32_t control) {
    struct { uint32_t depth_control; } native = {control};
    (void)native;
    char backing[2] = {0};
    void *depth_data = backing;
    size_t depth_allocation = 32;
    struct { void *stencil_data; size_t stencil_allocation_size; } buffer = {backing + 1, 8}, *depth = &buffer;
''' + policy + depth_flush + "\n{\n" + stencil_flush + "}\n" + r'''
}
int main(void) {
    for (unsigned control = 0; control < 256; ++control) {
        flushes = 0; run(control);
#ifdef PS5_GPU_PRESENT_BATCH
        assert(flushes == (control ? 2u : 0u));
#else
        assert(flushes == 2);
#endif
    }
    /* Disable, CPU update, re-enable: active draw still flushes both buffers. */
    flushes = 0; run(0); run(2); run(1); run(3);
#ifdef PS5_GPU_PRESENT_BATCH
    assert(flushes == 6);
#else
    assert(flushes == 8);
#endif
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c, exe = Path(tmp) / "depth_flush.c", Path(tmp) / "depth_flush"
    c.write_text(code)
    for flags in ([], ["-DPS5_GPU_PRESENT_BATCH=1"]):
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                        str(c), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print("PASS: disabled depth/stencil flush elision; all nonzero controls and default retain flushing")
