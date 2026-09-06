#!/usr/bin/env python3
"""Exercise Mesa's real storage wrapper with a failing allocator; no GPU needed."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "third_party/mesa-26.2.0/src/mesa/main/fbobject.c").read_text()
start = source.index("void\n_mesa_renderbuffer_storage(")
function = source[start:source.index("\n}", start) + 2]

code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <GL/gl.h>
#define MESA_FORMAT_NONE 0
#define NO_SAMPLES 1000
#define FLUSH_VERTICES(...) ((void)0)
#define _mesa_base_fbo_format(...) GL_RGBA
#define _mesa_check_sample_count(...) GL_NO_ERROR
#define _mesa_check_storage_sample_count(...) GL_NO_ERROR
#define invalidate_rb NULL
#define _mesa_HashWalk(...) (++invalidations)
struct gl_context {
    struct { unsigned MaxRenderbufferSize; } Const;
    GLenum error;
};
struct gl_renderbuffer {
    GLuint Width, Height;
    GLenum InternalFormat, _BaseFormat;
    int Format, NumSamples, NumStorageSamples;
    bool AttachedAnytime;
    bool (*AllocStorage)(struct gl_context *, struct gl_renderbuffer *,
                         GLenum, GLuint, GLuint);
};
static unsigned allocations, errors, invalidations;
static bool fail_allocation, unsupported;
void _mesa_error(struct gl_context *ctx, GLenum error, const char *fmt) {
    assert(fmt && error == GL_OUT_OF_MEMORY);
    ctx->error = error;
    ++errors;
}
static bool allocate(struct gl_context *ctx, struct gl_renderbuffer *rb,
                     GLenum format, GLuint width, GLuint height) {
    (void)ctx; (void)format;
    ++allocations;
    rb->Width = width;
    rb->Height = height;
    if (fail_allocation) return false;
    rb->Format = unsupported ? MESA_FORMAT_NONE : 1;
    return true;
}
''' + function + r'''
int main(void) {
    struct gl_context ctx = {.Const = {8192}};
    struct gl_renderbuffer rb = {.AllocStorage = allocate, .AttachedAnytime = true};
    for (int samples = 0; samples <= 4; samples += 4) {
        fail_allocation = true;
        _mesa_renderbuffer_storage(&ctx, &rb, GL_RGBA8, 64, 64, samples, samples);
        assert(ctx.error == GL_OUT_OF_MEMORY);
        assert(rb.Width == 0 && rb.Height == 0 && rb.Format == MESA_FORMAT_NONE);
        assert(rb.InternalFormat == GL_NONE && rb._BaseFormat == GL_NONE);
        assert(rb.NumSamples == 0 && rb.NumStorageSamples == 0);
        fail_allocation = false;
        ctx.error = GL_NO_ERROR;
        _mesa_renderbuffer_storage(&ctx, &rb, GL_RGBA8, 64, 64, samples, samples);
        assert(ctx.error == GL_NO_ERROR && rb.Width == 64 && rb.Height == 64);
        assert(rb.InternalFormat == GL_RGBA8 && rb.Format != MESA_FORMAT_NONE);
        assert(rb.NumSamples == samples && rb.NumStorageSamples == samples);
        unsigned before = allocations;
        _mesa_renderbuffer_storage(&ctx, &rb, GL_RGBA8, 64, 64, samples, samples);
        assert(allocations == before); /* Existing storage is reused. */
    }
    /* Unsupported format and zero-size success are not allocation failures. */
    unsupported = true;
    _mesa_renderbuffer_storage(&ctx, &rb, GL_RGBA8, 32, 32, 0, 0);
    assert(ctx.error == GL_NO_ERROR && rb.Format == MESA_FORMAT_NONE);
    unsupported = false;
    _mesa_renderbuffer_storage(&ctx, &rb, GL_RGBA8, 0, 0, 0, 0);
    assert(ctx.error == GL_NO_ERROR && rb.Width == 0 && rb.Height == 0);
    assert(errors == 2 && allocations == 6 && invalidations == 6);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "renderbuffer-oom")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "third_party/mesa-26.2.0/include"),
                    "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: Mesa allocation failure reports OOM; state reset, retry, reuse, invalidation")
