// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

struct format_case {
   const char *name;
   GLenum internal_format;
   unsigned channels;
   float tolerance;
};

static const float source_pixels[16] = {
   1, 0, 0, 1,  0, 1, 0, 1,
   0, 0, 1, 1,  1, 1, 1, 1,
};

static int
close_enough(float actual, float expected, float tolerance)
{
   float difference = actual - expected;

   if (difference < 0.0f)
      difference = -difference;
   return difference <= tolerance;
}

static float
source_component(unsigned x, unsigned y, unsigned channel,
                 unsigned channels)
{
   if (channel >= channels)
      return channel == 3 ? 1.0f : 0.0f;
   return source_pixels[(y * 2u + x) * 4u + channel];
}

static float
expected_component(unsigned x, unsigned y, unsigned channel,
                   unsigned channels)
{
   float fx = ((2.0f * x + 1.0f) * 2.0f / 8.0f) - 0.5f;
   float fy = ((2.0f * y + 1.0f) * 2.0f / 8.0f) - 0.5f;
   int x0 = (int)fx;
   int y0 = (int)fy;
   float tx;
   float ty;
   unsigned ix0;
   unsigned ix1;
   unsigned iy0;
   unsigned iy1;
   float top;
   float bottom;

   if (fx < 0.0f)
      x0 = -1;
   if (fy < 0.0f)
      y0 = -1;
   tx = fx - x0;
   ty = fy - y0;
   ix0 = x0 < 0 ? 0u : x0 > 1 ? 1u : (unsigned)x0;
   ix1 = x0 + 1 < 0 ? 0u : x0 + 1 > 1 ? 1u : (unsigned)(x0 + 1);
   iy0 = y0 < 0 ? 0u : y0 > 1 ? 1u : (unsigned)y0;
   iy1 = y0 + 1 < 0 ? 0u : y0 + 1 > 1 ? 1u : (unsigned)(y0 + 1);
   top = source_component(ix0, iy0, channel, channels) * (1.0f - tx) +
         source_component(ix1, iy0, channel, channels) * tx;
   bottom = source_component(ix0, iy1, channel, channels) * (1.0f - tx) +
            source_component(ix1, iy1, channel, channels) * tx;
   return top * (1.0f - ty) + bottom * ty;
}

static int
run_linear_case(const struct format_case *test, const GLuint framebuffers[2],
                GLuint texture, GLuint renderbuffer)
{
   float pixels[4 * 4 * 4] = {0};
   GLenum source_status;
   GLenum destination_status;
   GLenum error;
   int passed = 1;

   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, test->internal_format, 2, 2, 0,
                GL_RGBA, GL_FLOAT, source_pixels);
   glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, test->internal_format, 4, 4);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
   glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   source_status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
   glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, renderbuffer);
   destination_status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
   if (source_status != GL_FRAMEBUFFER_COMPLETE ||
       destination_status != GL_FRAMEBUFFER_COMPLETE) {
      printf("[ps5-egl-render-blit] case=%s fbo=%x/%x result=1\n",
             test->name, source_status, destination_status);
      return 0;
   }

   glBlitFramebuffer(0, 0, 2, 2, 0, 0, 4, 4,
                     GL_COLOR_BUFFER_BIT, GL_LINEAR);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[1]);
   glReadPixels(0, 0, 4, 4, GL_RGBA, GL_FLOAT, pixels);
   error = glGetError();
   for (unsigned y = 0; y < 4; ++y) {
      for (unsigned x = 0; x < 4; ++x) {
         for (unsigned channel = 0; channel < 4; ++channel) {
            float expected = expected_component(x, y, channel,
                                                test->channels);
            float actual = pixels[(y * 4u + x) * 4u + channel];

            passed &= close_enough(actual, expected, test->tolerance);
         }
      }
   }
   passed &= error == GL_NO_ERROR;
   printf("[ps5-egl-render-blit] case=%s center=%.4f/%.4f/%.4f/%.4f "
          "error=0x%x result=%d\n",
          test->name, pixels[20], pixels[21], pixels[22], pixels[23],
          error, passed ? 0 : 1);
   return passed;
}

static int
run_conversion_case(const GLuint framebuffers[2], GLuint textures[2])
{
   static const float red[4] = {0.125f, 0.375f, 0.625f, 0.875f};
   float rgba[16] = {0};
   GLenum error;
   int passed = 1;

   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, 2, 2, 0,
                GL_RED, GL_FLOAT, red);
   glBindTexture(GL_TEXTURE_2D, textures[1]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 2, 2, 0,
                GL_RGBA, GL_FLOAT, NULL);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
   glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, textures[0], 0);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
   glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, textures[1], 0);
   if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) !=
          GL_FRAMEBUFFER_COMPLETE) {
      printf("[ps5-egl-render-blit] conversion-fbo result=1\n");
      return 0;
   }
   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
   if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) !=
          GL_FRAMEBUFFER_COMPLETE) {
      printf("[ps5-egl-render-blit] conversion-fbo result=1\n");
      return 0;
   }

   glBlitFramebuffer(0, 0, 2, 2, 0, 0, 2, 2,
                     GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[1]);
   glReadPixels(0, 0, 2, 2, GL_RGBA, GL_FLOAT, rgba);
   error = glGetError();
   for (unsigned pixel = 0; pixel < 4; ++pixel) {
      passed &= close_enough(rgba[pixel * 4], red[pixel], 0.001f);
      passed &= rgba[pixel * 4 + 1] == 0.0f;
      passed &= rgba[pixel * 4 + 2] == 0.0f;
      passed &= rgba[pixel * 4 + 3] == 1.0f;
   }
   passed &= error == GL_NO_ERROR;
   printf("[ps5-egl-render-blit] conversion=R16F-RGBA32F error=0x%x "
          "result=%d\n", error, passed ? 0 : 1);
   return passed;
}

int
main(void)
{
   static const struct format_case cases[] = {
      {"r8-snorm", GL_R8_SNORM, 1, 0.012f},
      {"rg8-snorm", GL_RG8_SNORM, 2, 0.012f},
      {"rgba8-snorm", GL_RGBA8_SNORM, 4, 0.012f},
      {"r16", GL_R16, 1, 0.0001f},
      {"rg16", GL_RG16, 2, 0.0001f},
      {"rgba16", GL_RGBA16, 4, 0.0001f},
      {"r16-snorm", GL_R16_SNORM, 1, 0.0001f},
      {"rg16-snorm", GL_RG16_SNORM, 2, 0.0001f},
      {"rgba16-snorm", GL_RGBA16_SNORM, 4, 0.0001f},
      {"r16f", GL_R16F, 1, 0.001f},
      {"rg16f", GL_RG16F, 2, 0.001f},
      {"rgba16f", GL_RGBA16F, 4, 0.001f},
      {"r32f", GL_R32F, 1, 0.00001f},
      {"rg32f", GL_RG32F, 2, 0.00001f},
      {"rgba32f", GL_RGBA32F, 4, 0.00001f},
      {"rgb10-a2", GL_RGB10_A2, 4, 0.003f},
      {"r11g11b10f", GL_R11F_G11F_B10F, 3, 0.012f},
   };
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
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint major = 0, minor = 0, count = 0;
   GLuint framebuffers[2] = {0}, textures[2] = {0}, renderbuffer = 0;
   unsigned matching = 0;
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

   glGenFramebuffers(2, framebuffers);
   glGenTextures(2, textures);
   for (unsigned i = 0; i < 2; ++i) {
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   }
   glGenRenderbuffers(1, &renderbuffer);
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
      matching += run_linear_case(&cases[i], framebuffers,
                                  textures[0], renderbuffer);
   matching += run_conversion_case(framebuffers, textures);
   passed = major == 1 && minor == 4 &&
            matching == sizeof(cases) / sizeof(cases[0]) + 1 &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-render-blit] matching=%u result=%d\n",
          matching, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffers[0] || framebuffers[1])
         glDeleteFramebuffers(2, framebuffers);
      if (textures[0] || textures[1])
         glDeleteTextures(2, textures);
      if (renderbuffer)
         glDeleteRenderbuffers(1, &renderbuffer);
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
   printf("[ps5-egl-render-blit] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
