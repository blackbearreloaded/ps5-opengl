// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Optional matched workload; included after the shared draw/pixel helpers. */
#ifndef PS5_CUBES_OBJECTS
#define PS5_CUBES_OBJECTS 128
#endif
#ifndef PS5_CUBES_SECONDS
#define PS5_CUBES_SECONDS 30
#endif
#ifndef PS5_CUBES_SWAP_COMPLETED
#define PS5_CUBES_SWAP_COMPLETED 0
#endif
#if PS5_CUBES_OBJECTS != 32 && PS5_CUBES_OBJECTS != 128 && PS5_CUBES_OBJECTS != 512
#error PS5_CUBES_OBJECTS must be 32, 128 or 512
#endif
#if PS5_CUBES_SECONDS < 1 || PS5_CUBES_SECONDS > 60
#error PS5_CUBES_SECONDS must be an integer from 1 to 60
#endif
#if PS5_CUBES_SWAP_COMPLETED != 0 && PS5_CUBES_SWAP_COMPLETED != 1
#error PS5_CUBES_SWAP_COMPLETED must be 0 or 1
#endif
#if !defined(PS5_CUBES_HOST_REFERENCE) && !defined(PS5_NATIVE_CUBES_TEST)
#error Native profiles require PS5_NATIVE_CUBES_TEST for post-swap driver status checks
#endif

enum { PROFILE_WARMUP = 30, PROFILE_MAX_FRAMES = 16384 };
struct profile_sample {
   int64_t clear_ns, submit_ns, finish_ns, swap_ns, total_ns, interval_ns;
   unsigned native_draws;
};
/* ponytail: fixed 16384-frame ceiling; fail rather than truncate, raise if needed. */
static struct profile_sample profile_samples[PROFILE_MAX_FRAMES];

static int profile_swap(EGLDisplay display, EGLSurface surface, unsigned *draws)
{
   if (!check(eglSwapBuffers(display, surface), "profile present")) return 0;
#ifdef PS5_CUBES_HOST_REFERENCE
   /* A host pbuffer swap need not retire draws. Included in host swap_ns. */
   glFinish();
#endif
#ifdef PS5_NATIVE_CUBES_TEST
   if (!check(ps5_egl_current_draw_status(draws) == 0, "profile native completed draw")) return 0;
#else
   *draws = 0;
#endif
   return check(glGetError() == GL_NO_ERROR && eglGetError() == EGL_SUCCESS,
                "profile completed errors");
}

static int profile_draw_delta(unsigned current, unsigned *previous, unsigned expected)
{
   if (!check(current >= *previous && current - *previous == expected,
              "profile native per-frame draw count")) return 0;
   *previous = current;
   return 1;
}

static int profile_frame(EGLDisplay display, EGLSurface surface, unsigned mode,
                         unsigned frame, int64_t previous, int64_t *end,
                         struct profile_sample *sample, unsigned *previous_draw,
                         unsigned draws_per_frame)
{
   int64_t t[3];
   /* Frame-indexed angle gives ordinary and instanced draws the same scene. */
   draw_commands(PS5_CUBES_OBJECTS, mode, .2f + frame * .075f, t);
   int64_t finished = t[2];
#if !PS5_CUBES_SWAP_COMPLETED
   glFinish();
   finished = now_ns();
#endif
   unsigned current_draw;
   if (!profile_swap(display, surface, &current_draw)) return 0;
   *end = now_ns();
   sample->native_draws = current_draw - *previous_draw;
   if (!profile_draw_delta(current_draw, previous_draw, draws_per_frame)) return 0;
   if (!check(t[0] > 0 && t[1] > t[0] && t[2] > t[1] && finished >= t[2] &&
              *end > finished && previous > 0 && t[0] >= previous,
              "profile monotonic clock")) return 0;
   sample->clear_ns = t[1] - t[0];
   sample->submit_ns = t[2] - t[1]; /* Draw API wall time, not pure CPU execution. */
   sample->finish_ns = finished - t[2];
   sample->swap_ns = *end - finished;
   sample->total_ns = *end - t[0];
   sample->interval_ns = *end - previous;
#if !PS5_CUBES_SWAP_COMPLETED
   if (!check(sample->finish_ns > 0, "profile finish clock")) return 0;
#endif
   return 1;
}

static int profile(EGLDisplay display, EGLSurface surface, unsigned *completed)
{
   const unsigned objects = PS5_CUBES_OBJECTS;
   const int64_t target_ns = (int64_t)PS5_CUBES_SECONDS * 1000000000;
#ifdef PS5_CUBES_HOST_REFERENCE
   const unsigned host = 1;
#else
   const unsigned host = 0;
#endif
   printf("[ps5-cubes-profile] config version=2 width=%u height=%u objects=%u modes=2"
          " warmup=%u seconds=%u swap_completed=%u host=%u max_frames=%u\n",
          WIDTH, HEIGHT, objects, PROFILE_WARMUP, PS5_CUBES_SECONDS,
          PS5_CUBES_SWAP_COMPLETED, host, PROFILE_MAX_FRAMES);
   float positions[PS5_CUBES_OBJECTS][4];
   for (unsigned i = 0; i < objects; ++i) object_position(objects, i, positions[i]);
   glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
   for (unsigned mode = 0; mode < 2; ++mode) {
      unsigned first_draw = 0, last_draw = 0;
#ifdef PS5_NATIVE_CUBES_TEST
      if (!check(ps5_egl_current_draw_status(&first_draw) == 0,
                 "profile native initial status")) return 0;
#endif
      int64_t t[3];
      if (!draw(objects, mode, 0, t) || !oracle(objects, mode) ||
          !profile_swap(display, surface, &last_draw)) return 0;
      /* Calibrate on the completed, excluded oracle frame. GPU color clear adds
       * one internal draw; CPU clear adds none. Require this exact delta on
       * every subsequent completed frame, with no mid-frame status query. */
      const unsigned public_draws = mode ? 1 : objects;
      const unsigned draws_per_frame = last_draw - first_draw;
      const char *clear_path = "host";
#ifdef PS5_NATIVE_CUBES_TEST
      if (!check(last_draw >= first_draw &&
                 (draws_per_frame == public_draws || draws_per_frame == public_draws + 1),
                 "profile native clear calibration")) return 0;
      clear_path = draws_per_frame == public_draws ? "cpu" : "gpu";
#endif
      printf("[ps5-cubes-profile] calibration mode=%u objects=%u public_draws=%u"
             " native_draws_per_frame=%u clear_path=%s\n",
             mode, objects, public_draws, draws_per_frame, clear_path);
      unsigned previous_draw = last_draw;
      int64_t previous = now_ns(), end = 0;
      struct profile_sample warmup;
      for (unsigned frame = 0; frame < PROFILE_WARMUP; ++frame) {
         if (!profile_frame(display, surface, mode, frame, previous, &end, &warmup,
                            &previous_draw, draws_per_frame)) return 0;
         previous = end;
      }
      /* Anchor at the last warmup completion; do not log during measurement. */
      int64_t start = previous;
      unsigned frames = 0;
      while (previous - start < target_ns) {
         if (!check(frames < PROFILE_MAX_FRAMES, "profile sample capacity")) return 0;
         if (!profile_frame(display, surface, mode, PROFILE_WARMUP + frames,
                            previous, &end, &profile_samples[frames],
                            &previous_draw, draws_per_frame)) return 0;
         previous = end;
         ++frames;
      }
      if (!draw(objects, mode, 0, t) || !oracle(objects, mode) ||
          !profile_swap(display, surface, &last_draw) ||
          !profile_draw_delta(last_draw, &previous_draw, draws_per_frame)) return 0;
      const unsigned native_draws = last_draw - first_draw;
      if (!check(native_draws == (PROFILE_WARMUP + frames + 2) * draws_per_frame,
                 "profile native draw count")) return 0;
      for (unsigned frame = 0; frame < frames; ++frame) {
         const struct profile_sample *s = &profile_samples[frame];
         printf("[ps5-cubes-profile] frame mode=%u objects=%u frame=%u clear_ns=%lld"
                " submit_ns=%lld finish_ns=%lld swap_ns=%lld total_ns=%lld interval_ns=%lld native_draws=%u\n",
                mode, objects, frame, (long long)s->clear_ns, (long long)s->submit_ns,
                (long long)s->finish_ns, (long long)s->swap_ns, (long long)s->total_ns,
                (long long)s->interval_ns, s->native_draws);
      }
      printf("[ps5-cubes-profile] mode mode=%u objects=%u frames=%u measured_ns=%lld native_draws=%u\n",
             mode, objects, frames, (long long)(previous - start), native_draws);
      ++*completed;
   }
   return 1;
}
