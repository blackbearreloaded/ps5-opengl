// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 32
#define HEIGHT 32

int ps5_egl_current_draw_status(unsigned *draw_calls);

struct format_case {
   const char *name;
   GLenum internal_format;
   unsigned channels;
   unsigned bits;
   int is_signed;
   int packed_10_10_10_2;
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
      printf("[ps5-egl-render-integer] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static void
unsigned_values(const struct format_case *test, GLuint clear[4],
                GLuint draw[4])
{
   static const GLuint clear8[4] = {17, 65, 129, 250};
   static const GLuint draw8[4] = {23, 71, 151, 241};
   static const GLuint clear16[4] = {1000, 20000, 40000, 65000};
   static const GLuint draw16[4] = {2000, 21000, 41000, 64000};
   static const GLuint clear32[4] = {
      UINT32_C(0x12345678), UINT32_C(0x87654321),
      UINT32_C(0x0badc0de), UINT32_C(0xfedcba98),
   };
   static const GLuint draw32[4] = {
      UINT32_C(0x23456789), UINT32_C(0x76543210),
      UINT32_C(0x1badc0de), UINT32_C(0xedcba987),
   };
   static const GLuint clear_packed[4] = {101, 509, 901, 2};
   static const GLuint draw_packed[4] = {907, 503, 103, 3};
   const GLuint *clear_source = test->packed_10_10_10_2 ? clear_packed :
                                test->bits == 8 ? clear8 :
                                test->bits == 16 ? clear16 : clear32;
   const GLuint *draw_source = test->packed_10_10_10_2 ? draw_packed :
                               test->bits == 8 ? draw8 :
                               test->bits == 16 ? draw16 : draw32;

   for (unsigned channel = 0; channel < 4; ++channel) {
      clear[channel] = clear_source[channel];
      draw[channel] = draw_source[channel];
   }
}

static void
signed_values(unsigned bits, GLint clear[4], GLint draw[4])
{
   static const GLint clear8[4] = {-100, -7, 31, 100};
   static const GLint draw8[4] = {-91, -3, 37, 93};
   static const GLint clear16[4] = {-30000, -1000, 1000, 30000};
   static const GLint draw16[4] = {-29000, -2000, 2000, 29000};
   static const GLint clear32[4] = {
      INT32_C(-123456789), INT32_C(-12345),
      INT32_C(12345), INT32_C(123456789),
   };
   static const GLint draw32[4] = {
      INT32_C(-234567890), INT32_C(-23456),
      INT32_C(23456), INT32_C(234567890),
   };
   const GLint *clear_source = bits == 8 ? clear8 :
                               bits == 16 ? clear16 : clear32;
   const GLint *draw_source = bits == 8 ? draw8 :
                              bits == 16 ? draw16 : draw32;

   for (unsigned channel = 0; channel < 4; ++channel) {
      clear[channel] = clear_source[channel];
      draw[channel] = draw_source[channel];
   }
}

static int
check_unsigned(const GLuint actual[4], const GLuint expected[4],
               unsigned channels)
{
   for (unsigned channel = 0; channel < 4; ++channel) {
      GLuint value = channel < channels ? expected[channel]
                                        : (channel == 3 ? 1u : 0u);

      if (actual[channel] != value)
         return 0;
   }
   return 1;
}

static int
check_signed(const GLint actual[4], const GLint expected[4],
             unsigned channels)
{
   for (unsigned channel = 0; channel < 4; ++channel) {
      GLint value = channel < channels ? expected[channel]
                                       : (channel == 3 ? 1 : 0);

      if (actual[channel] != value)
         return 0;
   }
   return 1;
}

static int
run_case(const struct format_case *test, const GLuint programs[2],
         const GLint locations[2], GLuint framebuffer, GLuint renderbuffer)
{
   GLenum status;
   GLenum error;
   int passed;

   glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, test->internal_format,
                         WIDTH, HEIGHT);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, renderbuffer);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE) {
      printf("[ps5-egl-render-integer] case=%s fbo=0x%x result=1\n",
             test->name, status);
      return 0;
   }

   if (test->is_signed) {
      GLint clear[4], draw[4], clear_pixel[4] = {0}, draw_pixel[4] = {0};

      signed_values(test->bits, clear, draw);
      glClearBufferiv(GL_COLOR, 0, clear);
      glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA_INTEGER, GL_INT,
                   clear_pixel);
      glUseProgram(programs[1]);
      glUniform4iv(locations[1], 1, draw);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glFinish();
      glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA_INTEGER, GL_INT,
                   draw_pixel);
      error = glGetError();
      passed = error == GL_NO_ERROR &&
               check_signed(clear_pixel, clear, test->channels) &&
               check_signed(draw_pixel, draw, test->channels);
      printf("[ps5-egl-render-integer] case=%s clear=%d/%d/%d/%d "
             "draw=%d/%d/%d/%d error=0x%x result=%d\n",
             test->name, clear_pixel[0], clear_pixel[1], clear_pixel[2],
             clear_pixel[3], draw_pixel[0], draw_pixel[1], draw_pixel[2],
             draw_pixel[3], error, passed ? 0 : 1);
   } else {
      GLuint clear[4], draw[4], clear_pixel[4] = {0}, draw_pixel[4] = {0};

      unsigned_values(test, clear, draw);
      glClearBufferuiv(GL_COLOR, 0, clear);
      glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA_INTEGER,
                   GL_UNSIGNED_INT, clear_pixel);
      glUseProgram(programs[0]);
      glUniform4uiv(locations[0], 1, draw);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glFinish();
      glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA_INTEGER,
                   GL_UNSIGNED_INT, draw_pixel);
      error = glGetError();
      passed = error == GL_NO_ERROR &&
               check_unsigned(clear_pixel, clear, test->channels) &&
               check_unsigned(draw_pixel, draw, test->channels);
      printf("[ps5-egl-render-integer] case=%s clear=%u/%u/%u/%u "
             "draw=%u/%u/%u/%u error=0x%x result=%d\n",
             test->name, clear_pixel[0], clear_pixel[1], clear_pixel[2],
             clear_pixel[3], draw_pixel[0], draw_pixel[1], draw_pixel[2],
             draw_pixel[3], error, passed ? 0 : 1);
   }
   return passed;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main(){gl_Position=vec4(position,0,1);}\n";
   static const char *fragment_sources[2] = {
      "#version 330 core\n"
      "uniform uvec4 source_color; layout(location=0) out uvec4 color;\n"
      "void main(){color=source_color;}\n",
      "#version 330 core\n"
      "uniform ivec4 source_color; layout(location=0) out ivec4 color;\n"
      "void main(){color=source_color;}\n",
   };
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static const struct format_case cases[] = {
      {"r8ui", GL_R8UI, 1, 8, 0, 0},
      {"rg8ui", GL_RG8UI, 2, 8, 0, 0},
      {"rgba8ui", GL_RGBA8UI, 4, 8, 0, 0},
      {"r8i", GL_R8I, 1, 8, 1, 0},
      {"rg8i", GL_RG8I, 2, 8, 1, 0},
      {"rgba8i", GL_RGBA8I, 4, 8, 1, 0},
      {"r16ui", GL_R16UI, 1, 16, 0, 0},
      {"rg16ui", GL_RG16UI, 2, 16, 0, 0},
      {"rgba16ui", GL_RGBA16UI, 4, 16, 0, 0},
      {"r16i", GL_R16I, 1, 16, 1, 0},
      {"rg16i", GL_RG16I, 2, 16, 1, 0},
      {"rgba16i", GL_RGBA16I, 4, 16, 1, 0},
      {"r32ui", GL_R32UI, 1, 32, 0, 0},
      {"rg32ui", GL_RG32UI, 2, 32, 0, 0},
      {"rgba32ui", GL_RGBA32UI, 4, 32, 0, 0},
      {"r32i", GL_R32I, 1, 32, 1, 0},
      {"rg32i", GL_RG32I, 2, 32, 1, 0},
      {"rgba32i", GL_RGBA32I, 4, 32, 1, 0},
      {"rgb10-a2ui", GL_RGB10_A2UI, 4, 10, 0, 1},
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
   GLuint shaders[3] = {0}, programs[2] = {0}, vao = 0, vbo = 0;
   GLuint framebuffer = 0, renderbuffer = 0;
   GLint locations[2] = {-1, -1};
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
   for (unsigned i = 0; i < 2; ++i) {
      GLint linked = GL_FALSE;

      if (!compile_shader(GL_FRAGMENT_SHADER, fragment_sources[i],
                          &shaders[i + 1]))
         goto cleanup;
      programs[i] = glCreateProgram();
      glAttachShader(programs[i], shaders[0]);
      glAttachShader(programs[i], shaders[i + 1]);
      glLinkProgram(programs[i]);
      glGetProgramiv(programs[i], GL_LINK_STATUS, &linked);
      locations[i] = glGetUniformLocation(programs[i], "source_color");
      if (!linked || locations[i] < 0)
         goto cleanup;
   }

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glGenFramebuffers(1, &framebuffer);
   glGenRenderbuffers(1, &renderbuffer);
   glViewport(0, 0, WIDTH, HEIGHT);
   glDisable(GL_BLEND);
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
      matching += run_case(&cases[i], programs, locations,
                           framebuffer, renderbuffer);

   draw_status = ps5_egl_current_draw_status(&draw_calls);
   passed = major == 1 && minor == 4 &&
            matching == sizeof(cases) / sizeof(cases[0]) &&
            draw_status == 0 &&
            draw_calls == sizeof(cases) / sizeof(cases[0]) &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-render-integer] matching=%u draw=%d/%u result=%d\n",
          matching, draw_status, draw_calls, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (renderbuffer)
         glDeleteRenderbuffers(1, &renderbuffer);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      for (unsigned i = 0; i < 2; ++i)
         if (programs[i])
            glDeleteProgram(programs[i]);
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
   printf("[ps5-egl-render-integer] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
