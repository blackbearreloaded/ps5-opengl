// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define SIZE 64
#define EXPECTED_PIXEL UINT32_C(0xffff00ff)
#define EXPECTED_HASH UINT32_C(0x64e31dc5)

static uint32_t
hash32(const void *data, size_t size)
{
   const uint8_t *bytes = data;
   uint32_t hash = UINT32_C(2166136261);

   while (size--)
      hash = (hash ^ *bytes++) * UINT32_C(16777619);
   return hash;
}

static GLuint
compile_shader(GLenum type, const char *source)
{
   GLuint shader = glCreateShader(type);
   GLint compiled = GL_FALSE;

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512];
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-border] shader=0x%x log=%.*s\n",
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
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform sampler2D custom_sampler;\n"
      "uniform sampler2D transparent_sampler;\n"
      "uniform sampler2D black_sampler;\n"
      "uniform sampler2D white_sampler;\n"
      "layout(location=0) out vec4 color;\n"
      "bool near4(vec4 a, vec4 b) {\n"
      "  return all(lessThan(abs(a - b), vec4(0.0001)));\n"
      "}\n"
      "void main() {\n"
      "  vec2 p = vec2(-2.0);\n"
      "  bool ok = near4(texture(custom_sampler, p),\n"
      "                  vec4(0.2, 0.4, 0.6, 0.8));\n"
      "  ok = ok && near4(texture(transparent_sampler, p), vec4(0.0));\n"
      "  ok = ok && near4(texture(black_sampler, p),\n"
      "                   vec4(0.0, 0.0, 0.0, 1.0));\n"
      "  ok = ok && near4(texture(white_sampler, p), vec4(1.0));\n"
      "  color = ok ? vec4(1.0, 0.0, 1.0, 1.0)\n"
      "             : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const float vertices[] = {
      -1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 1.0f,
   };
   static const float border_colors[4][4] = {
      {0.2f, 0.4f, 0.6f, 0.8f},
      {0.0f, 0.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 1.0f, 1.0f},
   };
   static const char *uniform_names[4] = {
      "custom_sampler", "transparent_sampler",
      "black_sampler", "white_sampler",
   };
   static uint32_t pixels[SIZE * SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
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
   EGLint major = 0, minor = 0, count = 0, profile = 0;
   GLuint vao = 0, vbo = 0, vs = 0, fs = 0, program = 0, texture = 0;
   GLuint samplers[4] = {0};
   GLint linked = GL_FALSE;
   GLint wraps[4] = {0};
   float queried_custom[4] = {0};
   GLenum draw_error = GL_NO_ERROR, cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   unsigned matching = 0;
   uint32_t hash = 0;
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
       !eglSwapInterval(display, 0) ||
       !eglQueryContext(display, context,
                        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, &profile))
      goto cleanup;
   made_current = 1;

   vs = compile_shader(GL_VERTEX_SHADER, vertex_source);
   fs = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
   if (!vs || !fs)
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);
   for (unsigned index = 0; index < 4; ++index) {
      GLint location = glGetUniformLocation(program, uniform_names[index]);

      if (location < 0)
         goto cleanup;
      glUniform1i(location, index);
   }

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &texture);
   for (unsigned index = 0; index < 4; ++index) {
      glActiveTexture(GL_TEXTURE0 + index);
      glBindTexture(GL_TEXTURE_2D, texture);
   }
   {
      static const uint32_t texel = UINT32_C(0xff0000ff);

      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, &texel);
   }

   glGenSamplers(4, samplers);
   for (unsigned index = 0; index < 4; ++index) {
      glSamplerParameteri(samplers[index], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glSamplerParameteri(samplers[index], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glSamplerParameteri(samplers[index], GL_TEXTURE_WRAP_S,
                          GL_CLAMP_TO_BORDER);
      glSamplerParameteri(samplers[index], GL_TEXTURE_WRAP_T,
                          GL_CLAMP_TO_BORDER);
      glSamplerParameterfv(samplers[index], GL_TEXTURE_BORDER_COLOR,
                           border_colors[index]);
      glBindSampler(index, samplers[index]);
      glGetSamplerParameteriv(samplers[index], GL_TEXTURE_WRAP_S,
                              &wraps[index]);
   }
   glGetSamplerParameterfv(samplers[0], GL_TEXTURE_BORDER_COLOR,
                           queried_custom);

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   draw_error = glGetError();
   hash = hash32(pixels, sizeof(pixels));
   for (unsigned index = 0; index < SIZE * SIZE; ++index)
      matching += pixels[index] == EXPECTED_PIXEL;
   if (!eglSwapBuffers(display, surface))
      goto cleanup;

   passed = major == 1 && minor == 4 &&
            profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            strncmp((const char *)glGetString(GL_VERSION), "3.3 ", 4) == 0 &&
            wraps[0] == GL_CLAMP_TO_BORDER &&
            wraps[1] == GL_CLAMP_TO_BORDER &&
            wraps[2] == GL_CLAMP_TO_BORDER &&
            wraps[3] == GL_CLAMP_TO_BORDER &&
            memcmp(queried_custom, border_colors[0],
                   sizeof(queried_custom)) == 0 &&
            matching == SIZE * SIZE && hash == EXPECTED_HASH &&
            draw_error == GL_NO_ERROR;

cleanup:
   glBindSampler(0, 0);
   glBindSampler(1, 0);
   glBindSampler(2, 0);
   glBindSampler(3, 0);
   glDeleteSamplers(4, samplers);
   if (texture)
      glDeleteTextures(1, &texture);
   if (vbo)
      glDeleteBuffers(1, &vbo);
   if (vao)
      glDeleteVertexArrays(1, &vao);
   if (program)
      glDeleteProgram(program);
   if (fs)
      glDeleteShader(fs);
   if (vs)
      glDeleteShader(vs);
   if (made_current)
      cleanup_gl_error = glGetError();
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_egl_error = eglGetError();
   passed = passed && cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
            cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-border] wraps=%x/%x/%x/%x custom=%.3f/%.3f/%.3f/%.3f pixels=%u hash=%08x error=%x/%x/%x result=%d\n",
          wraps[0], wraps[1], wraps[2], wraps[3], queried_custom[0],
          queried_custom[1], queried_custom[2], queried_custom[3], matching,
          hash, draw_error, cleanup_gl_error, cleanup_egl_error,
          passed ? 0 : 1);
   return passed ? 0 : 1;
}
