#!/usr/bin/env python3
"""Compile the actual fast-path eligibility and inline-uniform snapshot code."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / "src/gallium/ps5/ps5_screen.c").read_text()
assert "#define PS5_GPU_CLEAR_MIN_PIXELS 16384u" in source
start = source.index("static bool\nps5_clear_gpu_color(")
clear_body = source[start:source.index("\nstatic void\nps5_clear(", start)]
assert clear_body.index("context->deferred_color_clear = buffers == PIPE_CLEAR_COLOR0;") < \
       clear_body.index("   util_blitter_clear(") < \
       clear_body.index("context->deferred_color_clear = false;")
prefix = source[start:source.index("   if (!context->blitter)", start)]
code = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define PS5_ENABLE_MRT_CANDIDATE 1
#define PS5_ENABLE_UBO_CANDIDATE 1
#define PS5_MAX_CONSTANT_BUFFER_SIZE 16384
#define PS5_GPU_CLEAR_MIN_PIXELS 16384u
#define PIPE_CLEAR_COLOR 0x3fc
#define PIPE_CLEAR_COLOR0 4
#define PIPE_MASK_RGBA 15
#define PIPE_TEXTURE_2D 2
#define PIPE_FORMAT_R8G8B8A8_UNORM 1
union pipe_color_union { uint32_t ui[4]; float f[4]; };
struct pipe_scissor_state { unsigned unused; };
struct resource { unsigned target, nr_samples, nr_storage_samples, format; };
struct ps5_resource { struct resource base; unsigned render_staging_size; uint8_t *data; size_t size; };
struct pipe_surface { struct ps5_resource *texture; unsigned level, first_layer, last_layer, format; };
struct constant { struct resource *buffer; unsigned offset, size; bool valid, copied; };
#define ps5_constant_state constant
struct pipe_constant_buffer { struct resource *buffer; unsigned buffer_offset, buffer_size; const void *user_buffer; };
struct ps5_context {
    struct { unsigned width, height, nr_cbufs; struct pipe_surface cbufs[1]; } framebuffer;
    bool framebuffer_valid;
    unsigned render_condition_query, stream_output_target_count, active_occlusion_query;
    unsigned active_primitives_generated_query, active_primitives_emitted_query;
    struct constant constants[2][1]; struct ps5_resource *descriptor_storage[2];
};
static unsigned target_width = 128, target_height = 128;
static unsigned ps5_surface_width(const struct pipe_surface *s) { (void)s; return target_width; }
static unsigned ps5_surface_height(const struct pipe_surface *s) { (void)s; return target_height; }
static size_t ps5_copied_constant_offset(unsigned slot) { assert(slot == 1); return 16; }
''' + prefix + r'''
   if (state->valid && state->copied) {
       assert(cb.user_buffer == copied_constants && cb.buffer_size == state->size);
       assert(!memcmp(cb.user_buffer, context->descriptor_storage[1]->data + 16, state->size));
   } else if (state->valid) {
       assert(cb.buffer == state->buffer && cb.buffer_offset == state->offset && !cb.user_buffer);
   } else {
       assert(!cb.buffer && !cb.user_buffer && !cb.buffer_size);
   }
   return true;
}
static void test_adapter(void);
int main(void) {
    test_adapter();
    uint8_t bytes[PS5_MAX_CONSTANT_BUFFER_SIZE + 16]; memset(bytes, 0xa5, sizeof(bytes));
    struct ps5_resource target = {.base = {.target=2, .format=1}};
    struct ps5_resource storage = {.data=bytes, .size=sizeof(bytes)};
    struct ps5_context good = {.framebuffer={.width=128, .height=128, .nr_cbufs=1,
        .cbufs={{.texture=&target, .format=1}}}, .framebuffer_valid=true,
        .descriptor_storage={0, &storage}};
    union pipe_color_union color = {{0}};
    struct pipe_scissor_state scissor = {0};
    assert(ps5_clear_gpu_color(&good, 4, 15, 0, &color));
    good.framebuffer.width = target_width = 64;
    good.framebuffer.height = target_height = 64;
    assert(!ps5_clear_gpu_color(&good, 4, 15, 0, &color));
    good.framebuffer.width = target_width = 113;
    good.framebuffer.height = target_height = 47;
    assert(!ps5_clear_gpu_color(&good, 4, 15, 0, &color));
    good.framebuffer.width = target_width = 16383;
    good.framebuffer.height = target_height = 1;
    assert(!ps5_clear_gpu_color(&good, 4, 15, 0, &color));
    good.framebuffer.width = target_width = 16384;
    assert(ps5_clear_gpu_color(&good, 4, 15, 0, &color));
    good.framebuffer.width = target_width = 0;
    assert(!ps5_clear_gpu_color(&good, 4, 15, 0, &color));
    good.framebuffer.width = target_width = 128;
    good.framebuffer.height = target_height = 128;
    assert(!ps5_clear_gpu_color(0, 4, 15, 0, &color));
    assert(!ps5_clear_gpu_color(&good, 4, 15, 0, 0));
    assert(!ps5_clear_gpu_color(&good, 4, 15, &scissor, &color));
    assert(!ps5_clear_gpu_color(&good, 4, 7, 0, &color));
    assert(!ps5_clear_gpu_color(&good, 8, 15, 0, &color));
    assert(!ps5_clear_gpu_color(&good, 12, 15, 0, &color));
#define REJECT(field, value) do { struct ps5_context c = good; c.field = value; \
    assert(!ps5_clear_gpu_color(&c, 4, 15, 0, &color)); } while (0)
    REJECT(framebuffer_valid, false); REJECT(framebuffer.width, 63); REJECT(framebuffer.height, 31);
    REJECT(framebuffer.nr_cbufs, 2); REJECT(framebuffer.cbufs[0].texture, 0);
    REJECT(framebuffer.cbufs[0].level, 1); REJECT(framebuffer.cbufs[0].first_layer, 1);
    REJECT(framebuffer.cbufs[0].last_layer, 1); REJECT(framebuffer.cbufs[0].format, 2);
    REJECT(render_condition_query, 1); REJECT(stream_output_target_count, 1);
    REJECT(active_occlusion_query, 1); REJECT(active_primitives_generated_query, 1);
    REJECT(active_primitives_emitted_query, 1);
#define REJECT_TARGET(field, value) do { target.field = value; \
    assert(!ps5_clear_gpu_color(&good, 4, 15, 0, &color)); target.field = 0; } while (0)
    REJECT_TARGET(base.nr_samples, 4); REJECT_TARGET(base.nr_storage_samples, 4);
    REJECT_TARGET(render_staging_size, 1);
    good.constants[1][0] = (struct constant){.buffer=&target.base, .offset=32, .size=64, .valid=true};
    assert(ps5_clear_gpu_color(&good, 4, 15, 0, &color));
    good.constants[1][0] = (struct constant){.size=sizeof(bytes)-16, .valid=true, .copied=true};
    assert(ps5_clear_gpu_color(&good, 4, 15, 0, &color));
    REJECT(constants[1][0].size, sizeof(bytes)); REJECT(descriptor_storage[1], 0);
    storage.size = 15; assert(!ps5_clear_gpu_color(&good, 4, 15, 0, &color));
}
'''
start = source.index("   if (templ && templ->type == PIPE_SHADER_IR_TGSI")
adapter = source[start:source.index("\n   if (!templ || templ->type != PIPE_SHADER_IR_NIR", start)]
code += r'''
enum { PIPE_SHADER_IR_TGSI, PIPE_SHADER_IR_NIR, PSBC_STAGE_GEOMETRY = 3 };
struct pipe_shader_state { int type; const void *tokens; struct { void *nir; } ir;
    struct { unsigned num_outputs; } stream_output; };
static unsigned conversions;
static unsigned normalizations;
static int conversion_fails;
static void *tgsi_to_nir(const void *tokens, void *screen, bool cache) {
    assert(tokens && screen && !cache); ++conversions;
    return conversion_fails ? NULL : screen;
}
static void nir_lower_io_passes(void *nir, bool renumber) {
    assert(nir && !renumber); ++normalizations;
}
static bool adapt(const struct pipe_shader_state *templ, int stage) {
    int sentinel;
    void *screen = &sentinel;
    struct pipe_shader_state converted;
''' + adapter + r'''
    return templ && templ->type == PIPE_SHADER_IR_NIR && templ->ir.nir;
}
static void test_adapter(void) {
    int sentinel;
    struct pipe_shader_state state = {.type=PIPE_SHADER_IR_NIR, .ir.nir=&sentinel};
    assert(adapt(&state, 1) && conversions == 0); /* Original NIR unchanged. */
    state = (struct pipe_shader_state){.type=PIPE_SHADER_IR_TGSI, .tokens=&sentinel};
    assert(adapt(&state, 1) && conversions == 1);
    assert(adapt(&state, 2) && conversions == 2);
    assert(!adapt(&state, PSBC_STAGE_GEOMETRY) && conversions == 2);
    state.stream_output.num_outputs = 1;
    assert(!adapt(&state, 1) && conversions == 2);
    state.stream_output.num_outputs = 0;
    conversion_fails = 1;
    assert(!adapt(&state, 1) && conversions == 3);
    state.tokens = NULL;
    assert(!adapt(&state, 1) && conversions == 3);
    assert(!adapt(NULL, 1));
    assert(normalizations == 2);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "gpu-clear-state")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-x", "c", "-o", executable, "-"], input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: GPU-clear size boundary/eligibility, uniform snapshot bounds, guarded TGSI adapter, NIR unchanged")

# Compile the actual shared callback through its GPU/fallback dispatch boundary.
# The unchanged CPU pixel loops are covered by native mixed/masked/depth gates.
start = source.index("static void\nps5_clear(")
dispatch = source[start:source.index("   if (PS5_ENABLE_MSAA4_CANDIDATE", start)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define PIPE_CLEAR_DEPTH 1u
#define PIPE_CLEAR_STENCIL 2u
#define PIPE_CLEAR_COLOR0 4u
#define PIPE_CLEAR_COLOR 0x3fcu
struct pipe_context { int unused; };
struct ps5_resource { struct { unsigned format; } base; };
struct ps5_context { struct pipe_context base; int last_draw_status;
    struct { struct { struct ps5_resource *texture; } zsbuf; } framebuffer; };
struct pipe_scissor_state { unsigned unused; };
union pipe_color_union { float f[4]; };
static char order[16];
static unsigned used, pending, expected_depth;
static bool condition = true, depth_ok = true, gpu_ok = true;
static int gpu_status;
static void record(char stage) { assert(used < 15); order[used++] = stage; }
static void ps5_draw_batch_drain(void) { record('D'); pending = 0; }
static bool ps5_render_condition_passes(struct ps5_context *c)
{ assert(c); record('R'); return condition; }
static bool ps5_clear_depth_stencil(struct ps5_context *c, unsigned buffers,
    uint8_t mask, const struct pipe_scissor_state *scissor, double depth, unsigned stencil)
{
    assert(c && !pending && buffers == expected_depth && mask == 0x5a && !scissor);
    assert(depth == .25 && stencil == 0x73); record('Z'); return depth_ok;
}
static bool ps5_clear_gpu_color(struct ps5_context *c, unsigned buffers,
    uint32_t mask, const struct pipe_scissor_state *scissor, const union pipe_color_union *color)
{
    assert(c && !pending && buffers && !(buffers & 3) && mask == 15 && !scissor && color);
    record('C'); c->last_draw_status = gpu_status;
    if (gpu_ok) pending = 1;
    return gpu_ok;
}
''' + dispatch + r'''
    (void)resource; record('F'); /* Existing CPU color fallback follows here. */
}
static void run(unsigned buffers, const char *expected, unsigned queued)
{
    struct ps5_context c = {0};
    union pipe_color_union color = {{0}};
    memset(order, 0, sizeof(order)); used = 0; expected_depth = buffers & 3;
    ps5_clear(&c.base, buffers, 15, 0x5a, NULL, &color, .25, 0x73);
    assert(!strcmp(order, expected) && pending == queued);
}
int main(void)
{
    run(4, "DRC", 1);
    run(1, "DRZ", 0); /* Prior GPU work must retire before this CPU access. */
    run(2, "DRZ", 0); run(3, "DRZ", 0); run(0, "DR", 0);
    for (unsigned buffers = 5; buffers <= 7; ++buffers) run(buffers, "DRZC", 1);
    condition = false; run(7, "DR", 0); condition = true;
    depth_ok = false; run(7, "DRZ", 0); depth_ok = true;
    gpu_ok = false; run(7, "DRZCF", 0); gpu_ok = true;
    gpu_status = -30; run(7, "DRZC", 1); /* No CPU replay after attempted GPU failure. */
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "mixed-clear-order")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-x", "c", "-o", executable, "-"], input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: shared mixed-clear dispatch drains prior work, completes CPU depth/stencil before GPU color, preserves failure/fallback ordering")
