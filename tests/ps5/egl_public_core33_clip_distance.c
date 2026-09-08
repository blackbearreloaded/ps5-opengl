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
#define WHITE UINT32_C(0xffffffff)

int ps5_egl_current_draw_status(unsigned *draw_calls);

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
      printf("[ps5-egl-clip-distance] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static int
link_program(GLuint vertex, GLuint fragment, GLuint *result)
{
   GLint linked = GL_FALSE;
   GLuint program = glCreateProgram();

   glAttachShader(program, vertex);
   glAttachShader(program, fragment);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetProgramInfoLog(program, sizeof(log), &length, log);
      printf("[ps5-egl-clip-distance] link log=%.*s\n", length, log);
      glDeleteProgram(program);
      return 0;
   }
   *result = program;
   return 1;
}

static unsigned
draw_and_count(GLuint program, GLenum distance, int enabled,
               uint32_t *pixels, int *status, unsigned *draw_calls)
{
   unsigned matching = 0;

   for (unsigned i = 0; i < 8; ++i)
      glDisable(GL_CLIP_DISTANCE0 + i);
   if (enabled)
      glEnable(distance);
   glUseProgram(program);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   *status = ps5_egl_current_draw_status(draw_calls);
   glFinish();
   glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   for (unsigned i = 0; i < PIXELS; ++i)
      matching += pixels[i] == WHITE;
   return matching;
}

int
main(void)
{
   static const char *vertex0_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() {\n"
      "  gl_Position = vec4(position, 0.0, 1.0);\n"
      "  gl_ClipDistance[0] = position.x;\n"
      "}\n";
   static const char *vertex1_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() {\n"
      "  gl_Position = vec4(position, 0.0, 1.0);\n"
      "  gl_ClipDistance[1] = position.x;\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = vec4(1.0); }\n";
   static const float vertices[] = {
      -1.0f, -1.0f,
       3.0f, -1.0f,
      -1.0f,  3.0f,
   };
   static uint32_t pixels[PIXELS];
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
   GLuint shaders[3] = {0, 0, 0};
   GLuint programs[2] = {0, 0};
   GLuint vertex_array = 0, vertex_buffer = 0;
   GLint max_distances = 0;
   unsigned white[4] = {0, 0, 0, 0};
   unsigned draw_calls = 0;
   int status[4] = {-1, -1, -1, -1};
   GLenum error = GL_NO_ERROR;
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

   if (!compile_shader(GL_VERTEX_SHADER, vertex0_source, &shaders[0]) ||
       !compile_shader(GL_VERTEX_SHADER, vertex1_source, &shaders[1]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[2]) ||
       !link_program(shaders[0], shaders[2], &programs[0]) ||
       !link_program(shaders[1], shaders[2], &programs[1]))
      goto cleanup;

   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
   glGenBuffers(1, &vertex_buffer);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glViewport(0, 0, WIDTH, HEIGHT);
   glGetIntegerv(GL_MAX_CLIP_DISTANCES, &max_distances);

   white[0] = draw_and_count(programs[0], GL_CLIP_DISTANCE0, 0,
                             pixels, &status[0], &draw_calls);
   white[1] = draw_and_count(programs[0], GL_CLIP_DISTANCE0, 1,
                             pixels, &status[1], &draw_calls);
   white[2] = draw_and_count(programs[1], GL_CLIP_DISTANCE1, 0,
                             pixels, &status[2], &draw_calls);
   white[3] = draw_and_count(programs[1], GL_CLIP_DISTANCE1, 1,
                             pixels, &status[3], &draw_calls);
   error = glGetError();
   passed = max_distances >= 8 &&
            status[0] == 0 && status[1] == 0 &&
            status[2] == 0 && status[3] == 0 &&
            white[0] == PIXELS && white[1] == PIXELS / 2 &&
            white[2] == PIXELS && white[3] == PIXELS / 2 &&
            error == GL_NO_ERROR;

cleanup:
   for (unsigned i = 0; i < 8; ++i)
      if (made_current)
         glDisable(GL_CLIP_DISTANCE0 + i);
   if (made_current) {
      glUseProgram(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glBindVertexArray(0);
      if (vertex_buffer)
         glDeleteBuffers(1, &vertex_buffer);
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
      for (unsigned i = 0; i < 2; ++i)
         if (programs[i])
            glDeleteProgram(programs[i]);
      for (unsigned i = 0; i < 3; ++i)
         if (shaders[i])
            glDeleteShader(shaders[i]);
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
   printf("[ps5-egl-clip-distance] max=%d white=%u/%u/%u/%u "
          "status=%d/%d/%d/%d draws=%u error=%x cleanup=%u result=%d\n",
          max_distances, white[0], white[1], white[2], white[3],
          status[0], status[1], status[2], status[3], draw_calls,
          error, cleanup_ok == EGL_TRUE, passed);
   return passed ? 0 : 1;
}
