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
#define CROP_WIDTH 64
#define CROP_HEIGHT 64
#define EXPECTED_PIXEL UINT32_C(0xffff00ff)
#define EXPECTED_HASH UINT32_C(0x64e31dc5)

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
      printf("[ps5-egl-half-probe] shader type=0x%x compile=0 log=%.*s\n",
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

int
main(void)
{
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 120\n"
      "void main() { gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); }\n";
   static const uint16_t vertices[6] = {
      UINT16_C(0xb800), UINT16_C(0xb800),
      UINT16_C(0x3800), UINT16_C(0xb800),
      UINT16_C(0x0000), UINT16_C(0x3800),
   };
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
   GLint linked = GL_FALSE;
   GLint attrib_type = 0, attrib_size = 0, attrib_stride = 0;
   const GLubyte *gl_version = NULL, *glsl_version = NULL;
   const char *extensions = NULL;
   GLenum invalid_error = GL_NO_ERROR, setup_error = GL_NO_ERROR;
   GLenum gl_error = GL_NO_ERROR, cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   unsigned matching = 0, unexpected = 0;
   uint32_t pixel_hash = 0;
   int extension_present = 0, made_current = 0, passed = 0;

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
   if (!gl_version || !glsl_version || !extensions)
      goto cleanup;
   extension_present = has_extension(extensions, "GL_ARB_half_float_vertex");
   printf("[ps5-egl-half-probe] egl=%d.%d surface=%dx%d context=%d gl=%s glsl=%s extension=%d\n",
          egl_major, egl_minor, width, height, context_version, gl_version,
          glsl_version, extension_present);

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex_shader) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fragment_shader))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vertex_shader);
   glAttachShader(program, fragment_shader);
   glBindAttribLocation(program, 0, "a_position");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);

   glGenBuffers(1, &buffer);
   glBindBuffer(GL_ARRAY_BUFFER, buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

   glVertexAttribPointer(0, 0, GL_HALF_FLOAT, GL_FALSE,
                         2 * sizeof(uint16_t), NULL);
   invalid_error = glGetError();
   if (invalid_error != GL_INVALID_VALUE)
      goto cleanup;

   glVertexAttribPointer(0, 2, GL_HALF_FLOAT, GL_FALSE,
                         2 * sizeof(uint16_t), NULL);
   glEnableVertexAttribArray(0);
   glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attrib_type);
   glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attrib_size);
   glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attrib_stride);
   setup_error = glGetError();
   printf("[ps5-egl-half-probe] invalid=0x%x type=0x%x size=%d stride=%d setup=0x%x\n",
          invalid_error, attrib_type, attrib_size, attrib_stride, setup_error);
   if (attrib_type != GL_HALF_FLOAT || attrib_size != 2 ||
       attrib_stride != 2 * (GLint)sizeof(uint16_t) ||
       setup_error != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((WIDTH - CROP_WIDTH) / 2, (HEIGHT - CROP_HEIGHT) / 2,
                CROP_WIDTH, CROP_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   gl_error = glGetError();
   for (unsigned i = 0; i < CROP_WIDTH * CROP_HEIGHT; ++i) {
      matching += pixels[i] == EXPECTED_PIXEL;
      unexpected += pixels[i] != EXPECTED_PIXEL;
   }
   pixel_hash = hash32(pixels, sizeof(pixels));
   printf("[ps5-egl-half-probe] public-gl matching=%u unexpected=%u hash=%08x error=0x%x\n",
          matching, unexpected, pixel_hash, gl_error);
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && context_version == 2 && extension_present &&
            strncmp((const char *)gl_version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl_version, "1.20", 4) == 0 &&
            matching == CROP_WIDTH * CROP_HEIGHT && !unexpected &&
            pixel_hash == EXPECTED_HASH && gl_error == GL_NO_ERROR;

cleanup:
   if (buffer)
      glDeleteBuffers(1, &buffer);
   if (program)
      glDeleteProgram(program);
   if (fragment_shader)
      glDeleteShader(fragment_shader);
   if (vertex_shader)
      glDeleteShader(vertex_shader);
   if (made_current)
      cleanup_gl_error = glGetError();
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
   passed = passed && cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
            cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-half-probe] cleanup gl=0x%x egl=0x%x ok=%u result=%d\n",
          cleanup_gl_error, cleanup_egl_error, cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
