// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 64
#define HEIGHT 64

int ps5_egl_current_draw_status(unsigned *draw_calls);

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-core33-context-share] shader=0x%x log=%.*s\n",
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
      "void main(){gl_Position=vec4(position,0,1);}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform sampler2D shared_texture;\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){color=texture(shared_texture,vec2(.5));}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static const uint8_t green[4] = {0, 255, 0, 255};
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
   EGLContext contexts[2] = {EGL_NO_CONTEXT, EGL_NO_CONTEXT};
   EGLContext invalid_share = EGL_NO_CONTEXT;
   EGLint major = 0, minor = 0, count = 0;
   EGLint min_swap = -1, max_swap = -1;
   EGLint invalid_share_error = EGL_SUCCESS;
   EGLint low_interval_error = EGL_SUCCESS;
   EGLint high_interval_error = EGL_SUCCESS;
   GLuint shaders[2] = {0}, program = 0, texture = 0, vao = 0, vbo = 0;
   GLint linked = GL_FALSE;
   GLenum error = GL_NO_ERROR;
   uint8_t pixel[4] = {0};
   unsigned draw_calls = 0;
   int draw_status = -1, shared = 0, made_current = 0, passed = 0;
   EGLBoolean interval_one = EGL_FALSE, interval_zero = EGL_FALSE;
   EGLBoolean cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1 ||
       !eglGetConfigAttrib(display, config, EGL_MIN_SWAP_INTERVAL,
                           &min_swap) ||
       !eglGetConfigAttrib(display, config, EGL_MAX_SWAP_INTERVAL,
                           &max_swap))
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
   contexts[0] = eglCreateContext(display, config, EGL_NO_CONTEXT,
                                  context_attributes);
   invalid_share = eglCreateContext(
      display, config, (EGLContext)(uintptr_t)1, context_attributes);
   invalid_share_error = eglGetError();
   contexts[1] = eglCreateContext(display, config, contexts[0],
                                  context_attributes);
   if (surface == EGL_NO_SURFACE || contexts[0] == EGL_NO_CONTEXT ||
       contexts[1] == EGL_NO_CONTEXT || invalid_share != EGL_NO_CONTEXT ||
       invalid_share_error != EGL_BAD_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, contexts[0]))
      goto cleanup;
   made_current = 1;

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, green);

   if (!eglMakeCurrent(display, surface, surface, contexts[1]))
      goto cleanup;
   shared = glIsTexture(texture) == GL_TRUE;
   interval_one = eglSwapInterval(display, 1);
   interval_zero = eglSwapInterval(display, 0);
   eglSwapInterval(display, -1);
   low_interval_error = eglGetError();
   eglSwapInterval(display, 2);
   high_interval_error = eglGetError();

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[1]))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glUseProgram(program);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glUniform1i(glGetUniformLocation(program, "shared_texture"), 0);
   glViewport(0, 0, WIDTH, HEIGHT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1,
                GL_RGBA, GL_UNSIGNED_BYTE, pixel);
   error = glGetError();

   passed = major == 1 && minor == 4 && min_swap == 0 && max_swap == 1 &&
            shared && interval_one && interval_zero &&
            low_interval_error == EGL_BAD_PARAMETER &&
            high_interval_error == EGL_BAD_PARAMETER &&
            eglGetCurrentContext() == contexts[1] &&
            pixel[0] == 0 && pixel[1] == 255 &&
            pixel[2] == 0 && pixel[3] == 255 &&
            draw_status == 0 && draw_calls == 1 &&
            error == GL_NO_ERROR;
   printf("[ps5-egl-core33-context-share] share=%d swap=%d/%d/%d/%d "
          "pixel=%u/%u/%u/%u draw=%d/%u result=%d\n",
          shared, min_swap, max_swap, low_interval_error,
          high_interval_error, pixel[0], pixel[1], pixel[2], pixel[3],
          draw_status, draw_calls, passed ? 0 : 1);

cleanup:
   if (made_current) {
      if (texture)
         glDeleteTextures(1, &texture);
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
   for (unsigned i = 2; i-- > 0;)
      if (display != EGL_NO_DISPLAY && contexts[i] != EGL_NO_CONTEXT)
         cleanup_ok &= eglDestroyContext(display, contexts[i]);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf("[ps5-egl-core33-context-share] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
