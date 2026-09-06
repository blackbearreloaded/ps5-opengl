#!/usr/bin/env python3
"""Inject presenter shutdown failures into the actual runtime/EGL destructors."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
runtime = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
egl = (root / "src/egl/ps5_egl.c").read_text()
shutdown = runtime[runtime.index("int ps5_agc_gate2_shutdown_present(void)"):
                   runtime.index("static int runtime_video_acquire(")]
terminate = egl[egl.index("EGLAPI EGLBoolean EGLAPIENTRY\neglTerminate("):
                egl.index("EGLAPI EGLint EGLAPIENTRY\neglGetError(")]
destroy = egl[egl.index("EGLAPI EGLBoolean EGLAPIENTRY\neglDestroySurface("):
              egl.index("EGLAPI EGLDisplay EGLAPIENTRY\neglGetCurrentDisplay(")]
code = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define PS5_MULTIDRAW_BATCH 1
#define RENDER_MARKER 100
static int runtime_batch_faulted, runtime_batch_active;
static unsigned runtime_batch_count;
typedef struct { int (*unregister_buffers)(int, int); int (*close)(int); } video_api_t;
static video_api_t runtime_video_api;
static int runtime_video_handle = -1, runtime_video_registered;
static unsigned char *runtime_video_framebuffer;
static size_t runtime_video_framebuffer_size;
static unsigned runtime_present_count;
static uint64_t runtime_render_marker;
static int close_failure, unregister_failure, closes, unregisters;
static int close_video(int handle) { assert(handle == 7); ++closes; return close_failure ? -1 : 0; }
static int unregister_video(int handle, int group) {
    assert(handle == 7 && group == 0); ++unregisters;
    return unregister_failure ? (int)UINT32_C(0x80290009) : 0;
}
''' + shutdown + r'''
#define EGLAPI
#define EGLAPIENTRY
#define EGL_TRUE 1
#define EGL_FALSE 0
#define EGL_BAD_DISPLAY 0x3008
#define EGL_BAD_SURFACE 0x300d
#define EGL_BAD_ACCESS 0x3002
#define PS5_EGL_LOCK() ((void)0)
typedef void *EGLDisplay;
typedef void *EGLSurface;
typedef unsigned EGLBoolean;
struct pipe_resource { int unused; };
struct pipe_screen { void (*destroy)(struct pipe_screen *); };
struct ps5_egl_display {
    bool initialized; unsigned contexts, surfaces;
    struct pipe_screen *screen; int frontend;
    struct pipe_resource *scanout[2];
};
struct ps5_egl_surface {
    unsigned magic; bool window, current, used;
    int drawable; struct pipe_resource *depth_stencil, *targets[2];
    struct ps5_egl_surface *next;
};
static struct ps5_egl_display ps5_display;
static struct ps5_egl_surface surface, *ps5_surfaces, *ps5_window_surface;
static struct pipe_resource resource;
static unsigned char scanout[64];
static int releases, locked, egl_error;
static void ps5_set_error(int value) { egl_error = value; }
static int ps5_valid_display(EGLDisplay d, bool initialized) {
    return d == &ps5_display && (!initialized || ps5_display.initialized);
}
static struct ps5_egl_surface *ps5_surface(EGLSurface s) {
    return s == ps5_surfaces && surface.magic == 1 ? &surface : NULL;
}
static void ps5_screen_submit_lock(struct pipe_screen *s) { assert(s && !locked); ++locked; }
static void ps5_screen_submit_unlock(struct pipe_screen *s) { assert(s && locked == 1); --locked; }
static void pipe_resource_reference(struct pipe_resource **to, void *from) {
    assert(!from); releases += *to != NULL; *to = NULL;
}
static void st_api_destroy_drawable(int *drawable) { (void)drawable; ++releases; }
static void st_screen_destroy(int *frontend) { (void)frontend; ++releases; }
static void destroy_screen(struct pipe_screen *s) { assert(s); ++releases; }
static void release_surface(void *s) { assert(s == &surface); ++releases; }
#define free release_surface
''' + terminate + destroy + r'''
#undef free
static void setup(void) {
    static struct pipe_screen screen = {destroy_screen};
    runtime_video_api = (video_api_t){unregister_video, close_video};
    runtime_video_handle = 7; runtime_video_registered = 1;
    runtime_video_framebuffer = scanout; runtime_video_framebuffer_size = sizeof(scanout);
    runtime_present_count = 6; runtime_render_marker = 200;
    runtime_batch_faulted = runtime_batch_active = runtime_batch_count = 0;
    close_failure = unregister_failure = closes = unregisters = releases = locked = egl_error = 0;
    ps5_display = (struct ps5_egl_display){.initialized=true, .screen=&screen,
        .scanout={&resource, &resource}, .surfaces=1};
    surface = (struct ps5_egl_surface){.magic=1, .window=true, .used=true,
        .depth_stencil=&resource, .targets={&resource, &resource}};
    ps5_surfaces = ps5_window_surface = &surface;
}
static void inject(int failure) {
    close_failure = failure == 1;
    runtime_batch_active = failure == 2;
    runtime_batch_faulted = failure == 3;
    runtime_batch_count = failure == 4;
}
int main(void) {
    for (int busy = 0; busy <= 1; ++busy) {
        setup(); close_failure = 1; unregister_failure = busy;
        assert(ps5_agc_gate2_shutdown_present() != 0);
        assert(runtime_video_handle == 7 && runtime_video_framebuffer == scanout);
        assert(runtime_video_framebuffer_size == sizeof(scanout) && runtime_present_count == 6);
        assert(runtime_render_marker == 200 && runtime_video_api.close == close_video);
        assert(runtime_video_registered == busy);
        close_failure = 0;
        assert(ps5_agc_gate2_shutdown_present() == 0);
        assert(runtime_video_handle == -1 && !runtime_video_framebuffer && !runtime_video_registered);
        assert(unregisters == 1 + busy && closes == 2);
        assert(ps5_agc_gate2_shutdown_present() == 0 && closes == 2); /* Idempotent. */
    }
    for (int failure = 1; failure <= 4; ++failure) {
        setup(); inject(failure);
        assert(!eglDestroySurface(&ps5_display, &surface));
        assert(egl_error == EGL_BAD_ACCESS && !releases && !locked);
        assert(surface.magic == 1 && ps5_surfaces == &surface && ps5_window_surface == &surface);
        assert(ps5_display.surfaces == 1 && surface.targets[0] == &resource);
        inject(0); /* Simulated recovery only: no hardware error is injected. */
        assert(eglDestroySurface(&ps5_display, &surface));
        assert(!surface.magic && !ps5_surfaces && !ps5_window_surface && !ps5_display.surfaces);
        assert(releases == 5 && !locked);
        assert(eglTerminate(&ps5_display) && !ps5_display.initialized && !locked);

        setup(); ps5_display.surfaces = 0; inject(failure);
        assert(!eglTerminate(&ps5_display));
        assert(egl_error == EGL_BAD_ACCESS && !releases && !locked);
        assert(ps5_display.initialized && ps5_display.scanout[0] == &resource && ps5_display.screen);
        inject(0);
        assert(eglTerminate(&ps5_display));
        assert(!ps5_display.initialized && !ps5_display.scanout[0] && releases == 4 && !locked);
    }
    puts("present-shutdown: PASS close errors/batch guards retain runtime, surface and display ownership");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "present-shutdown")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-address",
                    "-x", "c", "-o", executable, "-"], input=code, text=True, check=True)
    subprocess.run([executable], cwd=temporary, check=True)
