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

#define WIDTH 64
#define HEIGHT 64

int ps5_egl_current_draw_status(unsigned *draw_calls);

static GLuint
compile_shader(GLenum type, const char *source)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
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
      "void main(){color=vec4(0,1,1,1);}\n";
   static const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_NONE,
   };
   static const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
   static const EGLint pbuffer_attributes[] = {
      EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLContext context = EGL_NO_CONTEXT;
   EGLContext invalid_context = EGL_NO_CONTEXT;
   EGLSurface pbuffer = EGL_NO_SURFACE;
   EGLint count = 0;
   EGLint config_id = -1;
   EGLint config_error = EGL_SUCCESS;
   EGLint mismatch_error = EGL_SUCCESS;
   EGLint interval_error = EGL_SUCCESS;
   GLuint shaders[2] = {0}, program = 0, vao = 0;
   GLuint texture = 0, framebuffer = 0;
   GLint linked = GL_FALSE;
   uint8_t pixel[4] = {0};
   unsigned draw_calls = 0;
   int draw_status = -1;
   int current = 0;
   int passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1 ||
       !strstr(eglQueryString(display, EGL_EXTENSIONS),
               "EGL_KHR_surfaceless_context"))
      goto cleanup;
   invalid_context = eglCreateContext(display, (EGLConfig)(uintptr_t)1,
                                      EGL_NO_CONTEXT, context_attributes);
   config_error = eglGetError();
   context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT,
                              context_attributes);
   pbuffer = eglCreatePbufferSurface(display, config, pbuffer_attributes);
   if (invalid_context != EGL_NO_CONTEXT || config_error != EGL_BAD_CONFIG ||
       context == EGL_NO_CONTEXT || pbuffer == EGL_NO_SURFACE ||
       !strstr(eglQueryString(display, EGL_EXTENSIONS),
               "EGL_KHR_no_config_context") ||
       !eglQueryContext(display, context, EGL_CONFIG_ID, &config_id))
      goto cleanup;

   if (eglMakeCurrent(display, pbuffer, EGL_NO_SURFACE, context))
      goto cleanup;
   mismatch_error = eglGetError();
   if (!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
      goto cleanup;
   current = 1;
   if (eglSwapInterval(display, 0))
      goto cleanup;
   interval_error = eglGetError();

   shaders[0] = compile_shader(GL_VERTEX_SHADER, vertex_source);
   shaders[1] = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
   if (!shaders[0] || !shaders[1])
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   glViewport(0, 0, WIDTH, HEIGHT);
   glUseProgram(program);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1,
                GL_RGBA, GL_UNSIGNED_BYTE, pixel);

   passed = config_id == 0 && mismatch_error == EGL_BAD_MATCH &&
            interval_error == EGL_BAD_SURFACE &&
            eglGetCurrentDisplay() == display &&
            eglGetCurrentContext() == context &&
            eglGetCurrentSurface(EGL_DRAW) == EGL_NO_SURFACE &&
            eglGetCurrentSurface(EGL_READ) == EGL_NO_SURFACE &&
            glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
               GL_FRAMEBUFFER_COMPLETE &&
            pixel[0] == 0 && pixel[1] == 255 &&
            pixel[2] == 255 && pixel[3] == 255 &&
            draw_status == 0 && draw_calls == 1 &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-core33-surfaceless] config=%d/0x%x mismatch=0x%x interval=0x%x "
          "pixel=%u/%u/%u/%u draw=%d/%u result=%d\n",
          config_id, config_error, mismatch_error, interval_error,
          pixel[0], pixel[1], pixel[2], pixel[3], draw_status, draw_calls,
          passed ? 0 : 1);

cleanup:
   if (current) {
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (texture)
         glDeleteTextures(1, &texture);
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
   if (display != EGL_NO_DISPLAY && pbuffer != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, pbuffer);
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf("[ps5-egl-core33-surfaceless] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
