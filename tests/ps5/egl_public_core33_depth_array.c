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
#define TEX_SIZE 4
#define LAYERS 2

#ifdef PS5_DEPTH_TARGETS_HOST_REFERENCE
#define TAG "[host-egl-core33-depth-array]"
#define SURFACE_TYPE EGL_PBUFFER_BIT
/* Host counts issued calls only; native still checks driver draw status. */
static unsigned host_draw_calls;
static int ps5_egl_current_draw_status(unsigned *draw_calls)
{
   *draw_calls = host_draw_calls;
   return 0;
}
#else
#define TAG "[ps5-egl-core33-depth-array]"
#define SURFACE_TYPE EGL_WINDOW_BIT
int ps5_egl_current_draw_status(unsigned *draw_calls);
#endif

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
      printf(TAG " shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main(){gl_Position=vec4(position,0,1);}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform sampler2DArray raw_depth;\n"
      "uniform sampler2DArrayShadow compared_depth;\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){\n"
      " float a=texture(raw_depth,vec3(.5,.5,0)).r;\n"
      " float b=texture(raw_depth,vec3(.5,.5,1)).r;\n"
      " float c=texture(compared_depth,vec4(.5,.5,0,.5));\n"
      " float d=texture(compared_depth,vec4(.5,.5,1,.5));\n"
      " bool ok=abs(a-.25)<.001&&abs(b-.75)<.001&&c<.01&&d>.99;\n"
      " color=ok?vec4(0,1,1,1):vec4(1,0,0,1);\n"
      "}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static float source[TEX_SIZE * TEX_SIZE * LAYERS];
   static float mip[2 * 2 * LAYERS];
   uint8_t pixel[4] = {0};
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, SURFACE_TYPE,
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
   GLuint textures[2] = {0};
   GLint linked = GL_FALSE;
   GLenum error = GL_NO_ERROR;
   unsigned draw_calls = 0;
   int mip_ok = 1, draw_status = -1, made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   for (unsigned i = 0; i < TEX_SIZE * TEX_SIZE; ++i) {
      source[i] = 0.25f;
      source[TEX_SIZE * TEX_SIZE + i] = 0.75f;
   }

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
#ifdef PS5_DEPTH_TARGETS_HOST_REFERENCE
   const EGLint surface_attributes[] = {
      EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE,
   };
   surface = eglCreatePbufferSurface(display, config, surface_attributes);
#else
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
#endif
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;
#ifdef PS5_DEPTH_TARGETS_HOST_REFERENCE
   printf(TAG " draw_counter=host-issued renderer=%s version=%s\n",
          glGetString(GL_RENDERER), glGetString(GL_VERSION));
#endif

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[1]))
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
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(2, textures);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D_ARRAY, textures[0]);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                   GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                TEX_SIZE, TEX_SIZE, LAYERS, 0,
                GL_DEPTH_COMPONENT, GL_FLOAT, source);
   glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
   glGetTexImage(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT, GL_FLOAT, mip);
   for (unsigned i = 0; i < 4; ++i) {
      mip_ok &= mip[i] == 0.25f;
      mip_ok &= mip[4 + i] == 0.75f;
   }

   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D_ARRAY, textures[1]);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE,
                   GL_COMPARE_REF_TO_TEXTURE);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
   glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                TEX_SIZE, TEX_SIZE, LAYERS, 0,
                GL_DEPTH_COMPONENT, GL_FLOAT, source);

   glUseProgram(program);
   glUniform1i(glGetUniformLocation(program, "raw_depth"), 0);
   glUniform1i(glGetUniformLocation(program, "compared_depth"), 1);
   glViewport(0, 0, WIDTH, HEIGHT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
#ifdef PS5_DEPTH_TARGETS_HOST_REFERENCE
   ++host_draw_calls;
#endif
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1,
                GL_RGBA, GL_UNSIGNED_BYTE, pixel);
   error = glGetError();
#ifdef PS5_DEPTH_TARGETS_HOST_REFERENCE
   passed = major == 1 && minor >= 4;
#else
   passed = major == 1 && minor == 4;
#endif
   passed &= mip_ok &&
            pixel[0] == 0 && pixel[1] == 255 &&
            pixel[2] == 255 && pixel[3] == 255 &&
            draw_status == 0 && draw_calls == 1 && error == GL_NO_ERROR;
   printf(TAG " mip=%d pixel=%u/%u/%u/%u "
          "draw=%d/%u error=0x%x result=%d\n",
          mip_ok, pixel[0], pixel[1], pixel[2], pixel[3],
          draw_status, draw_calls, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      if (textures[0] || textures[1])
         glDeleteTextures(2, textures);
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
   printf(TAG " cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
