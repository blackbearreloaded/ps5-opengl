// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 128
#define HEIGHT 96
#define PIXELS (WIDTH * HEIGHT)

int ps5_egl_current_draw_status(unsigned *draw_calls);
int sceKernelDebugOutText(int channel, const char *text);

static void
log_line(const char *format, ...)
{
   char text[1024];
   va_list args;

   va_start(args, format);
   vsnprintf(text, sizeof(text), format, args);
   va_end(args);
   sceKernelDebugOutText(0, text);
}

static GLuint
shader(GLenum type, const char *source)
{
   GLint ok = GL_FALSE;
   GLuint value = glCreateShader(type);

   glShaderSource(value, 1, &source, NULL);
   glCompileShader(value);
   glGetShaderiv(value, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[1024] = {0};
      GLsizei length = 0;

      glGetShaderInfoLog(value, sizeof(log), &length, log);
      log_line("[ps5-gl46-compat] shader=0x%x log=%.*s\n", type, length, log);
      glDeleteShader(value);
      return 0;
   }
   return value;
}

static GLuint
program(const char *vs_source, const char *fs_source)
{
   GLuint vs = shader(GL_VERTEX_SHADER, vs_source);
   GLuint fs = shader(GL_FRAGMENT_SHADER, fs_source);
   GLuint value = 0;
   GLint ok = GL_FALSE;

   if (!vs || !fs)
      goto out;
   value = glCreateProgram();
   glAttachShader(value, vs);
   glAttachShader(value, fs);
   glLinkProgram(value);
   glGetProgramiv(value, GL_LINK_STATUS, &ok);
   if (!ok) {
      char log[1024] = {0};
      GLsizei length = 0;

      glGetProgramInfoLog(value, sizeof(log), &length, log);
      log_line("[ps5-gl46-compat] link-log=%.*s\n", length, log);
      glDeleteProgram(value);
      value = 0;
   }
out:
   if (vs)
      glDeleteShader(vs);
   if (fs)
      glDeleteShader(fs);
   return value;
}

static void
read_pixels(uint8_t *pixels)
{
   glFinish();
   glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

static unsigned
colored_half(const uint8_t *pixels, unsigned first_x, unsigned last_x)
{
   unsigned count = 0;

   for (unsigned y = 0; y < HEIGHT; ++y) {
      for (unsigned x = first_x; x < last_x; ++x) {
         const uint8_t *p = &pixels[(y * WIDTH + x) * 4];
         count += p[0] || p[1] || p[2];
      }
   }
   return count;
}

struct worker {
   EGLDisplay display;
   EGLContext context;
   GLuint main_texture;
   GLuint main_program;
   GLuint texture;
   GLuint program;
   int result;
};

static void *
shared_worker(void *data)
{
   static const char *vs =
      "#version 460 compatibility\n"
      "void main(){gl_Position=vec4(0);}\n";
   static const char *fs =
      "#version 460 compatibility\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){color=vec4(1);}\n";
   static const uint8_t blue[4] = {0, 0, 255, 255};
   struct worker *worker = data;

   if (!eglMakeCurrent(worker->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       worker->context))
      return NULL;
   worker->result = glIsTexture(worker->main_texture) == GL_TRUE &&
                    glIsProgram(worker->main_program) == GL_TRUE;
   glGenTextures(1, &worker->texture);
   glBindTexture(GL_TEXTURE_2D, worker->texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, blue);
   worker->program = program(vs, fs);
   worker->result &= worker->texture && worker->program &&
                     glGetError() == GL_NO_ERROR;
   glFinish();
   worker->result &= eglMakeCurrent(worker->display, EGL_NO_SURFACE,
                                    EGL_NO_SURFACE, EGL_NO_CONTEXT);
   return NULL;
}

int
main(void)
{
#ifdef PS5_GLTHREAD_TEST
   if (setenv("PS5_GLTHREAD", "1", 1) != 0) return 1;
#endif
   static const char *material_vs =
      "#version 460 compatibility\n"
      "layout(location=0) in vec2 position;\n"
      "flat out float value;\n"
      "void main(){gl_Position=vec4(position,0,1);"
      "value=gl_FrontMaterial.ambient.a;}\n";
   static const char *material_fs =
      "#version 460 compatibility\n"
      "flat in float value; layout(location=0) out vec4 color;\n"
      "void main(){color=vec4(value,value,value,1);}\n";
   static const char *color_vs =
      "#version 460 compatibility\n"
      "layout(location=0) in vec2 position; uniform float depth;\n"
      "void main(){gl_Position=vec4(position,depth,1);gl_PointSize=8;}\n";
   static const char *color_fs =
      "#version 460 compatibility\n"
      "uniform vec4 value; layout(location=0) out vec4 color;\n"
      "void main(){color=value;}\n";
   static const char *point_fs =
      "#version 460 compatibility\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){color=vec4(gl_PointCoord.x<.5,gl_PointCoord.y<.5,0,1);}\n";
   static const char *flat_vs =
       "#version 460 compatibility\n"
       "layout(location=0) in vec2 position; flat out vec3 c;\n"
       "void main(){gl_Position=vec4(position,0,1);"
       "c=position.x<0?(position.y<0?vec3(1,0,0):vec3(0,0,1)):"
       "(position.y<0?vec3(0,1,0):vec3(1,1,0));}\n";
   static const char *flat_fs =
      "#version 460 compatibility\n"
      "flat in vec3 c; layout(location=0) out vec4 color;\n"
      "void main(){color=vec4(c,1);}\n";
   static const float fullscreen[] = {-1, -1, 3, -1, -1, 3};
   static const float point[] = {0, 0};
   static const float faces[] = {
      -.9f, -.7f, -.1f, -.7f, -.5f, .7f,
       .1f, -.7f,  .5f,  .7f,  .9f, -.7f,
   };
   static const float quads[] = {-.7f,-.7f,.7f,-.7f,.7f,.7f,-.7f,.7f};
   static const float strip[] = {-.7f,-.7f,-.7f,.7f,.7f,-.7f,.7f,.7f};
   static const float polygon_vertices[] = {
      0,.8f,-.75f,.25f,-.45f,-.7f,.45f,-.7f,.75f,.25f,
   };
   static const uint8_t indices[] = {0, 1, 2, 3, 4};
   static const GLenum alpha_funcs[] = {
      GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
      GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS,
   };
   static const float reject_alpha[] = {.5f,.5f,.25f,.75f,.5f,.5f,.25f,.5f};
   static const float accept_alpha[] = {.5f,.25f,.5f,.5f,.75f,.25f,.5f,.5f};
   static const GLenum legacy_modes[] = {GL_QUADS, GL_QUAD_STRIP, GL_POLYGON};
   static const float *legacy_vertices[] = {quads, strip, polygon_vertices};
   static const GLsizei legacy_counts[] = {4, 4, 5};
   const EGLint config_attribs[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
      EGL_DEPTH_SIZE, 24, EGL_NONE,
   };
   const EGLint surface_attribs[] = {
      EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE,
   };
   const EGLint context_attribs[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
      EGL_CONTEXT_MINOR_VERSION_KHR, 6,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT, shared = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0, profile = 0;
   GLint gl_major = 0, gl_minor = 0, profile_mask = 0;
   GLuint vao = 0, vbo = 0, ebo = 0, programs[4] = {0};
   GLuint float_texture = 0, framebuffer = 0, shared_texture = 0;
   uint8_t pixels[PIXELS * 4];
   uint8_t flat_first[8] = {0}, flat_last[8] = {0};
   uint8_t triangle_first[4] = {0}, triangle_last[4] = {0};
   unsigned material_passes = 0, alpha_passes = 0, point_passes = 0;
   unsigned polygon_passes = 0, legacy_passes = 0, draw_calls = 0;
   int draw_status = -1, made_current = 0, passed = 0;
   unsigned setup_stage = 0;
   EGLint setup_error = EGL_SUCCESS;
   GLfloat unclamped[4] = {0}, clamped[4] = {0};
   GLint polygon_mode[2] = {0}, quads_follow = 0;
   struct worker worker = {0};
   pthread_t thread;
   pthread_attr_t thread_attributes;
   int thread_attributes_initialized = 0, thread_created = 0, sharing_passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   log_line("[ps5-gl46-compat] stage=egl-start\n");
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY)
      goto cleanup;
   setup_stage = 1;
   if (!eglInitialize(display, &egl_major, &egl_minor))
      goto cleanup;
   setup_stage = 2;
   if (!eglBindAPI(EGL_OPENGL_API))
      goto cleanup;
   setup_stage = 3;
   if (!eglChooseConfig(display, config_attribs, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   setup_stage = 4;
   surface = eglCreatePbufferSurface(display, config, surface_attribs);
   if (surface == EGL_NO_SURFACE)
      goto cleanup;
   setup_stage = 5;
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attribs);
   if (context == EGL_NO_CONTEXT)
      goto cleanup;
   setup_stage = 6;
   shared = eglCreateContext(display, config, context, context_attribs);
   if (shared == EGL_NO_CONTEXT)
      goto cleanup;
   setup_stage = 7;
   if (!eglMakeCurrent(display, surface, surface, context))
      goto cleanup;
   made_current = 1;
   setup_stage = 8;
   if (!eglSwapInterval(display, 0))
      goto cleanup;
   setup_stage = 9;
   if (!eglQueryContext(display, context,
                        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, &profile))
      goto cleanup;
   setup_stage = 10;
   log_line("[ps5-gl46-compat] stage=context-current\n");
   glGetIntegerv(GL_MAJOR_VERSION, &gl_major);
   glGetIntegerv(GL_MINOR_VERSION, &gl_minor);
   glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
   log_line("[ps5-gl46-compat] version=%s glsl=%s egl-profile=0x%x gl-profile=0x%x\n",
            glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION),
            profile, profile_mask);

   programs[0] = program(material_vs, material_fs);
   programs[1] = program(color_vs, color_fs);
   programs[2] = program(color_vs, point_fs);
   programs[3] = program(flat_vs, flat_fs);
   if (!programs[0] || !programs[1] || !programs[2] || !programs[3])
      goto cleanup;
   log_line("[ps5-gl46-compat] stage=programs-linked\n");

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(faces), NULL, GL_DYNAMIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glGenBuffers(1, &ebo);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0, 0, 0, 0);

   glUseProgram(programs[0]);
   glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(fullscreen), fullscreen);
   for (unsigned i = 0; i < 4; ++i) {
      const GLfloat material[4] = {.2f, .2f, .2f, i & 1 ? 1.0f : 0.0f};
      uint8_t pixel[4];

      glMaterialfv(GL_FRONT, GL_AMBIENT, material);
      glClear(GL_COLOR_BUFFER_BIT);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glFinish();
      glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1,
                   GL_RGBA, GL_UNSIGNED_BYTE, pixel);
      material_passes += pixel[0] == (i & 1 ? 255 : 0) &&
                         pixel[1] == pixel[0] && pixel[2] == pixel[0];
   }
   log_line("[ps5-gl46-compat] stage=material passes=%u\n", material_passes);

   glUseProgram(programs[1]);
   GLint depth = glGetUniformLocation(programs[1], "depth");
   GLint value = glGetUniformLocation(programs[1], "value");
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   for (unsigned i = 0; i < 8; ++i) {
      uint8_t pixel[4];

      glClearDepth(1.0);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_ALPHA_TEST);
      glAlphaFunc(alpha_funcs[i], .5f);
      glUniform1f(depth, .25f);
      glUniform4f(value, 1, 0, 0, reject_alpha[i]);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      if (alpha_funcs[i] == GL_NEVER) {
         glDisable(GL_ALPHA_TEST);
      } else if (alpha_funcs[i] == GL_ALWAYS) {
         glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      }
      glUniform1f(depth, .5f);
      glUniform4f(value, 0, 1, 0, accept_alpha[i]);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glFinish();
      glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1,
                   GL_RGBA, GL_UNSIGNED_BYTE, pixel);
      alpha_passes += pixel[0] == 0 && pixel[1] == 255 && pixel[2] == 0;
   }
   glDisable(GL_ALPHA_TEST);
   glDisable(GL_DEPTH_TEST);
   log_line("[ps5-gl46-compat] stage=alpha passes=%u\n", alpha_passes);

   glGenTextures(1, &float_texture);
   glBindTexture(GL_TEXTURE_2D, float_texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, WIDTH, HEIGHT, 0,
                GL_RGBA, GL_FLOAT, NULL);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, float_texture, 0);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      goto cleanup;
   glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(fullscreen), fullscreen);
   glUseProgram(programs[1]);
   glUniform1f(depth, 0);
   glUniform4f(value, -.5f, 1.5f, .25f, 1);
   glClampColor(GL_CLAMP_FRAGMENT_COLOR, GL_FALSE);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA, GL_FLOAT, unclamped);
   glClampColor(GL_CLAMP_FRAGMENT_COLOR, GL_TRUE);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA, GL_FLOAT, clamped);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   log_line("[ps5-gl46-compat] stage=clamp values=%g/%g:%g/%g\n",
            unclamped[0], unclamped[1], clamped[0], clamped[1]);

   glUseProgram(programs[2]);
   glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(point), point);
   glEnable(GL_PROGRAM_POINT_SIZE);
   glEnable(GL_POINT_SPRITE);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_POINTS, 0, 1);
   read_pixels(pixels);
   unsigned quadrants[4] = {0};
   for (unsigned i = 0; i < PIXELS; ++i) {
      const uint8_t *p = &pixels[i * 4];
      if (p[3])
         quadrants[(p[0] ? 1 : 0) | (p[1] ? 2 : 0)]++;
   }
   point_passes = glIsEnabled(GL_POINT_SPRITE) == GL_TRUE &&
                  quadrants[0] == 16 && quadrants[1] == 16 &&
                  quadrants[2] == 16 && quadrants[3] == 16;
   glDisable(GL_POINT_SPRITE);
   point_passes += glIsEnabled(GL_POINT_SPRITE) == GL_FALSE;
   glDisable(GL_PROGRAM_POINT_SIZE);
   log_line("[ps5-gl46-compat] stage=point passes=%u\n", point_passes);

   glUseProgram(programs[1]);
   glUniform1f(depth, 0);
   glUniform4f(value, 1, 1, 1, 1);
   glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(faces), faces);
   glPolygonMode(GL_FRONT, GL_LINE);
   glPolygonMode(GL_BACK, GL_FILL);
   glGetIntegerv(GL_POLYGON_MODE, polygon_mode);
   glDisable(GL_CULL_FACE);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 6);
   read_pixels(pixels);
   unsigned mixed_front = colored_half(pixels, 0, WIDTH / 2);
   unsigned mixed_back = colored_half(pixels, WIDTH / 2, WIDTH);
   glEnable(GL_CULL_FACE);
   glCullFace(GL_BACK);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 6);
   read_pixels(pixels);
   unsigned cull_back_front = colored_half(pixels, 0, WIDTH / 2);
   unsigned cull_back_back = colored_half(pixels, WIDTH / 2, WIDTH);
   glCullFace(GL_FRONT);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 6);
   read_pixels(pixels);
   unsigned cull_front_front = colored_half(pixels, 0, WIDTH / 2);
   unsigned cull_front_back = colored_half(pixels, WIDTH / 2, WIDTH);
   glDisable(GL_CULL_FACE);
   glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
   glGetIntegerv(GL_POLYGON_MODE, polygon_mode);
   polygon_passes = mixed_front > 0 && mixed_back > mixed_front * 3 &&
                    cull_back_front > 0 && cull_back_back == 0 &&
                    cull_front_front == 0 && cull_front_back > 0 &&
                    polygon_mode[0] == GL_FILL && polygon_mode[1] == GL_FILL;
   log_line("[ps5-gl46-compat] stage=polygon pass=%u counts=%u/%u/%u/%u/%u/%u\n",
            polygon_passes, mixed_front, mixed_back, cull_back_front,
            cull_back_back, cull_front_front, cull_front_back);

   glUseProgram(programs[3]);
   for (unsigned mode = 0; mode < 3; ++mode) {
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      legacy_counts[mode] * 2 * sizeof(float),
                      legacy_vertices[mode]);
      for (unsigned indexed = 0; indexed < 2; ++indexed) {
         glClear(GL_COLOR_BUFFER_BIT);
         if (indexed)
            glDrawElements(legacy_modes[mode], legacy_counts[mode],
                           GL_UNSIGNED_BYTE, NULL);
         else
            glDrawArrays(legacy_modes[mode], 0, legacy_counts[mode]);
         read_pixels(pixels);
         legacy_passes += colored_half(pixels, 0, WIDTH) > 1000;
      }
   }
   glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quads), quads);
   glGetIntegerv(GL_QUADS_FOLLOW_PROVOKING_VERTEX_CONVENTION, &quads_follow);
   glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glReadPixels(90, 30, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, triangle_first);
   glProvokingVertex(GL_LAST_VERTEX_CONVENTION);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glReadPixels(90, 30, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, triangle_last);
   glProvokingVertex(GL_FIRST_VERTEX_CONVENTION);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_QUADS, 0, 4);
   glReadPixels(35, 65, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, flat_first);
   glReadPixels(90, 30, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, flat_first + 4);
   legacy_passes += flat_first[0] == 255 && flat_first[1] == 0 &&
                    flat_first[2] == 0 && flat_first[4] == 255 &&
                    flat_first[5] == 0 && flat_first[6] == 0;
   glProvokingVertex(GL_LAST_VERTEX_CONVENTION);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawElements(GL_QUADS, 4, GL_UNSIGNED_BYTE, NULL);
   glReadPixels(35, 65, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, flat_last);
   glReadPixels(90, 30, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, flat_last + 4);
   legacy_passes += flat_last[0] == 0 && flat_last[1] == 0 &&
                    flat_last[2] == 255 && flat_last[4] == 0 &&
                    flat_last[5] == 0 && flat_last[6] == 255;
   log_line("[ps5-gl46-compat] stage=legacy passes=%u follows=%d tri=%u/%u/%u:%u/%u/%u flat=%u/%u/%u,%u/%u/%u:%u/%u/%u,%u/%u/%u\n",
            legacy_passes, quads_follow,
            triangle_first[0], triangle_first[1], triangle_first[2],
            triangle_last[0], triangle_last[1], triangle_last[2],
            flat_first[0], flat_first[1], flat_first[2],
            flat_first[4], flat_first[5], flat_first[6],
            flat_last[0], flat_last[1], flat_last[2],
            flat_last[4], flat_last[5], flat_last[6]);

   glGenTextures(1, &shared_texture);
   glBindTexture(GL_TEXTURE_2D, shared_texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   worker.display = display;
   worker.context = shared;
   worker.main_texture = shared_texture;
   worker.main_program = programs[0];
   cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                EGL_NO_CONTEXT);
   made_current = 0;
   thread_attributes_initialized =
      pthread_attr_init(&thread_attributes) == 0;
   thread_created = thread_attributes_initialized &&
      pthread_attr_setstacksize(&thread_attributes, 8u * 1024u * 1024u) == 0 &&
      pthread_create(&thread, &thread_attributes, shared_worker, &worker) == 0;
   if (thread_attributes_initialized)
      pthread_attr_destroy(&thread_attributes);
   if (thread_created)
      pthread_join(thread, NULL);
   sharing_passed = thread_created && worker.result &&
                    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   shared) &&
                    glIsTexture(worker.texture) == GL_TRUE &&
                    glIsProgram(worker.program) == GL_TRUE;
   if (sharing_passed) {
      EGLBoolean destroyed_current = eglDestroyContext(display, shared);
      EGLint destroy_error = eglGetError();

      glDeleteTextures(1, &worker.texture);
      glDeleteProgram(worker.program);
      sharing_passed &= !destroyed_current && destroy_error == EGL_BAD_ACCESS &&
                        glGetError() == GL_NO_ERROR &&
                        eglMakeCurrent(display, EGL_NO_SURFACE,
                                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }

   made_current = eglMakeCurrent(display, surface, surface, context);
   sharing_passed &= made_current;
   log_line("[ps5-gl46-compat] stage=sharing pass=%u\n", sharing_passed);

   draw_status = ps5_egl_current_draw_status(&draw_calls);
   passed = egl_major == 1 && egl_minor == 4 && gl_major == 4 && gl_minor == 6 &&
            profile == EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR &&
            (profile_mask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) &&
            material_passes == 4 && alpha_passes == 8 && point_passes == 2 &&
            fabsf(unclamped[0] + .5f) < .01f &&
            fabsf(unclamped[1] - 1.5f) < .01f &&
            fabsf(clamped[0]) < .01f && fabsf(clamped[1] - 1) < .01f &&
            polygon_passes && legacy_passes == 8 && sharing_passed &&
            glGetError() == GL_NO_ERROR;
   log_line("[ps5-gl46-compat] material=%u/4 alpha=%u/8 clamp=%g/%g:%g/%g "
            "point=%u/2 polygon=%u legacy=%u/8 share=%u draw=%d/%u result=%d\n",
            material_passes, alpha_passes, unclamped[0], unclamped[1],
            clamped[0], clamped[1], point_passes, polygon_passes,
            legacy_passes, sharing_passed, draw_status, draw_calls,
            passed ? 0 : 1);

cleanup:
   if (setup_stage < 10) {
      setup_error = eglGetError();
      log_line("[ps5-gl46-compat] setup-failed stage=%u egl=0x%x\n",
               setup_stage, setup_error);
   }
   if (!made_current && context != EGL_NO_CONTEXT &&
       surface != EGL_NO_SURFACE &&
       eglMakeCurrent(display, surface, surface, context))
      made_current = 1;
   if (made_current) {
      if (shared_texture)
         glDeleteTextures(1, &shared_texture);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (float_texture)
         glDeleteTextures(1, &float_texture);
      if (ebo)
         glDeleteBuffers(1, &ebo);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      for (unsigned i = 0; i < 4; ++i)
         if (programs[i])
            glDeleteProgram(programs[i]);
      cleanup_ok &= glGetError() == GL_NO_ERROR;
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
   }
   if (display != EGL_NO_DISPLAY && shared != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, shared);
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   log_line("[ps5-gl46-compat] cleanup=%u result=%d\n",
            cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
