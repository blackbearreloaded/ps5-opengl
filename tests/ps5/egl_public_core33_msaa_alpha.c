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

int ps5_egl_current_draw_status(unsigned *draw_calls);

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint ok = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-msaa-alpha] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static int
draw_and_resolve(GLuint program, GLint color_location, GLuint msaa_fbo,
                 GLuint resolve_fbo, float alpha, uint8_t *minimum,
                 uint8_t *maximum)
{
   static uint32_t pixels[WIDTH * HEIGHT];
   uint8_t low = 255;
   uint8_t high = 0;

   glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glUseProgram(program);
   glUniform4f(color_location, 1.0f, 0.0f, 0.0f, alpha);
   glDrawArrays(GL_TRIANGLES, 0, 3);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
   glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT,
                     GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glFinish();
   glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   if (glGetError() != GL_NO_ERROR)
      return 0;
   for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
      uint8_t red = (uint8_t)pixels[i];

      if (red < low)
         low = red;
      if (red > high)
         high = red;
   }
   *minimum = low;
   *maximum = high;
   return 1;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position=vec4(position,0,1); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform vec4 source_color;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color=source_color; }\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
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
   GLuint shaders[2] = {0, 0}, program = 0, vao = 0, vbo = 0;
   GLuint msaa_fbo = 0, resolve_fbo = 0, buffers[2] = {0, 0};
   GLint linked = GL_FALSE, max_samples = 0, color_location = -1;
   GLenum msaa_status = 0, resolve_status = 0, error = GL_NO_ERROR;
   uint8_t control_min = 0, control_max = 0;
   uint8_t zero_min = 0, zero_max = 0;
   uint8_t half_min = 0, half_max = 0;
   uint8_t one_min = 0, one_max = 0;
   uint8_t blend_min = 0, blend_max = 0;
   uint8_t alpha_one_min = 0, alpha_one_max = 0;
   unsigned draw_calls = 0;
   int draw_status = -1, made_current = 0, passed = 0;
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

   glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
   if (max_samples < 4 ||
       !compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[1]))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   color_location = glGetUniformLocation(program, "source_color");
   if (!linked || color_location < 0)
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenRenderbuffers(2, buffers);
   glBindRenderbuffer(GL_RENDERBUFFER, buffers[0]);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8,
                                    WIDTH, HEIGHT);
   glGenFramebuffers(1, &msaa_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, buffers[0]);
   msaa_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glBindRenderbuffer(GL_RENDERBUFFER, buffers[1]);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
   glGenFramebuffers(1, &resolve_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, buffers[1]);
   resolve_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (msaa_status != GL_FRAMEBUFFER_COMPLETE ||
       resolve_status != GL_FRAMEBUFFER_COMPLETE ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glEnable(GL_MULTISAMPLE);
   glDisable(GL_BLEND);
   glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
   glDisable(GL_SAMPLE_ALPHA_TO_ONE);
   if (!draw_and_resolve(program, color_location, msaa_fbo, resolve_fbo,
                         0.5f, &control_min, &control_max))
      goto cleanup;
   glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
   if (!draw_and_resolve(program, color_location, msaa_fbo, resolve_fbo,
                         0.0f, &zero_min, &zero_max) ||
       !draw_and_resolve(program, color_location, msaa_fbo, resolve_fbo,
                         0.5f, &half_min, &half_max) ||
       !draw_and_resolve(program, color_location, msaa_fbo, resolve_fbo,
                         1.0f, &one_min, &one_max))
      goto cleanup;

   glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
   if (!draw_and_resolve(program, color_location, msaa_fbo, resolve_fbo,
                         0.25f, &blend_min, &blend_max))
      goto cleanup;
   glEnable(GL_SAMPLE_ALPHA_TO_ONE);
   if (!draw_and_resolve(program, color_location, msaa_fbo, resolve_fbo,
                         0.25f, &alpha_one_min, &alpha_one_max))
      goto cleanup;

   draw_status = ps5_egl_current_draw_status(&draw_calls);
   error = glGetError();
   passed = major == 1 && minor == 4 && max_samples >= 4 &&
            control_min == 255 && control_max == 255 &&
            zero_min == 0 && zero_max == 0 &&
            half_min > 0 && half_max < 255 &&
            one_min == 255 && one_max == 255 &&
            blend_min >= 63 && blend_max <= 64 &&
            alpha_one_min == 255 && alpha_one_max == 255 &&
            draw_status == 0 && draw_calls == 6 && error == GL_NO_ERROR;
   printf("[ps5-egl-msaa-alpha] limits=%d status=%x/%x "
          "control=%u/%u atoc=%u/%u,%u/%u,%u/%u "
          "a2one=%u/%u,%u/%u draw=%d/%u error=0x%x result=%d\n",
          max_samples, msaa_status, resolve_status,
          control_min, control_max, zero_min, zero_max,
          half_min, half_max, one_min, one_max,
          blend_min, blend_max, alpha_one_min, alpha_one_max,
          draw_status, draw_calls, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (msaa_fbo)
         glDeleteFramebuffers(1, &msaa_fbo);
      if (resolve_fbo)
         glDeleteFramebuffers(1, &resolve_fbo);
      glDeleteRenderbuffers(2, buffers);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      if (program)
         glDeleteProgram(program);
      for (unsigned i = 0; i < 2; ++i)
         if (shaders[i])
            glDeleteShader(shaders[i]);
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
   printf("[ps5-egl-msaa-alpha] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
