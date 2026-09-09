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

#ifdef PS5_MSAA_DEPTH_ARRAY_HOST_REFERENCE
#define TAG "[host-egl-msaa4-depth-array-texture]"
#define SURFACE_TYPE EGL_PBUFFER_BIT
/* Issued calls only; this counter is not native execution evidence. */
static unsigned host_draw_calls;
static int ps5_egl_current_draw_status(unsigned *calls)
{
   *calls = host_draw_calls;
   return 0;
}
#else
#define TAG "[ps5-egl-msaa4-depth-array-texture]"
#define SURFACE_TYPE EGL_WINDOW_BIT
int ps5_egl_current_draw_status(unsigned *calls);
#endif

static int
check_errors(const char *phase)
{
   unsigned calls = 0;
   int status = ps5_egl_current_draw_status(&calls);
   GLenum error = glGetError();

   if (status || error != GL_NO_ERROR)
      printf(TAG " phase=%s draw=%d/%u error=0x%x\n", phase, status, calls, error);
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
test_format(GLenum format)
{
   const int packed = format == GL_DEPTH32F_STENCIL8;
   const GLenum attachment = packed ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
   const GLbitfield mask = GL_DEPTH_BUFFER_BIT | (packed ? GL_STENCIL_BUFFER_BIT : 0);
   GLuint texture = 0, buffers[2] = {0}, fbos[3] = {0};
   uint8_t pixels[PIXELS * 4] = {0}, stencils[PIXELS];
   float depths[PIXELS];
   unsigned resolved[2] = {0}, stencil_matches[2] = {0}, sampled = 0;
   unsigned before = 0, after = 0, explicit_draws = 0;
   GLint samples = -1, fixed = GL_FALSE;
   int passed = 0, cleanup_ok;

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, texture);
   glTexImage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 4, format,
                         SIZE, SIZE, LAYERS, GL_TRUE);
   glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0, GL_TEXTURE_SAMPLES, &samples);
   glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0,
                           GL_TEXTURE_FIXED_SAMPLE_LOCATIONS, &fixed);
   if (!check_errors("allocate") || samples != 4 || fixed != GL_TRUE)
      goto cleanup;
   glGenFramebuffers(3, fbos);
   glGenRenderbuffers(2, buffers);
   glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   glDepthMask(GL_TRUE);
   glStencilMask(0xff);
   /* Initialize every layer/sample. Distinct layers catch accidental layer-zero
    * routing; different halves also exercise the fetched pixel coordinates.
    * ponytail: uniform samples; sample-mask draws are needed for sample isolation. */
   for (unsigned layer = 0; layer < LAYERS; ++layer) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, texture, 0, layer);
      if (!check_framebuffer(4))
         goto cleanup;
      glDisable(GL_SCISSOR_TEST);
      glClearDepth((layer + 1) * 0.125);
      glClearStencil((layer + 1) * 0x11);
      glClear(mask);
      glEnable(GL_SCISSOR_TEST);
      glScissor(SIZE / 2, 0, SIZE / 2, SIZE);
      glClearDepth((layer + 1) * 0.125 + 0.0625);
      glClear(GL_DEPTH_BUFFER_BIT);
      if (!check_errors("initialize-layer"))
         goto cleanup;
   }
   glDisable(GL_SCISSOR_TEST);
   glBindFramebuffer(GL_FRAMEBUFFER, fbos[1]);
   glBindRenderbuffer(GL_RENDERBUFFER, buffers[0]);
   glRenderbufferStorage(GL_RENDERBUFFER, format, SIZE, SIZE);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, buffers[0]);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   if (!check_framebuffer(0))
      goto cleanup;
   for (unsigned layer = 2; layer < LAYERS; ++layer) {
      glBindFramebuffer(GL_FRAMEBUFFER, fbos[1]);
      glClearDepth(0);
      glClearStencil(0xee);
      glClear(mask);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
      glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, attachment, texture, 0, layer);
      /* Equal extents/formats, nearest, and uniform samples: legal depth/stencil
       * resolve. Only the single-sample destination is passed to glReadPixels. */
      glBlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE, mask, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, fbos[1]);
      glFinish();
      for (unsigned i = 0; i < PIXELS; ++i)
         depths[i] = -1.0f;
      memset(stencils, 0xee, sizeof(stencils));
      glReadPixels(0, 0, SIZE, SIZE, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
      if (packed)
         glReadPixels(0, 0, SIZE, SIZE, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencils);
      if (!check_errors("resolve-read"))
         goto cleanup;
      for (unsigned i = 0; i < PIXELS; ++i) {
         float expected = (layer + 1) * 0.125f + (i % SIZE >= SIZE / 2 ? 0.0625f : 0);
         resolved[layer - 2] += depths[i] == expected;
         if (packed)
            stencil_matches[layer - 2] += stencils[i] == (layer + 1) * 0x11;
      }
   }

   glBindFramebuffer(GL_FRAMEBUFFER, fbos[2]);
   glBindRenderbuffer(GL_RENDERBUFFER, buffers[1]);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, SIZE, SIZE);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, buffers[1]);
   glDrawBuffer(GL_COLOR_ATTACHMENT0);
   glReadBuffer(GL_COLOR_ATTACHMENT0);
   if (!check_framebuffer(0))
      goto cleanup;
   glClearColor(1, 0, 1, 0);
   glClear(GL_COLOR_BUFFER_BIT);
   glFinish();
   if (!check_errors("sample-initialize") || ps5_egl_current_draw_status(&before))
      goto cleanup;
   glDrawArrays(GL_TRIANGLES, 0, 3);
#ifdef PS5_MSAA_DEPTH_ARRAY_HOST_REFERENCE
   ++host_draw_calls;
#endif
   glFinish();
   if (ps5_egl_current_draw_status(&after) || after != before + 1 ||
       !check_errors("sample-draw"))
      goto cleanup;
   explicit_draws = 1;
   memset(pixels, 0x7f, sizeof(pixels));
   glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   if (!check_errors("sample-read"))
      goto cleanup;
   for (unsigned i = 0; i < PIXELS; ++i)
      sampled += pixels[4 * i] == 255 && pixels[4 * i + 1] == 255 &&
                 pixels[4 * i + 2] == 255 && pixels[4 * i + 3] == 255;
   passed = sampled == PIXELS && resolved[0] == PIXELS && resolved[1] == PIXELS &&
            (!packed || (stencil_matches[0] == PIXELS && stencil_matches[1] == PIXELS));

cleanup:
   glDisable(GL_SCISSOR_TEST);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0);
   glBindRenderbuffer(GL_RENDERBUFFER, 0);
   glDeleteFramebuffers(3, fbos);
   glDeleteRenderbuffers(2, buffers);
   glDeleteTextures(1, &texture);
   cleanup_ok = check_errors("variant-cleanup");
   passed &= cleanup_ok;
   printf(TAG " format=%s samples=%d fixed=%d layers=2/3 resolve=%u/%u stencil=%u/%u "
          "sampled=%u/%u rgba=%u/%u/%u/%u draw_delta=%u cleanup=%d result=%d\n",
          packed ? "D32S8" : "D32", samples, fixed, resolved[0], resolved[1],
          stencil_matches[0], stencil_matches[1], sampled, PIXELS,
          pixels[0], pixels[1], pixels[2], pixels[3], explicit_draws, cleanup_ok, passed ? 0 : 1);
   return passed;
}

static GLuint
compile_shader(GLenum type, const char *source)
{
   GLuint shader = glCreateShader(type);
   GLint compiled = GL_FALSE;

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
      "uniform sampler2DMSArray source;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "  float expected2 = p.x < 16 ? 0.375 : 0.4375;\n"
      "  float expected3 = p.x < 16 ? 0.5 : 0.5625;\n"
      "  bool ok2 = true, ok3 = true;\n"
      "  for (int sample = 0; sample < 4; ++sample) {\n"
      "    float d2 = texelFetch(source, ivec3(p, 2), sample).r;\n"
      "    float d3 = texelFetch(source, ivec3(p, 3), sample).r;\n"
      "    ok2 = (d2 == expected2) && ok2;\n"
      "    ok3 = (d3 == expected3) && ok3;\n"
      "  }\n"
      "  bool size_ok = all(equal(textureSize(source), ivec3(32, 32, 4)));\n"
      "  color = vec4(ok2 ? 1.0 : 0.0, ok3 ? 1.0 : 0.0, size_ok ? 1.0 : 0.0, 1.0);\n"
      "}\n";
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, SURFACE_TYPE, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE,
   };
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL;
   EGLint count = 0;
   GLuint shaders[2] = {0}, program = 0, vao = 0;
   GLint linked = GL_FALSE, sampler = -1;
   unsigned calls = 0;
   int current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) || count != 1)
      goto cleanup;
#ifdef PS5_MSAA_DEPTH_ARRAY_HOST_REFERENCE
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
   printf(TAG " draw_counter=%s renderer=%s version=%s uniform_samples=1 sample_isolation=0\n",
#ifdef PS5_MSAA_DEPTH_ARRAY_HOST_REFERENCE
          "host-issued",
#else
          "native-driver",
#endif
          glGetString(GL_RENDERER), glGetString(GL_VERSION));
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
   sampler = glGetUniformLocation(program, "source");
   if (sampler < 0)
      goto cleanup;
   glUniform1i(sampler, 0);
   glActiveTexture(GL_TEXTURE0);
   glViewport(0, 0, SIZE, SIZE);
   glDisable(GL_DITHER);
   glDisable(GL_BLEND);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_STENCIL_TEST);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   passed = test_format(GL_DEPTH_COMPONENT32F);
   passed &= test_format(GL_DEPTH32F_STENCIL8);
   passed &= check_errors("final");
   int status = ps5_egl_current_draw_status(&calls);
   passed &= status == 0;
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
