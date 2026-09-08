// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define SIZE 64
#define WHITE UINT32_C(0xffffffff)
#define WHITE_HASH UINT32_C(0x4847ddc5)

static int
compile_shader(GLenum type, const char *source, GLuint *result)
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
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-msaa4-depth-texture] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static int
link_program(GLuint vertex, GLuint fragment, GLuint *result)
{
   GLint linked = GL_FALSE;
   GLuint program = glCreateProgram();

   if (!program)
      return 0;
   glAttachShader(program, vertex);
   glAttachShader(program, fragment);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      glDeleteProgram(program);
      return 0;
   }
   *result = program;
   return 1;
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
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform sampler2DMS source;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "  float expected = p.x < 32 ? 0.25 : 0.75;\n"
      "  bool ok = all(equal(textureSize(source), ivec2(64, 64)));\n"
      "  for (int sample = 0; sample < 4; ++sample)\n"
      "    ok = ok && abs(texelFetch(source, p, sample).r - expected) < 0.0001;\n"
      "  color = ok ? vec4(1.0) : vec4(0.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const float vertices[] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static uint32_t pixels[SIZE * SIZE];
   static float depths[SIZE * SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
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
   EGLint egl_major = 0, egl_minor = 0, count = 0;
   GLuint vertex_shader = 0, fragment_shader = 0, program = 0;
   GLuint vertex_array = 0, vertex_buffer = 0;
   GLuint depth_msaa = 0, depth_single = 0, output = 0;
   GLuint source_fbo = 0, resolve_fbo = 0, output_fbo = 0;
   GLenum status[3] = {0, 0, 0}, error = GL_NO_ERROR;
   GLint max_color_samples = 0, max_depth_samples = 0;
   GLint max_integer_samples = 0, samples = 0, fixed = 0, sampler = -1;
   unsigned sampled = 0, resolved_left = 0, resolved_right = 0;
   uint32_t hash = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;
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
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;

   glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &max_color_samples);
   glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &max_depth_samples);
   glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &max_integer_samples);
   if (max_color_samples < 4 || max_depth_samples < 4 ||
       max_integer_samples < 1 ||
       !compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex_shader) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source,
                       &fragment_shader) ||
       !link_program(vertex_shader, fragment_shader, &program))
      goto cleanup;

   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
   glGenBuffers(1, &vertex_buffer);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &depth_msaa);
   glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, depth_msaa);
   glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4,
                           GL_DEPTH_COMPONENT32F, SIZE, SIZE, GL_TRUE);
   glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE, 0,
                            GL_TEXTURE_SAMPLES, &samples);
   glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE, 0,
                            GL_TEXTURE_FIXED_SAMPLE_LOCATIONS, &fixed);
   glGenFramebuffers(1, &source_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, source_fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                          GL_TEXTURE_2D_MULTISAMPLE, depth_msaa, 0);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   status[0] = glCheckFramebufferStatus(GL_FRAMEBUFFER);

   glEnable(GL_SCISSOR_TEST);
   glDepthMask(GL_TRUE);
   glScissor(0, 0, SIZE / 2, SIZE);
   glClearDepth(0.25);
   glClear(GL_DEPTH_BUFFER_BIT);
   glScissor(SIZE / 2, 0, SIZE / 2, SIZE);
   glClearDepth(0.75);
   glClear(GL_DEPTH_BUFFER_BIT);
   glDisable(GL_SCISSOR_TEST);

   glGenTextures(1, &depth_single);
   glBindTexture(GL_TEXTURE_2D, depth_single);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SIZE, SIZE, 0,
                GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
   glGenFramebuffers(1, &resolve_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                          GL_TEXTURE_2D, depth_single, 0);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   status[1] = glCheckFramebufferStatus(GL_FRAMEBUFFER);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, source_fbo);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
   glBlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE,
                     GL_DEPTH_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glReadPixels(0, 0, SIZE, SIZE, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
   for (unsigned y = 0; y < SIZE; ++y) {
      for (unsigned x = 0; x < SIZE; ++x) {
         float value = depths[y * SIZE + x];

         if (x < SIZE / 2)
            resolved_left += value > 0.2499f && value < 0.2501f;
         else
            resolved_right += value > 0.7499f && value < 0.7501f;
      }
   }

   glGenRenderbuffers(1, &output);
   glBindRenderbuffer(GL_RENDERBUFFER, output);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, SIZE, SIZE);
   glGenFramebuffers(1, &output_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, output);
   status[2] = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status[0] != GL_FRAMEBUFFER_COMPLETE ||
       status[1] != GL_FRAMEBUFFER_COMPLETE ||
       status[2] != GL_FRAMEBUFFER_COMPLETE || samples != 4 ||
       fixed != GL_TRUE || glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, SIZE, SIZE);
   glDisable(GL_DEPTH_TEST);
   glUseProgram(program);
   sampler = glGetUniformLocation(program, "source");
   if (sampler < 0)
      goto cleanup;
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, depth_msaa);
   glUniform1i(sampler, 0);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   error = glGetError();
   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      sampled += pixels[i] == WHITE;
   hash = hash32(pixels, sizeof(pixels));

   passed = egl_major == 1 && egl_minor == 4 &&
            max_color_samples >= 4 && max_depth_samples >= 4 &&
            max_integer_samples >= 1 && samples == 4 && fixed == GL_TRUE &&
            status[0] == GL_FRAMEBUFFER_COMPLETE &&
            status[1] == GL_FRAMEBUFFER_COMPLETE &&
            status[2] == GL_FRAMEBUFFER_COMPLETE &&
            resolved_left == SIZE * SIZE / 2 &&
            resolved_right == SIZE * SIZE / 2 &&
            sampled == SIZE * SIZE && hash == WHITE_HASH &&
            error == GL_NO_ERROR && eglSwapBuffers(display, surface);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (output_fbo)
         glDeleteFramebuffers(1, &output_fbo);
      if (resolve_fbo)
         glDeleteFramebuffers(1, &resolve_fbo);
      if (source_fbo)
         glDeleteFramebuffers(1, &source_fbo);
      if (output)
         glDeleteRenderbuffers(1, &output);
      if (depth_single)
         glDeleteTextures(1, &depth_single);
      if (depth_msaa)
         glDeleteTextures(1, &depth_msaa);
      if (vertex_buffer)
         glDeleteBuffers(1, &vertex_buffer);
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
      if (program)
         glDeleteProgram(program);
      if (vertex_shader)
         glDeleteShader(vertex_shader);
      if (fragment_shader)
         glDeleteShader(fragment_shader);
      cleanup_ok &= glGetError() == GL_NO_ERROR;
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE,
                                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf("[ps5-egl-msaa4-depth-texture] max=%d/%d/%d status=%x/%x/%x "
          "samples=%d fixed=%d resolve=%u/%u sampled=%u:%08x "
          "hash=%08x error=%x cleanup=%u result=%d\n",
          max_color_samples, max_depth_samples, max_integer_samples,
          status[0], status[1], status[2], samples, fixed,
          resolved_left, resolved_right, sampled, pixels[0], hash, error,
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
