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
#define SIZE 64
#define EXPECTED_PIXEL UINT32_C(0xff00ff00)
#define SAMPLERS 16
#define PHASES 6

/* GLSL 3.30 requires constant sampler-array indices. Keep every slot live. */
#define CHECK_TEXTURE(SAMPLER, BASE, INDEX) \
   " ok = ok && all(lessThan(abs(texelFetch(" SAMPLER "[" #INDEX \
   "], ivec2(0), 0) - ((" BASE " + vec4(" #INDEX \
   ".0, 0.0, 0.0, 0.0)) / 255.0)), vec4(0.001))) && " \
   "all(lessThan(abs(textureLod(" SAMPLER "[" #INDEX \
   "], vec2(-1.0), 0.0) - ((" BASE " + vec4(" #INDEX \
   ".0, 0.0, 0.0, 0.0)) / 255.0)), vec4(0.001)));\n"
#define CHECK_TEXTURES(S, B) \
   CHECK_TEXTURE(S, B, 0) CHECK_TEXTURE(S, B, 1) \
   CHECK_TEXTURE(S, B, 2) CHECK_TEXTURE(S, B, 3) \
   CHECK_TEXTURE(S, B, 4) CHECK_TEXTURE(S, B, 5) \
   CHECK_TEXTURE(S, B, 6) CHECK_TEXTURE(S, B, 7) \
   CHECK_TEXTURE(S, B, 8) CHECK_TEXTURE(S, B, 9) \
   CHECK_TEXTURE(S, B, 10) CHECK_TEXTURE(S, B, 11) \
   CHECK_TEXTURE(S, B, 12) CHECK_TEXTURE(S, B, 13) \
   CHECK_TEXTURE(S, B, 14) CHECK_TEXTURE(S, B, 15)

#ifndef PS5_GEOMETRY_HOST_REFERENCE
int ps5_egl_current_draw_status(unsigned *draw_calls);
#endif

static uint32_t
hash32(const void *data, size_t size)
{
   const uint8_t *bytes = data;
   uint32_t hash = UINT32_C(2166136261);

   while (size--)
      hash = (hash ^ *bytes++) * UINT32_C(16777619);
   return hash;
}

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   char log[512] = {0};
   GLsizei length = 0;
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-geometry-texture] shader=0x%x log=%.*s\n",
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
      "layout(location = 0) in vec2 a_position;\n"
      "uniform sampler2D u_vertex_texture[16];\n"
      "uniform vec4 u_vertex_expected;\n"
      "void main() {\n"
      "  bool ok = true;\n"
      CHECK_TEXTURES("u_vertex_texture", "u_vertex_expected")
      "  float x_offset = ok ? 0.0 : 2.0;\n"
      "  gl_Position = vec4(a_position.x + x_offset, a_position.y, 0.0, 1.0);\n"
      "}\n";
   static const char *geometry_source =
      "#version 330 core\n"
      "layout(triangles) in;\n"
      "layout(triangle_strip, max_vertices = 3) out;\n"
      "uniform sampler2D u_texture[16];\n"
      "uniform vec4 u_expected;\n"
      "void main() {\n"
      "  bool ok = true;\n"
      CHECK_TEXTURES("u_texture", "u_expected")
      "  float x_offset = ok ? 0.0 : 2.0;\n"
      "  for (int i = 0; i < 3; ++i) {\n"
      "    gl_Position = gl_in[i].gl_Position + vec4(x_offset, 0.0, 0.0, 0.0);\n"
      "    EmitVertex();\n"
      "  }\n"
      "  EndPrimitive();\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "layout(location = 0) out vec4 out_color;\n"
      "void main() { out_color = vec4(0.0, 1.0, 0.0, 1.0); }\n";
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static const uint8_t texels[2][4] = {
      {37, 123, 212, 255}, {211, 67, 19, 255},
   };
   static uint32_t pixels[SIZE * SIZE];
   static uint32_t expected[SIZE * SIZE];
   const EGLint config_attributes[] = {
#ifdef PS5_GEOMETRY_HOST_REFERENCE
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
#else
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
#endif
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
   EGLint egl_major = 0, egl_minor = 0, count = 0;
   GLuint shaders[3] = {0, 0, 0};
   GLuint program = 0, vertex_array = 0, vertex_buffer = 0;
   GLuint textures[2 * SAMPLERS] = {0};
   GLint linked = GL_FALSE, sampler = -1, vertex_sampler = -1;
   GLint expected_location = -1, vertex_expected_location = -1;
   GLint max_geometry_units = 0, max_vertex_units = 0, max_combined_units = 0;
   GLenum error = GL_NO_ERROR;
   EGLBoolean cleanup_ok = EGL_TRUE;
   unsigned matching = 0, draw_calls = 0, total_matching = 0;
   int draw_status = -1, made_current = 0, passed = 0;

   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      expected[i] = EXPECTED_PIXEL;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
#ifdef PS5_GEOMETRY_HOST_REFERENCE
   const EGLint pbuffer_attributes[] = {EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE};
   surface = eglCreatePbufferSurface(display, config, pbuffer_attributes);
#else
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
#endif
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;

   glGetIntegerv(GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS, &max_geometry_units);
   glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &max_vertex_units);
   glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_combined_units);
   if (max_geometry_units < 16 || max_vertex_units < 16 || max_combined_units < 32 ||
       !compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_GEOMETRY_SHADER, geometry_source, &shaders[1]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[2]))
      goto cleanup;

   program = glCreateProgram();
   for (unsigned i = 0; i < 3; ++i)
      glAttachShader(program, shaders[i]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetProgramInfoLog(program, sizeof(log), &length, log);
      printf("[ps5-egl-geometry-texture] link-log=%.*s\n", length, log);
      goto cleanup;
   }
   glUseProgram(program);
   sampler = glGetUniformLocation(program, "u_texture");
   vertex_sampler = glGetUniformLocation(program, "u_vertex_texture");
   expected_location = glGetUniformLocation(program, "u_expected");
   vertex_expected_location = glGetUniformLocation(program, "u_vertex_expected");
   if (sampler < 0 || vertex_sampler < 0 || expected_location < 0 ||
       vertex_expected_location < 0)
      goto cleanup;

   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
   glGenBuffers(1, &vertex_buffer);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(2 * SAMPLERS, textures);
   for (unsigned unit = 0; unit < 2 * SAMPLERS; ++unit) {
      const uint8_t *base = texels[unit / SAMPLERS];
      const uint8_t texel[4] = {
         (uint8_t)(base[0] + unit % SAMPLERS), base[1], base[2], base[3],
      };
      const GLfloat border[4] = {
         texel[0] / 255.0f, texel[1] / 255.0f, texel[2] / 255.0f, 1.0f,
      };
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, textures[unit]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
      glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, texel);
   }

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   for (unsigned phase = 0; phase < PHASES; ++phase) {
      /* Rebind both arrays/default UBOs without relinking. Deliberately alias
       * each stage's highest slot, then restore it to prove recovery. */
      const unsigned geometry_bank = phase ? 1 : 0;
      const unsigned vertex_bank = 1 - geometry_bank;
      const uint8_t *g = texels[geometry_bank], *v = texels[vertex_bank];
      GLint geometry_units[SAMPLERS], vertex_units[SAMPLERS];
      const uint32_t expected_pixel = phase == 2 || phase == 4
         ? UINT32_C(0xff000000) : EXPECTED_PIXEL;
      for (unsigned i = 0; i < SAMPLERS; ++i) {
         geometry_units[i] = geometry_bank * SAMPLERS + i;
         vertex_units[i] = vertex_bank * SAMPLERS + i;
      }
      if (phase == 2)
         geometry_units[SAMPLERS - 1] = geometry_units[0];
      if (phase == 4)
         vertex_units[SAMPLERS - 1] = vertex_units[0];
      glUniform1iv(sampler, SAMPLERS, geometry_units);
      glUniform1iv(vertex_sampler, SAMPLERS, vertex_units);
      glUniform4f(expected_location, g[0], g[1], g[2], g[3]);
      glUniform4f(vertex_expected_location, v[0], v[1], v[2], v[3]);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
#ifndef PS5_GEOMETRY_HOST_REFERENCE
      draw_status = ps5_egl_current_draw_status(&draw_calls);
#else
      draw_status = 0;
      draw_calls = phase + 1;
#endif
      glFinish();
      glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                   SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      error = glGetError();
      matching = 0;
      for (unsigned i = 0; i < SIZE * SIZE; ++i) {
         expected[i] = expected_pixel;
         matching += pixels[i] == expected_pixel;
      }
      passed = egl_major == 1 && egl_minor >= 4 && max_geometry_units >= 16 &&
               draw_status == 0 && draw_calls == phase + 1 &&
               matching == SIZE * SIZE &&
               hash32(pixels, sizeof(pixels)) == hash32(expected, sizeof(expected)) &&
               error == GL_NO_ERROR;
      printf("[ps5-egl-geometry-texture] phase=%u samplers=16+16 defaults=1+1 fetch+border "
             "draw=%d/%u matching=%u hash=%08x expected=%08x error=0x%x result=%d\n",
             phase, draw_status, draw_calls, matching,
             hash32(pixels, sizeof(pixels)), hash32(expected, sizeof(expected)),
             error, passed ? 0 : 1);
      if (!passed)
         goto cleanup;
      total_matching += matching;
   }
   passed &= total_matching == PHASES * SIZE * SIZE;

cleanup:
   if (made_current) {
      glDeleteTextures(2 * SAMPLERS, textures);
      if (vertex_buffer)
         glDeleteBuffers(1, &vertex_buffer);
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
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
   printf("[ps5-egl-geometry-texture] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
