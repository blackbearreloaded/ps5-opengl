// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "c11/threads.h"

/*
 * The payload SDK declares open_memstream(), but its libc stubs do not export
 * it (nor funopen(), fopencookie(), fmemopen(), or tmpfile()).  ACO uses a
 * memory stream only to assemble optional diagnostics.  Returning ENOSYS is
 * therefore sufficient for normal compilation and keeps the missing feature
 * explicit.  Error paths that require a formatted diagnostic must be hardened
 * before this compiler is exposed as a production GL implementation.
 */
FILE *
open_memstream(char **bufp, size_t *sizep)
{
   if (bufp)
      *bufp = NULL;
   if (sizep)
      *sizep = 0;
   errno = ENOSYS;
   return NULL;
}

/* The payload SDK currently has no public CPU-affinity API. */
__attribute__((weak)) bool
util_set_thread_affinity(thrd_t thread, const uint32_t *mask,
                         uint32_t *old_mask, unsigned num_mask_bits)
{
   (void)thread;
   (void)mask;
   if (old_mask)
      memset(old_mask, 0, (num_mask_bits + 7u) / 8u);
   return false;
}
