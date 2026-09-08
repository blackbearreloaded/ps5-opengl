// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080

int ps5_egl_current_draw_status(unsigned *draw_calls);

static int
has_extension(const char *extensions, const char *name)
{
   const size_t length = strlen(name);
   const char *match = extensions;

   while (match && (match = strstr(match, name))) {
      if ((match == extensions || match[-1] == ' ') &&
          (match[length] == '\0' || match[length] == ' '))
         return 1;
      match += length;
   }
   return 0;
}

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return -1;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512];
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-rg-tile] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
   return 0;
}

static int
link_program(GLuint vertex, GLuint fragment, GLuint *result)
{
   GLint linked = GL_FALSE;
   GLuint program = glCreateProgram();

   if (!program)
      return -1;
   glAttachShader(program, vertex);
   glAttachShader(program, fragment);
   glBindAttribLocation(program, 0, "a_data");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      glDeleteProgram(program);
      return -1;
   }
   *result = program;
   return 0;
}

static int
run_case(GLenum internal_format, GLenum upload_format, const char *name,
         unsigned width, unsigned height, GLuint program, GLuint vbo,
         unsigned expected_calls)
{
   const float left = -0.5f / 255.0f;
   const float bottom = -0.5f / 255.0f;
   const float right = ((float)(2 * width) - 0.5f) / 255.0f;
   const float top = ((float)(2 * height) - 0.5f) / 255.0f;
   const float vertices[12] = {
      -1.0f, -1.0f, left,  bottom,
       3.0f, -1.0f, right, bottom,
      -1.0f,  3.0f, left,  top,
   };
   GLuint texture = 0;
   GLuint framebuffer = 0;
   GLenum status = 0;
   GLenum error;
   unsigned draw_calls = 0;
   int draw_status = -100;
   int passed;

   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
   glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), NULL);

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0,
                upload_format, GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   error = glGetError();
   if (status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR) {
      glViewport(0, 0, width, height);
      glUseProgram(program);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      draw_status = ps5_egl_current_draw_status(&draw_calls);
      glFinish();
      error = glGetError();
   }
   passed = status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR &&
            draw_status == 0 && draw_calls == expected_calls;
   printf("[ps5-egl-rg-tile] %s status=0x%x error=0x%x draw=%d/%u "
          "result=%d\n", name, status, error, draw_status, draw_calls,
          passed ? 0 : 1);

   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   if (framebuffer)
      glDeleteFramebuffers(1, &framebuffer);
   if (texture)
      glDeleteTextures(1, &texture);
   return passed;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec4 a_data;\n"
      "varying vec2 v_value;\n"
      "void main() {\n"
      "  gl_Position = vec4(a_data.xy, 0.0, 1.0);\n"
      "  v_value = a_data.zw;\n"
      "}\n";
   static const char *fragment_sources[3] = {
      "#version 120\n"
      "varying vec2 v_value;\n"
      "void main() { gl_FragColor = vec4(v_value.x, 0.0, 0.0, 1.0); }\n",
      "#version 120\n"
      "varying vec2 v_value;\n"
      "void main() { gl_FragColor = vec4(v_value.y, 0.0, 0.0, 1.0); }\n",
      "#version 120\n"
      "varying vec2 v_value;\n"
      "void main() { gl_FragColor = vec4(v_value, 0.0, 1.0); }\n",
   };
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0, width = 0, height = 0;
   GLuint vs = 0, fs[3] = {0}, programs[3] = {0}, vbo = 0;
   const GLubyte *version = NULL;
   const char *extensions = NULL;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0;
   int fbo_extension = 0, rg_extension = 0;
   int r8_x = 0, r8_y = 0, rg8_xy = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;
   eglQuerySurface(display, surface, EGL_WIDTH, &width);
   eglQuerySurface(display, surface, EGL_HEIGHT, &height);
   version = glGetString(GL_VERSION);
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!version || !extensions)
      goto cleanup;
   fbo_extension = has_extension(extensions, "GL_ARB_framebuffer_object") ||
                   has_extension(extensions, "GL_EXT_framebuffer_object");
   rg_extension = has_extension(extensions, "GL_ARB_texture_rg");

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs))
      goto cleanup;
   for (unsigned i = 0; i < 3; ++i) {
      if (compile_shader(GL_FRAGMENT_SHADER, fragment_sources[i], &fs[i]) ||
          link_program(vs, fs[i], &programs[i]))
         goto cleanup;
   }
   glGenBuffers(1, &vbo);
   glEnableVertexAttribArray(0);
   r8_x = run_case(GL_R8, GL_RED, "r8-x", 256, 256,
                   programs[0], vbo, 1);
   r8_y = run_case(GL_R8, GL_RED, "r8-y", 256, 256,
                   programs[1], vbo, 2);
   rg8_xy = run_case(GL_RG8, GL_RG, "rg8-xy", 256, 128,
                     programs[2], vbo, 3);
   passed = egl_major == 1 && egl_minor == 4 &&
            width == DISPLAY_WIDTH && height == DISPLAY_HEIGHT &&
            fbo_extension && rg_extension && r8_x && r8_y && rg8_xy &&
            strncmp((const char *)version, "2.1 ", 4) == 0 &&
            eglSwapBuffers(display, surface);

cleanup:
   if (vbo)
      glDeleteBuffers(1, &vbo);
   for (unsigned i = 0; i < 3; ++i) {
      if (programs[i])
         glDeleteProgram(programs[i]);
      if (fs[i])
         glDeleteShader(fs[i]);
   }
   if (vs)
      glDeleteShader(vs);
   if (made_current) {
      cleanup_gl_error = glGetError();
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE,
                                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY) {
      cleanup_ok &= eglTerminate(display);
      cleanup_egl_error = eglGetError();
   }
   passed &= cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
             cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-rg-tile] egl=%d.%d size=%dx%d ext=%d/%d "
          "cases=%d/%d/%d cleanup=%u/0x%x/0x%x result=%d\n",
          egl_major, egl_minor, width, height, fbo_extension, rg_extension,
          r8_x, r8_y, rg8_xy, cleanup_ok, cleanup_gl_error,
          cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
