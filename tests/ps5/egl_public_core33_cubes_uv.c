// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Reuse the cube scene, replacing only fragment color with UV/material values. */
#define PS5_CUBES_UV_DIAGNOSTIC 1
#ifndef PS5_CUBES_HOST_REFERENCE
#define PS5_NATIVE_CUBES_TEST 1
#endif
#define main cubes_uv_main
#include "../../examples/core33-cubes/main.c"
#undef main

int main(void)
{
#ifndef PS5_CUBES_HOST_REFERENCE
   if (setenv("PSBC_DEBUG_NIR", "1", 1) || setenv("PSBC_DEBUG_IO", "1", 1)) return 1;
#endif
   return cubes_uv_main();
}
