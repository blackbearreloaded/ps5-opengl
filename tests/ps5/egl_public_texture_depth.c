// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define TEXTURE_SIZE 64
#define CROP_WIDTH 64
#define CROP_HEIGHT 64
#define RED_PIXEL UINT32_C(0xff0000ff)
#define GREEN_PIXEL UINT32_C(0xff00ff00)
#define RED_HASH UINT32_C(0xc40abdc5)
#define GREEN_HASH UINT32_C(0xc38d1dc5)
#ifdef PS5_MIXED_FBO_TEST
#define COLOR_WIDTH 128
#define COLOR_HEIGHT 96
#define DEPTH_WIDTH 64
#define DEPTH_HEIGHT 80
#define FBO_WIDTH 64
#define FBO_HEIGHT 80
#else
#define COLOR_WIDTH WIDTH
#define COLOR_HEIGHT HEIGHT
#define DEPTH_WIDTH WIDTH
#define DEPTH_HEIGHT HEIGHT
#define FBO_WIDTH WIDTH
#define FBO_HEIGHT HEIGHT
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
      printf("[ps5-egl-depth] shader type=0x%x compile=0 log=%.*s\n",
             type, length, log);
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

#ifdef PS5_MIXED_FBO_TEST
static int
has_extension(const char *extensions, const char *name)
{
   size_t length = strlen(name);
   const char *match = extensions;

   while (match && (match = strstr(match, name))) {
      if ((match == extensions || match[-1] == ' ') &&
          (match[length] == '\0' || match[length] == ' '))
         return 1;
      match += length;
   }
   return 0;
}
#endif

static void
summarize_pixels(const uint32_t *pixels, uint32_t expected,
                 unsigned *matching, unsigned *unexpected)
{
   *matching = 0;
   *unexpected = 0;
   for (unsigned i = 0; i < CROP_WIDTH * CROP_HEIGHT; ++i) {
      *matching += pixels[i] == expected;
      *unexpected += pixels[i] != expected;
   }
}

int
main(void)
{
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "attribute vec2 a_uv;\n"
      "varying vec2 v_uv;\n"
      "uniform float u_depth;\n"
      "void main() {\n"
      "  gl_Position = vec4(a_position, u_depth, 1.0);\n"
      "  v_uv = a_uv;\n"
      "}\n";
   static const char *fragment_source =
      "#version 120\n"
      "varying vec2 v_uv;\n"
      "uniform sampler2D u_texture;\n"
      "void main() { gl_FragColor = texture2D(u_texture, v_uv); }\n";
   static const float vertices[12] = {
#ifdef PS5_MIXED_FBO_TEST
      -1.0f, -1.0f, 0.0f, 0.0f,
       3.0f, -1.0f, 2.0f, 0.0f,
      -1.0f,  3.0f, 0.0f, 2.0f,
#else
      -0.5f, -0.5f, 0.5f, 0.5f,
       0.5f, -0.5f, 0.5f, 0.5f,
       0.0f,  0.5f, 0.5f, 0.5f,
#endif
   };
   static uint32_t texels[TEXTURE_SIZE * TEXTURE_SIZE];
   static uint32_t pixels[CROP_WIDTH * CROP_HEIGHT];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0;
   EGLint width = 0, height = 0, context_version = 0;
   GLuint vertex_shader = 0, fragment_shader = 0, program = 0, buffer = 0;
   GLuint textures[2] = {0};
   GLuint framebuffer = 0, color_renderbuffer = 0, depth_renderbuffer = 0;
   GLint linked = GL_FALSE;
   GLint sampler_location = -1, depth_location = -1;
   GLint depth_internal_format = 0, depth_bits = 0;
   GLenum framebuffer_status = 0, setup_error = GL_NO_ERROR;
   GLenum write_error = GL_NO_ERROR, mask_error = GL_NO_ERROR;
   GLenum detach_sync_error = GL_NO_ERROR;
   unsigned write_matching = 0, write_unexpected = 0;
   unsigned mask_matching = 0, mask_unexpected = 0;
   uint32_t write_hash = 0, mask_hash = 0;
   const GLubyte *gl_version = NULL, *glsl_version = NULL;
   const char *extensions = NULL;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0;
   int framebuffer_extension_present = 1;
   int passed = 0;

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

   if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
       !eglQuerySurface(display, surface, EGL_HEIGHT, &height) ||
       !eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION,
                        &context_version))
      goto cleanup;
   gl_version = glGetString(GL_VERSION);
   glsl_version = glGetString(GL_SHADING_LANGUAGE_VERSION);
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   printf("[ps5-egl-depth] egl=%d.%d surface=%dx%d context=%d gl=%s glsl=%s\n",
          egl_major, egl_minor, width, height, context_version,
          gl_version ? (const char *)gl_version : "null",
          glsl_version ? (const char *)glsl_version : "null");
   if (!gl_version || !glsl_version || !extensions)
      goto cleanup;
#ifdef PS5_MIXED_FBO_TEST
   framebuffer_extension_present =
      has_extension(extensions, "GL_ARB_framebuffer_object");
#endif

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex_shader) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fragment_shader))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vertex_shader);
   glAttachShader(program, fragment_shader);
   glBindAttribLocation(program, 0, "a_position");
   glBindAttribLocation(program, 1, "a_uv");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);
   sampler_location = glGetUniformLocation(program, "u_texture");
   depth_location = glGetUniformLocation(program, "u_depth");
   if (sampler_location < 0 || depth_location < 0)
      goto cleanup;
   glUniform1i(sampler_location, 0);

   glGenTextures(2, textures);
   for (unsigned texture = 0; texture < 2; ++texture) {
      uint32_t color = texture ? GREEN_PIXEL : RED_PIXEL;
      for (unsigned i = 0; i < TEXTURE_SIZE * TEXTURE_SIZE; ++i)
         texels[i] = color;
      glBindTexture(GL_TEXTURE_2D, textures[texture]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEXTURE_SIZE, TEXTURE_SIZE,
                   0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
   }

   glGenBuffers(1, &buffer);
   glBindBuffer(GL_ARRAY_BUFFER, buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                         (const GLvoid *)(uintptr_t)(2 * sizeof(float)));
   glEnableVertexAttribArray(1);

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glGenRenderbuffers(1, &color_renderbuffer);
   glBindRenderbuffer(GL_RENDERBUFFER, color_renderbuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8,
                         COLOR_WIDTH, COLOR_HEIGHT);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, color_renderbuffer);
   glGenRenderbuffers(1, &depth_renderbuffer);
   glBindRenderbuffer(GL_RENDERBUFFER, depth_renderbuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
                         DEPTH_WIDTH, DEPTH_HEIGHT);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                GL_RENDERBUFFER_INTERNAL_FORMAT,
                                &depth_internal_format);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE,
                                &depth_bits);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             GL_RENDERBUFFER, depth_renderbuffer);
   framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   setup_error = glGetError();
   printf("[ps5-egl-depth] fbo status=0x%x internal=0x%x bits=%d error=0x%x\n",
          framebuffer_status, depth_internal_format, depth_bits, setup_error);
   if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE ||
       depth_internal_format != GL_DEPTH_COMPONENT || depth_bits != 32 ||
       setup_error != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, FBO_WIDTH, FBO_HEIGHT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glDepthMask(GL_TRUE);
   glClearDepth(1.0);
   glClear(GL_DEPTH_BUFFER_BIT);
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glUniform1f(depth_location, 0.25f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glBindTexture(GL_TEXTURE_2D, textures[1]);
   glUniform1f(depth_location, 0.75f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   memset(pixels, 0, sizeof(pixels));
   glReadPixels((FBO_WIDTH - CROP_WIDTH) / 2,
                (FBO_HEIGHT - CROP_HEIGHT) / 2,
                CROP_WIDTH, CROP_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   write_error = glGetError();
   summarize_pixels(pixels, RED_PIXEL, &write_matching, &write_unexpected);
   write_hash = hash32(pixels, sizeof(pixels));
   printf("[ps5-egl-depth] write matching=%u unexpected=%u hash=%08x error=0x%x\n",
          write_matching, write_unexpected, write_hash, write_error);

   glDepthMask(GL_TRUE);
   glClearDepth(1.0);
   glClear(GL_DEPTH_BUFFER_BIT);
   glDepthMask(GL_FALSE);
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glUniform1f(depth_location, 0.25f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glBindTexture(GL_TEXTURE_2D, textures[1]);
   glUniform1f(depth_location, 0.75f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   memset(pixels, 0, sizeof(pixels));
   glReadPixels((FBO_WIDTH - CROP_WIDTH) / 2,
                (FBO_HEIGHT - CROP_HEIGHT) / 2,
                CROP_WIDTH, CROP_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   mask_error = glGetError();
   summarize_pixels(pixels, GREEN_PIXEL, &mask_matching, &mask_unexpected);
   mask_hash = hash32(pixels, sizeof(pixels));
   printf("[ps5-egl-depth] mask matching=%u unexpected=%u hash=%08x error=0x%x\n",
          mask_matching, mask_unexpected, mask_hash, mask_error);

   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && context_version == 2 &&
            framebuffer_extension_present &&
            strncmp((const char *)gl_version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl_version, "1.20", 4) == 0 &&
            write_matching == CROP_WIDTH * CROP_HEIGHT && !write_unexpected &&
            write_hash == RED_HASH && write_error == GL_NO_ERROR &&
            mask_matching == CROP_WIDTH * CROP_HEIGHT && !mask_unexpected &&
            mask_hash == GREEN_HASH && mask_error == GL_NO_ERROR;

cleanup:
   if (made_current) {
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);
      if (framebuffer) {
         glBindFramebuffer(GL_FRAMEBUFFER, 0);
         glClear(0);
         detach_sync_error = glGetError();
      }
      if (depth_renderbuffer)
         glDeleteRenderbuffers(1, &depth_renderbuffer);
      if (color_renderbuffer)
         glDeleteRenderbuffers(1, &color_renderbuffer);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (textures[0] || textures[1])
         glDeleteTextures(2, textures);
      if (buffer)
         glDeleteBuffers(1, &buffer);
      if (program)
         glDeleteProgram(program);
      if (fragment_shader)
         glDeleteShader(fragment_shader);
      if (vertex_shader)
         glDeleteShader(vertex_shader);
      cleanup_gl_error = glGetError();
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
      made_current = 0;
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_egl_error = eglGetError();
   passed = passed && cleanup_ok && detach_sync_error == GL_NO_ERROR &&
            cleanup_gl_error == GL_NO_ERROR &&
            cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-depth] cleanup sync=0x%x gl=0x%x egl=0x%x ok=%u result=%d\n",
          detach_sync_error, cleanup_gl_error, cleanup_egl_error, cleanup_ok,
          passed ? 0 : 1);
   return passed ? 0 : 1;
}
