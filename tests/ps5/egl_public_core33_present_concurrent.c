// SPDX-License-Identifier: GPL-3.0-or-later
// A second context can queue offscreen work between window flush and swap.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

extern int sceKernelUsleep(uint32_t);
static EGLDisplay display;
static EGLSurface offscreen;
static EGLContext worker_context;
static unsigned stop, ready, worker_frames;
static int worker_ok;

static void *draw_worker(void *unused)
{
   (void)unused;
   if (!eglBindAPI(EGL_OPENGL_API) ||
       !eglMakeCurrent(display, offscreen, offscreen, worker_context)) {
      __atomic_store_n(&ready, 2, __ATOMIC_RELEASE);
      return NULL;
   }
   glViewport(0, 0, 128, 128);
   glClearColor(0, 1, 0, 1);
   __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
   while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) {
      glClear(GL_COLOR_BUFFER_BIT);
      if ((++worker_frames & 15u) == 0) glFlush();
      sceKernelUsleep(1000);
   }
   unsigned char pixel[4] = {0};
   glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
   worker_ok = worker_frames && glGetError() == GL_NO_ERROR &&
      pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0 && pixel[3] == 255;
   worker_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   return NULL;
}

int main(void)
{
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE};
   const EGLint dimensions[] = {EGL_WIDTH, 128, EGL_HEIGHT, 128, EGL_NONE};
   EGLConfig config;
   EGLint count;
   EGLSurface window = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   pthread_t worker;
   unsigned frames = 0;
   int started = 0, current = 0, passed = 0, cleanup = 1;
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) || count != 1)
      return 1;
   window = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, NULL);
   offscreen = eglCreatePbufferSurface(display, config, dimensions);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
   worker_context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
   if (!window || !offscreen || !context || !worker_context ||
       !eglMakeCurrent(display, window, window, context)) goto done;
   current = 1;
   if (!eglSwapInterval(display, 0)) goto done;
   pthread_attr_t attributes;
   if (pthread_attr_init(&attributes)) goto done;
   int result = pthread_attr_setstacksize(&attributes, 8u * 1024u * 1024u);
   if (!result) result = pthread_create(&worker, &attributes, draw_worker, NULL);
   pthread_attr_destroy(&attributes);
   if (result) goto done;
   started = 1;
   for (unsigned wait = 0; wait < 2000 && !__atomic_load_n(&ready, __ATOMIC_ACQUIRE); ++wait)
      sceKernelUsleep(1000);
   if (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) != 1) goto done;
   glViewport(0, 0, 1920, 1080);
   glClearColor(1, 0, 0, 1);
   for (; frames < 300; ++frames) {
      glClear(GL_COLOR_BUFFER_BIT);
      if (frames == 299) {
         unsigned char pixel[4] = {0};
         glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
         if (pixel[0] != 255 || pixel[1] || pixel[2] || pixel[3] != 255) goto done;
      }
      GLenum error = glGetError();
      if (error != GL_NO_ERROR) {
         printf("[ps5-present-concurrent] draw-error=%x frame=%u\n", error, frames);
         goto done;
      }
      if (!eglSwapBuffers(display, window)) goto done;
   }
   passed = 1;
done:
   if (!passed) printf("[ps5-present-concurrent] failed frame=%u egl=%x\n", frames, eglGetError());
   __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
   if (started) cleanup &= pthread_join(worker, NULL) == 0;
   passed &= started && worker_ok;
   if (current) cleanup &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   if (worker_context) cleanup &= eglDestroyContext(display, worker_context);
   if (context) cleanup &= eglDestroyContext(display, context);
   if (offscreen) cleanup &= eglDestroySurface(display, offscreen);
   if (window) cleanup &= eglDestroySurface(display, window);
   cleanup &= eglTerminate(display);
   printf("[ps5-present-concurrent] swaps=%u worker-clears=%u pixels=%d cleanup=%d result=%d\n",
          frames, worker_frames, passed, cleanup, passed && cleanup ? 0 : 1);
   return passed && cleanup ? 0 : 1;
}
