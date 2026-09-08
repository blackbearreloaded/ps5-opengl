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
#define BUFFER_OFFSET 64
#define EXPECTED_PIXEL UINT32_C(0xffff00ff)
#define EXPECTED_HASH UINT32_C(0x64e31dc5)
#define WRITE_ACCESS (GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | \
                      GL_MAP_FLUSH_EXPLICIT_BIT)

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
      printf("[ps5-egl-map-probe] shader type=0x%x compile=0 log=%.*s\n",
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
   static const float vertices[6] = {
      -0.5f, -0.5f,
       0.5f, -0.5f,
       0.0f,  0.5f,
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
   const GLsizeiptr buffer_size = BUFFER_OFFSET + sizeof(vertices);
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0;
   EGLint width = 0, height = 0, context_version = 0;
   GLuint vertex_shader = 0, fragment_shader = 0, program = 0, buffer = 0;
   GLint linked = GL_FALSE;
   GLint mapped_state = 0, map_offset = 0, map_length = 0, map_access = 0;
   GLint post_mapped = 0, read_offset = 0, read_length = 0, read_access = 0;
   const GLubyte *gl_version = NULL, *glsl_version = NULL;
   const char *extensions = NULL;
   void *mapped = NULL;
   void *invalid_map = NULL;
   GLboolean write_unmap = GL_FALSE, read_unmap = GL_FALSE;
   GLenum allocation_error = GL_NO_ERROR, invalid_error = GL_NO_ERROR;
   GLenum write_error = GL_NO_ERROR, read_error = GL_NO_ERROR;
   GLenum gl_error = GL_NO_ERROR, cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   unsigned matching = 0, unexpected = 0;
   uint32_t pixel_hash = 0;
   int extension_present = 0, bytes_match = 0;
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
   extension_present = has_extension(extensions, "GL_ARB_map_buffer_range");
   printf("[ps5-egl-map-probe] egl=%d.%d surface=%dx%d context=%d gl=%s glsl=%s extension=%d\n",
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
   glBufferData(GL_ARRAY_BUFFER, buffer_size, NULL, GL_STATIC_DRAW);
   allocation_error = glGetError();
   if (allocation_error != GL_NO_ERROR)
      goto cleanup;

   invalid_map = glMapBufferRange(GL_ARRAY_BUFFER, buffer_size - 4, 8,
                                  GL_MAP_WRITE_BIT);
   invalid_error = glGetError();
   printf("[ps5-egl-map-probe] invalid null=%u error=0x%x\n",
          invalid_map == NULL, invalid_error);
   if (invalid_map) {
      mapped = invalid_map;
      goto cleanup;
   }
   if (invalid_error != GL_INVALID_VALUE)
      goto cleanup;

   mapped = glMapBufferRange(GL_ARRAY_BUFFER, BUFFER_OFFSET, sizeof(vertices),
                             WRITE_ACCESS);
   if (!mapped)
      goto cleanup;
   glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAPPED, &mapped_state);
   glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_OFFSET, &map_offset);
   glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_LENGTH, &map_length);
   glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_ACCESS_FLAGS, &map_access);
   memcpy(mapped, vertices, sizeof(vertices));
   glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, sizeof(vertices));
   write_error = glGetError();
   write_unmap = glUnmapBuffer(GL_ARRAY_BUFFER);
   mapped = NULL;
   glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAPPED, &post_mapped);
   printf("[ps5-egl-map-probe] write mapped=%d offset=%d length=%d access=0x%x flush=0x%x unmap=%u post=%d\n",
          mapped_state, map_offset, map_length, map_access, write_error,
          write_unmap, post_mapped);
   if (!mapped_state || map_offset != BUFFER_OFFSET ||
       map_length != (GLint)sizeof(vertices) || map_access != WRITE_ACCESS ||
       write_error != GL_NO_ERROR || !write_unmap || post_mapped)
      goto cleanup;

   mapped = glMapBufferRange(GL_ARRAY_BUFFER, BUFFER_OFFSET, sizeof(vertices),
                             GL_MAP_READ_BIT);
   if (!mapped)
      goto cleanup;
   glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_OFFSET, &read_offset);
   glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_LENGTH, &read_length);
   glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_ACCESS_FLAGS, &read_access);
   bytes_match = memcmp(mapped, vertices, sizeof(vertices)) == 0;
   read_error = glGetError();
   read_unmap = glUnmapBuffer(GL_ARRAY_BUFFER);
   mapped = NULL;
   printf("[ps5-egl-map-probe] read offset=%d length=%d access=0x%x bytes=%d error=0x%x unmap=%u\n",
          read_offset, read_length, read_access, bytes_match, read_error,
          read_unmap);
   if (read_offset != BUFFER_OFFSET ||
       read_length != (GLint)sizeof(vertices) ||
       read_access != GL_MAP_READ_BIT || !bytes_match ||
       read_error != GL_NO_ERROR || !read_unmap)
      goto cleanup;

   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float),
                         (const void *)(uintptr_t)BUFFER_OFFSET);
   glEnableVertexAttribArray(0);
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
   printf("[ps5-egl-map-probe] public-gl matching=%u unexpected=%u hash=%08x error=0x%x\n",
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
   if (mapped)
      glUnmapBuffer(GL_ARRAY_BUFFER);
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
   printf("[ps5-egl-map-probe] cleanup gl=0x%x egl=0x%x ok=%u result=%d\n",
          cleanup_gl_error, cleanup_egl_error, cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
