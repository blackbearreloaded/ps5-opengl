#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define SIZE 64
#define SCISSOR_MIN 16
#define SCISSOR_MAX 48

int ps5_egl_current_draw_status(unsigned *draw_calls);

int
main(void)
{
   static const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_NONE,
   };
   static const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL;
   EGLint count = 0;
   GLuint framebuffer = 0, color = 0, depth_stencil = 0;
   uint8_t *colors = NULL, *stencils = NULL;
   float *depths = NULL;
   unsigned inside_ok = 0, outside_ok = 0, draw_calls = 0;
   int draw_status = -1, current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   colors = malloc(SIZE * SIZE * 4u);
   depths = malloc(SIZE * SIZE * sizeof(*depths));
   stencils = malloc(SIZE * SIZE);
   if (!colors || !depths || !stencils)
      goto cleanup;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context))
      goto cleanup;
   current = 1;

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glGenRenderbuffers(1, &color);
   glBindRenderbuffer(GL_RENDERBUFFER, color);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, SIZE, SIZE);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, color);
   glGenRenderbuffers(1, &depth_stencil);
   glBindRenderbuffer(GL_RENDERBUFFER, depth_stencil);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8,
                         SIZE, SIZE);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                             GL_RENDERBUFFER, depth_stencil);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      goto cleanup;

   glViewport(0, 0, SIZE, SIZE);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glDepthMask(GL_TRUE);
   glStencilMask(0xff);
   glClearColor(1.0f, 0.0f, 0.0f, 0.25f);
   glClearDepth(0.75);
   glClearStencil(0x11);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
           GL_STENCIL_BUFFER_BIT);

   glEnable(GL_SCISSOR_TEST);
   glScissor(SCISSOR_MIN, SCISSOR_MIN,
             SCISSOR_MAX - SCISSOR_MIN, SCISSOR_MAX - SCISSOR_MIN);
   glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE);
   glStencilMask(0x0f);
   glClearColor(0.25f, 1.0f, 1.0f, 0.75f);
   glClearDepth(0.25);
   glClearStencil(0x0a);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
           GL_STENCIL_BUFFER_BIT);
   glDisable(GL_SCISSOR_TEST);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glStencilMask(0xff);
   glFinish();

   glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, colors);
   glReadPixels(0, 0, SIZE, SIZE, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
   glReadPixels(0, 0, SIZE, SIZE, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE,
                stencils);
   for (unsigned y = 0; y < SIZE; ++y) {
      for (unsigned x = 0; x < SIZE; ++x) {
         unsigned index = y * SIZE + x;
         const uint8_t *pixel = colors + index * 4u;
         int inside = x >= SCISSOR_MIN && x < SCISSOR_MAX &&
                      y >= SCISSOR_MIN && y < SCISSOR_MAX;

         if (inside) {
            inside_ok += pixel[0] == 255 && pixel[1] == 255 &&
                         pixel[2] == 255 && pixel[3] == 64 &&
                         fabsf(depths[index] - 0.25f) < 0.00001f &&
                         stencils[index] == 0x1a;
         } else {
            outside_ok += pixel[0] == 255 && pixel[1] == 0 &&
                          pixel[2] == 0 && pixel[3] == 64 &&
                          fabsf(depths[index] - 0.75f) < 0.00001f &&
                          stencils[index] == 0x11;
         }
      }
   }
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   passed = inside_ok == (SCISSOR_MAX - SCISSOR_MIN) *
                         (SCISSOR_MAX - SCISSOR_MIN) &&
            outside_ok == SIZE * SIZE - inside_ok &&
            draw_status == 0 && draw_calls >= 1 &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-core33-scissored-clear] inside=%u outside=%u "
          "draw=%d/%u result=%d\n",
          inside_ok, outside_ok, draw_status, draw_calls, passed ? 0 : 1);

cleanup:
   if (current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (depth_stencil)
         glDeleteRenderbuffers(1, &depth_stencil);
      if (color)
         glDeleteRenderbuffers(1, &color);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      cleanup_ok &= glGetError() == GL_NO_ERROR;
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   free(stencils);
   free(depths);
   free(colors);
   passed &= cleanup_ok;
   printf("[ps5-egl-core33-scissored-clear] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
