// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080

extern int sceKernelUsleep(uint32_t microseconds);

static GLuint
compile_shader(GLenum type, const char *source)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512];
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-visible] shader type=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   return shader;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "uniform float offset_x;\n"
      "void main() { gl_Position = vec4(position.x + offset_x, "
      "position.y, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform vec3 tint;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = vec4(tint, 1.0); }\n";
   static const float vertices[6] = {
      -0.32f, -0.35f, 0.32f, -0.35f, 0.0f, 0.35f,
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
   EGLDisplay display;
   EGLConfig config = NULL;
   EGLSurface surface;
   EGLContext context;
   EGLint major = 0, minor = 0, count = 0;
   GLuint vertex_shader, fragment_shader, program, vao, vbo;
   GLint linked = GL_FALSE;
   GLint offset_location, tint_location;
   unsigned frame = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &major, &minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      return 1;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      return 1;

   vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
   fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
   if (!vertex_shader || !fragment_shader)
      return 1;
   program = glCreateProgram();
   glAttachShader(program, vertex_shader);
   glAttachShader(program, fragment_shader);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      return 1;
   glUseProgram(program);
   offset_location = glGetUniformLocation(program, "offset_x");
   tint_location = glGetUniformLocation(program, "tint");

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);
   glViewport(0, 0, WIDTH, HEIGHT);

   for (;;) {
      unsigned step = frame % 13u;
      float offset = -0.6f + (float)step * 0.1f;
      float green = (float)(frame & 1u);

      glUniform1f(offset_location, offset);
      glUniform3f(tint_location, 1.0f - green, green, 1.0f);
      glClearColor(0.02f, 0.04f, 0.12f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      if (glGetError() != GL_NO_ERROR ||
          !eglSwapBuffers(display, surface)) {
         printf("[ps5-egl-visible] frame=%u failed gl=0x%x egl=0x%x\n",
                frame, glGetError(), eglGetError());
         return 1;
      }
      if (frame == 0) {
         printf("[ps5-egl-visible] ready egl=%d.%d gl=%s glsl=%s\n",
                major, minor, glGetString(GL_VERSION),
                glGetString(GL_SHADING_LANGUAGE_VERSION));
         printf("[pss-opengl-native] gate completed status=0\n");
      }
      ++frame;
      sceKernelUsleep(UINT32_C(1000000));
   }
}
