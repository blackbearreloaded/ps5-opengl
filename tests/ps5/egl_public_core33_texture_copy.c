// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

static int
pixel_is(const uint8_t *pixel, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
   return pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == a;
}

int
main(void)
{
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
   const GLfloat red[4] = {1, 0, 0, 1};
   const GLfloat green[4] = {0, 1, 0, 1};
   const GLfloat blue[4] = {0, 0, 1, 1};
   const uint8_t red_pixel[4] = {255, 0, 0, 255};
   uint8_t pixels_2d[4 * 4 * 4] = {0};
   uint8_t pixels_1d[4 * 4] = {0};
   uint8_t pixels_3d[2 * 2 * 2 * 4] = {0};
   uint8_t zero_3d[2 * 2 * 2 * 4] = {0};
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint major = 0, minor = 0, count = 0;
   GLuint framebuffer = 0, renderbuffer = 0;
   GLuint textures[3] = {0};
   GLenum status = 0, status_3d = 0, error = GL_NO_ERROR;
   int made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;

   glGenFramebuffers(1, &framebuffer);
   glGenRenderbuffers(1, &renderbuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 4, 4);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, renderbuffer);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE)
      goto cleanup;

   glGenTextures(3, textures);
   glClearBufferfv(GL_COLOR, 0, red);
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 4, 4, 0);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_2d);
   passed = 1;
   for (unsigned i = 0; i < 16; ++i)
      passed &= pixel_is(pixels_2d + i * 4u, 255, 0, 0, 255);

   glClearBufferfv(GL_COLOR, 0, green);
   glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 0, 0, 2, 2);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_2d);
   for (unsigned y = 0; y < 4; ++y) {
      for (unsigned x = 0; x < 4; ++x) {
         const uint8_t *pixel = pixels_2d + (y * 4u + x) * 4u;
         int inside = x >= 1 && x <= 2 && y >= 1 && y <= 2;

         passed &= inside ? pixel_is(pixel, 0, 255, 0, 255)
                          : pixel_is(pixel, 255, 0, 0, 255);
      }
   }

   glClearBufferfv(GL_COLOR, 0, blue);
   glBindTexture(GL_TEXTURE_1D, textures[1]);
   glCopyTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 0, 0, 4, 0);
   glClearBufferfv(GL_COLOR, 0, green);
   glCopyTexSubImage1D(GL_TEXTURE_1D, 0, 1, 0, 0, 2);
   glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 1,
                   GL_RGBA, GL_UNSIGNED_BYTE, red_pixel);
   glGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_1d);
   passed &= pixel_is(pixels_1d + 0, 255, 0, 0, 255);
   passed &= pixel_is(pixels_1d + 4, 0, 255, 0, 255);
   passed &= pixel_is(pixels_1d + 8, 0, 255, 0, 255);
   passed &= pixel_is(pixels_1d + 12, 0, 0, 255, 255);

   glClearBufferfv(GL_COLOR, 0, blue);
   glBindTexture(GL_TEXTURE_3D, textures[2]);
   glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 2, 2, 2, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, zero_3d);
   glCopyTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 1, 0, 0, 2, 2);
   glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 1, 1, 1, 1,
                   GL_RGBA, GL_UNSIGNED_BYTE, red_pixel);
   glFramebufferTexture3D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_3D, textures[2], 0, 0);
   status_3d = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glClearBufferfv(GL_COLOR, 0, green);
   glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels_3d);
   for (unsigned i = 0; i < 4; ++i)
      passed &= pixel_is(pixels_3d + i * 4u, 0, 255, 0, 255);
   passed &= pixel_is(pixels_3d + 4 * 4u, 255, 0, 0, 255);
   for (unsigned i = 5; i < 8; ++i)
      passed &= pixel_is(pixels_3d + i * 4u, 0, 0, 255, 255);

   error = glGetError();
   passed &= status_3d == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR;
   printf("[ps5-egl-core33-texture-copy] fbo=0x%x/0x%x error=0x%x "
          "result=%d\n", status, status_3d, error, passed ? 0 : 1);

cleanup:
   if (textures[0] || textures[1] || textures[2])
      glDeleteTextures(3, textures);
   if (renderbuffer)
      glDeleteRenderbuffers(1, &renderbuffer);
   if (framebuffer)
      glDeleteFramebuffers(1, &framebuffer);
   if (made_current) {
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
   passed &= cleanup_ok;
   printf("[ps5-egl-core33-texture-copy] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
