// Upstream geometry, transforms, Sokol pipeline and draw loop; native EGL glue.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#undef GL_VERSION_4_2
#undef GL_VERSION_4_3
#define SOKOL_EXTERNAL_GL_LOADER
#define SOKOL_DEBUG
#include "cube.inc"

static EGLDisplay display = EGL_NO_DISPLAY;
static EGLSurface surface = EGL_NO_SURFACE;
static EGLContext context = EGL_NO_CONTEXT;
static EGLint width, height;
static unsigned frame, probes, face_mask;
static int failed;
static unsigned char *pixels;
static GLFWwindow window;

static int check(int ok, const char *stage)
{
    if (!ok) {
        printf("[ps5-sokol-cube] FAIL %s gl=0x%x egl=0x%x\n", stage, glGetError(), eglGetError());
        failed = 1;
    }
    return ok;
}

static void cube_log(const char *tag, uint32_t level, uint32_t item, const char *message,
                     uint32_t line, const char *file, void *user)
{
    (void)tag; (void)file; (void)user;
    if (level <= 2) failed = 1;
    printf("[ps5-sokol-cube] log level=%u item=%u line=%u %s\n", level, item, line, message ? message : "");
}

static void glfw_init(const glfw_desc_t *desc) { check(desc->sample_count == 1, "single-sample adapter"); }
static GLFWwindow *glfw_window(void) { return &window; }
static int glfw_width(void) { return width; }
static int glfw_height(void) { return height; }
static sg_environment glfw_environment(void)
{
    return (sg_environment){.defaults = {.color_format = SG_PIXELFORMAT_RGBA8,
        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL, .sample_count = 1}};
}
static sg_swapchain glfw_swapchain(void)
{
    return (sg_swapchain){.width = width, .height = height, .sample_count = 1,
        .color_format = SG_PIXELFORMAT_RGBA8, .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL};
}
static int glfwWindowShouldClose(GLFWwindow *unused) { (void)unused; return failed || frame >= 180; }
// ponytail: autonomous sample only; add real pad/event handling for an interactive port.
static void glfwPollEvents(void) {}

// Ray/unit-box oracle: independent of GPU vertex processing, culling and rasterization.
// Returns -2 for an edge probe, -1 for background, or the nearest cube face.
static int expected_face(mat44_t inverse, int x, int y)
{
    float nx = 2.0f * (x + .5f) / width - 1.0f, ny = 2.0f * (y + .5f) / height - 1.0f;
    vec4_t near = vec4_mul_mat44(vec4(nx, ny, -1, 1), inverse);
    vec4_t far = vec4_mul_mat44(vec4(nx, ny, 1, 1), inverse);
    float origin[3] = {near.x / near.w, near.y / near.w, near.z / near.w};
    float direction[3] = {far.x / far.w - origin[0], far.y / far.w - origin[1], far.z / far.w - origin[2]};
    float enter = 0, leave = 1;
    int face = -1;
    for (int axis = 0; axis < 3; ++axis) {
        if (fabsf(direction[axis]) < 1e-7f) {
            if (fabsf(origin[axis]) > 1) return -1;
            continue;
        }
        float a = (-1 - origin[axis]) / direction[axis], b = (1 - origin[axis]) / direction[axis];
        int side = axis * 2;
        if (a > b) { float tmp = a; a = b; b = tmp; ++side; }
        if (a > enter) { enter = a; face = side; }
        if (b < leave) leave = b;
        if (enter > leave) return -1;
    }
    if (face >= 0) for (int axis = 0; axis < 3; ++axis) {
        if (axis != face / 2 && fabsf(origin[axis] + enter * direction[axis]) > .96f) return -2;
    }
    return face;
}

static void check_frame(void)
{
    static const unsigned char colors[7][4] = {
        {0,0,255,255}, {255,128,0,255}, {0,128,255,255}, {255,0,128,255},
        {255,0,0,255}, {0,255,0,255}, {128,128,128,255}
    };
    mat44_t inverse;
    if (!check(mat44_inverse(&inverse, NULL, compute_mvp(frame + 1, 2 * (frame + 1), width, height)), "inverse matrix")) return;
#ifdef PS5_SOKOL_HOST_REFERENCE
    // Host-only negative control: erase the rendered cube before the same oracle.
    if (getenv("PS5_CUBE_CORRUPT")) {
        glClearColor(.5f,.5f,.5f,1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
#endif
    unsigned mismatches = 0, foreground = 0, background = 0, checked = 0;
    for (int gy = 1; gy <= 17; ++gy) {
        int y = height * gy / 18;
        glReadPixels(0, y, width, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (int gx = 1; gx <= 31; ++gx) {
            int x = width * gx / 32;
            int face = expected_face(inverse, x, y);
            if (face == -2) continue;
            if (face < 0) { ++background; face = 6; }
            else { ++foreground; face_mask |= 1u << face; }
            const unsigned char *actual = pixels + x * 4;
            for (int c = 0; c < 4; ++c) if (abs(actual[c] - colors[face][c]) > 2) {
                if (mismatches < 4) printf("[ps5-sokol-cube] pixel=%d,%d face=%d c=%d got=%u expected=%u\n",
                    x, y, face, c, actual[c], colors[face][c]);
                ++mismatches;
            }
            ++checked;
        }
    }
    int ok = check(!mismatches && foreground >= 20 && background >= 100 && glGetError() == GL_NO_ERROR, "rotation readback");
    ++probes;
    printf("[ps5-sokol-cube] frame=%u probes=%u foreground=%u background=%u mismatches=%u %s\n",
        frame, checked, foreground, background, mismatches, ok ? "PASS" : "FAIL");
}

static void glfwSwapBuffers(GLFWwindow *unused)
{
    (void)unused;
    if (frame == 0 || frame == 44 || frame == 89 || frame == 134 || frame == 179) check_frame();
    check(glGetError() == GL_NO_ERROR && eglSwapBuffers(display, surface), "render/present");
    ++frame;
}

static void glfwTerminate(void)
{
    if (display == EGL_NO_DISPLAY) return;
    EGLBoolean ok = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (context != EGL_NO_CONTEXT) ok &= eglDestroyContext(display, context);
    if (surface != EGL_NO_SURFACE) ok &= eglDestroySurface(display, surface);
    ok &= eglTerminate(display);
    check(ok && eglGetError() == EGL_SUCCESS, "EGL cleanup");
    display = EGL_NO_DISPLAY;
}

int main(void)
{
#ifdef PS5_SOKOL_HOST_REFERENCE
    const EGLint surface_type = EGL_PBUFFER_BIT;
#else
    const EGLint surface_type = EGL_WINDOW_BIT;
#endif
    const EGLint attrs[] = {EGL_SURFACE_TYPE,surface_type,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_STENCIL_SIZE,8,EGL_NONE};
    const EGLint ctx_attrs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
    EGLConfig config = NULL;
    EGLint count = 0;
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!check(display != EGL_NO_DISPLAY && eglInitialize(display,NULL,NULL) && eglBindAPI(EGL_OPENGL_API) &&
        eglChooseConfig(display,attrs,&config,1,&count) && count == 1, "EGL initialization")) goto done;
#ifdef PS5_SOKOL_HOST_REFERENCE
    const EGLint pbuffer[] = {EGL_WIDTH,1920,EGL_HEIGHT,1080,EGL_NONE};
    surface = eglCreatePbufferSurface(display,config,pbuffer);
#else
    surface = eglCreateWindowSurface(display,config,(EGLNativeWindowType)0,NULL);
#endif
    context = eglCreateContext(display,config,EGL_NO_CONTEXT,ctx_attrs);
    if (!check(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
        eglMakeCurrent(display,surface,surface,context) && eglQuerySurface(display,surface,EGL_WIDTH,&width) &&
        eglQuerySurface(display,surface,EGL_HEIGHT,&height) && width == 1920 && height == 1080, "EGL context/dimensions")) goto done;
#ifdef PS5_SOKOL_HOST_REFERENCE
    // The software-Mesa pbuffer is single-buffered; select its actual color buffer.
    glDrawBuffer(GL_FRONT);
    glReadBuffer(GL_FRONT);
#endif
    // The oracle samples only 17 rows; do not compete with GLSL setup for 8 MiB.
    pixels = malloc((size_t)width * 4);
    if (!check(pixels != NULL, "readback allocation")) goto done;
    printf("[ps5-sokol-cube] upstream=8afa83928ce1870efeb0d513e7c4dce4f5db7b3e GL=%s renderer=%s\n",
        glGetString(GL_VERSION), glGetString(GL_RENDERER));
    run_upstream_cube();
    check(frame == 180 && probes == 5 && (face_mask & (face_mask - 1)) != 0, "complete rotation coverage");
done:
    glfwTerminate();
    free(pixels);
    printf("[ps5-sokol-cube] finished frames=%u probes=%u face_mask=0x%x status=%d\n", frame, probes, face_mask, failed);
    return failed;
}
