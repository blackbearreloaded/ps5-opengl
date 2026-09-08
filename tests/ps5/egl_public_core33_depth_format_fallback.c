// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 64
#define HEIGHT 64

int ps5_egl_current_draw_status(unsigned *draw_calls);

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint ok = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-depth-formats] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static int
run_format(GLenum internal_format, GLuint program, GLint z_location,
           GLint color_location, GLuint fbo, int packed,
           GLint expected_depth_size)
{
   GLuint depth = 0;
   GLint depth_size = 0, stencil_size = 0, reported_format = 0;
   GLint attachment_depth_size = 0, attachment_type = 0;
   uint32_t pixel = 0;
   GLenum status;
   int passed;

   glGenRenderbuffers(1, &depth);
   glBindRenderbuffer(GL_RENDERBUFFER, depth);
   glRenderbufferStorage(GL_RENDERBUFFER, internal_format, WIDTH, HEIGHT);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_DEPTH_SIZE,
                                &depth_size);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_STENCIL_SIZE,
                                &stencil_size);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                GL_RENDERBUFFER_INTERNAL_FORMAT,
                                &reported_format);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                             packed ? GL_DEPTH_STENCIL_ATTACHMENT
                                    : GL_DEPTH_ATTACHMENT,
                             GL_RENDERBUFFER, depth);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (packed) {
      glGetFramebufferAttachmentParameteriv(
         GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
         GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &attachment_depth_size);
      glGetFramebufferAttachmentParameteriv(
         GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
         GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &attachment_type);
   }
   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClearDepth(1.0);
   glClearStencil(0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
           (packed ? GL_STENCIL_BUFFER_BIT : 0));
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glDepthMask(GL_TRUE);
   if (packed) {
      glEnable(GL_STENCIL_TEST);
      glStencilMask(0xff);
      glStencilFunc(GL_ALWAYS, 7, 0xff);
      glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
   } else {
      glDisable(GL_STENCIL_TEST);
   }
   glUseProgram(program);
   glUniform1f(z_location, 0.0f);
   glUniform4f(color_location, 1.0f, 0.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glUniform1f(z_location, 0.5f);
   glUniform4f(color_location, 0.0f, 1.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA,
                GL_UNSIGNED_BYTE, &pixel);
   passed = status == GL_FRAMEBUFFER_COMPLETE &&
            depth_size == expected_depth_size &&
            stencil_size == (packed ? 8 : 0) &&
            (!packed || (reported_format == GL_DEPTH24_STENCIL8 &&
                         attachment_depth_size == 24 &&
                         attachment_type == GL_UNSIGNED_NORMALIZED)) &&
            pixel == UINT32_C(0xff0000ff) &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-depth-formats] format=0x%x reported=0x%x size=%d/%d "
          "attachment=%d/0x%x status=0x%x pixel=%08x result=%d\n",
          internal_format, reported_format, depth_size, stencil_size,
          attachment_depth_size, attachment_type, status, pixel,
          passed ? 0 : 1);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                             packed ? GL_DEPTH_STENCIL_ATTACHMENT
                                    : GL_DEPTH_ATTACHMENT,
                             GL_RENDERBUFFER, 0);
   glDeleteRenderbuffers(1, &depth);
   return passed;
}

static int
check_texture_queries(GLenum internal_format, GLenum upload_format,
                      GLenum upload_type, GLint expected_depth_size,
                      int packed, GLuint restore_fbo)
{
   GLuint texture = 0, fbo = 0;
   GLint reported_format = 0, depth_size = 0, stencil_size = 0, depth_type = 0;
   GLint attachment_depth_size = 0, attachment_type = 0;
   GLenum status;
   int passed;

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, internal_format, WIDTH, HEIGHT, 0,
                upload_format, upload_type, NULL);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                            &reported_format);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_DEPTH_SIZE,
                            &depth_size);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_STENCIL_SIZE,
                            &stencil_size);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_DEPTH_TYPE,
                            &depth_type);

   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER,
                          packed ? GL_DEPTH_STENCIL_ATTACHMENT
                                 : GL_DEPTH_ATTACHMENT,
                          GL_TEXTURE_2D, texture, 0);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &attachment_depth_size);
   glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &attachment_type);

   passed = status == GL_FRAMEBUFFER_COMPLETE &&
            reported_format == internal_format &&
            depth_size == expected_depth_size &&
            stencil_size == (packed ? 8 : 0) &&
            depth_type == GL_UNSIGNED_NORMALIZED &&
            attachment_depth_size == expected_depth_size &&
            attachment_type == GL_UNSIGNED_NORMALIZED &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-depth-formats] texture-format=0x%x depth=%d/%d/0x%x "
          "attachment=%d/0x%x status=0x%x result=%d\n",
          reported_format, depth_size, stencil_size, depth_type,
          attachment_depth_size, attachment_type, status, passed ? 0 : 1);

   glDeleteFramebuffers(1, &fbo);
   glDeleteTextures(1, &texture);
   glBindFramebuffer(GL_FRAMEBUFFER, restore_fbo);
   return passed;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position; uniform float z;\n"
      "void main() { gl_Position=vec4(position,z,1); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform vec4 source_color; layout(location=0) out vec4 color;\n"
      "void main() { color=source_color; }\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static const GLenum formats[4] = {
      GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT24,
      GL_DEPTH_COMPONENT32, GL_DEPTH24_STENCIL8,
   };
   static const GLint expected_depth_sizes[4] = {32, 32, 32, 24};
   static const GLenum upload_formats[4] = {
      GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT,
      GL_DEPTH_COMPONENT, GL_DEPTH_STENCIL,
   };
   static const GLenum upload_types[4] = {
      GL_UNSIGNED_SHORT, GL_UNSIGNED_INT,
      GL_UNSIGNED_INT, GL_UNSIGNED_INT_24_8,
   };
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
   EGLint major = 0, minor = 0, count = 0;
   GLuint shaders[2] = {0}, program = 0, vao = 0, vbo = 0;
   GLuint fbo = 0, color = 0;
   GLint linked = GL_FALSE, z_location = -1, color_location = -1;
   unsigned matching = 0, draw_calls = 0;
   int draw_status = -1, made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
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

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[1]))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   z_location = glGetUniformLocation(program, "z");
   color_location = glGetUniformLocation(program, "source_color");
   if (!linked || z_location < 0 || color_location < 0)
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glGenFramebuffers(1, &fbo);
   glGenRenderbuffers(1, &color);
   glBindRenderbuffer(GL_RENDERBUFFER, color);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, color);
   for (unsigned i = 0; i < 4; ++i)
      matching += run_format(formats[i], program, z_location, color_location,
                             fbo, i == 3, expected_depth_sizes[i]);
   for (unsigned i = 0; i < 4; ++i)
      matching += check_texture_queries(formats[i], upload_formats[i],
                                        upload_types[i],
                                        expected_depth_sizes[i], i == 3, fbo);

   draw_status = ps5_egl_current_draw_status(&draw_calls);
   passed = major == 1 && minor == 4 && matching == 8 &&
            draw_status == 0 && draw_calls == 8 &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-depth-formats] matching=%u draw=%d/%u result=%d\n",
          matching, draw_status, draw_calls, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (fbo)
         glDeleteFramebuffers(1, &fbo);
      if (color)
         glDeleteRenderbuffers(1, &color);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      if (program)
         glDeleteProgram(program);
      for (unsigned i = 0; i < 2; ++i)
         if (shaders[i])
            glDeleteShader(shaders[i]);
      cleanup_ok &= glGetError() == GL_NO_ERROR;
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf("[ps5-egl-depth-formats] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
