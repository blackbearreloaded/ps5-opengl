// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Reuse two bounded gates with their own EGL setup and pixel oracles.
 * One folder launch covers 24 mip cycles and 18 format/conversion checks. */
#define main transfer_workload_main
#include "egl_public_core33_transfer_workload.c"
#undef main
#define main render_format_blit_main
#include "egl_public_core33_render_format_blit.c"
#undef main

int
main(void)
{
   int result = transfer_workload_main();
   if (result)
      return result;
   if (ps5_opengl_heap_snapshot) ps5_opengl_heap_snapshot("begin", 0);
   result = render_format_blit_main();
   if (ps5_opengl_heap_snapshot) ps5_opengl_heap_snapshot("end", 0);
   printf("[ps5-egl-transfer-regressions] gates=2 result=%d\n", result);
   return result;
}
