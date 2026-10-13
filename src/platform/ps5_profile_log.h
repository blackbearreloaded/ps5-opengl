// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PS5_PROFILE_LOG_H
#define PS5_PROFILE_LOG_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Statistics printed while the application draws are opt-in. A line costs the
 * thread that prints it whatever the application's stdout costs: where a log
 * file is written line by line, the thirty lines of one report every 10,000
 * draws held a frame for 180 ms. PS5_DRAW_PROFILE_LOG=1, set before the first
 * draw, prints them as before. The counters run either way, and the reports
 * at teardown do not ask. */
static inline bool
ps5_profile_log(void)
{
   static int enabled = -1;
   int value = __atomic_load_n(&enabled, __ATOMIC_RELAXED);

   if (value < 0) {
      const char *text = getenv("PS5_DRAW_PROFILE_LOG");

      value = text && !strcmp(text, "1");
      __atomic_store_n(&enabled, value, __ATOMIC_RELAXED);
   }
   return value;
}

#endif
