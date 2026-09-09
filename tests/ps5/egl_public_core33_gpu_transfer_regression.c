// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
int ps5_transfer_blit_main(void);
int ps5_transfer_clear_main(void);
int ps5_transfer_formats_main(void);

int main(void)
{
   int (*const tests[])(void) = {
      ps5_transfer_blit_main, ps5_transfer_clear_main, ps5_transfer_formats_main
   };
   const char *const names[] = {"blit", "clear", "formats"};
   for (unsigned i = 0; i < 3; ++i) {
      printf("[gpu-transfer-regression] begin=%s\n", names[i]);
      if (tests[i]()) {
         printf("[gpu-transfer-regression] completed=%u/3 result=1\n", i);
         return 1;
      }
   }
   puts("[gpu-transfer-regression] completed=3/3 result=0");
   return 0;
}
