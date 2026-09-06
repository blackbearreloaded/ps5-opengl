// TV-facing example, separate from the immutable six-frame renderer oracle.
// Only public GL/EGL, upstream ImGui, and a small native controller adapter.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <time.h>
#include <unistd.h>

#ifndef PS5_IMGUI_HOST_REFERENCE
// Minimal independently authored ABI subset from ps5-input-investigation's
// include/ps5_pad.hpp: 120-byte current-state record, no motion/touch access.
struct alignas(8) DemoPadState {
    uint32_t buttons;
    unsigned char unused0[72];
    int32_t connected;
    unsigned char unused1[40];
};
static_assert(sizeof(DemoPadState) == 120, "pad record size");
static_assert(offsetof(DemoPadState, connected) == 0x4c, "pad connection offset");
extern "C" {
int sceUserServiceInitialize(void*);
int sceUserServiceGetInitialUser(int*);
int sceUserServiceTerminate();
int scePadInit();
int scePadOpen(int, int, int, const void*);
int scePadReadState(int, DemoPadState*);
int scePadClose(int);
}
#endif

static double demo_seconds()
{
    timespec now = {};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1.0;
    return now.tv_sec + now.tv_nsec * 1e-9;
}

static void demo_buttons(uint32_t buttons)
{
    static const struct { uint32_t mask; ImGuiKey key; } keys[] = {
        {0x10, ImGuiKey_GamepadDpadUp}, {0x20, ImGuiKey_GamepadDpadRight},
        {0x40, ImGuiKey_GamepadDpadDown}, {0x80, ImGuiKey_GamepadDpadLeft},
        {0x4000, ImGuiKey_GamepadFaceDown}, {0x2000, ImGuiKey_GamepadFaceRight},
        {0x400, ImGuiKey_GamepadL1}, {0x800, ImGuiKey_GamepadR1}
    };
    for (const auto& key : keys)
        ImGui::GetIO().AddKeyEvent(key.key, (buttons & key.mask) != 0);
}

static bool render_frames(EGLDisplay display, EGLSurface surface)
{
    EGLint width = 0, height = 0;
    if (!check(eglQuerySurface(display, surface, EGL_WIDTH, &width) &&
               eglQuerySurface(display, surface, EGL_HEIGHT, &height) &&
               width == 1920 && height == 1080, "TV surface 1920x1080"))
        return false;

    int pad = -1;
    bool owns_user_service = false;
#ifndef PS5_IMGUI_HOST_REFERENCE
    owns_user_service = sceUserServiceInitialize(nullptr) == 0;
    int user = -1;
    if (sceUserServiceGetInitialUser(&user) == 0 && scePadInit() >= 0)
        pad = scePadOpen(user, 0, 0, nullptr);
    printf("[ps5-imgui-tv] controller handle=%d\n", pad);
#else
    (void)pad;
    (void)owns_user_service;
#endif
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    bool ok = true, animate = true;
    float speed = 1.0f, phase = 0.0f;
    int palette = 0, changes = 0;
    unsigned frame = 0;
#ifdef PS5_IMGUI_PROFILE
    const double duration = 30.0;
    double totals[5] = {};
    unsigned measured = 0;
#ifdef PS5_IMGUI_HOST_REFERENCE
    const unsigned warmup = 2;
#else
    const unsigned warmup = 30;
#endif
#else
    const double duration = 300.0;
#endif
    double start = demo_seconds(), previous = start, next_log = 0;
    ok = check(start >= 0, "monotonic clock");
    // ponytail: five-minute demo, not a permanent shell/input platform backend.
    // Reuse a real application's event loop for long-lived application ports.
    while (ok) {
        double now = demo_seconds();
        if (!check(now >= previous, "monotonic frame clock")) { ok = false; break; }
#ifdef PS5_IMGUI_HOST_REFERENCE
        if (frame == 12) break;
        const double elapsed = frame / 30.0;
        io.DeltaTime = 1.0f / 30.0f;
        const bool connected = true;
        // Exercise the real ImGui navigation path: toggle the focused checkbox.
        demo_buttons(frame == 4 || frame == 8 ? 0x4000 : 0);
#else
        const double elapsed = now - start;
        if (elapsed >= duration) break;
        io.DeltaTime = static_cast<float>(now - previous);
        if (io.DeltaTime < 0.001f) io.DeltaTime = 0.001f;
        if (io.DeltaTime > 0.1f) io.DeltaTime = 0.1f;
        DemoPadState state = {};
        const bool connected = pad >= 0 && scePadReadState(pad, &state) >= 0 &&
                               state.connected && !(state.buttons & 0x80000000u);
        demo_buttons(connected ? state.buttons : 0);
#endif
        previous = now;
        if (animate) phase += io.DeltaTime * speed;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(80, 60));
        ImGui::SetNextWindowSize(ImVec2(1760, 960));
        ImGui::Begin("PS5 OpenGL TV demo", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(1.5f);
        ImGui::TextColored(ImVec4(0.25f, 0.9f, 1, 1), "HELLO, PS5!");
        ImGui::SetWindowFontScale(1);
        ImGui::TextUnformatted("Dear ImGui rendered through our OpenGL 3.3 Core port");
        ImGui::Text("Renderer: %s", glGetString(GL_RENDERER));
        ImGui::Separator();
        ImGui::TextUnformatted("D-pad: navigate / adjust     Cross: select     Circle: back");
        ImGui::Text("Controller: %s     Frame: %u     %.1f FPS     %ds left",
                    connected ? "connected" : "not available - animation continues",
                    frame, io.Framerate, static_cast<int>(duration - elapsed));
        if (frame == 0) ImGui::SetKeyboardFocusHere();
        if (ImGui::Checkbox("Animate shapes", &animate)) ++changes;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(420);
        if (ImGui::SliderFloat("Speed", &speed, 0.2f, 3.0f, "%.1fx")) ++changes;
        if (ImGui::RadioButton("Cyan", &palette, 0)) ++changes;
        ImGui::SameLine();
        if (ImGui::RadioButton("Coral", &palette, 1)) ++changes;
        ImGui::SameLine();
        if (ImGui::RadioButton("Gold", &palette, 2)) ++changes;
        ImGui::SameLine();
        ImGui::Text("Widget changes: %d", changes);
        ImGui::Spacing();

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float canvas_width = ImGui::GetContentRegionAvail().x;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + canvas_width, origin.y + 410),
                            IM_COL32(8, 15, 28, 255), 18);
        const ImU32 colors[] = {IM_COL32(45, 215, 245, 255), IM_COL32(255, 115, 95, 255),
                                IM_COL32(255, 210, 65, 255)};
        const float x = origin.x + canvas_width * 0.5f + std::sin(phase) * canvas_width * 0.3f;
        draw->AddCircleFilled(ImVec2(x, origin.y + 135), 65, colors[palette], 48);
        draw->AddRectFilled(ImVec2(origin.x + 70, origin.y + 270),
                            ImVec2(origin.x + 310, origin.y + 355), IM_COL32(130, 100, 255, 255), 16);
        draw->AddRectFilled(ImVec2(origin.x + 180, origin.y + 285),
                            ImVec2(origin.x + 420, origin.y + 370), IM_COL32(245, 110, 170, 160), 16);
        draw->AddTriangleFilled(ImVec2(origin.x + canvas_width - 200, origin.y + 235),
                                ImVec2(origin.x + canvas_width - 285, origin.y + 365),
                                ImVec2(origin.x + canvas_width - 115, origin.y + 365), colors[palette]);
        ImGui::Dummy(ImVec2(canvas_width, 430));
        ImGui::TextUnformatted("Live geometry, font textures, alpha blending, and interactive widgets.");
        ImGui::ProgressBar((std::sin(phase) + 1.0f) * 0.5f, ImVec2(-1, 30), "OpenGL + ImGui");
        ImGui::Text("Native app: PPSA99005  |  1920 x 1080  |  bounded %.0f-second run", duration);
        ImGui::End();
        ImGui::Render();
#ifdef PS5_IMGUI_PROFILE
        double stages[6] = {now, demo_seconds(), 0, 0, 0, 0};
#endif

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.035f, 0.047f, 0.09f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
#ifdef PS5_IMGUI_PROFILE
        stages[2] = demo_seconds();
#endif
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#ifdef PS5_IMGUI_PROFILE
        stages[3] = demo_seconds();
#endif
        if (!check(glGetError() == GL_NO_ERROR, "TV draw")) { ok = false; break; }
        if (frame == 0 || frame == 10) {
            unsigned char p[4] = {};
            glReadPixels(static_cast<int>(x), height - 1 - static_cast<int>(origin.y + 135),
                         1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
            const ImU32 color = colors[palette];
            ok = check(glGetError() == GL_NO_ERROR && abs(p[0] - static_cast<int>(color & 255)) <= 2 &&
                       abs(p[1] - static_cast<int>((color >> 8) & 255)) <= 2 &&
                       abs(p[2] - static_cast<int>((color >> 16) & 255)) <= 2 && p[3] == 255,
                       "TV shape pixel");
            printf("[ps5-imgui-tv] readback frame=%u rgba=%u,%u,%u,%u %s\n",
                   frame, p[0], p[1], p[2], p[3], ok ? "PASS" : "FAIL");
        }
#ifdef PS5_IMGUI_PROFILE
        stages[4] = demo_seconds();
#endif
        if (!ok || !check(eglSwapBuffers(display, surface), "TV swap")) { ok = false; break; }
#ifdef PS5_IMGUI_PROFILE
        stages[5] = demo_seconds();
        for (unsigned i = 0; i < 5; ++i) {
            ok = check(stages[i + 1] >= stages[i], "profile monotonic clock") && ok;
            if (frame >= warmup) totals[i] += stages[i + 1] - stages[i];
        }
        if (frame >= warmup) ++measured;
#endif
        if (elapsed >= next_log) {
            printf("[ps5-imgui-tv] visible frame=%u elapsed=%.1f pad=%d changes=%d vertices=%d\n",
                   frame, elapsed, connected, changes, ImGui::GetDrawData()->TotalVtxCount);
            next_log += 30.0;
        }
        ++frame;
#if !defined(PS5_IMGUI_HOST_REFERENCE) && !defined(PS5_IMGUI_PROFILE)
        const double spent = demo_seconds() - now;
        if (spent < 0) { ok = false; break; }
        if (spent < 1.0 / 30.0) usleep(static_cast<unsigned>((1.0 / 30.0 - spent) * 1e6));
#endif
    }
#ifdef PS5_IMGUI_PROFILE
    ok = check(measured >= 2 * warmup, "profile post-warm-up frames") && ok;
    if (measured) {
        const double scale = 1000.0 / measured;
        printf("[ps5-imgui-perf] frames=%u warmup=%u ui_ms=%.6f clear_ms=%.6f draw_ms=%.6f readback_ms=%.6f swap_ms=%.6f cpu_wall_ms=%.6f status=%d\n",
               measured, warmup, totals[0] * scale, totals[1] * scale,
               totals[2] * scale, totals[3] * scale, totals[4] * scale,
               (totals[0] + totals[1] + totals[2] + totals[3] + totals[4]) * scale, ok ? 0 : 1);
    }
#endif
#ifdef PS5_IMGUI_HOST_REFERENCE
    ok = check(frame == 12 && changes == 2 && animate, "TV gamepad toggles / bounded loop") && ok;
#else
    if (pad >= 0) ok = check(scePadClose(pad) == 0, "TV pad close") && ok;
    if (owns_user_service) ok = check(sceUserServiceTerminate() == 0, "TV user service close") && ok;
#endif
    io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    printf("[ps5-imgui-tv] finished frames=%u changes=%d status=%d\n", frame, changes, ok ? 0 : 1);
    return ok;
}
