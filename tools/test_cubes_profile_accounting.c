// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Exercise the real profile loop with a deterministic clock and native counter.
 * Mock accounting only: this is not evidence of GPU retirement or performance. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#define PS5_NATIVE_CUBES_TEST 1
#define PS5_CUBES_SECONDS 1
typedef int EGLDisplay;
typedef int EGLSurface;
enum { WIDTH = 1920, HEIGHT = 1080, GL_NO_ERROR = 0, EGL_SUCCESS = 0,
       GL_ARRAY_BUFFER = 1, GL_STATIC_DRAW = 2 };
static unsigned counter, calls, clear_draws, fault_at, status_at;
static int fault_delta, status_failure;
static int64_t clock_ns;
static int check(int ok, const char *stage) { (void)stage; return ok; }
static int64_t now_ns(void) { return clock_ns += 100000000; }
static int eglSwapBuffers(EGLDisplay d, EGLSurface s) { (void)d; (void)s; return 1; }
static int glGetError(void) { return GL_NO_ERROR; }
static int eglGetError(void) { return EGL_SUCCESS; }
static void glFinish(void) {}
static int ps5_egl_current_draw_status(unsigned *out)
{
   if (out) *out = counter;
   return status_failure && calls == status_at ? -1 : 0;
}
static void object_position(unsigned n, unsigned i, float *p)
{ (void)n; (void)i; for (unsigned j = 0; j < 4; ++j) p[j] = 0; }
static void glBufferData(int target, size_t size, const void *data, int usage)
{ (void)target; (void)size; (void)data; (void)usage; }
static void draw_commands(unsigned n, unsigned mode, float angle, int64_t t[3])
{
   (void)angle;
   for (unsigned i = 0; i < 3; ++i) t[i] = now_ns();
   counter += (mode ? 1 : n) + clear_draws;
   if (++calls == fault_at) counter += fault_delta;
}
static int draw(unsigned n, unsigned mode, float angle, int64_t t[3])
{ draw_commands(n, mode, angle, t); glFinish(); return 1; }
static int oracle(unsigned n, unsigned mode) { (void)n; (void)mode; return 1; }
#include "../examples/core33-cubes/profile.h"

static void run(unsigned clear, unsigned fault, int delta, int fail_status,
                unsigned initial, int expected)
{
   counter = initial; calls = 0; clock_ns = 0; clear_draws = clear;
   fault_at = fault; fault_delta = delta; status_at = fault; status_failure = fail_status;
   unsigned completed = 0;
   assert(profile(1, 1, &completed) == expected);
   assert(expected ? completed == 2 : completed < 2);
}
int main(void)
{
   /* 0.4s completed frames in swap mode, 0.5s in explicit-finish mode. */
   const unsigned frames = PS5_CUBES_SWAP_COMPLETED ? 3 : 2;
   const unsigned per_mode = PROFILE_WARMUP + frames + 2;
   for (unsigned clear = 0; clear <= 1; ++clear) {
      run(clear, 0, 0, 0, 17, 1);
      /* Every oracle, warmup and measured frame in BOTH draw modes must be
       * checked. Includes compensating path ambiguity during calibration. */
      for (unsigned frame = 1; frame <= 2 * per_mode; ++frame) {
         run(clear, frame, -1, 0, 17, 0);
         run(clear, frame, 1, 0, 17, 0);
         run(clear, frame, 0, 1, 17, 0);
      }
      run(clear, 0, 0, 0, UINT_MAX - 10, 0); /* Counter wrap at calibration. */
      run(clear, 0, 0, 0, UINT_MAX - 2 * (128 + clear), 0); /* Later wrap. */
   }
   run(2, 0, 0, 0, 17, 0); /* Neither legitimate clear path. */
   puts("profile native accounting mock: PASS (CPU/GPU clear, lost/extra draws, status, wrap)");
   return 0;
}
