/* Real upstream SDL2 + actual adapter + EGL boundary double (no GPU).
 * Copyright (C) 2026 BlackBearReloaded; SPDX-License-Identifier: GPL-3.0-or-later */
#include <SDL.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int display_token, config_token, surface_token, context_token;
#define DISPLAY ((EGLDisplay)&display_token)
#define CONFIG ((EGLConfig)&config_token)
#define SURFACE ((EGLSurface)&surface_token)
#define CONTEXT ((EGLContext)&context_token)
static int initialized, surface_live, context_live, current, swaps, creates, destroys;
static unsigned reads, clears;
static GLfloat color[4];
static const char *failure;
static EGLint error_code;
static int fail(const char *name)
{
    if (!failure || strcmp(name, failure)) return 0;
    failure = NULL; error_code = EGL_BAD_ALLOC; return 1;
}
EGLDisplay eglGetDisplay(EGLNativeDisplayType id)
{ assert(id == EGL_DEFAULT_DISPLAY); return fail("display") ? EGL_NO_DISPLAY : DISPLAY; }
EGLBoolean eglInitialize(EGLDisplay d, EGLint *major, EGLint *minor)
{
    assert(d == DISPLAY && !initialized);
    if (fail("initialize")) return EGL_FALSE;
    initialized = 1;
    if (major) *major = 1;
    if (minor) *minor = 4;
    return EGL_TRUE;
}
EGLBoolean eglTerminate(EGLDisplay d)
{
    assert(d == DISPLAY && initialized && !surface_live && !context_live && !current);
    initialized = 0; return EGL_TRUE;
}
EGLint eglGetError(void) { EGLint e = error_code; error_code = EGL_SUCCESS; return e; }
EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint *a, EGLConfig *c, EGLint size, EGLint *n)
{
    assert(d == DISPLAY && initialized && size == 1);
    assert(a[0] == EGL_SURFACE_TYPE && a[1] == EGL_WINDOW_BIT);
    assert(a[2] == EGL_RENDERABLE_TYPE && a[3] == EGL_OPENGL_BIT);
    if (fail("config")) return EGL_FALSE;
    *c = CONFIG; *n = fail("no-config") ? 0 : 1; return EGL_TRUE;
}
EGLSurface eglCreateWindowSurface(EGLDisplay d, EGLConfig c, EGLNativeWindowType w, const EGLint *a)
{
    assert(d == DISPLAY && c == CONFIG && !w && !a && !surface_live);
    if (fail("surface")) return EGL_NO_SURFACE;
    surface_live = 1; ++creates; return SURFACE;
}
EGLBoolean eglQuerySurface(EGLDisplay d, EGLSurface s, EGLint a, EGLint *v)
{
    assert(d == DISPLAY && s == SURFACE && surface_live);
    if (fail("query")) return EGL_FALSE;
    *v = a == EGL_WIDTH ? 1920 : 1080;
    if (fail("size")) *v = 640;
    return EGL_TRUE;
}
EGLBoolean eglBindAPI(EGLenum api)
{ assert(api == EGL_OPENGL_API); return !fail("bind"); }
EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *a)
{
    assert(d == DISPLAY && c == CONFIG && !share && !context_live);
    assert(a[0] == EGL_CONTEXT_MAJOR_VERSION_KHR && a[1] == 3);
    assert(a[2] == EGL_CONTEXT_MINOR_VERSION_KHR && a[3] == 3);
    assert(a[4] == EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR && a[5] == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR);
    if (fail("context")) return EGL_NO_CONTEXT;
    context_live = 1; clears = 0; return CONTEXT;
}
EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext c)
{
    assert(d == DISPLAY && initialized);
    if (fail("current")) return EGL_FALSE;
    if (c) assert(c == CONTEXT && context_live && surface_live && draw == SURFACE && read == SURFACE);
    else assert(!draw && !read);
    current = c != EGL_NO_CONTEXT; return EGL_TRUE;
}
EGLBoolean eglDestroyContext(EGLDisplay d, EGLContext c)
{
    assert(d == DISPLAY && c == CONTEXT && context_live);
    if (current) { error_code = EGL_BAD_ACCESS; return EGL_FALSE; }
    context_live = 0; return EGL_TRUE;
}
EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface s)
{ assert(d == DISPLAY && s == SURFACE && surface_live && !current); surface_live = 0; ++destroys; return EGL_TRUE; }
EGLBoolean eglSwapInterval(EGLDisplay d, EGLint i)
{ assert(d == DISPLAY && current && (i == 0 || i == 1)); return !fail("interval"); }
EGLBoolean eglSwapBuffers(EGLDisplay d, EGLSurface s)
{ assert(d == DISPLAY && s == SURFACE && current); if (fail("swap")) return EGL_FALSE; ++swaps; return EGL_TRUE; }

static void APIENTRY mock_integer(GLenum name, GLint *v)
{ assert(current); *v = name == GL_MAJOR_VERSION ? 3 : name == GL_MINOR_VERSION ? 3 : 0; }
static const GLubyte *APIENTRY mock_string(GLenum name)
{
    (void)name; assert(current);
    if (fail("quit")) { SDL_Event event; SDL_zero(event); event.type = SDL_QUIT; assert(SDL_PushEvent(&event) == 1); }
    return (const GLubyte *)"3.3 host contract double";
}
static void APIENTRY mock_viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{ assert(current && x == 0 && y == 0 && w == 1920 && h == 1080); }
static void APIENTRY mock_color(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{ assert(current); color[0] = r; color[1] = g; color[2] = b; color[3] = a; }
static void APIENTRY mock_clear(GLbitfield bits)
{ assert(current && bits == GL_COLOR_BUFFER_BIT); ++clears; }
static void APIENTRY mock_read(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *data)
{
    GLubyte *pixel = data;
    assert(current && x == 960 && y == 540 && w == 1 && h == 1);
    assert(format == GL_RGBA && type == GL_UNSIGNED_BYTE && data);
    assert(clears == 1 || clears == 180); ++reads;
    for (unsigned channel = 0; channel < 4; ++channel)
        pixel[channel] = (GLubyte)(color[channel] * 255.0f + 0.5f);
    if (fail("tolerance")) ++pixel[2];
    if ((clears == 1 && fail("pixel0")) || (clears == 180 && fail("pixel179"))) pixel[2] += 2;
}
static GLenum APIENTRY mock_error(void) { assert(current); return GL_NO_ERROR; }
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
    if (fail("proc")) return NULL;
#define PROC(n, f) if (!strcmp(name, n)) return (__eglMustCastToProperFunctionPointerType)f
    PROC("glGetIntegerv", mock_integer); PROC("glGetString", mock_string);
    PROC("glViewport", mock_viewport); PROC("glClearColor", mock_color);
    PROC("glClear", mock_clear); PROC("glGetError", mock_error);
    PROC("glReadPixels", mock_read);
#undef PROC
    return NULL;
}

static void attributes(void)
{
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) == 0);
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) == 0);
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) == 0);
}
static SDL_Window *window(void)
{ return SDL_CreateWindow("contract", 0, 0, 1920, 1080, SDL_WINDOW_OPENGL); }
static void clean(void)
{ assert(!initialized && !surface_live && !context_live && !current && creates == destroys); }

static int other_thread(void *data)
{
    assert(SDL_GL_MakeCurrent(data, CONTEXT) < 0);
    return 0;
}

extern int g19_example_main(int argc, char **argv);
int main(void)
{
    SDL_Window *w;
    SDL_GLContext c;
    SDL_Event event;
    int width = 0, height = 0, seen = 0;
    const char *init_failures[] = {"display", "initialize"};
    const char *window_failures[] = {"config", "no-config", "surface", "query", "size"};
    const char *context_failures[] = {"bind", "context", "current"};
    SDL_SetMainReady();
    assert(SDL_setenv("SDL_VIDEODRIVER", "ps5-g19", 1) == 0);
    for (unsigned i = 0; i < SDL_arraysize(init_failures); ++i) {
        failure = init_failures[i]; assert(SDL_Init(SDL_INIT_VIDEO) < 0); SDL_Quit(); clean();
    }
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) == 0);
    assert(!strcmp(SDL_GetCurrentVideoDriver(), "ps5-g19"));
    assert(SDL_GetNumVideoDisplays() == 1 && SDL_GetNumDisplayModes(0) == 1);
    attributes();
    assert(SDL_GL_LoadLibrary("libOSMesa.so") < 0);
    assert(!SDL_CreateWindow("wrong-size", 0, 0, 640, 480, SDL_WINDOW_OPENGL));
    assert(!SDL_CreateWindow("software", 0, 0, 1920, 1080, 0));
    for (unsigned i = 0; i < SDL_arraysize(window_failures); ++i) {
        failure = window_failures[i]; assert(!window()); assert(!surface_live);
    }
    w = window(); assert(w);
    assert(!window() && surface_live); /* Failed second window must not destroy first. */
    assert(!SDL_GL_GetProcAddress("glMissing"));
    for (unsigned i = 0; i < SDL_arraysize(context_failures); ++i) {
        failure = context_failures[i]; assert(!SDL_GL_CreateContext(w)); assert(!context_live);
    }
    assert(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) == 0);
    assert(!SDL_GL_CreateContext(w)); attributes();
    c = SDL_GL_CreateContext(w); assert(c && current);
    assert(!SDL_GL_CreateContext(w));
    assert(SDL_GL_GetCurrentContext() == c && SDL_GL_GetCurrentWindow() == w);
    assert(SDL_GL_MakeCurrent(NULL, NULL) == 0 && !current);
    assert(SDL_GL_MakeCurrent(w, c) == 0 && current);
    {
        SDL_Thread *thread = SDL_CreateThread(other_thread, "contract", w);
        assert(thread); SDL_WaitThread(thread, NULL);
        assert(SDL_GL_GetCurrentContext() == c && current);
    }
    SDL_GL_GetDrawableSize(w, &width, &height); assert(width == 1920 && height == 1080);
    assert(SDL_GL_SetSwapInterval(-1) < 0 && SDL_GL_SetSwapInterval(2) < 0);
    assert(SDL_GL_SetSwapInterval(0) == 0 && SDL_GL_GetSwapInterval() == 0);
    failure = "interval"; assert(SDL_GL_SetSwapInterval(1) < 0 && SDL_GL_GetSwapInterval() == 0);
    SDL_ClearError(); SDL_GL_SwapWindow(w); assert(!*SDL_GetError() && swaps == 1);
    failure = "swap"; SDL_GL_SwapWindow(w); assert(strstr(SDL_GetError(), "eglSwapBuffers") && swaps == 1);
    SDL_SetWindowSize(w, 640, 480); SDL_GetWindowSize(w, &width, &height);
    assert(width == 1920 && height == 1080);
    SDL_zero(event); event.type = SDL_USEREVENT; event.user.code = 26;
    assert(SDL_PushEvent(&event) == 1);
    while (SDL_PollEvent(&event)) if (event.type == SDL_USEREVENT && event.user.code == 26) seen = 1;
    assert(seen);
    /* Upstream virtual joystick travels through the same SDL event loop. */
    {
        int index = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 1, 0);
        SDL_Joystick *pad;
        assert(index >= 0); pad = SDL_JoystickOpen(index); assert(pad);
        assert(SDL_JoystickSetVirtualButton(pad, 0, SDL_PRESSED) == 0);
        seen = 0;
        while (SDL_PollEvent(&event)) if (event.type == SDL_JOYBUTTONDOWN) seen = 1;
        assert(seen); SDL_JoystickClose(pad); assert(SDL_JoystickDetachVirtual(index) == 0);
    }
    failure = "current"; SDL_GL_DeleteContext(c);
    assert(context_live && current && SDL_GL_GetCurrentContext() == c);
    SDL_GL_DeleteContext(c); assert(!SDL_GL_GetCurrentContext());
    SDL_DestroyWindow(w); SDL_Quit(); clean();
    /* SDL_Quit must also clean a consumer that forgot explicit deletes. */
    assert(SDL_Init(SDL_INIT_VIDEO) == 0); attributes(); w = window(); assert(w);
    assert(SDL_GL_CreateContext(w)); SDL_Quit(); clean();
    assert(g19_example_main(0, NULL) == 0); clean(); assert(swaps == 181 && reads == 2);
    failure = "proc"; assert(g19_example_main(0, NULL) == 1); clean();
    failure = "swap"; assert(g19_example_main(0, NULL) == 1); clean();
    failure = "pixel0"; assert(g19_example_main(0, NULL) == 1); clean();
    assert(swaps == 181 && reads == 4);
    failure = "pixel179"; assert(g19_example_main(0, NULL) == 1); clean();
    assert(swaps == 360 && reads == 6);
    failure = "tolerance"; assert(g19_example_main(0, NULL) == 0); clean();
    assert(swaps == 540 && reads == 8);
    failure = "quit"; assert(g19_example_main(0, NULL) == 0); clean();
    assert(swaps == 540 && reads == 8); /* Early exit is valid but not full acceptance. */
    puts("G26 real-SDL host contract: PASS (no GPU or physical input)");
    return 0;
}
