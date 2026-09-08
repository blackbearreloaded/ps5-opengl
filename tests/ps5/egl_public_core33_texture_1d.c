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
      printf("[ps5-egl-1d] shader=0x%x log=%.*s\n", type, length, log);
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
      "uniform sampler1D line_texture;\n"
      "uniform sampler1DArray array_texture;\n"
      "layout(location=0) out vec4 color;\n"
      "bool near4(vec4 a, vec4 b) {\n"
      "  return all(lessThan(abs(a - b), vec4(0.0001)));\n"
      "}\n"
      "void main() {\n"
      "  bool ok = textureSize(line_texture, 0) == 64;\n"
      "  ok = ok && all(equal(textureSize(array_texture, 0), ivec2(64, 3)));\n"
      "  ok = ok && near4(textureLod(line_texture, 0.5, 0.0),\n"
      "                   vec4(1.0, 0.0, 0.0, 1.0));\n"
      "  ok = ok && near4(textureLod(line_texture, 0.5, 6.0),\n"
      "                   vec4(1.0, 0.0, 0.0, 1.0));\n"
      "  ok = ok && near4(textureLod(array_texture, vec2(0.5, 2.0), 0.0),\n"
      "                   vec4(0.0, 0.0, 1.0, 1.0));\n"
      "  ok = ok && near4(textureLod(array_texture, vec2(0.5, 2.0), 6.0),\n"
      "                   vec4(0.0, 0.0, 1.0, 1.0));\n"
      "  color = ok ? vec4(1.0, 0.0, 1.0, 1.0)\n"
      "             : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const float vertices[] = {
      -1.0f, -1.0f, 1.0f, -1.0f, 0.0f, 1.0f,
   };
   static uint32_t line_texels[SIZE];
   static uint32_t array_texels[3][SIZE];
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
   GLuint vao = 0, vbo = 0, vs = 0, fs = 0, program = 0;
   GLuint textures[2] = {0};
   GLint linked = GL_FALSE;
   GLint max_size = 0, max_layers = 0;
   GLenum draw_error = GL_NO_ERROR, cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   unsigned matching = 0;
   uint32_t hash = 0;
   int made_current = 0, passed = 0;

   for (unsigned x = 0; x < SIZE; ++x) {
      line_texels[x] = UINT32_C(0xff0000ff);
      array_texels[0][x] = UINT32_C(0xff0000ff);
      array_texels[1][x] = UINT32_C(0xff00ff00);
      array_texels[2][x] = UINT32_C(0xffff0000);
   }

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
   glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
   glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);

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
   {
      GLint line = glGetUniformLocation(program, "line_texture");
      GLint array = glGetUniformLocation(program, "array_texture");

      if (line < 0 || array < 0)
         goto cleanup;
      glUniform1i(line, 0);
      glUniform1i(array, 1);
   }

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(2, textures);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_1D, textures[0]);
   glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER,
                   GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAX_LEVEL, 6);
   glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, SIZE, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, line_texels);
   glGenerateMipmap(GL_TEXTURE_1D);

   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_1D_ARRAY, textures[1]);
   glTexParameteri(GL_TEXTURE_1D_ARRAY, GL_TEXTURE_MIN_FILTER,
                   GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(GL_TEXTURE_1D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_1D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_1D_ARRAY, GL_TEXTURE_MAX_LEVEL, 6);
   glTexImage2D(GL_TEXTURE_1D_ARRAY, 0, GL_RGBA8, SIZE, 3, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, array_texels);
   glGenerateMipmap(GL_TEXTURE_1D_ARRAY);

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
            max_size >= SIZE && max_layers >= 3 &&
            matching == SIZE * SIZE && hash == EXPECTED_HASH &&
            draw_error == GL_NO_ERROR;

cleanup:
   if (textures[0] || textures[1])
      glDeleteTextures(2, textures);
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
   printf("[ps5-egl-1d] max=%d/%d pixels=%u hash=%08x error=%x/%x/%x result=%d\n",
          max_size, max_layers, matching, hash, draw_error,
          cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
