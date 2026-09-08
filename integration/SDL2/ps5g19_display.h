/* Copyright (C) 2026 BlackBearReloaded; SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* Fixed render/presentation profile, not negotiated HDMI timing. */
#if __has_include(<ps5_opengl_display.h>)
#include <ps5_opengl_display.h>
#else
/* SDKs predating the profile header retain the explicit legacy 1080p60 mode. */
#define PS5_OPENGL_NATIVE_WIDTH 1920
#define PS5_OPENGL_NATIVE_HEIGHT 1080
#define PS5_OPENGL_NATIVE_FPS 60
#endif

#if !((PS5_OPENGL_NATIVE_WIDTH == 1920 && PS5_OPENGL_NATIVE_HEIGHT == 1080) || \
      (PS5_OPENGL_NATIVE_WIDTH == 2560 && PS5_OPENGL_NATIVE_HEIGHT == 1440) || \
      (PS5_OPENGL_NATIVE_WIDTH == 3840 && PS5_OPENGL_NATIVE_HEIGHT == 2160)) || \
    !(PS5_OPENGL_NATIVE_FPS == 60 || PS5_OPENGL_NATIVE_FPS == 120)
#error Unsupported PS5 OpenGL display profile
#endif
