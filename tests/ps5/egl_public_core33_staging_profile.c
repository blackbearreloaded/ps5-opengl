// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define TAG "[ps5-egl-staging-profile]"
/* Common footprint within the Core 3.3 minimum 3D texture size (256).
 * Keep every case comparable, including the existing PS5 3D limit. */
#define WIDTH 256
#define HEIGHT 144
#define WARMUP 4
#define MAX_CYCLES 100000
#define TARGET_NS UINT64_C(3000000000)
#define MAX_NS UINT64_C(5000000000)
#ifdef PS5_STAGING_HOST_REFERENCE
#define HOST 1
#define SURFACE_TYPE EGL_PBUFFER_BIT
#else
#define HOST 0
#define SURFACE_TYPE EGL_WINDOW_BIT
#endif

/* Transfer/FBO sequence follows transfer_workload and layered_mip_fbo.
 * Only the tested texture's format/subresource and framebuffer-sRGB state change.
 * Source/sample renderbuffers stay RGBA8; dimensions, calls and regions match.
 * This is composite CPU-wall throughput, including glFinish each cycle, not
 * isolated transfer bandwidth, GPU timestamps, presentation or game FPS.
 * ponytail: seven fixed cases, sampled guards only; use the existing transfer
 * regression gate for exhaustive images, not this short diagnostic batch.
 */
static const struct test_case {
   const char *name, *sampler, *coordinate;
   GLenum target, format;
   int level, layer, depth, channels, framebuffer_srgb;
} cases[] = {
   {"rgba8-2d", "sampler2D", "uv", GL_TEXTURE_2D, GL_RGBA8, 0, 0, 1, 4, 0},
   {"srgb8-2d", "sampler2D", "uv", GL_TEXTURE_2D, GL_SRGB8_ALPHA8, 0, 0, 1, 4, 1},
   {"r8-2d", "sampler2D", "uv", GL_TEXTURE_2D, GL_R8, 0, 0, 1, 1, 0},
   {"rgba16f-2d", "sampler2D", "uv", GL_TEXTURE_2D, GL_RGBA16F, 0, 0, 1, 4, 0},
   {"rgba8-2d-mip1", "sampler2D", "uv", GL_TEXTURE_2D, GL_RGBA8, 1, 0, 1, 4, 0},
   {"rgba8-array-layer1", "sampler2DArray", "vec3(uv, 1.0)",
    GL_TEXTURE_2D_ARRAY, GL_RGBA8, 0, 1, 3, 4, 0},
   {"rgba8-3d-layer1", "sampler3D", "vec3(uv, 0.5)",
    GL_TEXTURE_3D, GL_RGBA8, 0, 1, 3, 4, 0},
};
static const uint8_t zeroes[WIDTH * 2 * HEIGHT * 2 * 4];
static const int probes[][2] = {
   {0, 0}, {WIDTH - 1, HEIGHT - 1}, {2, 5}, {3, 5}, {4, 5},
   {WIDTH / 2, HEIGHT / 2}, {WIDTH - 9, HEIGHT - 12}, {WIDTH - 8, HEIGHT - 12},
};
static const uint8_t colors[2][4] = {{64, 128, 192, 255}, {192, 64, 128, 255}};
/* Rounded standard sRGB transfer values for these fixed oracle colors.
 * Copies/uploads retain encoded bytes; sampling decodes RGB regardless of
 * GL_FRAMEBUFFER_SRGB. Only FBO writes encode when enabled; alpha stays linear.
 */
static const uint8_t decoded_colors[2][4] = {{13, 55, 134, 255}, {134, 13, 55, 255}};
static const uint8_t patch[4] = {64, 128, 192, 128};
static const uint8_t encoded_patch[4] = {137, 188, 225, 128};
static const uint8_t decoded_patch[4] = {13, 55, 134, 128};

static uint64_t now_ns(void)
{
   struct timespec t;
   return clock_gettime(CLOCK_MONOTONIC, &t) == 0
      ? (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec : 0;
}

/* Same compile/link-status and fullscreen triangle idiom as the native gates. */
static GLuint make_program(const char *fragment)
{
   const char *sources[] = {
      "#version 330 core\nlayout(location=0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n", fragment,
   };
   GLuint program = glCreateProgram();
   if (!program) return 0;
   for (unsigned i = 0; i < 2; ++i) {
      GLuint shader = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
      GLint ok = GL_FALSE;
      if (shader) {
         glShaderSource(shader, 1, &sources[i], NULL);
         glCompileShader(shader);
         glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
         if (ok) glAttachShader(program, shader);
         else {
            char log[512] = {0};
            glGetShaderInfoLog(shader, sizeof(log), NULL, log);
            printf(TAG " shader=%u log=%s\n", i, log);
         }
         glDeleteShader(shader);
      }
      if (!ok) { glDeleteProgram(program); return 0; }
   }
   GLint ok = GL_FALSE;
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   if (!ok) {
      char log[512] = {0};
      glGetProgramInfoLog(program, sizeof(log), NULL, log);
      printf(TAG " link=%s\n", log);
      glDeleteProgram(program);
      return 0;
   }
   return program;
}

static int attach(const struct test_case *c, GLuint texture, int level, int layer)
{
   if (c->target == GL_TEXTURE_2D)
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, c->target, texture, level);
   else
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, level, layer);
   return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static int cycle(const struct test_case *c, const GLuint fbo[3], GLuint render,
                 GLint color, GLuint sample, unsigned index)
{
   const uint8_t *rgba = colors[index % 2];
   const uint8_t white[] = {255, 255, 255, 255};
   glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
   glUseProgram(render);
   glUniform4f(color, rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, 1);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   if (c->target == GL_TEXTURE_2D) {
      glCopyTexSubImage2D(c->target, c->level, 3, 5, 0, 0, WIDTH - 6, HEIGHT - 10);
      glTexSubImage2D(c->target, c->level, 3, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
   } else {
      glCopyTexSubImage3D(c->target, c->level, 3, 5, c->layer, 0, 0, WIDTH - 6, HEIGHT - 10);
      glTexSubImage3D(c->target, c->level, 3, 5, c->layer, 1, 1, 1,
                      GL_RGBA, GL_UNSIGNED_BYTE, white);
   }
   glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
   glEnable(GL_SCISSOR_TEST);
   glScissor(WIDTH - 8, HEIGHT - 12, 5, 7);
   glUniform4f(color, patch[0] / 255.0f, patch[1] / 255.0f,
               patch[2] / 255.0f, patch[3] / 255.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glDisable(GL_SCISSOR_TEST);
   /* Sample before readback, with the tested texture detached from the draw FBO. */
   glBindFramebuffer(GL_FRAMEBUFFER, fbo[2]);
   glUseProgram(sample);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   return glGetError() == GL_NO_ERROR;
}

static int check_pixels(const struct test_case *c, const char *phase,
                        const char *image, unsigned index, int guard)
{
   const int srgb = c->format == GL_SRGB8_ALPHA8;
   const int sampled = !strcmp(image, "sample");
   const uint8_t *copied = srgb && sampled ? decoded_colors[index % 2] : colors[index % 2];
   const uint8_t *drawn = patch;
   if (srgb) {
      if (c->framebuffer_srgb) drawn = sampled ? patch : encoded_patch;
      else if (sampled) drawn = decoded_patch;
   }
   for (unsigned p = 0; p < sizeof(probes) / sizeof(probes[0]); ++p) {
      const int x = probes[p][0], y = probes[p][1];
      uint8_t actual[4] = {0xa5, 0xa5, 0xa5, 0xa5};
      uint8_t expected[4] = {0, 0, 0, 0};
      if (!guard && x >= 3 && x < WIDTH - 3 && y >= 5 && y < HEIGHT - 5) {
         for (unsigned k = 0; k < 4; ++k) expected[k] = copied[k];
         if (x >= WIDTH - 8 && y >= HEIGHT - 12)
            for (unsigned k = 0; k < 4; ++k) expected[k] = drawn[k];
         if (x == 3 && y == 5)
            for (unsigned k = 0; k < 4; ++k) expected[k] = 255;
      }
      for (unsigned k = (unsigned)c->channels; k < 4; ++k)
         expected[k] = k == 3 ? 255 : 0;
      glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
      for (unsigned k = 0; k < 4; ++k) {
         int delta = (int)actual[k] - expected[k];
         if (delta < -1 || delta > 1) {
            printf(TAG " mismatch case=%s phase=%s image=%s xy=%d/%d channel=%u "
                   "actual=%u expected=%u\n", c->name, phase, image, x, y, k, actual[k], expected[k]);
            return 0;
         }
      }
   }
   return glGetError() == GL_NO_ERROR;
}

static int oracle(const struct test_case *c, const GLuint fbo[3], GLuint texture,
                  const char *phase, unsigned index, GLuint render, GLint color, GLuint sample)
{
   int count = 16;
   glBindFramebuffer(GL_FRAMEBUFFER, fbo[2]);
   if (!check_pixels(c, phase, "sample", index, 0)) return 0;
   glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
   if (!check_pixels(c, phase, "selected", index, 0)) return 0;
   if (c->level) {
      if (!attach(c, texture, 0, 0) || !check_pixels(c, phase, "base-guard", index, 1)) return 0;
      count += 8;
   }
   for (int z = 0; z < c->depth; ++z) {
      if (z == c->layer) continue;
      if (!attach(c, texture, c->level, z) || !check_pixels(c, phase, "layer-guard", index, 1)) return 0;
      count += 8;
   }
   if (c->format == GL_SRGB8_ALPHA8) {
      /* Check disabled encoding after the measured-state samples. This extra
       * diagnostic cycle runs only in the before/after oracle, never in timing.
       * The next measured cycle rewrites the interior and restores its patch.
       */
      struct test_case off = *c;
      off.framebuffer_srgb = 0;
      glDisable(GL_FRAMEBUFFER_SRGB);
      if (!cycle(&off, fbo, render, color, sample, index)) return 0;
      glBindFramebuffer(GL_FRAMEBUFFER, fbo[2]);
      if (!check_pixels(&off, phase, "sample", index, 0)) return 0;
      glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
      if (!check_pixels(&off, phase, "selected", index, 0)) return 0;
      glEnable(GL_FRAMEBUFFER_SRGB);
      count += 16;
   }
   return attach(c, texture, c->level, c->layer) && glGetError() == GL_NO_ERROR ? count : 0;
}

static int run_case(const struct test_case *c, GLuint render, GLint color)
{
   GLuint texture = 0, fbo[3] = {0}, rb[2] = {0}, sample = 0;
   uint64_t setup_ns = 0, warmup_ns = 0, elapsed = 0, previous = 0;
   uint64_t start = now_ns(), end = 0, cleanup_start = 0, cleanup_ns = 0;
   unsigned cycles = 0;
   int before = 0, after = 0, passed = 0;
   const char *stage = "setup";
   char fragment[512];
   if (!start) goto cleanup;
   if (c->framebuffer_srgb) glEnable(GL_FRAMEBUFFER_SRGB);
   else glDisable(GL_FRAMEBUFFER_SRGB);
   snprintf(fragment, sizeof(fragment), "#version 330 core\nuniform %s u_texture;\n"
            "layout(location=0) out vec4 color;\nvoid main() {\n"
            "vec2 uv = gl_FragCoord.xy / vec2(256.0, 144.0);\n"
            "color = textureLod(u_texture, %s, %d.0); }\n", c->sampler, c->coordinate, c->level);
   sample = make_program(fragment);
   if (!sample) goto cleanup;
   GLint sampler = glGetUniformLocation(sample, "u_texture");
   if (sampler < 0) goto cleanup;
   glUseProgram(sample);
   glUniform1i(sampler, 0);
   glGenTextures(1, &texture);
   glBindTexture(c->target, texture);
   glTexParameteri(c->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(c->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(c->target, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(c->target, GL_TEXTURE_MAX_LEVEL, c->level);
   for (int level = 0; level <= c->level; ++level) {
      const int scale = 1 << (c->level - level);
      if (c->target == GL_TEXTURE_2D)
         glTexImage2D(c->target, level, c->format, WIDTH * scale, HEIGHT * scale,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, zeroes);
      else
         glTexImage3D(c->target, level, c->format, WIDTH, HEIGHT, c->depth,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, zeroes);
   }
   glGenFramebuffers(3, fbo);
   glGenRenderbuffers(2, rb);
   for (unsigned i = 0; i < 2; ++i) {
      glBindFramebuffer(GL_FRAMEBUFFER, fbo[i ? 2 : 0]);
      glBindRenderbuffer(GL_RENDERBUFFER, rb[i]);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb[i]);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) goto cleanup;
   }
   glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
   if (!attach(c, texture, c->level, c->layer)) goto cleanup;
   GLint encoding = 0;
   glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                         GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
   if (encoding != (c->format == GL_SRGB8_ALPHA8 ? GL_SRGB : GL_LINEAR)) goto cleanup;
   glViewport(0, 0, WIDTH, HEIGHT);
   glFinish();
   end = now_ns();
   if (end <= start || glGetError() != GL_NO_ERROR) goto cleanup;
   setup_ns = end - start;
   stage = "warmup";
   start = now_ns();
   for (unsigned i = 0; i < WARMUP; ++i)
      if (!cycle(c, fbo, render, color, sample, i)) goto cleanup;
   end = now_ns();
   if (!start || end <= start) goto cleanup;
   warmup_ns = end - start;
   stage = "before";
   before = oracle(c, fbo, texture, stage, WARMUP - 1, render, color, sample);
   if (!before) goto cleanup;
   glFinish(); /* Exclude oracle work and its attachment restoration. */
   if (glGetError() != GL_NO_ERROR) goto cleanup;
   stage = "measure";
   start = now_ns();
   if (!start) goto cleanup;
   do {
      previous = elapsed;
      if (!cycle(c, fbo, render, color, sample, cycles)) goto cleanup;
      ++cycles;
      end = now_ns();
      if (end <= start || end - start <= elapsed) goto cleanup;
      elapsed = end - start;
   } while (elapsed < TARGET_NS && cycles < MAX_CYCLES);
   if (elapsed < TARGET_NS || elapsed > MAX_NS) goto cleanup;
   stage = "after";
   after = oracle(c, fbo, texture, stage, cycles - 1, render, color, sample);
   passed = after == before;

cleanup:
   glFinish(); /* Drain probes separately from deletion timing. */
   cleanup_start = now_ns();
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_FRAMEBUFFER_SRGB);
   glUseProgram(0);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindRenderbuffer(GL_RENDERBUFFER, 0);
   glBindTexture(c->target, 0);
   glDeleteFramebuffers(3, fbo);
   glDeleteRenderbuffers(2, rb);
   glDeleteTextures(1, &texture);
   if (sample) glDeleteProgram(sample);
   glFinish();
   GLenum error = glGetError();
   end = now_ns();
   if (cleanup_start && end > cleanup_start) cleanup_ns = end - cleanup_start;
   passed &= error == GL_NO_ERROR && cleanup_ns > 0;
   if (!passed) printf(TAG " failure case=%s stage=%s error=0x%x\n", c->name, stage, error);
   printf(TAG " case=%s framebuffer_srgb=%d encoding=%s setup_ns=%" PRIu64 " warmup_ns=%" PRIu64
          " before=%d cycles=%u previous_ns=%" PRIu64 " measured_ns=%" PRIu64
          " after=%d cleanup_ns=%" PRIu64 " result=%d\n", c->name, c->framebuffer_srgb,
          c->format == GL_SRGB8_ALPHA8 ? "srgb" : "linear", setup_ns, warmup_ns,
          before, cycles, previous, elapsed, after, cleanup_ns, passed ? 0 : 1);
   fflush(stdout);
   return passed;
}

int main(void)
{
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, SURFACE_TYPE, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE,
   };
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE,
   };
   const float vertices[] = {-1, -1, 3, -1, -1, 3};
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLContext context = EGL_NO_CONTEXT;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLConfig config = NULL;
   EGLint count = 0;
   GLuint render = 0, vao = 0, vbo = 0;
   unsigned completed = 0;
   int made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;
   uint64_t start = now_ns(), end = 0;
   printf(TAG " config version=3 host=%d cases=7 width=256 height=144 warmup=4 "
          "target_ns=3000000000 max_ns=5000000000 max_cycles=100000 completion=glFinish\n", HOST);
   if (!start) goto cleanup;
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) || count != 1) goto cleanup;
#ifdef PS5_STAGING_HOST_REFERENCE
   const EGLint surface_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
   surface = eglCreatePbufferSurface(display, config, surface_attributes);
#else
   surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, NULL);
#endif
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context)) goto cleanup;
   made_current = 1;
   printf(TAG " renderer=%s version=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
   GLint max_2d = 0, max_3d = 0, max_layers = 0, max_renderbuffer = 0;
   glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_2d);
   glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &max_3d);
   glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
   glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &max_renderbuffer);
   printf(TAG " limits texture2d=%d texture3d=%d layers=%d renderbuffer=%d\n",
          max_2d, max_3d, max_layers, max_renderbuffer);
   if (max_2d < WIDTH * 2 || max_3d < WIDTH || max_layers < 3 ||
       max_renderbuffer < WIDTH || glGetError() != GL_NO_ERROR) goto cleanup;
   glDisable(GL_DITHER);
   glDisable(GL_BLEND);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_FRAMEBUFFER_SRGB);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
   glActiveTexture(GL_TEXTURE0);
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   render = make_program("#version 330 core\nuniform vec4 u_color;\n"
                         "layout(location=0) out vec4 color;\nvoid main() { color = u_color; }\n");
   if (!render) goto cleanup;
   GLint color = glGetUniformLocation(render, "u_color");
   glFinish();
   end = now_ns();
   if (color < 0 || end <= start || glGetError() != GL_NO_ERROR) goto cleanup;
   printf(TAG " session_setup_ns=%" PRIu64 "\n", end - start);
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      if (!run_case(&cases[i], render, color)) goto cleanup;
      ++completed;
   }
   passed = completed == 7;

cleanup:
   start = now_ns();
   if (made_current) {
      glUseProgram(0);
      if (render) glDeleteProgram(render);
      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glDeleteBuffers(1, &vbo);
      glDeleteVertexArrays(1, &vao);
      glFinish();
      cleanup_ok &= glGetError() == GL_NO_ERROR;
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (context != EGL_NO_CONTEXT) cleanup_ok &= eglDestroyContext(display, context);
   if (surface != EGL_NO_SURFACE) cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY) cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   end = now_ns();
   cleanup_ok &= start > 0 && end > start;
   passed &= cleanup_ok;
   printf(TAG " finished cases=%u session_cleanup_ns=%" PRIu64 " cleanup=%u result=%d\n",
          completed, end > start ? end - start : 0, cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
