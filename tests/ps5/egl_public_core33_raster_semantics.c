// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define TARGET_WIDTH 128
#define TARGET_HEIGHT 96
#define PIXELS (TARGET_WIDTH * TARGET_HEIGHT)
#define INTERIOR_SIZE 16
#define INTERIOR_PIXELS (INTERIOR_SIZE * INTERIOR_SIZE)
#define RED UINT32_C(0xff0000ff)
#define GREEN UINT32_C(0xff00ff00)
#define BLUE UINT32_C(0xffff0000)
#define WHITE UINT32_C(0xffffffff)
#define YELLOW UINT32_C(0xff00ffff)
#define ALPHA_BLACK UINT32_C(0xff000000)

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
      printf("[ps5-egl-raster-semantics] shader=0x%x log=%.*s\n",
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
   GLint linked = GL_FALSE;
   GLuint program = glCreateProgram();

   if (!program)
      return 0;
   glAttachShader(program, vertex);
   glAttachShader(program, fragment);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      glDeleteProgram(program);
      return 0;
   }
   *result = program;
   return 1;
}

static unsigned
matching_pixels(const uint32_t *pixels, unsigned count, uint32_t expected)
{
   unsigned matching = 0;

   for (unsigned i = 0; i < count; ++i)
      matching += pixels[i] == expected;
   return matching;
}

static unsigned
nonzero_pixels(const uint32_t *pixels, unsigned count)
{
   unsigned nonzero = 0;

   for (unsigned i = 0; i < count; ++i)
      nonzero += pixels[i] != 0;
   return nonzero;
}

static void
read_target(uint32_t *pixels)
{
   glFinish();
   glReadPixels(0, 0, TARGET_WIDTH, TARGET_HEIGHT,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

int
main(void)
{
   static const char *flat_vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "flat out vec3 vertex_color;\n"
      "void main() {\n"
      "  gl_Position = vec4(position, 0.0, 1.0);\n"
      "  vertex_color = position.x < -0.5 ? vec3(1.0, 0.0, 0.0)\n"
      "               : position.x > 0.5 ? vec3(0.0, 1.0, 0.0)\n"
      "                                  : vec3(0.0, 0.0, 1.0);\n"
      "}\n";
   static const char *flat_fragment_source =
      "#version 330 core\n"
      "flat in vec3 vertex_color;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = vec4(vertex_color, 1.0); }\n";
   static const char *coord_vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
   static const char *point_vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "flat out vec3 vertex_color;\n"
      "void main() {\n"
      "  gl_Position = vec4(position, 0.0, 1.0);\n"
      "  gl_PointSize = 8.0;\n"
      "  vertex_color = vec3(1.0);\n"
      "}\n";
   static const char *coord_fragment_source =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  bool left = gl_FragCoord.x < 64.0;\n"
      "  bool bottom = gl_FragCoord.y < 48.0;\n"
      "  float marker = left ? (bottom ? 1.0 : 0.5)\n"
      "                      : (bottom ? 0.25 : 0.0);\n"
      "  color = vec4(fract(gl_FragCoord.x), fract(gl_FragCoord.y),\n"
      "               marker, 1.0);\n"
      "}\n";
   static const char *point_coord_fragment_source =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  color = vec4(gl_PointCoord.x < 0.5 ? 1.0 : 0.0,\n"
      "               gl_PointCoord.y < 0.5 ? 1.0 : 0.0,\n"
      "               0.0, 1.0);\n"
      "}\n";
   static const char *front_facing_fragment_source =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  color = gl_FrontFacing ? vec4(0.0, 1.0, 0.0, 1.0)\n"
      "                         : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const float vertices[] = {
      -0.75f, -0.75f,
       0.75f, -0.75f,
       0.00f,  0.75f,
   };
   static const float line_vertices[] = {
      -0.75f, 0.0f,
       0.00f, 0.0f,
   };
   static const float point_vertex[] = {0.0f, 0.0f};
   static const GLenum topology_modes[] = {
      GL_LINES, GL_LINE_STRIP, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN,
   };
   static const unsigned topology_counts[] = {2, 2, 3, 3};
   static const float fullscreen_vertices[] = {
      -1.0f, -1.0f,
       3.0f, -1.0f,
      -1.0f,  3.0f,
   };
   static uint32_t pixels[PIXELS];
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
   EGLint major = 0, minor = 0, count = 0, profile = 0;
   GLuint shaders[7] = {0, 0, 0, 0, 0, 0, 0};
   GLuint programs[5] = {0, 0, 0, 0, 0};
   GLuint vertex_array = 0, vertex_buffer = 0;
   GLuint texture = 0, framebuffer = 0;
   GLenum framebuffer_status = 0, invalid_error = GL_NO_ERROR;
   GLenum error = GL_NO_ERROR;
   GLint default_provoking = 0, current_provoking = 0;
   unsigned draw_calls = 0, flat_first = 0, flat_last = 0;
   unsigned topology_expected[4][2] = {{0}};
   unsigned topology_colored[4][2] = {{0}};
   unsigned coordinate_matches = 0;
   unsigned fixed_point_pixels = 0, program_point_pixels = 0;
   unsigned line1_pixels = 0, line4_pixels = 0;
   unsigned front_face_pixels[2] = {0, 0};
   unsigned front_face_colored[2] = {0, 0};
   unsigned point_coord_counts[2][4] = {{0}};
   uint32_t point_coord_samples[2][4] = {{0}};
   GLfloat point_range[2] = {0.0f, 0.0f};
   GLfloat line_range[2] = {0.0f, 0.0f};
   GLenum point_invalid = GL_NO_ERROR, line_invalid = GL_NO_ERROR;
   uint32_t first_sample = 0, last_sample = 0;
   int draw_status[11] = {-1, -1, -1, -1, -1, -1,
                          -1, -1, -1, -1, -1};
   int topology_status[4][2] = {{-1, -1}, {-1, -1},
                                {-1, -1}, {-1, -1}};
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
       !eglSwapInterval(display, 0) ||
       !eglQueryContext(display, context,
                        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, &profile))
      goto cleanup;
   made_current = 1;

   if (!compile_shader(GL_VERTEX_SHADER, flat_vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, flat_fragment_source,
                       &shaders[1]) ||
       !compile_shader(GL_VERTEX_SHADER, coord_vertex_source, &shaders[2]) ||
       !compile_shader(GL_FRAGMENT_SHADER, coord_fragment_source,
                       &shaders[3]) ||
       !compile_shader(GL_VERTEX_SHADER, point_vertex_source, &shaders[4]) ||
       !compile_shader(GL_FRAGMENT_SHADER, point_coord_fragment_source,
                       &shaders[5]) ||
       !compile_shader(GL_FRAGMENT_SHADER, front_facing_fragment_source,
                       &shaders[6]) ||
       !link_program(shaders[0], shaders[1], &programs[0]) ||
       !link_program(shaders[2], shaders[3], &programs[1]) ||
       !link_program(shaders[4], shaders[1], &programs[2]) ||
       !link_program(shaders[4], shaders[5], &programs[3]) ||
       !link_program(shaders[2], shaders[6], &programs[4]))
      goto cleanup;

   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
   glGenBuffers(1, &vertex_buffer);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TARGET_WIDTH, TARGET_HEIGHT,
                0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, TARGET_WIDTH, TARGET_HEIGHT);
   glGetIntegerv(GL_PROVOKING_VERTEX, &default_provoking);
   glProvokingVertex(GL_TRIANGLES);
   invalid_error = glGetError();
   glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
   glUseProgram(programs[0]);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[0] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels((TARGET_WIDTH - INTERIOR_SIZE) / 2,
                (TARGET_HEIGHT - INTERIOR_SIZE) / 2,
                INTERIOR_SIZE, INTERIOR_SIZE,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   first_sample = pixels[0];
   flat_first = matching_pixels(pixels, INTERIOR_PIXELS, RED);

   glProvokingVertex(GL_LAST_VERTEX_CONVENTION);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[1] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels((TARGET_WIDTH - INTERIOR_SIZE) / 2,
                (TARGET_HEIGHT - INTERIOR_SIZE) / 2,
                INTERIOR_SIZE, INTERIOR_SIZE,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   last_sample = pixels[0];
   flat_last = matching_pixels(pixels, INTERIOR_PIXELS, BLUE);
   glGetIntegerv(GL_PROVOKING_VERTEX, &current_provoking);

   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   for (unsigned topology = 0; topology < 4; ++topology) {
      const void *data = topology < 2 ? (const void *)line_vertices
                                      : (const void *)vertices;
      const GLsizeiptr size = topology < 2 ? sizeof(line_vertices)
                                           : sizeof(vertices);

      glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
      for (unsigned convention = 0; convention < 2; ++convention) {
         const uint32_t expected = convention ? BLUE : RED;

         glClear(GL_COLOR_BUFFER_BIT);
         glProvokingVertex(convention ? GL_LAST_VERTEX_CONVENTION
                                      : GL_FIRST_VERTEX_CONVENTION);
         glDrawArrays(topology_modes[topology], 0,
                      topology_counts[topology]);
         topology_status[topology][convention] =
            ps5_egl_current_draw_status(&draw_calls);
         read_target(pixels);
         topology_expected[topology][convention] =
            matching_pixels(pixels, PIXELS, expected);
         topology_colored[topology][convention] =
            nonzero_pixels(pixels, PIXELS);
      }
   }

   glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, point_range);
   glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, line_range);
   glPointSize(0.0f);
   point_invalid = glGetError();
   glLineWidth(0.0f);
   line_invalid = glGetError();

   glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(point_vertex), point_vertex);
   glUseProgram(programs[2]);
   glDisable(GL_PROGRAM_POINT_SIZE);
   glPointSize(4.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_POINTS, 0, 1);
   draw_status[3] = ps5_egl_current_draw_status(&draw_calls);
   read_target(pixels);
   fixed_point_pixels = matching_pixels(pixels, PIXELS, WHITE);

   glEnable(GL_PROGRAM_POINT_SIZE);
   glPointSize(2.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_POINTS, 0, 1);
   draw_status[4] = ps5_egl_current_draw_status(&draw_calls);
   read_target(pixels);
   program_point_pixels = matching_pixels(pixels, PIXELS, WHITE);
   glDisable(GL_PROGRAM_POINT_SIZE);

   glUseProgram(programs[3]);
   glEnable(GL_PROGRAM_POINT_SIZE);
   for (unsigned origin = 0; origin < 2; ++origin) {
      const unsigned x0 = (TARGET_WIDTH - 8) / 2;
      const unsigned y0 = (TARGET_HEIGHT - 8) / 2;
      static const uint32_t colors[4] = {
         RED, ALPHA_BLACK, YELLOW, GREEN,
      };

      glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN,
                        origin ? GL_LOWER_LEFT : GL_UPPER_LEFT);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_POINTS, 0, 1);
      draw_status[7 + origin] = ps5_egl_current_draw_status(&draw_calls);
      read_target(pixels);
      for (unsigned color = 0; color < 4; ++color)
         point_coord_counts[origin][color] =
            matching_pixels(pixels, PIXELS, colors[color]);
      point_coord_samples[origin][0] = pixels[y0 * TARGET_WIDTH + x0];
      point_coord_samples[origin][1] = pixels[y0 * TARGET_WIDTH + x0 + 7];
      point_coord_samples[origin][2] = pixels[(y0 + 7) * TARGET_WIDTH + x0];
      point_coord_samples[origin][3] =
         pixels[(y0 + 7) * TARGET_WIDTH + x0 + 7];
   }
   glPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_UPPER_LEFT);
   glDisable(GL_PROGRAM_POINT_SIZE);

   glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(line_vertices), line_vertices);
   glUseProgram(programs[0]);
   glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
   glLineWidth(1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_LINES, 0, 2);
   draw_status[5] = ps5_egl_current_draw_status(&draw_calls);
   read_target(pixels);
   line1_pixels = matching_pixels(pixels, PIXELS, RED);

   glLineWidth(4.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_LINES, 0, 2);
   draw_status[6] = ps5_egl_current_draw_status(&draw_calls);
   read_target(pixels);
   line4_pixels = matching_pixels(pixels, PIXELS, RED);
   glLineWidth(1.0f);

   glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
   glUseProgram(programs[4]);
   for (unsigned winding = 0; winding < 2; ++winding) {
      const uint32_t expected = winding ? RED : GREEN;

      glFrontFace(winding ? GL_CW : GL_CCW);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      draw_status[9 + winding] = ps5_egl_current_draw_status(&draw_calls);
      read_target(pixels);
      front_face_pixels[winding] = matching_pixels(pixels, PIXELS, expected);
      front_face_colored[winding] = nonzero_pixels(pixels, PIXELS);
   }
   glFrontFace(GL_CCW);

   glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(fullscreen_vertices),
                   fullscreen_vertices);
   glUseProgram(programs[1]);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[2] = ps5_egl_current_draw_status(&draw_calls);
   read_target(pixels);
   for (unsigned y = 0; y < TARGET_HEIGHT; ++y) {
      for (unsigned x = 0; x < TARGET_WIDTH; ++x) {
         uint32_t pixel = pixels[y * TARGET_WIDTH + x];
         unsigned red = pixel & 0xffu;
         unsigned green = (pixel >> 8) & 0xffu;
         unsigned blue = (pixel >> 16) & 0xffu;
         unsigned expected_blue = x < TARGET_WIDTH / 2
                                     ? (y < TARGET_HEIGHT / 2 ? 255u : 128u)
                                     : (y < TARGET_HEIGHT / 2 ? 64u : 0u);

         coordinate_matches += (red == 127u || red == 128u) &&
                               (green == 127u || green == 128u) &&
                               blue == expected_blue &&
                               (pixel >> 24) == 255u;
      }
   }
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
            default_provoking == GL_LAST_VERTEX_CONVENTION &&
            current_provoking == GL_LAST_VERTEX_CONVENTION &&
            invalid_error == GL_INVALID_ENUM &&
            draw_status[0] == 0 && draw_status[1] == 0 &&
            draw_status[2] == 0 && draw_status[3] == 0 &&
            draw_status[4] == 0 && draw_status[5] == 0 &&
            draw_status[6] == 0 && draw_status[7] == 0 &&
            draw_status[8] == 0 && draw_status[9] == 0 &&
            draw_status[10] == 0 && draw_calls == 19 &&
            flat_first == INTERIOR_PIXELS &&
            flat_last == INTERIOR_PIXELS &&
            point_range[0] == 1.0f && point_range[1] == 64.0f &&
            line_range[0] == 1.0f && line_range[1] == 64.0f &&
            point_invalid == GL_INVALID_VALUE &&
            line_invalid == GL_INVALID_VALUE &&
            fixed_point_pixels == 16 && program_point_pixels == 64 &&
            line1_pixels == 48 && line4_pixels == 192 &&
            point_coord_counts[0][0] == 16 &&
            point_coord_counts[0][1] == 16 &&
            point_coord_counts[0][2] == 16 &&
            point_coord_counts[0][3] == 16 &&
            point_coord_counts[1][0] == 16 &&
            point_coord_counts[1][1] == 16 &&
            point_coord_counts[1][2] == 16 &&
            point_coord_counts[1][3] == 16 &&
            point_coord_samples[0][0] == RED &&
            point_coord_samples[0][1] == ALPHA_BLACK &&
            point_coord_samples[0][2] == YELLOW &&
            point_coord_samples[0][3] == GREEN &&
            point_coord_samples[1][0] == YELLOW &&
            point_coord_samples[1][1] == GREEN &&
            point_coord_samples[1][2] == RED &&
            point_coord_samples[1][3] == ALPHA_BLACK &&
            front_face_pixels[0] == 3456 &&
            front_face_pixels[0] == front_face_colored[0] &&
            front_face_pixels[1] == 3456 &&
            front_face_pixels[1] == front_face_colored[1] &&
            coordinate_matches == PIXELS && error == GL_NO_ERROR;
   for (unsigned topology = 0; topology < 4; ++topology) {
      for (unsigned convention = 0; convention < 2; ++convention) {
         passed &= topology_status[topology][convention] == 0 &&
                   topology_expected[topology][convention] != 0 &&
                   topology_expected[topology][convention] ==
                      topology_colored[topology][convention];
      }
   }
   printf("[ps5-egl-raster-semantics] fbo=%x provoking=%x/%x "
          "invalid=0x%x draw=%d/%d/%d/%u flat=%u/%u "
          "samples=%08x/%08x "
          "coord=%u corners=%08x/%08x/%08x/%08x "
          "error=0x%x result=%d\n",
          framebuffer_status, default_provoking, current_provoking,
          invalid_error, draw_status[0], draw_status[1], draw_status[2],
          draw_calls, flat_first, flat_last, first_sample, last_sample,
          coordinate_matches,
          pixels[0], pixels[TARGET_WIDTH - 1],
          pixels[(TARGET_HEIGHT - 1) * TARGET_WIDTH], pixels[PIXELS - 1],
          error, passed ? 0 : 1);
   printf("[ps5-egl-raster-semantics] topology "
          "lines=%u/%u:%u/%u strip=%u/%u:%u/%u "
          "tri-strip=%u/%u:%u/%u fan=%u/%u:%u/%u result=%d\n",
          topology_expected[0][0], topology_colored[0][0],
          topology_expected[0][1], topology_colored[0][1],
          topology_expected[1][0], topology_colored[1][0],
          topology_expected[1][1], topology_colored[1][1],
          topology_expected[2][0], topology_colored[2][0],
          topology_expected[2][1], topology_colored[2][1],
          topology_expected[3][0], topology_colored[3][0],
          topology_expected[3][1], topology_colored[3][1],
          passed ? 0 : 1);
   printf("[ps5-egl-raster-semantics] sizes "
          "point-range=%.3f/%.3f fixed=%u program=%u invalid=0x%x "
          "line-range=%.3f/%.3f width1=%u width4=%u invalid=0x%x "
          "result=%d\n",
          point_range[0], point_range[1], fixed_point_pixels,
          program_point_pixels, point_invalid,
          line_range[0], line_range[1], line1_pixels, line4_pixels,
          line_invalid, passed ? 0 : 1);
   printf("[ps5-egl-raster-semantics] point-coord "
          "upper=%u/%u/%u/%u:%08x/%08x/%08x/%08x "
          "lower=%u/%u/%u/%u:%08x/%08x/%08x/%08x result=%d\n",
          point_coord_counts[0][0], point_coord_counts[0][1],
          point_coord_counts[0][2], point_coord_counts[0][3],
          point_coord_samples[0][0], point_coord_samples[0][1],
          point_coord_samples[0][2], point_coord_samples[0][3],
          point_coord_counts[1][0], point_coord_counts[1][1],
          point_coord_counts[1][2], point_coord_counts[1][3],
          point_coord_samples[1][0], point_coord_samples[1][1],
          point_coord_samples[1][2], point_coord_samples[1][3],
          passed ? 0 : 1);
   printf("[ps5-egl-raster-semantics] front-facing "
          "ccw=%u/%u cw=%u/%u result=%d\n",
          front_face_pixels[0], front_face_colored[0],
          front_face_pixels[1], front_face_colored[1], passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (texture)
         glDeleteTextures(1, &texture);
      if (vertex_buffer)
         glDeleteBuffers(1, &vertex_buffer);
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
      for (unsigned i = 0; i < 5; ++i)
         if (programs[i])
            glDeleteProgram(programs[i]);
      for (unsigned i = 0; i < 7; ++i)
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
   printf("[ps5-egl-raster-semantics] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
