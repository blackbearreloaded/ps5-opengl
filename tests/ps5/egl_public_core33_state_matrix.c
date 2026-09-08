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
#define HEIGHT 64
#define BLACK UINT32_C(0xff000000)
#define RED UINT32_C(0xff0000ff)
#define GREEN UINT32_C(0xff00ff00)
#define WHITE UINT32_C(0xffffffff)
#define TRANSPARENT_YELLOW UINT32_C(0x0000ffff)

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
      printf("[ps5-egl-state-matrix] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static uint32_t
read_color(int x)
{
   uint32_t pixel = 0;

   glFinish();
   glReadPixels(x, HEIGHT / 2, 1, 1,
                GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
   return pixel;
}

static uint8_t
read_stencil(int x)
{
   uint8_t value = 0;

   glFinish();
   glReadPixels(x, HEIGHT / 2, 1, 1,
                GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &value);
   return value;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec3 position;\n"
      "void main() { gl_Position=vec4(position,1); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform vec4 source_color;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color=source_color; }\n";
   static const float vertices[] = {
      -1.0f, -1.0f, 0.0f,
       3.0f, -1.0f, 0.0f,
      -1.0f,  3.0f, 0.0f,
      -0.9f, -0.7f, 0.0f,
      -0.1f, -0.7f, 0.0f,
      -0.5f,  0.7f, 0.0f,
       0.1f, -0.7f, 0.0f,
       0.5f,  0.7f, 0.0f,
       0.9f, -0.7f, 0.0f,
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
   GLuint framebuffer = 0, color = 0, depth_stencil = 0;
   GLint linked = GL_FALSE, source_color = -1;
   GLint blend_rgb = 0, blend_alpha = 0, polygon_mode[2] = {0, 0};
   GLfloat blend_color[4] = {0};
   GLdouble depth_range[2] = {0};
   GLenum status = 0, error = GL_NO_ERROR;
   uint32_t blend_pixel = 0, stencil_pixels[2] = {0, 0};
   uint32_t cull_back[2] = {0, 0}, cull_front[2] = {0, 0};
   uint32_t polygon_line = 0, polygon_fill = 0, offset_pixel = 0;
   uint8_t stencil_values[2] = {0, 0};
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

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[1]))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   source_color = glGetUniformLocation(program, "source_color");
   if (!linked || source_color < 0)
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenRenderbuffers(1, &color);
   glBindRenderbuffer(GL_RENDERBUFFER, color);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
   glGenRenderbuffers(1, &depth_stencil);
   glBindRenderbuffer(GL_RENDERBUFFER, depth_stencil);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8, WIDTH, HEIGHT);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, color);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                             GL_RENDERBUFFER, depth_stencil);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR)
      goto cleanup;

   glUseProgram(program);
   glViewport(0, 0, WIDTH, HEIGHT);
   glDepthRange(0.0, 1.0);
   glGetDoublev(GL_DEPTH_RANGE, depth_range);

   glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0);
   glEnablei(GL_BLEND, 0);
   glBlendColor(1.0f, 0.0f, 1.0f, 1.0f);
   glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_REVERSE_SUBTRACT);
   glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR,
                       GL_ONE, GL_ONE);
   glGetFloatv(GL_BLEND_COLOR, blend_color);
   glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_rgb);
   glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_alpha);
   glUniform4f(source_color, 1.0f, 0.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   blend_pixel = read_color(WIDTH / 2);

   glDisablei(GL_BLEND, 0);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0);
   glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
   glEnable(GL_STENCIL_TEST);
   glStencilMaskSeparate(GL_FRONT, 0xff);
   glStencilMaskSeparate(GL_BACK, 0xff);
   glStencilFuncSeparate(GL_FRONT, GL_ALWAYS, 0x12, 0xff);
   glStencilFuncSeparate(GL_BACK, GL_ALWAYS, 0x34, 0xff);
   glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_REPLACE);
   glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_REPLACE);
   glUniform4f(source_color, 1.0f, 1.0f, 1.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 3, 6);
   stencil_values[0] = read_stencil(WIDTH / 4);
   stencil_values[1] = read_stencil(3 * WIDTH / 4);

   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glStencilMaskSeparate(GL_FRONT_AND_BACK, 0x00);
   glStencilFuncSeparate(GL_FRONT, GL_EQUAL, 0x12, 0xff);
   glStencilFuncSeparate(GL_BACK, GL_EQUAL, 0x34, 0xff);
   glStencilOpSeparate(GL_FRONT_AND_BACK, GL_KEEP, GL_KEEP, GL_KEEP);
   glDrawArrays(GL_TRIANGLES, 3, 6);
   stencil_pixels[0] = read_color(WIDTH / 4);
   stencil_pixels[1] = read_color(3 * WIDTH / 4);

   glDisable(GL_STENCIL_TEST);
   glClear(GL_COLOR_BUFFER_BIT);
   glEnable(GL_CULL_FACE);
   glFrontFace(GL_CCW);
   glCullFace(GL_BACK);
   glDrawArrays(GL_TRIANGLES, 3, 6);
   cull_back[0] = read_color(WIDTH / 4);
   cull_back[1] = read_color(3 * WIDTH / 4);
   glClear(GL_COLOR_BUFFER_BIT);
   glCullFace(GL_FRONT);
   glDrawArrays(GL_TRIANGLES, 3, 6);
   cull_front[0] = read_color(WIDTH / 4);
   cull_front[1] = read_color(3 * WIDTH / 4);

   glDisable(GL_CULL_FACE);
   glClear(GL_COLOR_BUFFER_BIT);
   glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
   glGetIntegerv(GL_POLYGON_MODE, polygon_mode);
   glDrawArrays(GL_TRIANGLES, 3, 3);
   polygon_line = read_color(WIDTH / 4);
   glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
   glDrawArrays(GL_TRIANGLES, 3, 3);
   polygon_fill = read_color(WIDTH / 4);

   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glUniform4f(source_color, 1.0f, 0.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 3, 3);
   glUniform4f(source_color, 0.0f, 1.0f, 0.0f, 1.0f);
   glEnable(GL_POLYGON_OFFSET_FILL);
   glPolygonOffset(0.0f, -1.0f);
   glDrawArrays(GL_TRIANGLES, 3, 3);
   offset_pixel = read_color(WIDTH / 4);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            depth_range[0] == 0.0 && depth_range[1] == 1.0 &&
            blend_color[0] == 1.0f && blend_color[1] == 0.0f &&
            blend_color[2] == 1.0f && blend_color[3] == 1.0f &&
            blend_rgb == GL_FUNC_ADD &&
            blend_alpha == GL_FUNC_REVERSE_SUBTRACT &&
            blend_pixel == TRANSPARENT_YELLOW &&
            stencil_values[0] == 0x12 && stencil_values[1] == 0x34 &&
            stencil_pixels[0] == WHITE && stencil_pixels[1] == WHITE &&
            cull_back[0] == WHITE && cull_back[1] == BLACK &&
            cull_front[0] == BLACK && cull_front[1] == WHITE &&
            polygon_mode[0] == GL_LINE && polygon_mode[1] == GL_LINE &&
            polygon_line == BLACK && polygon_fill == WHITE &&
            offset_pixel == GREEN && draw_status == 0 &&
            draw_calls == 9 && error == GL_NO_ERROR;
   printf("[ps5-egl-state-matrix] blend=%08x stencil=%02x/%02x "
          "stencil-color=%08x/%08x cull=%08x/%08x,%08x/%08x "
          "polygon=%08x/%08x offset=%08x draw=%d/%u error=0x%x result=%d\n",
          blend_pixel, stencil_values[0], stencil_values[1],
          stencil_pixels[0], stencil_pixels[1],
          cull_back[0], cull_back[1], cull_front[0], cull_front[1],
          polygon_line, polygon_fill, offset_pixel, draw_status, draw_calls,
          error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glDisable(GL_POLYGON_OFFSET_FILL);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_CULL_FACE);
      glDisable(GL_STENCIL_TEST);
      glDisable(GL_BLEND);
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (depth_stencil)
         glDeleteRenderbuffers(1, &depth_stencil);
      if (color)
         glDeleteRenderbuffers(1, &color);
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
   printf("[ps5-egl-state-matrix] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
