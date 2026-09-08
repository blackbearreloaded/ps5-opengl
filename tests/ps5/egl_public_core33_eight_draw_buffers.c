// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define TARGETS 8
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
      printf("[ps5-egl-eight-mrt] shader=0x%x log=%.*s\n",
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
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "layout(location=0) out vec4 c0;\n"
      "layout(location=1) out vec4 c1;\n"
      "layout(location=2) out vec4 c2;\n"
      "layout(location=3) out vec4 c3;\n"
      "layout(location=4) out vec4 c4;\n"
      "layout(location=5) out vec4 c5;\n"
      "layout(location=6) out vec4 c6;\n"
      "layout(location=7) out vec4 c7;\n"
      "void main() {\n"
      "  c0=vec4(1,0,0,1); c1=vec4(0,1,0,1);\n"
      "  c2=vec4(0,0,1,1); c3=vec4(1,1,1,1);\n"
      "  c4=vec4(1,1,0,1); c5=vec4(0,1,1,1);\n"
      "  c6=vec4(1,0,1,1); c7=vec4(0,0,0,1);\n"
      "}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static const GLenum attachments[TARGETS] = {
      GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
      GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,
      GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5,
      GL_COLOR_ATTACHMENT6, GL_COLOR_ATTACHMENT7,
   };
   static const uint32_t expected[TARGETS] = {
      UINT32_C(0xff0000ff), UINT32_C(0xff00ff00),
      UINT32_C(0xffff0000), UINT32_C(0xffffffff),
      UINT32_C(0xff00ffff), UINT32_C(0xffffff00),
      UINT32_C(0xffff00ff), UINT32_C(0xff000000),
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
   GLuint framebuffer = 0, renderbuffers[TARGETS] = {0};
   GLint linked = GL_FALSE, max_draw_buffers = 0, max_attachments = 0;
   GLenum status = 0, error = GL_NO_ERROR;
   uint32_t pixels[TARGETS] = {0};
   int draw_status = -1;
   unsigned draw_calls = 0;
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

   glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);
   glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_attachments);
   if (max_draw_buffers < TARGETS || max_attachments < TARGETS ||
       !compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
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

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glGenRenderbuffers(TARGETS, renderbuffers);
   for (unsigned i = 0; i < TARGETS; ++i) {
      glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[i]);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachments[i],
                                GL_RENDERBUFFER, renderbuffers[i]);
   }
   glDrawBuffers(TARGETS, attachments);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glUseProgram(program);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   for (unsigned i = 0; i < TARGETS; ++i) {
      glReadBuffer(attachments[i]);
      glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1,
                   GL_RGBA, GL_UNSIGNED_BYTE, &pixels[i]);
   }
   error = glGetError();
   passed = major == 1 && minor == 4 &&
            max_draw_buffers >= TARGETS && max_attachments >= TARGETS &&
            status == GL_FRAMEBUFFER_COMPLETE && draw_status == 0 &&
            draw_calls == 1 && error == GL_NO_ERROR;
   for (unsigned i = 0; i < TARGETS; ++i)
      passed &= pixels[i] == expected[i];

   printf("[ps5-egl-eight-mrt] limits=%d/%d status=%x draw=%d/%u "
          "pixels=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x "
          "error=0x%x result=%d\n",
          max_draw_buffers, max_attachments, status, draw_status, draw_calls,
          pixels[0], pixels[1], pixels[2], pixels[3], pixels[4], pixels[5],
          pixels[6], pixels[7], error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      glDeleteRenderbuffers(TARGETS, renderbuffers);
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
   printf("[ps5-egl-eight-mrt] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
