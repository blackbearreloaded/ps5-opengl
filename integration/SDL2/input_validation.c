/* Copyright (C) 2026 BlackBearReloaded; SPDX-License-Identifier: GPL-3.0-or-later */
#include <SDL.h>
#include <GL/glcorearb.h>
#include <stdio.h>

#ifndef G42_TIMEOUT_MS
#define G42_TIMEOUT_MS 120000
#endif
#ifdef G42_HOST_CONTRACT
#define INPUT_SOURCE "host-platform-double"
#else
#define INPUT_SOURCE "native-driver-candidate"
#endif

/* ponytail: one neutral controller, Cross and left-stick X; extend only after
 * this bounded physical sequence is independently observed on hardware. */
int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_GLContext context = NULL;
    SDL_Joystick *pad = NULL;
    SDL_JoystickID previous = -1, instance = -1;
    SDL_JoystickGUID identity = {{0}};
    SDL_DisplayMode mode;
    Uint64 start = 0;
    unsigned frames = 0, connections = 0, removals = 0, rejected = 0;
    int status = 1, down = 0, up = 0, moved = 0, centered = 0, ready = 0, width, height;
    const char *reason = "initialization";
    PFNGLVIEWPORTPROC viewport;
    PFNGLCLEARCOLORPROC color;
    PFNGLCLEARPROC clear;
    PFNGLGETERRORPROC error;
    (void)argc; (void)argv;
    printf("[sdl2-input] START source=%s timeout_ms=%u hardware_accepted=0\n",
           INPUT_SOURCE, (unsigned)G42_TIMEOUT_MS);
    SDL_SetMainReady();
    SDL_ClearError();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) < 0 || *SDL_GetError()) goto done;
    start = SDL_GetTicks64();
    if (SDL_GetDesktopDisplayMode(0, &mode) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) < 0) goto done;
    window = SDL_CreateWindow("SDL2 physical input validation", 0, 0,
                              mode.w, mode.h, SDL_WINDOW_OPENGL);
    if (!window) goto done;
    context = SDL_GL_CreateContext(window);
    if (!context || SDL_GL_MakeCurrent(window, context) < 0) goto done;
    viewport = (PFNGLVIEWPORTPROC)SDL_GL_GetProcAddress("glViewport");
    color = (PFNGLCLEARCOLORPROC)SDL_GL_GetProcAddress("glClearColor");
    clear = (PFNGLCLEARPROC)SDL_GL_GetProcAddress("glClear");
    error = (PFNGLGETERRORPROC)SDL_GL_GetProcAddress("glGetError");
    if (!viewport || !color || !clear || !error) goto done;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width != mode.w || height != mode.h) {
        SDL_SetError("Drawable does not match desktop mode"); goto done;
    }
    if (SDL_GL_SetSwapInterval(1) < 0) goto done;
    viewport(0, 0, width, height);
    printf("[sdl2-input] READY drawable=%dx%d nominal_refresh=%dHz button=0 axis=0\n",
           width, height, mode.refresh_rate);
    for (;;) {
        SDL_Event event;
        if (SDL_GetTicks64() - start >= G42_TIMEOUT_MS) {
            status = 2; reason = "timeout-incomplete"; break;
        }
        SDL_ClearError();
        /* Bound queue draining too; a busy queue cannot suppress the deadline. */
        for (unsigned batch = 0; batch < 64 && SDL_PollEvent(&event); ++batch) {
            Uint64 elapsed = SDL_GetTicks64() - start;
            if (event.type == SDL_QUIT || (event.type == SDL_KEYDOWN &&
                event.key.keysym.sym == SDLK_ESCAPE)) {
                status = 2; reason = "quit-incomplete"; goto done;
            }
            if (event.type == SDL_JOYDEVICEADDED && !pad) {
                int index = event.jdevice.which;
                SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
                SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(index);
                if (id < 0 || SDL_JoystickIsVirtual(index) || id == previous ||
                    (connections && SDL_memcmp(&guid, &identity, sizeof(guid)))) {
                    ++rejected;
                    printf("[sdl2-input] rejected added index=%d virtual_or_unmatched=1\n", index);
                    SDL_ClearError(); continue;
                }
                pad = SDL_JoystickOpen(index);
                if (!pad) { reason = "joystick-open"; goto done; }
                instance = SDL_JoystickInstanceID(pad);
                if (!SDL_JoystickGetAttached(pad) || SDL_JoystickNumButtons(pad) < 1 ||
                    SDL_JoystickNumAxes(pad) < 1 || SDL_JoystickGetButton(pad, 0) ||
                    SDL_abs((int)SDL_JoystickGetAxis(pad, 0)) > 8000) {
                    status = 2; reason = "controller-not-neutral-or-unsupported"; goto done;
                }
                identity = guid; ++connections;
                down = up = moved = centered = ready = 0;
                printf("[sdl2-input] connected t=%llu phase=%u instance=%d virtual=0 source=%s\n",
                       (unsigned long long)elapsed, connections, (int)instance, INPUT_SOURCE);
            } else if (event.type == SDL_JOYDEVICEREMOVED) {
                if (!pad || event.jdevice.which != instance || SDL_JoystickGetAttached(pad)) {
                    ++rejected; continue;
                }
                ++removals;
                printf("[sdl2-input] disconnected t=%llu phase=%u instance=%d attached=0\n",
                       (unsigned long long)elapsed, connections, (int)instance);
                SDL_JoystickClose(pad); pad = NULL; previous = instance;
                if (!(down && up && moved && centered)) {
                    status = 2; reason = "early-disconnect-incomplete"; goto done;
                }
            } else if (event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP) {
                if (!pad || event.jbutton.which != instance || !SDL_JoystickGetAttached(pad) ||
                    event.jbutton.button != 0 ||
                    event.jbutton.state != (event.type == SDL_JOYBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED) ||
                    SDL_JoystickGetButton(pad, 0) != event.jbutton.state) {
                    ++rejected; continue;
                }
                if (event.type == SDL_JOYBUTTONDOWN) down = 1;
                else if (down) up = 1;
                printf("[sdl2-input] button t=%llu phase=%u instance=%d button=0 state=%u matched=1\n",
                       (unsigned long long)elapsed, connections, (int)instance, event.jbutton.state);
            } else if (event.type == SDL_JOYAXISMOTION) {
                if (!pad || event.jaxis.which != instance || !SDL_JoystickGetAttached(pad) ||
                    event.jaxis.axis != 0 || SDL_JoystickGetAxis(pad, 0) != event.jaxis.value) {
                    ++rejected; continue;
                }
                if ((!moved && SDL_abs((int)event.jaxis.value) >= 16000) ||
                    (moved && !centered && SDL_abs((int)event.jaxis.value) <= 8000)) {
                    if (!moved) moved = 1; else centered = 1;
                    printf("[sdl2-input] axis t=%llu phase=%u instance=%d axis=0 value=%d moved=%d centered=%d matched=1\n",
                           (unsigned long long)elapsed, connections, (int)instance,
                           event.jaxis.value, moved, centered);
                }
            }
        }
        if (*SDL_GetError()) { reason = "event-pump"; goto done; }
        if (pad && !ready && down && up && moved && centered) {
            ready = 1;
            printf("[sdl2-input] phase-ready phase=%u instance=%d action=%s\n",
                   connections, (int)instance, connections == 1 ? "disconnect-controller" : "await-parent-teardown");
        }
        if (connections == 2 && removals == 1 && down && up && moved && centered) {
            status = 0; reason = "sequence-complete-awaiting-parent"; break;
        }
        color(connections == 2 ? 0.1f : 0.3f, up ? 0.6f : 0.1f, centered ? 0.6f : 0.1f, 1);
        clear(GL_COLOR_BUFFER_BIT);
        if (error() != GL_NO_ERROR) { reason = "gl-frame"; goto done; }
        SDL_ClearError(); SDL_GL_SwapWindow(window);
        if (*SDL_GetError()) { reason = "swap"; goto done; }
        ++frames;
        SDL_Delay(1);
    }
done:
    if (status == 1) fprintf(stderr, "[sdl2-input] failure reason=%s error=%s\n", reason, SDL_GetError());
    SDL_ClearError();
    if (pad) SDL_JoystickClose(pad);
    if (context) {
        if (SDL_GL_MakeCurrent(NULL, NULL) < 0) status = 1;
        SDL_GL_DeleteContext(context);
    }
    if (window) SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER);
    if (*SDL_GetError()) {
        fprintf(stderr, "[sdl2-input] cleanup-error=%s\n", SDL_GetError());
        status = 1; reason = "cleanup";
    }
    SDL_Quit();
    printf("[sdl2-input] END status=%d reason=%s connections=%u removals=%u button_down=%d button_up=%d axis_moved=%d axis_centered=%d rejected=%u frames=%u cleanup=%s hardware_accepted=0\n",
           status, reason, connections, removals, down, up, moved, centered, rejected, frames,
           SDL_WasInit(0) ? "failed" : "sdl-quit");
    return status;
}
