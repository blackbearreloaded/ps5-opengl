// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

// Reuse the complete six-frame oracle, including EGL teardown, in one process.
#define main imgui_session
#include "main.cpp"
#undef main

int main()
{
    for (unsigned session = 0; session < 3; ++session) {
        printf("[ps5-imgui-lifecycle] session=%u begin\n", session);
        if (imgui_session() != 0) return 1;
        printf("[ps5-imgui-lifecycle] session=%u PASS\n", session);
    }
    printf("[ps5-imgui-lifecycle] finished sessions=3 status=0\n");
    return 0;
}
