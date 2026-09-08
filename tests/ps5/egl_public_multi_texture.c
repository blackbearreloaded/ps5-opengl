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
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
   return 0;
}

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
draw_oracle(GLint first, GLint second, GLint first_unit, GLint second_unit,
            uint32_t expected, uint32_t expected_hash, uint32_t *pixels,
            const char *name)
{
   unsigned matching = 0;

   glUniform1i(first, first_unit);
   glUniform1i(second, second_unit);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   uint32_t hash = hash32(pixels, SIZE * SIZE * sizeof(*pixels));
   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      matching += pixels[i] == expected;
   printf("[ps5-egl-multi-texture] %s matching=%u hash=%08x error=0x%x\n",
          name, matching, hash, error);
   return matching == SIZE * SIZE && hash == expected_hash &&
          error == GL_NO_ERROR;
}

int
main(void)
{
#ifdef PS5_CORE33_SAMPLER_ARRAY_TEST
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform sampler2D u_textures[2];\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  vec4 a = texture(u_textures[0], vec2(0.5));\n"
      "  vec4 b = texture(u_textures[1], vec2(0.5));\n"
      "  color = vec4(a.r, b.g, 0.0, 1.0);\n"
      "}\n";
#else
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 120\n"
      "uniform sampler2D u_first;\n"
      "uniform sampler2D u_second;\n"
      "void main() {\n"
      "  vec4 a = texture2D(u_first, vec2(0.5));\n"
      "  vec4 b = texture2D(u_second, vec2(0.5));\n"
      "  gl_FragColor = vec4(a.r, b.g, 0.0, 1.0);\n"
      "}\n";
#endif
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static const uint8_t texels[2][4] = {
      {0x40, 0x11, 0x22, 0xff},
      {0x33, 0x80, 0x44, 0xff},
   };
   static uint32_t pixels[SIZE * SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
#ifdef PS5_CORE33_SAMPLER_ARRAY_TEST
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
#endif
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0, width = 0, height = 0;
   GLuint vs = 0, fs = 0, program = 0, vbo = 0;
#ifdef PS5_CORE33_SAMPLER_ARRAY_TEST
   GLuint vao = 0;
   EGLint profile = 0;
#endif
   GLuint textures[2] = {0, 0};
   GLint linked = GL_FALSE, first = -1, second = -1;
   const GLubyte *version = NULL, *glsl = NULL;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, direct_ok = 0, swapped_ok = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
#ifdef PS5_CORE33_SAMPLER_ARRAY_TEST
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
#else
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
#endif
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;
#ifdef PS5_CORE33_SAMPLER_ARRAY_TEST
   if (!eglQueryContext(display, context,
                        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, &profile))
      goto cleanup;
#endif
   eglQuerySurface(display, surface, EGL_WIDTH, &width);
   eglQuerySurface(display, surface, EGL_HEIGHT, &height);
   version = glGetString(GL_VERSION);
   glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
   if (!version || !glsl ||
       compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fs))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glBindAttribLocation(program, 0, "a_position");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);
#ifdef PS5_CORE33_SAMPLER_ARRAY_TEST
   first = glGetUniformLocation(program, "u_textures[0]");
   second = glGetUniformLocation(program, "u_textures[1]");
#else
   first = glGetUniformLocation(program, "u_first");
   second = glGetUniformLocation(program, "u_second");
#endif
   if (first < 0 || second < 0)
      goto cleanup;

#ifdef PS5_CORE33_SAMPLER_ARRAY_TEST
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
#endif
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);
   glGenTextures(2, textures);
   for (unsigned unit = 0; unit < 2; ++unit) {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, textures[unit]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, texels[unit]);
   }
   if (glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   direct_ok = draw_oracle(first, second, 0, 1, UINT32_C(0xff008040),
                           UINT32_C(0xebc31dc5), pixels, "units-0-1");
   swapped_ok = draw_oracle(first, second, 1, 0, UINT32_C(0xff001133),
                            UINT32_C(0x8feb3dc5), pixels, "units-1-0");
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && direct_ok && swapped_ok &&
#ifdef PS5_CORE33_SAMPLER_ARRAY_TEST
            profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            strncmp((const char *)version, "3.3 ", 4) == 0 &&
            strncmp((const char *)glsl, "3.30", 4) == 0;
#else
            strncmp((const char *)version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl, "1.20", 4) == 0;
#endif

cleanup:
   if (textures[0] || textures[1])
      glDeleteTextures(2, textures);
   if (vbo)
      glDeleteBuffers(1, &vbo);
#ifdef PS5_CORE33_SAMPLER_ARRAY_TEST
   if (vao)
      glDeleteVertexArrays(1, &vao);
#endif
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
   printf("[ps5-egl-multi-texture] cleanup=%x/%x result=%d\n",
          cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
