// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 128
#define HEIGHT 96
#define PIXELS (WIDTH * HEIGHT)

int ps5_egl_current_draw_status(unsigned *draw_calls);

#ifdef PS5_PUBLIC_RENDER_TARGET_LIMITS_TEST
extern int sceKernelDebugOutText(int channel, const char *text);

/* Supplemental bounded allocation test; the original full-image oracle below
 * still runs unchanged. Never submit a draw against an incomplete target. */
static int
check_extent(unsigned width, unsigned height, int renderbuffer)
{
   GLuint object = 0;
   GLenum error, status;
   unsigned matches = 0, draws = 0;
   int draw_status = -1;
   int submitted = 0;
   const unsigned probes[5][2] = {
      {0, 0}, {width - 1, 0}, {0, height - 1},
      {width - 1, height - 1}, {width / 2, height / 2},
   };

   printf("[ps5-egl-depth-limits] begin %s %ux%u\n",
          renderbuffer ? "renderbuffer" : "texture", width, height);
   if (renderbuffer) {
      glGenRenderbuffers(1, &object);
      glBindRenderbuffer(GL_RENDERBUFFER, object);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F,
                            width, height);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_RENDERBUFFER, object);
   } else {
      glGenTextures(1, &object);
      glBindTexture(GL_TEXTURE_2D, object);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             GL_TEXTURE_2D, object, 0);
   }
   error = glGetError();
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (error == GL_NO_ERROR && status == GL_FRAMEBUFFER_COMPLETE) {
      glViewport(0, 0, width, height);
      glClear(GL_DEPTH_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      submitted = 1;
      draw_status = ps5_egl_current_draw_status(&draws);
      glFinish();
      for (unsigned i = 0; i < 5; ++i) {
         float depth = -1.0f;
         glReadPixels(probes[i][0], probes[i][1], 1, 1,
                      GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
         matches += depth > 0.249f && depth < 0.251f;
      }
   }
   GLenum final_error = glGetError();
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, 0);
   if (renderbuffer)
      glDeleteRenderbuffers(1, &object);
   else
      glDeleteTextures(1, &object);
   GLenum cleanup_error = glGetError();
   printf("[ps5-egl-depth-limits] %s %ux%u status=%x allocate=%x "
          "draw=%d/%u probes=%u/5 final=%x cleanup=%x\n",
          renderbuffer ? "renderbuffer" : "texture", width, height, status,
          error, draw_status, draws, matches, final_error, cleanup_error);
   if (submitted && draw_status != 0)
      return -1;
   return error == GL_NO_ERROR && final_error == GL_NO_ERROR &&
          cleanup_error == GL_NO_ERROR && status == GL_FRAMEBUFFER_COMPLETE &&
          draw_status == 0 && matches == 5;
}
#endif

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-depth-only] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, -0.5, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "void main() {}\n";
   static const float vertices[] = {
      -1.0f, -1.0f,
       3.0f, -1.0f,
      -1.0f,  3.0f,
   };
   static float depths[PIXELS];
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
   GLuint vertex_shader = 0, fragment_shader = 0, program = 0;
   GLuint vertex_array = 0, vertex_buffer = 0;
   GLuint depth_texture = 0, framebuffer = 0;
   GLint linked = GL_FALSE, draw_buffer = -1, read_buffer = -1;
   GLenum status = 0, error = GL_NO_ERROR;
   unsigned depth_matches = 0, draw_calls = 0;
   int draw_status = -1;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, passed = 0;

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

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex_shader) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fragment_shader))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vertex_shader);
   glAttachShader(program, fragment_shader);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;

   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
   glGenBuffers(1, &vertex_buffer);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &depth_texture);
   glBindTexture(GL_TEXTURE_2D, depth_texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                WIDTH, HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                          GL_TEXTURE_2D, depth_texture, 0);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glGetIntegerv(GL_DRAW_BUFFER, &draw_buffer);
   glGetIntegerv(GL_READ_BUFFER, &read_buffer);

   glViewport(0, 0, WIDTH, HEIGHT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_ALWAYS);
   glDepthMask(GL_TRUE);
   glClearDepth(1.0);
   glClear(GL_DEPTH_BUFFER_BIT);
   glUseProgram(program);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels(0, 0, WIDTH, HEIGHT, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
   for (unsigned i = 0; i < PIXELS; ++i)
      depth_matches += depths[i] > 0.249f && depths[i] < 0.251f;
   error = glGetError();
   passed = status == GL_FRAMEBUFFER_COMPLETE &&
            draw_buffer == GL_NONE && read_buffer == GL_NONE &&
            draw_status == 0 && depth_matches == PIXELS &&
            error == GL_NO_ERROR;
#ifdef PS5_PUBLIC_RENDER_TARGET_LIMITS_TEST
   if (passed) {
      static const unsigned extents[][2] = {
         {1920, 1080}, {64, 8192}, {8192, 64}, {2048, 2048},
         {3072, 3072}, {4096, 4096}, {128, 96},
      };
      /* Each depth request is at most 64 MiB; driver references may outlive
       * deletion. The last case checks recovery after an allocation failure. */
      unsigned successes = 0;
      for (unsigned i = 0; i < sizeof(extents) / sizeof(extents[0]); ++i) {
         for (unsigned renderbuffer = 0; renderbuffer < 2; ++renderbuffer) {
            int result = check_extent(extents[i][0], extents[i][1], renderbuffer);
            if (result < 0) {
               passed = 0;
               goto cleanup;
            }
            successes += result;
         }
      }
      printf("[ps5-egl-depth-limits] batch=%u/14\n", successes);
      passed = successes == 14;
   }
#endif

cleanup:
   if (made_current) {
      glDisable(GL_DEPTH_TEST);
      glUseProgram(0);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindTexture(GL_TEXTURE_2D, 0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glBindVertexArray(0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (depth_texture)
         glDeleteTextures(1, &depth_texture);
      if (vertex_buffer)
         glDeleteBuffers(1, &vertex_buffer);
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
      if (program)
         glDeleteProgram(program);
      if (fragment_shader)
         glDeleteShader(fragment_shader);
      if (vertex_shader)
         glDeleteShader(vertex_shader);
      cleanup_ok = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                  EGL_NO_CONTEXT);
   }
   if (surface != EGL_NO_SURFACE)
      cleanup_ok = eglDestroySurface(display, surface) && cleanup_ok;
   if (context != EGL_NO_CONTEXT)
      cleanup_ok = eglDestroyContext(display, context) && cleanup_ok;
   if (display != EGL_NO_DISPLAY)
      cleanup_ok = eglTerminate(display) && cleanup_ok;
   passed = passed && cleanup_ok;
   printf("[ps5-egl-depth-only] status=%x buffers=%x/%x depth=%u/%u "
          "draw=%d/%u error=%x cleanup=%u result=%d\n",
          status, draw_buffer, read_buffer, depth_matches, PIXELS,
          draw_status, draw_calls, error, cleanup_ok == EGL_TRUE, passed);
#ifdef PS5_PUBLIC_RENDER_TARGET_LIMITS_TEST
   sceKernelDebugOutText(0, "[ps5-egl-depth-limits] finished\n");
#endif
   return passed ? 0 : 1;
}
