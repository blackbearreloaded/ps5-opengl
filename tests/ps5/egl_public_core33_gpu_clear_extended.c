// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Public GL oracle, not a fast-path eligibility test. Each operation starts
 * with two GPU-drawn halves; expected pixels never come from GPU readback. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define TAG "[gpu-clear-extended]"
#define WIDTH 1024
#define HEIGHT 768
#define COLOR GL_COLOR_BUFFER_BIT
#define DEPTH GL_DEPTH_BUFFER_BIT
#define STENCIL GL_STENCIL_BUFFER_BIT
#define ALL (COLOR | DEPTH | STENCIL)
#ifdef PS5_GPU_CLEAR_EXTENDED_HOST_REFERENCE
#define SURFACE_TYPE EGL_PBUFFER_BIT
static int ps5_egl_current_draw_status(unsigned *calls) { *calls = 0; return 0; }
#else
#define SURFACE_TYPE EGL_WINDOW_BIT
int ps5_egl_current_draw_status(unsigned *calls);
#endif

struct clear_case {
   const char *name;
   GLbitfield buffers;
   int scissor, x, y, w, h;
   unsigned color_mask, depth_mask, stencil_mask;
   float depth;
   unsigned stencil;
};

static const struct clear_case cases[] = {
   {"full-depth", DEPTH, 0, 0, 0, 0, 0, 15, 1, 255, .375f, 0x69},
   {"full-stencil", STENCIL, 0, 0, 0, 0, 0, 15, 1, 255, .375f, 0x69},
   {"full-depth-stencil", DEPTH | STENCIL, 0, 0, 0, 0, 0, 15, 1, 255, .375f, 0x96},
   {"full-mixed", ALL, 0, 0, 0, 0, 0, 15, 1, 255, .375f, 0x69},
   {"scissor-depth", DEPTH, 1, 17, 29, WIDTH-54, HEIGHT-72, 15, 1, 255, .625f, 0x69},
   {"scissor-stencil-low", STENCIL, 1, 17, 29, WIDTH-54, HEIGHT-72, 15, 1, 15, .375f, 0x69},
   {"scissor-mixed-high", ALL, 1, 17, 29, WIDTH-54, HEIGHT-72, 6, 1, 240, .625f, 0x69},
   {"depth-write-disabled", ALL, 1, 17, 29, WIDTH-54, HEIGHT-72, 9, 0, 0xa5, .375f, 0x69},
   {"all-write-disabled", ALL, 0, 0, 0, 0, 0, 0, 0, 0, .375f, 0x69},
   {"full-color", COLOR, 0, 0, 0, 0, 0, 15, 1, 255, .375f, 0x69},
   {"scissor-color", COLOR, 1, 17, 29, WIDTH-54, HEIGHT-72, 15, 1, 255, .375f, 0x69},
   {"scissor-color-mask", COLOR, 1, 17, 29, WIDTH-54, HEIGHT-72, 10, 1, 255, .375f, 0x69},
   {"empty-scissor", ALL, 1, 17, 29, 0, HEIGHT, 15, 1, 255, .375f, 0x69},
   {"clipped-scissor", ALL, 1, -17, HEIGHT-43, 127, 95, 15, 1, 255, .375f, 0x69},
};

static int healthy(const char *where, unsigned *calls)
{
   unsigned ignored = 0;
   if (!calls) calls = &ignored;
   GLenum error = glGetError();
   int status = ps5_egl_current_draw_status(calls);
   if (error || status)
      printf(TAG " stage=%s error=0x%x driver=%d draws=%u\n", where, error, status, *calls);
   return !error && !status;
}

static uint64_t now_ns(void)
{
   struct timespec t;
   return clock_gettime(CLOCK_MONOTONIC, &t) ? 0 :
      (uint64_t)t.tv_sec * UINT64_C(1000000000) + t.tv_nsec;
}

static GLuint make_program(void)
{
   const char *sources[] = {
      "#version 330 core\nlayout(location=0) in vec2 p; uniform float z;\n"
      "void main(){gl_Position=vec4(p,z,1);}\n",
      "#version 330 core\nuniform vec4 tint; out vec4 color;\n"
      "void main(){color=tint;}\n"
   };
   GLuint program = glCreateProgram();
   if (!program) return 0;
   for (unsigned i = 0; i < 2; ++i) {
      GLuint shader = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
      GLint ok = 0;
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
   GLint ok = 0;
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   if (!ok) {
      char log[512] = {0};
      glGetProgramInfoLog(program, sizeof(log), NULL, log);
      printf(TAG " link log=%s\n", log);
      glDeleteProgram(program);
      return 0;
   }
   return program;
}

/* RGBA8 values quantize to exact bytes; float values are exact half-floats,
 * including negative/HDR components to catch accidental UNORM clamping. */
static void color_value(int floating, unsigned value, float color[4])
{
   static const uint8_t bytes[3][4] = {{51, 102, 153, 204}, {204, 153, 102, 51}, {17, 85, 187, 221}};
   static const float floats[3][4] = {{-.5f, .25f, 1.5f, .75f}, {2, -.25f, .5f, 1}, {1.25f, -.5f, 2, .25f}};
   for (unsigned c = 0; c < 4; ++c)
      color[c] = floating ? floats[value][c] : bytes[value][c] / 255.0f;
}

static int seed(GLint z, GLint tint, int stencil, int floating)
{
   glDisable(GL_SCISSOR_TEST);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glDepthMask(GL_TRUE);
   glStencilMask(0xff);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_ALWAYS);
   if (stencil) glEnable(GL_STENCIL_TEST);
   else glDisable(GL_STENCIL_TEST);
   glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
   for (unsigned half = 0; half < 2; ++half) {
      float color[4];
      color_value(floating, half, color);
      glViewport(half * (WIDTH/2), 0, WIDTH/2, HEIGHT);
      glStencilFunc(GL_ALWAYS, half ? 0x3c : 0xa5, 0xff);
      glUniform1f(z, half ? .5f : -.5f);
      glUniform4fv(tint, 1, color);
      if (!healthy("seed-state", NULL)) return 0;
      glDrawArrays(GL_TRIANGLES, 0, 3);
      if (!healthy("seed-draw", NULL)) return 0;
   }
   glFinish();
   return healthy("seed-finish", NULL);
}

static void clear_state(const struct clear_case *test, int floating)
{
   float color[4];
   color_value(floating, 2, color);
   if (test->scissor) {
      glEnable(GL_SCISSOR_TEST);
      glScissor(test->x, test->y, test->w, test->h);
   } else glDisable(GL_SCISSOR_TEST);
   glColorMask(!!(test->color_mask & 1), !!(test->color_mask & 2),
               !!(test->color_mask & 4), !!(test->color_mask & 8));
   glDepthMask(test->depth_mask);
   /* Clear uses the front write mask even when the back mask disagrees. */
   glStencilMaskSeparate(GL_FRONT, test->stencil_mask);
   glStencilMaskSeparate(GL_BACK, test->stencil_mask ^ 0xff);
   glClearColor(color[0], color[1], color[2], color[3]);
   glClearDepth(test->depth);
   glClearStencil(test->stencil);
}

static int oracle(const struct clear_case *test, GLbitfield available,
                  int floating, void *pixels, int redrawn)
{
   float colors[3][4];
   for (unsigned i = 0; i < 3; ++i) color_value(floating, i, colors[i]);
   const GLbitfield planes[] = {DEPTH, STENCIL, COLOR};
   for (unsigned plane = 0; plane < 3; ++plane) {
      GLbitfield bit = planes[plane];
      if (!(available & bit)) continue;
      int bytes = bit == STENCIL || (bit == COLOR && !floating);
      unsigned channels = bit == COLOR ? 4 : 1;
      glReadPixels(0, 0, WIDTH, HEIGHT,
                   bit == COLOR ? GL_RGBA : bit == DEPTH ? GL_DEPTH_COMPONENT : GL_STENCIL_INDEX,
                   bytes ? GL_UNSIGNED_BYTE : GL_FLOAT, pixels);
      if (!healthy("readback", NULL)) return 0;
      for (int y = 0; y < HEIGHT; ++y) for (int x = 0; x < WIDTH; ++x) {
         unsigned half = x >= WIDTH/2;
         int inside = !test->scissor || (x >= test->x && x < test->x + test->w &&
                                         y >= test->y && y < test->y + test->h);
         int changed = inside && (test->buffers & bit) && !(redrawn && half);
         for (unsigned c = 0; c < channels; ++c) {
            float expected;
            if (bit == COLOR) {
               expected = colors[changed && (test->color_mask & (1u << c)) ? 2 : half][c];
               if (bytes) expected = (unsigned)(expected * 255.0f + .5f);
            } else if (bit == DEPTH) {
               expected = changed && test->depth_mask ? test->depth : half ? .75f : .25f;
            } else {
               unsigned old = half ? 0x3c : 0xa5;
               expected = changed ? (old & (~test->stencil_mask & 0xff)) |
                                    (test->stencil & test->stencil_mask) : old;
            }
            size_t index = ((size_t)y * WIDTH + x) * channels + c;
            float actual = bytes ? ((uint8_t *)pixels)[index] : ((float *)pixels)[index];
            /* All floating expected values are exactly representable; != also rejects NaNs. */
            if (actual != expected) {
               printf(TAG " mismatch case=%s plane=%s xy=%d/%d c=%u got=%.9g want=%.9g redrawn=%d\n",
                      test->name, bit == COLOR ? "color" : bit == DEPTH ? "depth" : "stencil",
                      x, y, c, actual, expected, redrawn);
               return 0;
            }
         }
      }
   }
   return 1;
}

static int time_depth(GLbitfield available, int floating, void *pixels, int scissored)
{
   struct clear_case test = cases[scissored ? 4 : 0];
   test.name = scissored ? "timed-scissored-depth" : "timed-full-depth";
   unsigned before = 0, after = 0;
   clear_state(&test, floating);
   if (!healthy("timing-state", NULL)) return 0;
   glClear(DEPTH); glFinish(); /* Warm-up is outside the interval. */
   if (!healthy("timing-warmup", &before)) return 0;
   uint64_t start = now_ns(), elapsed = 0;
   if (!start) return 0;
   const unsigned repeats = 8;
   for (unsigned i = 0; i < repeats; ++i) {
      test.depth = i & 1 ? .875f : .125f;
      glClearDepth(test.depth);
      glClear(DEPTH);
      glFinish(); /* Measure completed work; no seed/upload/readback in the interval. */
      if (!healthy("timing-clear", &after)) return 0;
      uint64_t end = now_ns();
      if (end <= start || end - start > UINT64_C(5000000000)) {
         printf(TAG " timing clock/deadline failure\n");
         return 0;
      }
      elapsed = end - start;
   }
   if (after < before || !oracle(&test, available, floating, pixels, 0)) return 0;
   printf(TAG " timing depth=%s mode=%s size=%dx%d effective=%dx%d clears=%u elapsed_ns=%llu ms_per_clear=%.3f draws=%u->%u delta=%u result=0\n",
          available & STENCIL ? "D32S8" : "D32", scissored ? "scissored" : "full-control",
          WIDTH, HEIGHT, scissored ? test.w : WIDTH, scissored ? test.h : HEIGHT, repeats,
          (unsigned long long)elapsed, (double)elapsed / repeats / 1e6, before, after, after-before);
   return 1;
}

static int run_target(int stencil, GLenum color_format, GLint z, GLint tint)
{
   GLuint fbo = 0, textures[2] = {0};
   int passed = 0, floating = color_format == GL_RGBA16F;
   GLbitfield available = DEPTH | (stencil ? STENCIL : 0) | (color_format ? COLOR : 0);
   /* One reusable readback buffer (<=12 MiB); one live pair of 2D textures.
    * No multisample/layered allocation, accumulated targets or stack images. */
   size_t bytes = (size_t)WIDTH * HEIGHT * sizeof(float) * (floating ? 4 : 1);
   void *pixels = malloc(bytes);
   if (!pixels) { printf(TAG " readback allocation failed\n"); return 0; }
   glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glGenTextures(2, textures);
   for (unsigned i = 0; i < 2; ++i) {
      if (i && !color_format) continue;
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexImage2D(GL_TEXTURE_2D, 0, i ? color_format : stencil ? GL_DEPTH32F_STENCIL8 : GL_DEPTH_COMPONENT32F,
                   WIDTH, HEIGHT, 0, i ? GL_RGBA : stencil ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT,
                   i ? GL_FLOAT : stencil ? GL_FLOAT_32_UNSIGNED_INT_24_8_REV : GL_FLOAT, NULL);
      glFramebufferTexture2D(GL_FRAMEBUFFER, i ? GL_COLOR_ATTACHMENT0 :
                              stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
                             GL_TEXTURE_2D, textures[i], 0);
   }
   glDrawBuffer(color_format ? GL_COLOR_ATTACHMENT0 : GL_NONE);
   glReadBuffer(color_format ? GL_COLOR_ATTACHMENT0 : GL_NONE);
   GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   printf(TAG " target depth=%s color=%s size=%dx%d fbo=0x%x\n", stencil ? "D32S8" : "D32",
          floating ? "RGBA16F" : color_format ? "RGBA8" : "none", WIDTH, HEIGHT, status);
   if (!healthy("target", NULL) || status != GL_FRAMEBUFFER_COMPLETE) goto done;
   for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
      const struct clear_case *test = &cases[i];
      unsigned before = 0, after = 0;
      if (!(test->buffers & available)) continue;
      if (!seed(z, tint, stencil, floating)) goto done;
      clear_state(test, floating);
      if (!healthy("clear-state", &before)) goto done;
      glClear(test->buffers & available);
      glFinish();
      if (!healthy(test->name, &after) || after < before ||
          !oracle(test, available, floating, pixels, 0)) goto done;
      printf(TAG " case=%s buffers=0x%x pixels=%u draws=%u->%u delta=%u result=0\n",
             test->name, test->buffers & available, WIDTH * HEIGHT, before, after, after-before);
      if (i == 0 || i == 4) {
         /* Reuse the right-half viewport, shader, uniforms, VAO and depth/
          * stencil state immediately after the internal clear, without rebinding. */
         glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
         if (!healthy("state-reuse", NULL) || !oracle(test, available, floating, pixels, 1)) goto done;
         printf(TAG " state-reuse result=0\n");
      }
   }
   /* Compare completed full-depth control and large scissored work on the
    * same level-0, single-layer, single-sample 2D target. Full depth may use
    * CPU memset; neither lane requires GPU draws or deferred submissions. */
   if (!color_format) for (int scissored = 0; scissored < 2; ++scissored)
      if (!seed(z, tint, stencil, floating) ||
          !time_depth(available, floating, pixels, scissored)) goto done;
   passed = 1;
done:
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindTexture(GL_TEXTURE_2D, 0);
   glDeleteFramebuffers(1, &fbo); glDeleteTextures(2, textures);
   free(pixels);
   return healthy("target-cleanup", NULL) && passed;
}

int main(void)
{
   const EGLint config_attrs[] = {EGL_SURFACE_TYPE, SURFACE_TYPE, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
   const EGLint context_attrs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE};
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL;
   EGLint count = 0;
   GLuint program = 0, vao = 0, vbo = 0;
   int current = 0, initialized = 0, passed = 0;
   EGLBoolean cleanup = EGL_TRUE;
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL)) goto done;
   initialized = 1;
   if (!eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attrs, &config, 1, &count) || count != 1) goto done;
#ifdef PS5_GPU_CLEAR_EXTENDED_HOST_REFERENCE
   const EGLint pb[] = {EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE};
   surface = eglCreatePbufferSurface(display, config, pb);
#else
   surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, NULL);
#endif
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context)) goto done;
   current = 1;
   printf(TAG " renderer=%s draw_counter=%s\n", (const char *)glGetString(GL_RENDERER),
#ifdef PS5_GPU_CLEAR_EXTENDED_HOST_REFERENCE
          "unavailable-host-reference"
#else
          "native-driver"
#endif
   );
   program = make_program();
   if (!program) goto done;
   glUseProgram(program);
   GLint z = glGetUniformLocation(program, "z"), tint = glGetUniformLocation(program, "tint");
   if (z < 0 || tint < 0) goto done;
   const float vertices[] = {-1, -1, 3, -1, -1, 3};
   glGenVertexArrays(1, &vao); glBindVertexArray(vao);
   glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL); glEnableVertexAttribArray(0);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glDisable(GL_DITHER);
   if (!healthy("setup", NULL)) goto done;
   if (!run_target(0, 0, z, tint) || !run_target(1, 0, z, tint) ||
       !run_target(0, GL_RGBA8, z, tint) || !run_target(1, GL_RGBA16F, z, tint)) goto done;
   passed = 1;
done:
   if (current) {
      glUseProgram(0); glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
      glDeleteProgram(program); glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
      cleanup &= healthy("cleanup", NULL);
      cleanup &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (context != EGL_NO_CONTEXT) cleanup &= eglDestroyContext(display, context);
   if (surface != EGL_NO_SURFACE) cleanup &= eglDestroySurface(display, surface);
   if (initialized) cleanup &= eglTerminate(display);
   EGLint error = eglGetError();
   if (error != EGL_SUCCESS) printf(TAG " EGL error=0x%x\n", error);
   cleanup &= error == EGL_SUCCESS;
   printf(TAG " cleanup=%u result=%d\n", cleanup, passed && cleanup ? 0 : 1);
   return passed && cleanup ? 0 : 1;
}
