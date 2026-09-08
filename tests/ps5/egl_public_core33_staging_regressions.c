// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Keep the independent public-API oracles; one launch runs both gates. */
#define _POSIX_C_SOURCE 200809L
#define main format_gate_main
#define run_case format_run_case
#include "egl_public_core33_render_format_float.c"
#undef main
#undef run_case
#undef WIDTH
#undef HEIGHT
#define main staging_profile_main
#include "egl_public_core33_staging_profile.c"
#undef main

int
main(void)
{
   int result = format_gate_main();
   if (result)
      return result;
   result = staging_profile_main();
   printf("[ps5-egl-staging-regressions] gates=2 result=%d\n", result);
   return result;
}
