// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define SIZE 32
#define LAYERS 3
#define LAYER_PIXELS (SIZE * SIZE)

int ps5_egl_current_draw_status(unsigned *draw_calls);

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
      printf("[ps5-egl-core33-layered-depth] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static int
link_program(const GLuint shaders[3], GLuint *result)
{
   GLint linked = GL_FALSE;
   GLuint program = glCreateProgram();

   if (!program)
      return 0;
   for (unsigned i = 0; i < 3; ++i)
      glAttachShader(program, shaders[i]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetProgramInfoLog(program, sizeof(log), &length, log);
      printf("[ps5-egl-core33-layered-depth] link=%.*s\n", length, log);
      glDeleteProgram(program);
      return 0;
   }
   *result = program;
   return 1;
}

static unsigned
count_depth(const float *values, unsigned layer, float expected)
{
   unsigned matching = 0;

   values += layer * LAYER_PIXELS;
   for (unsigned i = 0; i < LAYER_PIXELS; ++i) {
      float delta = values[i] - expected;

      matching += delta > -0.00001f && delta < 0.00001f;
   }
   return matching;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main(){gl_Position=vec4(position,0,1);}\n";
   static const char *geometry_source =
      "#version 330 core\n"
      "layout(triangles) in;\n"
      "layout(triangle_strip,max_vertices=9) out;\n"
      "void main(){\n"
      " for(int layer=0;layer<3;++layer){\n"
      "  float z=-0.5+0.5*float(layer);\n"
      "  for(int vertex=0;vertex<3;++vertex){\n"
      "   gl_Layer=layer;\n" /* EmitVertex leaves all outputs undefined (GLSL 3.30, 8.10). */
      "   gl_Position=vec4(gl_in[vertex].gl_Position.xy,z,1);\n"
      "   EmitVertex();\n"
      "  }\n"
      "  EndPrimitive();\n"
      " }\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "void main(){}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static float depth_values[LAYER_PIXELS * LAYERS];
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
   GLuint shaders[3] = {0}, program = 0, vao = 0, vbo = 0;
   GLuint texture = 0, framebuffer = 0;
   GLenum layer_status[2] = {0}, layered_status = 0;
   GLenum error = GL_NO_ERROR;
   GLint layered = GL_FALSE;
   unsigned clear_matches[3] = {0}, draw_matches[3] = {0};
   unsigned draw_calls = 0;
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
       !compile_shader(GL_GEOMETRY_SHADER, geometry_source, &shaders[1]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[2]) ||
       !link_program(shaders, &program))
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                SIZE, SIZE, LAYERS, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   /* Define the untouched layer too; NULL texture data does not promise zeros. */
   glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texture, 0);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      goto cleanup;
   glClearDepth(0.0);
   glClear(GL_DEPTH_BUFFER_BIT);
   glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             texture, 0, 1);
   layer_status[0] = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glClearDepth(0.25);
   glClear(GL_DEPTH_BUFFER_BIT);
   glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             texture, 0, 2);
   layer_status[1] = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glClearDepth(0.75);
   glClear(GL_DEPTH_BUFFER_BIT);
   glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
   glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT,
                 GL_FLOAT, depth_values);
   clear_matches[0] = count_depth(depth_values, 0, 0.0f);
   clear_matches[1] = count_depth(depth_values, 1, 0.25f);
   clear_matches[2] = count_depth(depth_values, 2, 0.75f);

   glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, texture, 0);
   layered_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_FRAMEBUFFER_ATTACHMENT_LAYERED, &layered);
   glClearDepth(1.0);
   glClear(GL_DEPTH_BUFFER_BIT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_ALWAYS);
   glDepthMask(GL_TRUE);
   glViewport(0, 0, SIZE, SIZE);
   glUseProgram(program);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
   glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT,
                 GL_FLOAT, depth_values);
   draw_matches[0] = count_depth(depth_values, 0, 0.25f);
   draw_matches[1] = count_depth(depth_values, 1, 0.5f);
   draw_matches[2] = count_depth(depth_values, 2, 0.75f);
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            layer_status[0] == GL_FRAMEBUFFER_COMPLETE &&
            layer_status[1] == GL_FRAMEBUFFER_COMPLETE &&
            layered_status == GL_FRAMEBUFFER_COMPLETE && layered &&
            clear_matches[0] == LAYER_PIXELS &&
            clear_matches[1] == LAYER_PIXELS &&
            clear_matches[2] == LAYER_PIXELS &&
            draw_matches[0] == LAYER_PIXELS &&
            draw_matches[1] == LAYER_PIXELS &&
            draw_matches[2] == LAYER_PIXELS &&
            draw_status == 0 && draw_calls == 1 && error == GL_NO_ERROR;
   printf("[ps5-egl-core33-layered-depth] fbo=0x%x/0x%x/0x%x "
          "layered=%d clear=%u/%u/%u draw=%u/%u/%u status=%d/%u "
          "error=0x%x result=%d\n",
          layer_status[0], layer_status[1], layered_status, layered,
          clear_matches[0], clear_matches[1], clear_matches[2],
          draw_matches[0], draw_matches[1], draw_matches[2],
          draw_status, draw_calls, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (texture)
         glDeleteTextures(1, &texture);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      if (program)
         glDeleteProgram(program);
      for (unsigned i = 0; i < 3; ++i)
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
   printf("[ps5-egl-core33-layered-depth] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
