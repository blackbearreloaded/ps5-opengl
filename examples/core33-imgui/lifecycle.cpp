// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

// Reuse the complete six-frame oracle, including EGL teardown, in one process.
#define main imgui_session
#include "main.cpp"
#undef main
#include <unistd.h>

#ifndef PS5_IMGUI_LIFECYCLE_SETTLE_SECONDS
#define PS5_IMGUI_LIFECYCLE_SETTLE_SECONDS 0
#endif
static_assert(PS5_IMGUI_LIFECYCLE_SETTLE_SECONDS >= 0 &&
              PS5_IMGUI_LIFECYCLE_SETTLE_SECONDS <= 10, "bounded HDMI settle interval");

int main()
{
    for (unsigned session = 0; session < 3; ++session) {
        printf("[ps5-imgui-lifecycle] session=%u begin\n", session);
        if (imgui_session() != 0) return 1;
        printf("[ps5-imgui-lifecycle] session=%u PASS\n", session);
        if (session != 2) {
            // Default: immediate application recreation. The HFR runtime owns
            // its conservative reopen interval; this override is diagnostic only.
            printf("[ps5-imgui-lifecycle] settle_after=%u seconds=%u\n",
                   session, unsigned(PS5_IMGUI_LIFECYCLE_SETTLE_SECONDS));
            unsigned remaining = PS5_IMGUI_LIFECYCLE_SETTLE_SECONDS;
            while (remaining) remaining = sleep(remaining);
        }
    }
    printf("[ps5-imgui-lifecycle] finished sessions=3 status=0\n");
    return 0;
}
