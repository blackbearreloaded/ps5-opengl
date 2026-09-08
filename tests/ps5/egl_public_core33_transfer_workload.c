// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define TAG "[ps5-egl-transfer-workload]"
#define ITERATIONS 8
#ifdef PS5_TRANSFER_HOST_REFERENCE
#define SURFACE_TYPE EGL_PBUFFER_BIT
#else
#define SURFACE_TYPE EGL_WINDOW_BIT
#endif

/* Fixed bounds: 2 MiB CPU buffers, at most 1.5 MiB logical RGBA8 storage.
 * Driver tiling/staging and the EGL window add implementation-owned storage.
 * ponytail: 24 object cycles only; use the existing soak gate for long sessions.
 */
static const uint8_t zeroes[512 * 512 * 4];
static uint8_t pixels[sizeof(zeroes)];
static const struct {
   const char *name;
   GLenum target;
   int size, depth, mip_depth;
   const char *fragment;
} cases[] = {
   {"2d-mip", GL_TEXTURE_2D, 512, 1, 1,
    "uniform sampler2D u_texture;\n"
    "void main() { color = textureLod(u_texture, gl_FragCoord.xy / u_size, 1.0); }\n"},
   {"array-mip", GL_TEXTURE_2D_ARRAY, 256, 3, 3,
    "uniform sampler2DArray u_texture;\n"
    "void main() { color = textureLod(u_texture, vec3(gl_FragCoord.xy / u_size, 2.0), 1.0); }\n"},
   {"volume-mip", GL_TEXTURE_3D, 256, 4, 2,
    "uniform sampler3D u_texture;\n"
    "void main() { color = textureLod(u_texture, vec3(gl_FragCoord.xy / u_size, 0.75), 1.0); }\n"},
};

static double
wall_seconds(void)
{
   struct timespec now;
   return clock_gettime(CLOCK_MONOTONIC, &now) == 0
      ? now.tv_sec + now.tv_nsec * 1e-9 : -1.0;
}

/* Same fullscreen triangle / compile-status idiom as layered_mip_fbo. */
static GLuint
make_program(const char *fragment)
{
   const char *sources[] = {
      "#version 330 core\nlayout(location=0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n",
      fragment,
   };
   GLuint program = glCreateProgram();
   if (!program)
      return 0;
   for (unsigned i = 0; i < 2; ++i) {
      GLint ok = GL_FALSE;
      GLuint shader = glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
      if (!shader) {
         glDeleteProgram(program);
         return 0;
      }
      glShaderSource(shader, 1, &sources[i], NULL);
      glCompileShader(shader);
      glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
      if (!ok) {
         char log[512] = {0};
         glGetShaderInfoLog(shader, sizeof(log), NULL, log);
         printf(TAG " shader=%u log=%s\n", i, log);
      } else {
         glAttachShader(program, shader);
      }
      glDeleteShader(shader);
      if (!ok) {
         glDeleteProgram(program);
         return 0;
      }
   }
   GLint ok = GL_FALSE;
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   if (!ok) {
      char log[512] = {0};
      glGetProgramInfoLog(program, sizeof(log), NULL, log);
      printf(TAG " link=%s\n", log);
      glDeleteProgram(program);
      return 0;
   }
   return program;
}

static int
check_image(const char *phase, int size, int depth, int selected, unsigned cycle)
{
   for (int z = 0; z < depth; ++z) {
      for (int y = 0; y < size; ++y) {
         for (int x = 0; x < size; ++x) {
            uint8_t expected[4] = {0, 0, 0, 0};
            if (z == selected && x >= 3 && x < size - 3 &&
                y >= 5 && y < size - 5) {
               expected[cycle % 2] = 255; /* Alternate red/green across reuse. */
               expected[3] = 255;
               if (x >= size - 8 && y >= size - 12) {
                  expected[0] = expected[1] = 0;
                  expected[2] = 255; /* Scissored FBO draw after the copy. */
               }
               if (x == 3 && y == 5)
                  memset(expected, 255, sizeof(expected)); /* CPU upload. */
            }
            const uint8_t *pixel = pixels + ((z * size + y) * size + x) * 4;
            if (memcmp(pixel, expected, 4)) {
               printf(TAG " mismatch=%s xyz=%d/%d/%d expected=%u/%u/%u/%u "
                      "observed=%u/%u/%u/%u\n", phase, x, y, z,
                      expected[0], expected[1], expected[2], expected[3],
                      pixel[0], pixel[1], pixel[2], pixel[3]);
               return 0;
            }
         }
      }
   }
   return 1;
}

static int
run_cycle(unsigned kind, unsigned cycle, GLuint render, GLint color,
          GLuint sample)
{
   const GLenum target = cases[kind].target;
   const int size = cases[kind].size / 2;
   const int layer = cases[kind].mip_depth - 1;
   const uint8_t white[4] = {255, 255, 255, 255};
   GLuint texture = 0, framebuffer = 0, renderbuffer = 0;
   int passed = 0;

   glGenTextures(1, &texture);
   glBindTexture(target, texture);
   glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 1);
   for (int level = 0; level < 2; ++level) {
      const int extent = cases[kind].size >> level;
      if (target == GL_TEXTURE_2D)
         glTexImage2D(target, level, GL_RGBA8, extent, extent, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, zeroes);
      else
         glTexImage3D(target, level, GL_RGBA8, extent, extent,
                      level ? cases[kind].mip_depth : cases[kind].depth,
                      0, GL_RGBA, GL_UNSIGNED_BYTE, zeroes);
   }
   glGenFramebuffers(1, &framebuffer);
   glGenRenderbuffers(1, &renderbuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, size, size);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, renderbuffer);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      goto cleanup;
   glViewport(0, 0, size, size);
   glUseProgram(render);
   glUniform4f(color, cycle % 2 == 0, cycle % 2 != 0, 0, 1);
   glDrawArrays(GL_TRIANGLES, 0, 3);

   /* Odd offsets and intact borders expose row/layer staging mistakes. */
   if (target == GL_TEXTURE_2D) {
      glCopyTexSubImage2D(target, 1, 3, 5, 0, 0, size - 6, size - 10);
      glTexSubImage2D(target, 1, 3, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, texture, 1);
   } else {
      glCopyTexSubImage3D(target, 1, 3, 5, layer, 0, 0, size - 6, size - 10);
      glTexSubImage3D(target, 1, 3, 5, layer, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 1, layer);
   }
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;
   glEnable(GL_SCISSOR_TEST);
   glScissor(size - 8, size - 12, 5, 7);
   glUniform4f(color, 0, 0, 1, 1);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glDisable(GL_SCISSOR_TEST);

   /* Sample immediately after the FBO write, before any readback can flush it. */
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, renderbuffer);
   glUseProgram(sample);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   memset(pixels, 0xa5, (size_t)size * size * 4);
   glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   if (!check_image("sample", size, 1, 0, cycle))
      goto cleanup;
   memset(pixels, 0xa5, sizeof(pixels));
   glGetTexImage(target, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   if (!check_image("mip", size, cases[kind].mip_depth, layer, cycle))
      goto cleanup;
   memset(pixels, 0xa5, sizeof(pixels));
   glGetTexImage(target, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   passed = check_image("base", cases[kind].size, cases[kind].depth, -1, cycle);

cleanup:
   glDisable(GL_SCISSOR_TEST);
   glUseProgram(0);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindRenderbuffer(GL_RENDERBUFFER, 0);
   glBindTexture(target, 0);
   glDeleteFramebuffers(1, &framebuffer);
   glDeleteRenderbuffers(1, &renderbuffer);
   glDeleteTextures(1, &texture);
   glFinish(); /* Include completed work and normal deletion in CPU wall time. */
   GLenum error = glGetError();
   passed &= error == GL_NO_ERROR;
   if (!passed)
      printf(TAG " case=%s cycle=%u error=0x%x result=1\n",
             cases[kind].name, cycle + 1, error);
   return passed;
}

int
main(void)
{
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, SURFACE_TYPE, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE,
   };
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
   const float vertices[] = {-1, -1, 3, -1, -1, 3};
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint count = 0;
   GLuint render = 0, sample = 0, vao = 0, vbo = 0;
   int made_current = 0, passed = 0;
   unsigned completed = 0, cycles = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   printf(TAG " begin cases=3 cycles_per_case=8 timing=cpu-wall exact=rgba8\n");
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) || count != 1)
      goto cleanup;
#ifdef PS5_TRANSFER_HOST_REFERENCE
   const EGLint surface_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
   surface = eglCreatePbufferSurface(display, config, surface_attributes);
#else
   surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, NULL);
#endif
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context))
      goto cleanup;
   made_current = 1;
   if (!eglSwapInterval(display, 0))
      goto cleanup;
   printf(TAG " renderer=%s version=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
   glDisable(GL_DITHER);
   glDisable(GL_BLEND);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_FRAMEBUFFER_SRGB);
   glActiveTexture(GL_TEXTURE0);
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   render = make_program("#version 330 core\nuniform vec4 u_color;\n"
                         "layout(location=0) out vec4 color;\n"
                         "void main() { color = u_color; }\n");
   if (!render)
      goto cleanup;
   GLint color = glGetUniformLocation(render, "u_color");
   if (color < 0)
      goto cleanup;
   for (unsigned kind = 0; kind < 3; ++kind) {
      char fragment[512];
      snprintf(fragment, sizeof(fragment), "#version 330 core\nuniform float u_size;\n"
               "layout(location=0) out vec4 color;\n%s", cases[kind].fragment);
      sample = make_program(fragment);
      if (!sample)
         goto cleanup;
      GLint sampler = glGetUniformLocation(sample, "u_texture");
      GLint extent = glGetUniformLocation(sample, "u_size");
      if (sampler < 0 || extent < 0)
         goto cleanup;
      glUseProgram(sample);
      glUniform1i(sampler, 0);
      glUniform1f(extent, cases[kind].size / 2);
      glFinish();
      double start = wall_seconds();
      if (start < 0 || glGetError() != GL_NO_ERROR)
         goto cleanup;
      for (unsigned cycle = 0; cycle < ITERATIONS; ++cycle) {
         if (!run_cycle(kind, cycle, render, color, sample))
            goto cleanup;
         ++cycles;
      }
      double end = wall_seconds();
      if (end < start)
         goto cleanup;
      glDeleteProgram(sample);
      sample = 0;
      if (glGetError() != GL_NO_ERROR)
         goto cleanup;
      ++completed;
      printf(TAG " case=%s cycles=8 base=%d/%d mip=%d/%d "
             "sample=8 mip_guard=8 base_guard=8 deletes=8 cpu_wall_ms=%.3f result=0\n",
             cases[kind].name, cases[kind].size, cases[kind].depth,
             cases[kind].size / 2, cases[kind].mip_depth, (end - start) * 1000.0);
   }
   passed = completed == 3;

cleanup:
   if (made_current) {
      glUseProgram(0);
      if (sample) glDeleteProgram(sample);
      if (render) glDeleteProgram(render);
      glBindVertexArray(0);
      glBindBuffer(GL_ARRAY_BUFFER, 0);
      glDeleteBuffers(1, &vbo);
      glDeleteVertexArrays(1, &vao);
      glFinish();
      cleanup_ok &= glGetError() == GL_NO_ERROR;
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (context != EGL_NO_CONTEXT) cleanup_ok &= eglDestroyContext(display, context);
   if (surface != EGL_NO_SURFACE) cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY) cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf(TAG " cleanup=%u result=%d\n", cleanup_ok, passed ? 0 : 1);
   printf(TAG " finished cases=%u cycles=%u result=%d\n",
          completed, cycles, passed ? 0 : 1);
   return passed ? 0 : 1;
}
