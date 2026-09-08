// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#ifdef PS5_DYNAMIC_COLOR_TARGET_TEST
#define TARGET_WIDTH 128
#define TARGET_HEIGHT 96
#define UPLOAD_PIXEL UINT32_C(0xff00ff00)
#define UPLOAD_HASH UINT32_C(0xc38d1dc5)
#else
#define TARGET_WIDTH DISPLAY_WIDTH
#define TARGET_HEIGHT DISPLAY_HEIGHT
#endif
#define CROP_SIZE 64
#define EXPECTED_PIXEL UINT32_C(0xff0000ff)
#define EXPECTED_HASH UINT32_C(0xc40abdc5)
#ifdef PS5_FRAMEBUFFER_SRGB_TEST
#define PRODUCER_VALUE "0.5, 0.5, 0.5, 1.0"
#define TEXTURE_INTERNAL_FORMAT GL_SRGB8_ALPHA8
#define DISABLED_EXPECTED_PIXEL UINT32_C(0xff373737)
#define DISABLED_EXPECTED_HASH UINT32_C(0x06bfddc5)
#define ENABLED_EXPECTED_PIXEL UINT32_C(0xff808080)
#define ENABLED_EXPECTED_HASH UINT32_C(0xcec31dc5)
#else
#define PRODUCER_VALUE "1.0, 0.0, 0.0, 1.0"
#define TEXTURE_INTERNAL_FORMAT GL_RGBA8
#endif

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
      printf("[ps5-egl-rtt] shader=0x%x log=%.*s\n", type, length, log);
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
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *producer_source =
      "#version 120\n"
      "void main() { gl_FragColor = vec4(" PRODUCER_VALUE "); }\n";
   static const char *consumer_source =
      "#version 120\n"
      "uniform sampler2D u_texture;\n"
      "void main() { gl_FragColor = texture2D(u_texture, vec2(0.5)); }\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static uint32_t pixels[CROP_SIZE * CROP_SIZE];
#ifdef PS5_DYNAMIC_COLOR_TARGET_TEST
   static uint32_t upload[TARGET_WIDTH * TARGET_HEIGHT];
#endif
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0, width = 0, height = 0;
   GLuint vs = 0, producer_fs = 0, consumer_fs = 0;
   GLuint producer = 0, consumer = 0, vbo = 0, texture = 0, framebuffer = 0;
   GLint sampler = -1;
   GLenum framebuffer_status = 0, draw_error = GL_NO_ERROR;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   const char *extensions = NULL;
   unsigned matching = 0;
   unsigned upload_matching = 0;
   unsigned enabled_matching = 0;
   uint32_t hash = 0;
   uint32_t upload_hash = 0;
   uint32_t enabled_hash = 0;
   int made_current = 0, fbo_extension = 0, blit_extension = 0;
   int framebuffer_srgb_extension = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;
   eglQuerySurface(display, surface, EGL_WIDTH, &width);
   eglQuerySurface(display, surface, EGL_HEIGHT, &height);
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!extensions)
      goto cleanup;
   fbo_extension = has_extension(extensions, "GL_ARB_framebuffer_object") ||
                   has_extension(extensions, "GL_EXT_framebuffer_object");
#ifdef PS5_COLOR_BLIT_TEST
   blit_extension = has_extension(extensions, "GL_EXT_framebuffer_blit");
#endif
#ifdef PS5_FRAMEBUFFER_SRGB_TEST
   framebuffer_srgb_extension =
      has_extension(extensions, "GL_EXT_framebuffer_sRGB");
#endif

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, producer_source, &producer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, consumer_source, &consumer_fs) ||
       link_program(vs, producer_fs, &producer) ||
       link_program(vs, consumer_fs, &consumer))
      goto cleanup;
   sampler = glGetUniformLocation(consumer, "u_texture");
   if (sampler < 0)
      goto cleanup;

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
#ifdef PS5_DYNAMIC_COLOR_TARGET_TEST
   for (unsigned i = 0; i < TARGET_WIDTH * TARGET_HEIGHT; ++i)
      upload[i] = UPLOAD_PIXEL;
#endif
   glTexImage2D(GL_TEXTURE_2D, 0, TEXTURE_INTERNAL_FORMAT,
                TARGET_WIDTH, TARGET_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE,
#ifdef PS5_DYNAMIC_COLOR_TARGET_TEST
                upload);
#else
                NULL);
#endif

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;

#ifdef PS5_DYNAMIC_COLOR_TARGET_TEST
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
   glUseProgram(consumer);
   glUniform1i(sampler, 0);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((DISPLAY_WIDTH - CROP_SIZE) / 2,
                (DISPLAY_HEIGHT - CROP_SIZE) / 2,
                CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   draw_error = glGetError();
   upload_hash = hash32(pixels, sizeof(pixels));
   for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
      upload_matching += pixels[i] == UPLOAD_PIXEL;
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
#endif
   glViewport(0, 0, TARGET_WIDTH, TARGET_HEIGHT);
#ifdef PS5_FRAMEBUFFER_SRGB_TEST
   glDisable(GL_FRAMEBUFFER_SRGB);
#endif
   glUseProgram(producer);
   glDrawArrays(GL_TRIANGLES, 0, 3);

#ifdef PS5_COLOR_BLIT_TEST
   /* This candidate intentionally exercises the driver's bounded CPU
    * detile/retile fallback. */
   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
   glBlitFramebuffer(0, 0, TARGET_WIDTH, TARGET_HEIGHT,
                     0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                     GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
#else
   /* No API wait, CPU copy, or detile is inserted between these draws. */
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
   glUseProgram(consumer);
   glUniform1i(sampler, 0);
   glDrawArrays(GL_TRIANGLES, 0, 3);
#endif
   glFinish();
   glReadPixels((DISPLAY_WIDTH - CROP_SIZE) / 2,
                (DISPLAY_HEIGHT - CROP_SIZE) / 2,
                CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   draw_error |= glGetError();
   hash = hash32(pixels, sizeof(pixels));
   for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
#ifdef PS5_FRAMEBUFFER_SRGB_TEST
      matching += pixels[i] == DISABLED_EXPECTED_PIXEL;
#else
      matching += pixels[i] == EXPECTED_PIXEL;
#endif
#ifdef PS5_FRAMEBUFFER_SRGB_TEST
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glViewport(0, 0, TARGET_WIDTH, TARGET_HEIGHT);
   glEnable(GL_FRAMEBUFFER_SRGB);
   glUseProgram(producer);
   glDrawArrays(GL_TRIANGLES, 0, 3);

   glDisable(GL_FRAMEBUFFER_SRGB);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
   glUseProgram(consumer);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((DISPLAY_WIDTH - CROP_SIZE) / 2,
                (DISPLAY_HEIGHT - CROP_SIZE) / 2,
                CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   draw_error |= glGetError();
   enabled_hash = hash32(pixels, sizeof(pixels));
   for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
      enabled_matching += pixels[i] == ENABLED_EXPECTED_PIXEL;
   passed = egl_major == 1 && egl_minor == 4 &&
            width == DISPLAY_WIDTH && height == DISPLAY_HEIGHT &&
            fbo_extension &&
            framebuffer_srgb_extension &&
            framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
            matching == CROP_SIZE * CROP_SIZE &&
            hash == DISABLED_EXPECTED_HASH &&
            enabled_matching == CROP_SIZE * CROP_SIZE &&
            enabled_hash == ENABLED_EXPECTED_HASH &&
            draw_error == GL_NO_ERROR;
#else
   passed = egl_major == 1 && egl_minor == 4 &&
            width == DISPLAY_WIDTH && height == DISPLAY_HEIGHT &&
            fbo_extension &&
            framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
            matching == CROP_SIZE * CROP_SIZE && hash == EXPECTED_HASH &&
            draw_error == GL_NO_ERROR;
#ifdef PS5_DYNAMIC_COLOR_TARGET_TEST
   passed &= upload_matching == CROP_SIZE * CROP_SIZE &&
             upload_hash == UPLOAD_HASH;
#endif
#ifdef PS5_COLOR_BLIT_TEST
   passed &= blit_extension;
#endif
#endif
   if (!eglSwapBuffers(display, surface))
      passed = 0;

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (texture)
         glDeleteTextures(1, &texture);
      if (vbo)
         glDeleteBuffers(1, &vbo);
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
   printf("[ps5-egl-rtt] egl=%d.%d size=%dx%d target=%dx%d ext=%d/%d/%d "
          "status=0x%x upload=%u/%08x matching=%u/%u "
          "hash=%08x/%08x error=0x%x "
          "cleanup=%u/0x%x/0x%x result=%d\n",
          egl_major, egl_minor, width, height, TARGET_WIDTH, TARGET_HEIGHT,
          fbo_extension, framebuffer_srgb_extension, blit_extension,
          framebuffer_status,
          upload_matching, upload_hash, matching, enabled_matching, hash,
          enabled_hash, draw_error, cleanup_ok, cleanup_gl_error,
          cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
