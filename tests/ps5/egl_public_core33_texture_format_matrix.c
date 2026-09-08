// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 64
#define HEIGHT 64

int ps5_egl_current_draw_status(unsigned *draw_calls);

enum sample_class {
   SAMPLE_FLOAT,
   SAMPLE_UINT,
   SAMPLE_SINT,
};

struct format_case {
   const char *name;
   GLenum internal_format;
   GLenum upload_format;
   GLenum upload_type;
   const void *data;
   enum sample_class sample_class;
   GLint sizes[4];
   GLfloat f[4];
   GLuint u[4];
   GLint i[4];
};

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
      printf("[ps5-egl-format-matrix] shader=0x%x log=%.*s\n",
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
   GLint ok = GL_FALSE;
   GLuint program = glCreateProgram();

   if (!program)
      return 0;
   glAttachShader(program, vertex);
   glAttachShader(program, fragment);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   if (!ok) {
      glDeleteProgram(program);
      return 0;
   }
   *result = program;
   return 1;
}

static int
run_case(const struct format_case *test, const GLuint programs[3])
{
   static const GLenum size_names[4] = {
      GL_TEXTURE_RED_SIZE, GL_TEXTURE_GREEN_SIZE,
      GL_TEXTURE_BLUE_SIZE, GL_TEXTURE_ALPHA_SIZE,
   };
   GLuint texture = 0;
   uint32_t pixel = 0;
   GLint sizes[4] = {0};
   GLint location;
   int passed = 1;

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexImage2D(GL_TEXTURE_2D, 0, test->internal_format, 1, 1, 0,
                test->upload_format, test->upload_type, test->data);
   for (unsigned component = 0; component < 4; ++component) {
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, size_names[component],
                               &sizes[component]);
      passed &= sizes[component] == test->sizes[component];
   }

   glUseProgram(programs[test->sample_class]);
   location = glGetUniformLocation(programs[test->sample_class], "expected");
   if (test->sample_class == SAMPLE_FLOAT)
      glUniform4fv(location, 1, test->f);
   else if (test->sample_class == SAMPLE_UINT)
      glUniform4uiv(location, 1, test->u);
   else
      glUniform4iv(location, 1, test->i);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA,
                GL_UNSIGNED_BYTE, &pixel);
   passed &= pixel == UINT32_C(0xffff00ff);
   passed &= glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-format-matrix] case=%s sizes=%d/%d/%d/%d "
          "pixel=%08x result=%d\n",
          test->name, sizes[0], sizes[1], sizes[2], sizes[3], pixel,
          passed ? 0 : 1);
   glDeleteTextures(1, &texture);
   return passed;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position=vec4(position,0,1); }\n";
   static const char *fragment_sources[3] = {
      "#version 330 core\n"
      "uniform sampler2D source; uniform vec4 expected;\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){ vec4 v=texture(source,vec2(.5));"
      "color=all(lessThanEqual(abs(v-expected),vec4(.001)))?"
      "vec4(1,0,1,1):vec4(1,0,0,1);}\n",
      "#version 330 core\n"
      "uniform usampler2D source; uniform uvec4 expected;\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){ color=all(equal(texture(source,vec2(.5)),expected))?"
      "vec4(1,0,1,1):vec4(1,0,0,1);}\n",
      "#version 330 core\n"
      "uniform isampler2D source; uniform ivec4 expected;\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){ color=all(equal(texture(source,vec2(.5)),expected))?"
      "vec4(1,0,1,1):vec4(1,0,0,1);}\n",
   };
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static const uint16_t r16 = 32768;
   static const uint16_t rg16[2] = {16384, 49152};
   static const uint16_t rgba16[4] = {65535, 32768, 0, 16384};
   static const int16_t r16sn = -16384;
   static const int16_t rg16sn[2] = {16384, -16384};
   static const int16_t rgba16sn[4] = {32767, -16384, 0, 16384};
   static const uint16_t r16f = 0x3800;
   static const uint16_t rg16f[2] = {0x3400, 0x3a00};
   static const float r32f = 0.125f;
   static const float rg32f[2] = {0.25f, 0.75f};
   static const uint8_t r8ui = 173;
   static const int8_t rg8i[2] = {-17, 81};
   static const uint16_t r16ui = 50000;
   static const int16_t rg16i[2] = {-30000, 12345};
   static const uint32_t r32ui = UINT32_C(0xf1234567);
   static const int32_t rg32i[2] = {INT32_MIN + 1, 123456789};
   static const uint32_t rgb10a2 = UINT32_C(1023) |
      (UINT32_C(512) << 10) | (UINT32_C(2) << 30);
   static const struct format_case cases[] = {
      {"r16", GL_R16, GL_RED, GL_UNSIGNED_SHORT, &r16, SAMPLE_FLOAT,
       {16, 0, 0, 0}, {32768.0f / 65535.0f, 0, 0, 1}, {0}, {0}},
      {"rg16", GL_RG16, GL_RG, GL_UNSIGNED_SHORT, rg16, SAMPLE_FLOAT,
       {16, 16, 0, 0}, {16384.0f / 65535.0f, 49152.0f / 65535.0f, 0, 1},
       {0}, {0}},
      {"rgba16", GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, rgba16, SAMPLE_FLOAT,
       {16, 16, 16, 16}, {1, 32768.0f / 65535.0f, 0,
                           16384.0f / 65535.0f}, {0}, {0}},
      {"r16-snorm", GL_R16_SNORM, GL_RED, GL_SHORT, &r16sn, SAMPLE_FLOAT,
       {16, 0, 0, 0}, {-16384.0f / 32767.0f, 0, 0, 1}, {0}, {0}},
      {"rg16-snorm", GL_RG16_SNORM, GL_RG, GL_SHORT, rg16sn, SAMPLE_FLOAT,
       {16, 16, 0, 0}, {16384.0f / 32767.0f, -16384.0f / 32767.0f, 0, 1},
       {0}, {0}},
      {"rgba16-snorm", GL_RGBA16_SNORM, GL_RGBA, GL_SHORT, rgba16sn,
       SAMPLE_FLOAT, {16, 16, 16, 16},
       {1, -16384.0f / 32767.0f, 0, 16384.0f / 32767.0f}, {0}, {0}},
      {"r16f", GL_R16F, GL_RED, GL_HALF_FLOAT, &r16f, SAMPLE_FLOAT,
       {16, 0, 0, 0}, {.5f, 0, 0, 1}, {0}, {0}},
      {"rg16f", GL_RG16F, GL_RG, GL_HALF_FLOAT, rg16f, SAMPLE_FLOAT,
       {16, 16, 0, 0}, {.25f, .75f, 0, 1}, {0}, {0}},
      {"r32f", GL_R32F, GL_RED, GL_FLOAT, &r32f, SAMPLE_FLOAT,
       {32, 0, 0, 0}, {.125f, 0, 0, 1}, {0}, {0}},
      {"rg32f", GL_RG32F, GL_RG, GL_FLOAT, rg32f, SAMPLE_FLOAT,
       {32, 32, 0, 0}, {.25f, .75f, 0, 1}, {0}, {0}},
      {"r8ui", GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &r8ui, SAMPLE_UINT,
       {8, 0, 0, 0}, {0}, {173, 0, 0, 1}, {0}},
      {"rg8i", GL_RG8I, GL_RG_INTEGER, GL_BYTE, rg8i, SAMPLE_SINT,
       {8, 8, 0, 0}, {0}, {0}, {-17, 81, 0, 1}},
      {"r16ui", GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, &r16ui,
       SAMPLE_UINT, {16, 0, 0, 0}, {0}, {50000, 0, 0, 1}, {0}},
      {"rg16i", GL_RG16I, GL_RG_INTEGER, GL_SHORT, rg16i, SAMPLE_SINT,
       {16, 16, 0, 0}, {0}, {0}, {-30000, 12345, 0, 1}},
      {"r32ui", GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &r32ui,
       SAMPLE_UINT, {32, 0, 0, 0}, {0}, {UINT32_C(0xf1234567), 0, 0, 1},
       {0}},
      {"rg32i", GL_RG32I, GL_RG_INTEGER, GL_INT, rg32i, SAMPLE_SINT,
       {32, 32, 0, 0}, {0}, {0}, {INT32_MIN + 1, 123456789, 0, 1}},
      {"rgb10-a2", GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV,
       &rgb10a2, SAMPLE_FLOAT, {10, 10, 10, 2},
       {1, 512.0f / 1023.0f, 0, 2.0f / 3.0f}, {0}, {0}},
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
   GLuint shaders[4] = {0}, programs[3] = {0}, vao = 0, vbo = 0;
   unsigned matching = 0, draw_calls = 0;
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

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]))
      goto cleanup;
   for (unsigned i = 0; i < 3; ++i)
      if (!compile_shader(GL_FRAGMENT_SHADER, fragment_sources[i],
                          &shaders[i + 1]) ||
          !link_program(shaders[0], shaders[i + 1], &programs[i]))
         goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glViewport(0, 0, WIDTH, HEIGHT);
   glDisable(GL_BLEND);
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
      matching += run_case(&cases[i], programs);

   draw_status = ps5_egl_current_draw_status(&draw_calls);
   passed = major == 1 && minor == 4 && matching == 17 &&
            draw_status == 0 && draw_calls == 17 &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-format-matrix] matching=%u draw=%d/%u result=%d\n",
          matching, draw_status, draw_calls, passed ? 0 : 1);

cleanup:
   if (made_current) {
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      for (unsigned i = 0; i < 3; ++i)
         if (programs[i])
            glDeleteProgram(programs[i]);
      for (unsigned i = 0; i < 4; ++i)
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
   printf("[ps5-egl-format-matrix] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
