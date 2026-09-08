// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stddef.h>
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

struct vertex8 {
   float position[2];
   int8_t signed_value[4];
   uint8_t unsigned_value[4];
};

struct vertex16 {
   float position[2];
   int16_t signed_value[4];
   uint16_t unsigned_value[4];
};

struct vertex32 {
   float position[2];
   int32_t signed_value[4];
   uint32_t unsigned_value[4];
};

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
      printf("[ps5-egl-int-vertex-matrix] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   return shader;
}

static int
run_type(GLuint vbo, GLenum signed_type, GLenum unsigned_type,
         const void *vertices, size_t bytes, GLsizei stride,
         size_t position_offset, size_t signed_offset, size_t unsigned_offset)
{
   static uint32_t pixels[SIZE * SIZE];
   int passed = 0;

   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, bytes, vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                         (const void *)position_offset);
   glEnableVertexAttribArray(0);

   for (GLint size = 1; size <= 4; ++size) {
      GLint integer[2] = {0, 0};
      GLint types[2] = {0, 0};
      GLint sizes[2] = {0, 0};
      unsigned matching = 0;
      uint32_t hash;

      glVertexAttribIPointer(1, size, signed_type, stride,
                             (const void *)signed_offset);
      glVertexAttribIPointer(2, size, unsigned_type, stride,
                             (const void *)unsigned_offset);
      glEnableVertexAttribArray(1);
      glEnableVertexAttribArray(2);
      for (GLuint index = 1; index <= 2; ++index) {
         glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_INTEGER,
                             &integer[index - 1]);
         glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE,
                             &types[index - 1]);
         glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE,
                             &sizes[index - 1]);
      }

      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glFinish();
      glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                   SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      hash = hash32(pixels, sizeof(pixels));
      for (unsigned index = 0; index < SIZE * SIZE; ++index)
         matching += pixels[index] == EXPECTED_PIXEL;
      if (integer[0] == GL_TRUE && integer[1] == GL_TRUE &&
          types[0] == (GLint)signed_type &&
          types[1] == (GLint)unsigned_type &&
          sizes[0] == size && sizes[1] == size &&
          matching == SIZE * SIZE && hash == EXPECTED_HASH &&
          glGetError() == GL_NO_ERROR)
         ++passed;
      printf("[ps5-egl-int-vertex-matrix] types=%x/%x size=%d pixels=%u "
             "hash=%08x result=%d\n", signed_type, unsigned_type, size,
             matching, hash, passed == size ? 0 : 1);
   }
   return passed;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 a_position;\n"
      "layout(location=1) in ivec4 a_signed;\n"
      "layout(location=2) in uvec4 a_unsigned;\n"
      "flat out vec4 v_color;\n"
      "void main() {\n"
      "  gl_Position = vec4(a_position * 0.5, 0.0, 1.0);\n"
      "  bool good = all(equal(a_signed, ivec4(1,0,0,1))) &&\n"
      "              all(equal(a_unsigned, uvec4(1,0,0,1)));\n"
      "  v_color = good ? vec4(1,0,1,1) : vec4(0,1,0,1);\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "flat in vec4 v_color;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = v_color; }\n";
#define VERTICES(type) \
   { \
      {{-1.0f, -1.0f}, {1, 0, 0, 1}, {1, 0, 0, 1}}, \
      {{ 1.0f, -1.0f}, {1, 0, 0, 1}, {1, 0, 0, 1}}, \
      {{ 0.0f,  1.0f}, {1, 0, 0, 1}, {1, 0, 0, 1}}, \
   }
   static const struct vertex8 vertices8[3] = VERTICES(vertex8);
   static const struct vertex16 vertices16[3] = VERTICES(vertex16);
   static const struct vertex32 vertices32[3] = VERTICES(vertex32);
#undef VERTICES
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
   GLint linked = GL_FALSE;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, cases = 0, passed = 0;

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
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

#define RUN_TYPE(bits, signed_gl, unsigned_gl) \
   run_type(vbo, signed_gl, unsigned_gl, vertices##bits, sizeof(vertices##bits), \
            sizeof(struct vertex##bits), offsetof(struct vertex##bits, position), \
            offsetof(struct vertex##bits, signed_value), \
            offsetof(struct vertex##bits, unsigned_value))
   cases += RUN_TYPE(8, GL_BYTE, GL_UNSIGNED_BYTE);
   cases += RUN_TYPE(16, GL_SHORT, GL_UNSIGNED_SHORT);
   cases += RUN_TYPE(32, GL_INT, GL_UNSIGNED_INT);
#undef RUN_TYPE
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = major == 1 && minor == 4 &&
            profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            glGetString(GL_VERSION) &&
            strncmp((const char *)glGetString(GL_VERSION), "3.3 ", 4) == 0 &&
            cases == 12 && glGetError() == GL_NO_ERROR;

cleanup:
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
   printf("[ps5-egl-int-vertex-matrix] cases=%d cleanup=%x/%x result=%d\n",
          cases, cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
