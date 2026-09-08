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
      printf("[ps5-egl-msaa-array] shader=0x%x log=%.*s\n",
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
      "uniform sampler2DMSArray source;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  ivec3 dimensions = textureSize(source);\n"
      "  vec4 layer0 = vec4(0.0);\n"
      "  vec4 layer1 = vec4(0.0);\n"
      "  for (int sample = 0; sample < 4; ++sample) {\n"
      "    layer0 += texelFetch(source, ivec3(0, 0, 0), sample);\n"
      "    layer1 += texelFetch(source, ivec3(0, 0, 1), sample);\n"
      "  }\n"
      "  bool size_ok = all(equal(dimensions, ivec3(64, 64, 2)));\n"
      "  color = vec4(size_ok ? 1.0 : 0.0, layer0.r * 0.25,\n"
      "               layer1.b * 0.25, 1.0);\n"
      "}\n";
   static const float vertices[] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static uint32_t pixels[SIZE * SIZE];
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
   GLuint texture = 0, msaa_fbo = 0, output_fbo = 0, output = 0;
   GLenum status[3] = {0, 0, 0}, error = GL_NO_ERROR;
   GLint samples = 0, fixed = 0, max_layers = 0, sampler = -1;
   unsigned matching = 0, resolved[2] = {0, 0};
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

   glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
   if (max_layers < 2 ||
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

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, texture);
   glTexImage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 4, GL_RGBA8,
                           SIZE, SIZE, 2, GL_TRUE);
   glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0,
                            GL_TEXTURE_SAMPLES, &samples);
   glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0,
                            GL_TEXTURE_FIXED_SAMPLE_LOCATIONS, &fixed);
   glGenFramebuffers(1, &msaa_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
   for (unsigned layer = 0; layer < 2; ++layer) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                texture, 0, layer);
      status[layer] = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      glClearColor(layer ? 0.0f : 1.0f, 0.0f, layer ? 1.0f : 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
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

   for (unsigned layer = 0; layer < 2; ++layer) {
      const uint32_t expected = layer ? UINT32_C(0xffff0000)
                                      : UINT32_C(0xff0000ff);

      glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo);
      glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                texture, 0, layer);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, output_fbo);
      glBlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
      glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      for (unsigned i = 0; i < SIZE * SIZE; ++i)
         resolved[layer] += pixels[i] == expected;
   }

   glBindFramebuffer(GL_FRAMEBUFFER, output_fbo);
   glViewport(0, 0, SIZE, SIZE);
   glDisable(GL_BLEND);
   glDisable(GL_DEPTH_TEST);
   glUseProgram(program);
   sampler = glGetUniformLocation(program, "source");
   if (sampler < 0)
      goto cleanup;
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, texture);
   glUniform1i(sampler, 0);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   error = glGetError();
   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      matching += pixels[i] == WHITE;
   hash = hash32(pixels, sizeof(pixels));

   passed = egl_major == 1 && egl_minor == 4 && max_layers >= 2 &&
            samples == 4 && fixed == GL_TRUE &&
            status[0] == GL_FRAMEBUFFER_COMPLETE &&
            status[1] == GL_FRAMEBUFFER_COMPLETE &&
            status[2] == GL_FRAMEBUFFER_COMPLETE &&
            resolved[0] == SIZE * SIZE && resolved[1] == SIZE * SIZE &&
            matching == SIZE * SIZE && hash == WHITE_HASH &&
            error == GL_NO_ERROR;

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (output_fbo)
         glDeleteFramebuffers(1, &output_fbo);
      if (msaa_fbo)
         glDeleteFramebuffers(1, &msaa_fbo);
      if (output)
         glDeleteRenderbuffers(1, &output);
      if (texture)
         glDeleteTextures(1, &texture);
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
   printf("[ps5-egl-msaa-array] status=%x/%x/%x samples=%d fixed=%d "
          "layers=%d resolve=%u/%u pixels=%u:%08x hash=%08x "
          "error=%x cleanup=%u result=%d\n",
          status[0], status[1], status[2], samples, fixed, max_layers,
          resolved[0], resolved[1], matching, pixels[0], hash, error,
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
