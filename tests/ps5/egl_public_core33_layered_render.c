// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define TARGET_SIZE 64
#define READ_SIZE 16

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
      printf("[ps5-egl-core33-layered] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static int
pixel_is(const uint8_t *pixel, unsigned layer)
{
   static const uint8_t expected[3][4] = {
      {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
   };

   return pixel[0] == expected[layer][0] &&
          pixel[1] == expected[layer][1] &&
          pixel[2] == expected[layer][2] &&
          pixel[3] == expected[layer][3];
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
   static const char *geometry_source =
      "#version 330 core\n"
      "layout(triangles) in;\n"
      "layout(triangle_strip, max_vertices=9) out;\n"
      "flat out int layer_index;\n"
      "void main() {\n"
      "  for (int layer = 0; layer < 3; ++layer) {\n"
      "    gl_Layer = layer;\n"
      "    layer_index = layer;\n"
      "    for (int vertex = 0; vertex < 3; ++vertex) {\n"
      "      gl_Position = gl_in[vertex].gl_Position;\n"
      "      EmitVertex();\n"
      "    }\n"
      "    EndPrimitive();\n"
      "  }\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "flat in int layer_index;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  color = layer_index == 0 ? vec4(1,0,0,1) :\n"
      "          layer_index == 1 ? vec4(0,1,0,1) : vec4(0,0,1,1);\n"
      "}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   uint8_t pixels[READ_SIZE * READ_SIZE * 4];
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
   GLuint shaders[3] = {0};
   GLuint program = 0, vao = 0, vbo = 0, texture = 0, framebuffer = 0;
   GLint linked = GL_FALSE, layered = GL_FALSE;
   GLenum status = 0, error = GL_NO_ERROR;
   unsigned matching[3] = {0};
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
       !compile_shader(GL_GEOMETRY_SHADER, geometry_source, &shaders[1]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[2]))
      goto cleanup;
   program = glCreateProgram();
   for (unsigned i = 0; i < 3; ++i)
      glAttachShader(program, shaders[i]);
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

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 1);
   glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                TARGET_SIZE, TARGET_SIZE, 3, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glTexImage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8,
                TARGET_SIZE / 2, TARGET_SIZE / 2, 3, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_FRAMEBUFFER_ATTACHMENT_LAYERED, &layered);
   if (status != GL_FRAMEBUFFER_COMPLETE || !layered)
      goto cleanup;

   glViewport(0, 0, TARGET_SIZE, TARGET_SIZE);
   glUseProgram(program);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   for (unsigned layer = 0; layer < 3; ++layer) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                texture, 0, layer);
      glReadPixels((TARGET_SIZE - READ_SIZE) / 2,
                   (TARGET_SIZE - READ_SIZE) / 2,
                   READ_SIZE, READ_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      for (unsigned i = 0; i < READ_SIZE * READ_SIZE; ++i)
         matching[layer] += pixel_is(pixels + i * 4u, layer);
   }
   error = glGetError();
   passed = major == 1 && minor == 4 &&
            status == GL_FRAMEBUFFER_COMPLETE && layered &&
            draw_status == 0 && draw_calls == 1 &&
            matching[0] == READ_SIZE * READ_SIZE &&
            matching[1] == READ_SIZE * READ_SIZE &&
            matching[2] == READ_SIZE * READ_SIZE && error == GL_NO_ERROR;
   printf("[ps5-egl-core33-layered] fbo=0x%x layered=%d draw=%d/%u "
          "matching=%u/%u/%u error=0x%x result=%d\n",
          status, layered, draw_status, draw_calls,
          matching[0], matching[1], matching[2], error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (texture)
         glDeleteTextures(1, &texture);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      if (program)
         glDeleteProgram(program);
      for (unsigned i = 0; i < 3; ++i)
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
   printf("[ps5-egl-core33-layered] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
