/* Copyright (C) 2026 BlackBearReloaded; SPDX-License-Identifier: GPL-3.0-or-later */
#include <SDL.h>
#include <GL/glcorearb.h>
#include <stdio.h>

/* Ordinary SDL2 consumer. The platform adapter is linked into SDL2, not here. */
int main(int argc, char **argv)
{
    SDL_Window *window = NULL;
    SDL_GLContext context = NULL;
    SDL_Joystick *pad = NULL;
    int status = 1, width, height, major = 0, minor = 0, running = 1;
    unsigned frames = 0, probes = 0;
    PFNGLGETINTEGERVPROC get_integer;
    PFNGLGETSTRINGPROC get_string;
    PFNGLVIEWPORTPROC viewport;
    PFNGLCLEARCOLORPROC clear_color;
    PFNGLCLEARPROC clear;
    PFNGLGETERRORPROC get_error;
    PFNGLREADPIXELSPROC read_pixels;
    (void)argc; (void)argv;
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) < 0) goto done;
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) < 0 ||
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) < 0) goto done;
    window = SDL_CreateWindow("SDL2 / G19", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_OPENGL);
    if (!window) goto done;
    context = SDL_GL_CreateContext(window);
    if (!context || SDL_GL_MakeCurrent(window, context) < 0) goto done;
    get_integer = (PFNGLGETINTEGERVPROC)SDL_GL_GetProcAddress("glGetIntegerv");
    get_string = (PFNGLGETSTRINGPROC)SDL_GL_GetProcAddress("glGetString");
    viewport = (PFNGLVIEWPORTPROC)SDL_GL_GetProcAddress("glViewport");
    clear_color = (PFNGLCLEARCOLORPROC)SDL_GL_GetProcAddress("glClearColor");
    clear = (PFNGLCLEARPROC)SDL_GL_GetProcAddress("glClear");
    get_error = (PFNGLGETERRORPROC)SDL_GL_GetProcAddress("glGetError");
    read_pixels = (PFNGLREADPIXELSPROC)SDL_GL_GetProcAddress("glReadPixels");
    if (!get_integer || !get_string || !viewport || !clear_color || !clear || !get_error || !read_pixels) goto done;
    get_integer(GL_MAJOR_VERSION, &major); get_integer(GL_MINOR_VERSION, &minor);
    if (major < 3 || (major == 3 && minor < 3) || !get_string(GL_VERSION)) {
        SDL_SetError("OpenGL 3.3 is required"); goto done;
    }
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0 || SDL_GL_SetSwapInterval(1) < 0) goto done;
    viewport(0, 0, width, height);
    printf("[sdl2-g19] GL=%s drawable=%dx%d\n", get_string(GL_VERSION), width, height);
    while (running && frames < 180) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) running = 0;
            if (event.type == SDL_JOYDEVICEADDED && !pad) {
                pad = SDL_JoystickOpen(event.jdevice.which);
                if (!pad) goto done;
            }
            if (event.type == SDL_JOYDEVICEREMOVED && pad &&
                event.jdevice.which == SDL_JoystickInstanceID(pad)) {
                SDL_JoystickClose(pad); pad = NULL;
            }
            if (event.type == SDL_JOYBUTTONDOWN) running = 0;
        }
        if (!running) break;
        clear_color((float)frames / 180.0f, 0.15f, 0.4f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        if (get_error() != GL_NO_ERROR) { SDL_SetError("GL frame failed"); goto done; }
        if (frames == 0 || frames == 179) {
            GLubyte pixel[4] = {0};
            const unsigned expected[4] = { (frames * 255u + 90u) / 180u, 38, 102, 255 };
            int match = 1;
            read_pixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            if (get_error() != GL_NO_ERROR) { SDL_SetError("GL center readback failed"); goto done; }
            for (unsigned channel = 0; channel < 4; ++channel) {
                int delta = (int)pixel[channel] - (int)expected[channel];
                if (delta < -1 || delta > 1) match = 0;
            }
            printf("[sdl2-g19] probe frame=%u rgba=%u,%u,%u,%u expected=%u,%u,%u,%u pass=%d\n",
                frames, (unsigned)pixel[0], (unsigned)pixel[1], (unsigned)pixel[2], (unsigned)pixel[3],
                expected[0], expected[1], expected[2], expected[3], match);
            if (!match) { SDL_SetError("GL center pixel mismatch"); goto done; }
            ++probes;
        }
        /* SDL2's public swap is void; this backend records failure in SDL's error. */
        SDL_ClearError();
        SDL_GL_SwapWindow(window);
        if (*SDL_GetError()) goto done;
        ++frames;
    }
    status = 0;
done:
    if (status) fprintf(stderr, "[sdl2-g19] failed: %s\n", SDL_GetError());
    SDL_ClearError();
    if (pad) SDL_JoystickClose(pad);
    if (context) {
        if (SDL_GL_MakeCurrent(NULL, NULL) < 0) status = 1;
        SDL_GL_DeleteContext(context);
    }
    if (window) SDL_DestroyWindow(window);
    /* Read errors before SDL_Quit destroys the main thread's error storage. */
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER);
    if (*SDL_GetError()) { fprintf(stderr, "[sdl2-g19] cleanup: %s\n", SDL_GetError()); status = 1; }
    SDL_Quit();
    printf("[sdl2-g19] frames=%u probes=%u status=%d\n", frames, probes, status);
    return status;
}
