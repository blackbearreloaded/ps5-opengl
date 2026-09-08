// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define WIDTH 1920
#define HEIGHT 1080
#define CAPTURE_OFFSET 256
#define OUTPUT_BYTES 512

_Static_assert(CAPTURE_OFFSET % sizeof(GLuint) == 0,
               "transform-feedback range must be dword aligned");

int ps5_egl_current_draw_status(unsigned *draw_calls);

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint ok = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return -1;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      GLchar log[512];
      GLsizei length = 0;
      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-xfb] compile type=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
   return 0;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 a_position;\n"
      "out vec2 captured;\n"
      "void main() {\n"
      "  captured = a_position + vec2(0.25, 0.5);\n"
      "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
      "}\n";
   static const char *fragment_source =
      "#version 330\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }\n";
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static const float expected[6] = {
      -0.25f, 0.0f, 0.75f, 0.0f, 0.25f, 1.0f,
   };
   static const char *varying = "captured";
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
   EGLint major = 0, minor = 0, count = 0;
   GLuint vs = 0, fs = 0, program = 0, vao = 0, vbo = 0, output = 0;
   GLint linked = GL_FALSE, context_major = 0, context_minor = 0;
   GLint buffers = 0, separate = 0, interleaved = 0;
   GLuint initial_words[OUTPUT_BYTES / sizeof(GLuint)];
   const GLubyte *captured = NULL;
   const float *captured_values = NULL;
   GLenum error = GL_NO_ERROR;
   int draw_status = -100;
   unsigned draw_calls = 0;
   int made_current = 0;
   int prefix_ok = 0;
   int values_ok = 0;
   int suffix_ok = 0;
   int passed = 0;

   for (unsigned i = 0; i < OUTPUT_BYTES / sizeof(GLuint); ++i)
      initial_words[i] = UINT32_C(0x5a5a5a5a);

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

   glGetIntegerv(GL_MAJOR_VERSION, &context_major);
   glGetIntegerv(GL_MINOR_VERSION, &context_minor);
   if (context_major != 3 || context_minor != 3)
      goto cleanup;

   glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, &buffers);
   glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS, &separate);
   glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS,
                 &interleaved);
   if (buffers < 4 || separate < 4 || interleaved < 64 ||
       compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) != 0 ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fs) != 0)
      goto cleanup;

   program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glBindAttribLocation(program, 0, "a_position");
   glTransformFeedbackVaryings(program, 1, &varying,
                               GL_INTERLEAVED_ATTRIBS);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   glGenBuffers(1, &output);
   glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, output);
   glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, OUTPUT_BYTES, initial_words,
                GL_DYNAMIC_READ);
   glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, output,
                     CAPTURE_OFFSET, sizeof(expected));
   glViewport(0, 0, WIDTH, HEIGHT);
   glBeginTransformFeedback(GL_TRIANGLES);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glEndTransformFeedback();
   glFinish();

   captured = glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0,
                               OUTPUT_BYTES, GL_MAP_READ_BIT);
   error = glGetError();
   if (captured) {
      captured_values = (const float *)(captured + CAPTURE_OFFSET);
      prefix_ok = !memcmp(captured, initial_words, CAPTURE_OFFSET);
      values_ok = !memcmp(captured + CAPTURE_OFFSET, expected,
                          sizeof(expected));
      suffix_ok = !memcmp(captured + CAPTURE_OFFSET + sizeof(expected),
                          (const GLubyte *)initial_words + CAPTURE_OFFSET +
                             sizeof(expected),
                          OUTPUT_BYTES - CAPTURE_OFFSET - sizeof(expected));
   }
   if (prefix_ok && values_ok && suffix_ok &&
       error == GL_NO_ERROR && draw_status == 0 && draw_calls == 1)
      passed = 1;
   printf("[ps5-egl-xfb] core=%d.%d limits=%d/%d/%d offset=%d "
          "canary=%d/%d values_ok=%d output=%g,%g,%g,%g,%g,%g "
          "draw_status=%d submissions=%u error=0x%x\n",
          context_major, context_minor, buffers, separate, interleaved,
          CAPTURE_OFFSET, prefix_ok, suffix_ok, values_ok,
          captured_values ? captured_values[0] : 0.0,
          captured_values ? captured_values[1] : 0.0,
          captured_values ? captured_values[2] : 0.0,
          captured_values ? captured_values[3] : 0.0,
          captured_values ? captured_values[4] : 0.0,
          captured_values ? captured_values[5] : 0.0,
          draw_status, draw_calls, error);
   if (captured)
      glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
   captured = NULL;
   if (!eglSwapBuffers(display, surface))
      passed = 0;

cleanup:
   if (output)
      glDeleteBuffers(1, &output);
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
   if (made_current && glGetError() != GL_NO_ERROR)
      passed = 0;
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     EGL_NO_CONTEXT);
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      eglTerminate(display);
   printf("[ps5-egl-xfb] result=%d\n", passed ? 0 : 1);
   return passed ? 0 : 1;
}
