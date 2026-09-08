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
#include <GL/glext.h>

#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#define TARGET_SIZE 64
#define CROP_SIZE 64
#define EXPECTED_PIXEL UINT32_C(0xffffff00)
#define EXPECTED_HASH UINT32_C(0xf1461dc5)

int ps5_egl_current_draw_status(unsigned *draw_calls);

#ifndef PS5_CORE_33_TEST
static int
has_extension(const char *extensions, const char *name)
{
   const size_t length = strlen(name);
   const char *match = extensions;

   while (match && (match = strstr(match, name))) {
      if ((match == extensions || match[-1] == ' ') &&
          (match[length] == '\0' || match[length] == ' '))
         return 1;
      match += length;
   }
   return 0;
}
#else
static int
has_core_extension(const char *name)
{
   GLint count = 0;

   glGetIntegerv(GL_NUM_EXTENSIONS, &count);
   for (GLint index = 0; index < count; ++index) {
      const char *extension = (const char *)glGetStringi(
         GL_EXTENSIONS, (GLuint)index);
      if (extension && strcmp(extension, name) == 0)
         return 1;
   }
   return 0;
}
#endif

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return -1;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512];
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-rgb10-a2ui] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
   return 0;
}

static int
link_program(GLuint vertex, GLuint fragment, GLuint *result)
{
   GLint linked = GL_FALSE;
   GLuint program = glCreateProgram();

   if (!program)
      return -1;
   glAttachShader(program, vertex);
   glAttachShader(program, fragment);
   glBindAttribLocation(program, 0, "a_position");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      char log[512];
      GLsizei length = 0;

      glGetProgramInfoLog(program, sizeof(log), &length, log);
      printf("[ps5-egl-rgb10-a2ui] link=%.*s\n", length, log);
      glDeleteProgram(program);
      return -1;
   }
   *result = program;
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

int
main(void)
{
#ifdef PS5_CORE_33_TEST
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *producer_source =
      "#version 330\n"
      "layout(location = 0) out uvec4 frag_value;\n"
      "void main() { frag_value = uvec4(1023u, 341u, 17u, 2u); }\n";
   static const char *consumer_source =
      "#version 330\n"
      "uniform usampler2D u_texture;\n"
      "out vec4 color;\n"
      "void main() {\n"
      "  uvec4 value = texture(u_texture, vec2(0.5));\n"
      "  color = all(equal(value, uvec4(1023u, 341u, 17u, 2u)))\n"
      "     ? vec4(0.0, 1.0, 1.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
#else
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *producer_source =
      "#version 120\n"
      "#extension GL_EXT_gpu_shader4 : require\n"
      "varying out uvec4 frag_value;\n"
      "void main() { frag_value = uvec4(1023u, 341u, 17u, 2u); }\n";
   static const char *consumer_source =
      "#version 120\n"
      "#extension GL_EXT_gpu_shader4 : require\n"
      "uniform usampler2D u_texture;\n"
      "void main() {\n"
      "  uvec4 value = texture2D(u_texture, vec2(0.5));\n"
      "  gl_FragColor = all(equal(value, uvec4(1023u, 341u, 17u, 2u)))\n"
      "     ? vec4(0.0, 1.0, 1.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
#endif
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static uint32_t pixels[CROP_SIZE * CROP_SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
#ifdef PS5_CORE_33_TEST
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
   EGLint context_version = 0, profile_mask = 0;
   GLuint vs = 0, producer_fs = 0, consumer_fs = 0;
   GLuint producer = 0, consumer = 0, vbo = 0, texture = 0, framebuffer = 0;
#ifdef PS5_CORE_33_TEST
   GLuint vertex_array = 0;
#endif
   GLint sampler = -1, internal_format = 0;
   GLenum setup_error = GL_NO_ERROR, bounds_error = GL_NO_ERROR;
   GLenum framebuffer_status = 0, cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   const GLubyte *version = NULL;
   char version_text[64] = "(null)";
#ifndef PS5_CORE_33_TEST
   const char *extensions = NULL;
#endif
   unsigned matching = 0, producer_calls = 0, consumer_calls = 0;
   uint32_t hash = 0;
   int producer_status = -100, consumer_status = -100;
   int rgb10_extension = 0, gpu_shader4_extension = 0;
   int made_current = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
#ifdef PS5_CORE_33_TEST
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
   eglQuerySurface(display, surface, EGL_WIDTH, &width);
   eglQuerySurface(display, surface, EGL_HEIGHT, &height);
   eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION,
                   &context_version);
#ifdef PS5_CORE_33_TEST
   eglQueryContext(display, context, EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                   &profile_mask);
#endif
   version = glGetString(GL_VERSION);
#ifdef PS5_CORE_33_TEST
   if (!version)
      goto cleanup;
   rgb10_extension = has_core_extension("GL_ARB_texture_rgb10_a2ui");
   gpu_shader4_extension = has_core_extension("GL_EXT_gpu_shader4");
#else
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!version || !extensions)
      goto cleanup;
   rgb10_extension = has_extension(extensions, "GL_ARB_texture_rgb10_a2ui");
   gpu_shader4_extension = has_extension(extensions, "GL_EXT_gpu_shader4");
#endif
   snprintf(version_text, sizeof(version_text), "%s", (const char *)version);

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, producer_source, &producer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, consumer_source, &consumer_fs) ||
       link_program(vs, producer_fs, &producer) ||
       link_program(vs, consumer_fs, &consumer))
      goto cleanup;
   sampler = glGetUniformLocation(consumer, "u_texture");
   if (sampler < 0)
      goto cleanup;

#ifdef PS5_CORE_33_TEST
   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
#endif
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB10_A2UI, TARGET_SIZE, TARGET_SIZE,
                0, GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV, NULL);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                            &internal_format);
   setup_error = glGetError();
   glTexSubImage2D(GL_TEXTURE_2D, 0, TARGET_SIZE, 0, 1, 1,
                   GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV, NULL);
   bounds_error = glGetError();

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (setup_error == GL_NO_ERROR && bounds_error == GL_INVALID_VALUE &&
       internal_format == GL_RGB10_A2UI &&
       framebuffer_status == GL_FRAMEBUFFER_COMPLETE) {
      glViewport(0, 0, TARGET_SIZE, TARGET_SIZE);
      glUseProgram(producer);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      producer_status = ps5_egl_current_draw_status(&producer_calls);

      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      glUseProgram(consumer);
      glUniform1i(sampler, 0);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      consumer_status = ps5_egl_current_draw_status(&consumer_calls);
      glFinish();
      glReadPixels((DISPLAY_WIDTH - CROP_SIZE) / 2,
                   (DISPLAY_HEIGHT - CROP_SIZE) / 2,
                   CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      if (glGetError() == GL_NO_ERROR) {
         hash = hash32(pixels, sizeof(pixels));
         for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
            matching += pixels[i] == EXPECTED_PIXEL;
      }
   }

   passed = egl_major == 1 && egl_minor == 4 &&
            width == DISPLAY_WIDTH && height == DISPLAY_HEIGHT &&
#ifdef PS5_CORE_33_TEST
            context_version == 3 &&
            profile_mask == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            strncmp(version_text, "3.3 ", 4) == 0 &&
#endif
            rgb10_extension &&
#ifndef PS5_CORE_33_TEST
            gpu_shader4_extension &&
#endif
            producer_status == 0 && consumer_status == 0 &&
            matching == CROP_SIZE * CROP_SIZE && hash == EXPECTED_HASH &&
            eglSwapBuffers(display, surface);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (texture)
         glDeleteTextures(1, &texture);
      if (vbo)
         glDeleteBuffers(1, &vbo);
#ifdef PS5_CORE_33_TEST
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
#endif
      if (producer)
         glDeleteProgram(producer);
      if (consumer)
         glDeleteProgram(consumer);
      if (producer_fs)
         glDeleteShader(producer_fs);
      if (consumer_fs)
         glDeleteShader(consumer_fs);
      if (vs)
         glDeleteShader(vs);
      cleanup_gl_error = glGetError();
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE,
                                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY) {
      cleanup_ok &= eglTerminate(display);
      cleanup_egl_error = eglGetError();
   }
   passed &= cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
             cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-rgb10-a2ui] egl=%d.%d gl=%s size=%dx%d "
          "context=%d/%04x ext=%d/%d "
          "internal=0x%x setup=0x%x bounds=0x%x fbo=0x%x "
          "pixels=%u/%08x draws=%d/%u,%d/%u "
          "cleanup=%u/0x%x/0x%x result=%d\n",
          egl_major, egl_minor, version_text, width, height,
          context_version, profile_mask,
          rgb10_extension, gpu_shader4_extension, internal_format,
          setup_error, bounds_error, framebuffer_status, matching, hash,
          producer_status, producer_calls, consumer_status, consumer_calls,
          cleanup_ok, cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
