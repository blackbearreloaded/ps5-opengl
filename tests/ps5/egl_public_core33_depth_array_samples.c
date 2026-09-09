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

#define SIZE 32
#define LAYERS 4
#define PIXELS (SIZE * SIZE)
#define ALL_BITS (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)

#ifdef PS5_DEPTH_ARRAY_SAMPLES_HOST_REFERENCE
#define TAG "[host-egl-core33-depth-array-samples]"
#define SURFACE_TYPE EGL_PBUFFER_BIT
/* This is an issued-call count, never native execution/status evidence. */
static unsigned host_draw_calls;
static int ps5_egl_current_draw_status(unsigned *draw_calls)
{
   *draw_calls = host_draw_calls;
   return 0;
}
#else
#define TAG "[ps5-egl-core33-depth-array-samples]"
#define SURFACE_TYPE EGL_WINDOW_BIT
int ps5_egl_current_draw_status(unsigned *draw_calls);
#endif

static int
check_errors(const char *phase)
{
   unsigned calls = 0;
   int status = ps5_egl_current_draw_status(&calls);
   GLenum error = glGetError();

   if (status || error != GL_NO_ERROR)
      printf(TAG " phase=%s draw=%d/%u error=0x%x\n",
             phase, status, calls, error);
   return status == 0 && error == GL_NO_ERROR;
}

static int
check_framebuffer(unsigned samples)
{
   GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   GLint actual = -1;

   glGetIntegerv(GL_SAMPLES, &actual);
   if (status != GL_FRAMEBUFFER_COMPLETE || actual != (GLint)samples) {
      printf(TAG " fbo=0x%x samples=%d expected=%u\n", status, actual, samples);
      return 0;
   }
   return check_errors("framebuffer");
}

static int
attach_layer(GLuint framebuffer, const GLuint textures[2],
             unsigned layer, unsigned samples)
{
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             textures[0], 0, layer);
   glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                             textures[1], 0, layer);
   /* Distinct per-layer pixels below decide routing, not attachment metadata. */
   return check_framebuffer(samples == 4 ? 4 : 0);
}

static int
verify_layers(GLuint source, GLuint resolve, const GLuint textures[2],
              unsigned samples, unsigned phase)
{
   uint8_t colors[PIXELS * 4], stencils[PIXELS];
   float depths[PIXELS];
   unsigned matching = 0;

   glDisable(GL_SCISSOR_TEST);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glDepthMask(GL_TRUE);
   glStencilMask(0xff);
   for (unsigned layer = 0; layer < LAYERS; ++layer) {
      if (!attach_layer(source, textures, layer, samples))
         return 0;
      glBindFramebuffer(GL_FRAMEBUFFER, resolve);
      if (!check_framebuffer(0))
         return 0;
      /* Initialize the resolve destination and CPU buffers on every read. */
      glClearColor(1, 0, 1, 0);
      glClearDepth(0);
      glClearStencil(0xee);
      glClear(ALL_BITS);
      if (!check_errors("resolve-initialize"))
         return 0;
      glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
      /* Equal dimensions/formats, GL_NEAREST, no scissor: legal for depth
       * and stencil too. Every sample at a pixel has the same expected
       * depth/stencil, independent of the resolve sample selection.
       * ponytail: uniform samples only; add sample-mask draws for sample isolation. */
      glBlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE, ALL_BITS, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, resolve);
      glFinish();
      memset(colors, 0x7f, sizeof(colors));
      memset(stencils, 0xee, sizeof(stencils));
      for (unsigned i = 0; i < PIXELS; ++i)
         depths[i] = -1.0f;
      /* Only this single-sample FBO is ever passed to glReadPixels. */
      glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, colors);
      glReadPixels(0, 0, SIZE, SIZE, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
      glReadPixels(0, 0, SIZE, SIZE, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencils);
      if (!check_errors("resolve-read"))
         return 0;
      unsigned layer_matches = 0;
      for (unsigned y = 0; y < SIZE; ++y) {
         for (unsigned x = 0; x < SIZE; ++x) {
            unsigned i = y * SIZE + x;
            uint8_t rgba[4] = {(layer & 1) ? 255 : 0,
                              (layer & 2) ? 255 : 0, 0, 255};
            float depth = 1.0f - (float)layer * 0.125f;
            uint8_t stencil = (uint8_t)(((layer + 1) << 4) | 1);
            int inside = layer >= 2 && x >= 8 && x < 24 && y >= 8 && y < 24;

            if (inside) {
               rgba[1] = rgba[2] = 255;
               stencil = (uint8_t)(((layer + 1) << 4) | 0x0a);
               if (phase >= 1 && x < 16)
                  depth = 0.25f;
               if (phase == 2 && x >= 16) {
                  rgba[0] = rgba[3] = 255;
                  rgba[1] = rgba[2] = 0;
                  depth = 0.5f;
                  stencil = 0xba;
               }
            }
            int ok = memcmp(colors + i * 4, rgba, 4) == 0 &&
                     depths[i] == depth && stencils[i] == stencil;
            if (!ok && layer_matches == i)
               printf(TAG " mismatch samples=%u phase=%u layer=%u xy=%u,%u "
                      "rgba=%u/%u/%u/%u expected=%u/%u/%u/%u "
                      "depth=%g expected=%g stencil=%02x expected=%02x\n",
                      samples, phase, layer, x, y,
                      colors[i * 4], colors[i * 4 + 1],
                      colors[i * 4 + 2], colors[i * 4 + 3],
                      rgba[0], rgba[1], rgba[2], rgba[3],
                      (double)depths[i], (double)depth, stencils[i], stencil);
            layer_matches += ok;
         }
      }
      matching += layer_matches;
      printf(TAG " samples=%u phase=%u layer=%u pixels=%u/%u\n",
             samples, phase, layer, layer_matches, PIXELS);
   }
   return matching == LAYERS * PIXELS;
}

static int
test_samples(unsigned samples)
{
   static const GLenum formats[2] = {GL_RGBA8, GL_DEPTH32F_STENCIL8};
   GLenum target = samples == 4 ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_ARRAY;
   GLuint textures[2] = {0}, buffers[2] = {0}, framebuffers[2] = {0};
   unsigned explicit_draws = 0;
   int passed = 0, cleanup_ok = 0;

   glGenTextures(2, textures);
   glGenRenderbuffers(2, buffers);
   glGenFramebuffers(2, framebuffers);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[1]);
   glDrawBuffer(GL_COLOR_ATTACHMENT0);
   glReadBuffer(GL_COLOR_ATTACHMENT0);
   for (unsigned i = 0; i < 2; ++i) {
      glBindTexture(target, textures[i]);
      if (samples == 4) {
         GLint actual = 0, fixed = GL_FALSE;

         glTexImage3DMultisample(target, 4, formats[i], SIZE, SIZE, LAYERS, GL_TRUE);
         glGetTexLevelParameteriv(target, 0, GL_TEXTURE_SAMPLES, &actual);
         glGetTexLevelParameteriv(target, 0, GL_TEXTURE_FIXED_SAMPLE_LOCATIONS, &fixed);
         if (actual != 4 || fixed != GL_TRUE) {
            printf(TAG " texture=%u samples=%d fixed=%d expected=4/1\n", i, actual, fixed);
            goto cleanup;
         }
      } else {
         glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
         glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
         glTexImage3D(target, 0, formats[i], SIZE, SIZE, LAYERS, 0,
                      i ? GL_DEPTH_STENCIL : GL_RGBA,
                      i ? GL_FLOAT_32_UNSIGNED_INT_24_8_REV : GL_UNSIGNED_BYTE, NULL);
      }
      glBindRenderbuffer(GL_RENDERBUFFER, buffers[i]);
      glRenderbufferStorage(GL_RENDERBUFFER, formats[i], SIZE, SIZE);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER,
         i ? GL_DEPTH_STENCIL_ATTACHMENT : GL_COLOR_ATTACHMENT0,
         GL_RENDERBUFFER, buffers[i]);
   }
   if (!check_framebuffer(0))
      goto cleanup;
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
   glDrawBuffer(GL_COLOR_ATTACHMENT0);
   glReadBuffer(GL_COLOR_ATTACHMENT0);
   glDisable(GL_SCISSOR_TEST);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glDepthMask(GL_TRUE);
   glStencilMask(0xff);
   /* NULL allocations are undefined: clear every layer and every sample. */
   for (unsigned layer = 0; layer < LAYERS; ++layer) {
      if (!attach_layer(framebuffers[0], textures, layer, samples))
         goto cleanup;
      glClearColor((layer & 1) != 0, (layer & 2) != 0, 0, 1);
      glClearDepth(1.0 - (double)layer * 0.125);
      glClearStencil(((layer + 1) << 4) | 1);
      glClear(ALL_BITS);
      if (!check_errors("initialize-layer"))
         goto cleanup;
   }
   for (unsigned phase = 0; phase < 3; ++phase) {
      for (unsigned layer = 2; layer < LAYERS; ++layer) {
         if (!attach_layer(framebuffers[0], textures, layer, samples))
            goto cleanup;
         glEnable(GL_SCISSOR_TEST);
         glScissor(8, 8, phase == 1 ? 8 : 16, 16);
         if (phase == 0) {
            glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE);
            glDepthMask(GL_FALSE);
            glStencilMask(0x0f);
            glClearColor(1, 1, 1, 0);
            glClearDepth(0.125);
            glClearStencil(0x0a);
            glClear(ALL_BITS);
         } else if (phase == 1) {
            glDepthMask(GL_TRUE);
            glClearDepth(0.25);
            glClear(GL_DEPTH_BUFFER_BIT);
         } else {
            unsigned before = 0, after = 0;
            int before_status = 0, after_status = 0;

            glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_EQUAL, 0xba, 0x0f);
            glStencilMask(0xf0);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            glFinish();
            before_status = ps5_egl_current_draw_status(&before);
            glDrawArrays(GL_TRIANGLES, 0, 3);
#ifdef PS5_DEPTH_ARRAY_SAMPLES_HOST_REFERENCE
            ++host_draw_calls;
#endif
            glFinish();
            after_status = ps5_egl_current_draw_status(&after);
            printf(TAG " samples=%u layer=%u draw=%d/%u->%d/%u\n",
                   samples, layer, before_status, before, after_status, after);
            /* Internal clears/resolves may draw: check this explicit draw's delta. */
            if (before_status || after_status || after != before + 1)
               goto cleanup;
            ++explicit_draws;
         }
         glFinish();
         if (!check_errors("clear-or-draw"))
            goto cleanup;
      }
      if (!verify_layers(framebuffers[0], framebuffers[1], textures, samples, phase))
         goto cleanup;
   }
   passed = explicit_draws == 2 && check_errors("variant");

cleanup:
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glDeleteFramebuffers(2, framebuffers);
   glDeleteRenderbuffers(2, buffers);
   glDeleteTextures(2, textures);
   cleanup_ok = check_errors("variant-cleanup");
   passed &= cleanup_ok;
   printf(TAG " samples=%u explicit_draws=%u cleanup=%d result=%d\n",
          samples, explicit_draws, cleanup_ok, passed ? 0 : 1);
   return passed;
}

static GLuint
compile_shader(GLenum type, const char *source)
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

      glGetShaderInfoLog(shader, sizeof(log), NULL, log);
      printf(TAG " shader=0x%x log=%s\n", type, log);
      glDeleteShader(shader);
      return 0;
   }
   return shader;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "const vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));\n"
      "void main(){gl_Position=vec4(p[gl_VertexID],0,1);}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){color=vec4(1,0,0,1);}\n";
   static const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, SURFACE_TYPE, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE,
   };
   static const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL;
   EGLint count = 0;
   GLuint shaders[2] = {0}, program = 0, vao = 0;
   GLint linked = GL_FALSE;
   unsigned calls = 0;
   int current = 0, passed = 0, status = -1;
   EGLBoolean cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) || count != 1)
      goto cleanup;
#ifdef PS5_DEPTH_ARRAY_SAMPLES_HOST_REFERENCE
   const EGLint surface_attributes[] = {EGL_WIDTH, SIZE, EGL_HEIGHT, SIZE, EGL_NONE};
   surface = eglCreatePbufferSurface(display, config, surface_attributes);
#else
   surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, NULL);
#endif
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context))
      goto cleanup;
   current = 1;
#ifdef PS5_DEPTH_ARRAY_SAMPLES_HOST_REFERENCE
   printf(TAG " draw_counter=host-issued renderer=%s version=%s\n",
          glGetString(GL_RENDERER), glGetString(GL_VERSION));
#else
   printf(TAG " draw_counter=native-driver\n");
#endif
   shaders[0] = compile_shader(GL_VERTEX_SHADER, vertex_source);
   shaders[1] = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
   if (!shaders[0] || !shaders[1])
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      char log[512] = {0};

      glGetProgramInfoLog(program, sizeof(log), NULL, log);
      printf(TAG " link=%s\n", log);
      goto cleanup;
   }
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glUseProgram(program);
   glViewport(0, 0, SIZE, SIZE);
   glDisable(GL_DITHER);
   glEnable(GL_MULTISAMPLE);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   passed = test_samples(1);
   passed = passed && test_samples(4);
   status = ps5_egl_current_draw_status(&calls);
   passed &= status == 0 && check_errors("final");
   printf(TAG " final_draw=%d/%u result=%d\n", status, calls, passed ? 0 : 1);

cleanup:
   if (current) {
      glUseProgram(0);
      glBindVertexArray(0);
      glDeleteVertexArrays(1, &vao);
      if (program)
         glDeleteProgram(program);
      for (unsigned i = 0; i < 2; ++i)
         if (shaders[i])
            glDeleteShader(shaders[i]);
      cleanup_ok &= check_errors("gl-cleanup");
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf(TAG " cleanup=%u result=%d\n", cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
