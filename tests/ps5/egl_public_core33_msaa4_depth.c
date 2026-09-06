#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define WIDTH 128
#define HEIGHT 128

#ifdef PS5_MSAA4_STENCIL_TEST
#define TEST_TAG "ps5-egl-msaa4-stencil"
#define DEPTH_FORMAT GL_DEPTH32F_STENCIL8
#define DEPTH_ATTACHMENT GL_DEPTH_STENCIL_ATTACHMENT
#define EXPECTED_STENCIL_BITS 8
#else
#define TEST_TAG "ps5-egl-msaa4-depth"
#define DEPTH_FORMAT GL_DEPTH_COMPONENT32F
#define DEPTH_ATTACHMENT GL_DEPTH_ATTACHMENT
#define EXPECTED_STENCIL_BITS 0
#endif

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return -1;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512];
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[%s] shader=0x%x log=%.*s\n", TEST_TAG, type, length, log);
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
   return 0;
}

static int
link_program(GLuint vertex, GLuint fragment, GLuint *result)
{
   GLint linked = GL_FALSE;
   GLuint program = glCreateProgram();

   if (!program)
      return -1;
   glAttachShader(program, vertex);
   glAttachShader(program, fragment);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      char log[512];
      GLsizei length = 0;

      glGetProgramInfoLog(program, sizeof(log), &length, log);
      printf("[%s] link log=%.*s\n", TEST_TAG, length, log);
      glDeleteProgram(program);
      return -1;
   }
   *result = program;
   return 0;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 position;\n"
      "uniform float clip_z;\n"
      "void main() { gl_Position = vec4(position, clip_z, 1.0); }\n";
   static const char *fragment_source =
      "#version 330\n"
      "uniform vec4 draw_color;\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { color = draw_color; }\n";
   static const float vertices[] = {
      -1.0f, -1.0f,
       1.0f, -1.0f,
      -1.0f,  1.0f,
      -1.0f, -1.0f,
       3.0f, -1.0f,
      -1.0f,  3.0f,
   };
   static uint32_t pixels[WIDTH * HEIGHT];
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
   EGLint egl_major = 0, egl_minor = 0, config_count = 0;
   GLuint vertex_shader = 0, fragment_shader = 0, program = 0;
   GLuint vao = 0, vbo = 0, msaa_fbo = 0, resolve_fbo = 0;
   GLuint msaa_color = 0, msaa_depth = 0, single_depth = 0;
   GLuint resolve_color = 0;
   GLenum mismatch_status = 0, msaa_status = 0, resolve_status = 0;
   GLenum draw_error = GL_NO_ERROR, cleanup_error = GL_NO_ERROR;
   EGLBoolean cleanup_ok = EGL_TRUE;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   GLint major = 0, minor = 0, max_samples = 0;
   GLint depth_samples = 0, depth_bits = 0, stencil_bits = 0;
   GLint clip_z = -1, draw_color = -1;
   unsigned red = 0, blue = 0, mixed = 0, bad = 0;
   int made_current = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1,
                        &config_count) || config_count != 1)
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

   glGetIntegerv(GL_MAJOR_VERSION, &major);
   glGetIntegerv(GL_MINOR_VERSION, &minor);
   glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
   if (major != 3 || minor != 3 || max_samples < 4 ||
       compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex_shader) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source,
                      &fragment_shader) ||
       link_program(vertex_shader, fragment_shader, &program))
      goto cleanup;
   clip_z = glGetUniformLocation(program, "clip_z");
   draw_color = glGetUniformLocation(program, "draw_color");
   if (clip_z < 0 || draw_color < 0)
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   glGenRenderbuffers(1, &msaa_color);
   glBindRenderbuffer(GL_RENDERBUFFER, msaa_color);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8,
                                    WIDTH, HEIGHT);
   glGenFramebuffers(1, &msaa_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, msaa_color);

   glGenRenderbuffers(1, &single_depth);
   glBindRenderbuffer(GL_RENDERBUFFER, single_depth);
   glRenderbufferStorage(GL_RENDERBUFFER, DEPTH_FORMAT, WIDTH, HEIGHT);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, DEPTH_ATTACHMENT,
                             GL_RENDERBUFFER, single_depth);
   mismatch_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

   glGenRenderbuffers(1, &msaa_depth);
   glBindRenderbuffer(GL_RENDERBUFFER, msaa_depth);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4,
                                    DEPTH_FORMAT, WIDTH, HEIGHT);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES,
                                &depth_samples);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                GL_RENDERBUFFER_DEPTH_SIZE, &depth_bits);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                GL_RENDERBUFFER_STENCIL_SIZE,
                                &stencil_bits);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, DEPTH_ATTACHMENT,
                             GL_RENDERBUFFER, msaa_depth);
   msaa_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

   glGenRenderbuffers(1, &resolve_color);
   glBindRenderbuffer(GL_RENDERBUFFER, resolve_color);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
   glGenFramebuffers(1, &resolve_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, resolve_color);
   resolve_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (mismatch_status != GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE ||
       msaa_status != GL_FRAMEBUFFER_COMPLETE ||
       resolve_status != GL_FRAMEBUFFER_COMPLETE ||
       depth_samples != 4 || depth_bits != 32 ||
       stencil_bits != EXPECTED_STENCIL_BITS ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;

   glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
   glViewport(0, 0, WIDTH, HEIGHT);
   glEnable(GL_MULTISAMPLE);
   glDisable(GL_BLEND);
   glDisable(GL_SCISSOR_TEST);
#ifdef PS5_MSAA4_STENCIL_TEST
   glDisable(GL_DEPTH_TEST);
   glEnable(GL_STENCIL_TEST);
   glStencilMask(0xff);
   glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
#else
   glDisable(GL_STENCIL_TEST);
   glEnable(GL_DEPTH_TEST);
   glDepthMask(GL_TRUE);
   glDepthFunc(GL_LESS);
#endif
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
#ifdef PS5_MSAA4_STENCIL_TEST
   glClearStencil(0);
   glClearDepth(1.0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
           GL_STENCIL_BUFFER_BIT);
#else
   glClear(GL_COLOR_BUFFER_BIT);
   glClearDepth(1.0);
   glClear(GL_DEPTH_BUFFER_BIT);
#endif
   glUseProgram(program);

#ifdef PS5_MSAA4_STENCIL_TEST
   glStencilFunc(GL_ALWAYS, 0x5a, 0xff);
#endif
   glUniform1f(clip_z, -0.5f);
   glUniform4f(draw_color, 1.0f, 0.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
#ifdef PS5_MSAA4_STENCIL_TEST
   glStencilMask(0x00);
   glStencilFunc(GL_NOTEQUAL, 0x5a, 0xff);
   glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
#endif
   glUniform1f(clip_z, 0.5f);
   glUniform4f(draw_color, 0.0f, 0.0f, 1.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 3, 3);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
   glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT,
                     GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glFinish();
   glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   draw_error = glGetError();

   for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
      const uint8_t *pixel = (const uint8_t *)&pixels[i];
      int sum = (int)pixel[0] + (int)pixel[2];

      if (pixel[1] || pixel[3] != 255) {
         bad++;
      } else if (pixel[0] >= 250 && pixel[2] <= 5) {
         red++;
      } else if (pixel[2] >= 250 && pixel[0] <= 5) {
         blue++;
      } else if (pixel[0] > 5 && pixel[2] > 5 &&
                 sum >= 251 && sum <= 259) {
         mixed++;
      } else {
         bad++;
      }
   }
   passed = egl_major == 1 && egl_minor == 4 &&
            mismatch_status == GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE &&
            msaa_status == GL_FRAMEBUFFER_COMPLETE &&
            resolve_status == GL_FRAMEBUFFER_COMPLETE &&
            depth_samples == 4 && depth_bits == 32 &&
            stencil_bits == EXPECTED_STENCIL_BITS &&
            red > 7000 && blue > 7000 && mixed >= 64 && !bad &&
            draw_error == GL_NO_ERROR;
   if (!eglSwapBuffers(display, surface))
      passed = 0;

cleanup:
   if (made_current) {
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_STENCIL_TEST);
      glDepthMask(GL_TRUE);
      glStencilMask(0xff);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (msaa_fbo)
         glDeleteFramebuffers(1, &msaa_fbo);
      if (resolve_fbo)
         glDeleteFramebuffers(1, &resolve_fbo);
      if (msaa_color)
         glDeleteRenderbuffers(1, &msaa_color);
      if (msaa_depth)
         glDeleteRenderbuffers(1, &msaa_depth);
      if (single_depth)
         glDeleteRenderbuffers(1, &single_depth);
      if (resolve_color)
         glDeleteRenderbuffers(1, &resolve_color);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      if (program)
         glDeleteProgram(program);
      if (vertex_shader)
         glDeleteShader(vertex_shader);
      if (fragment_shader)
         glDeleteShader(fragment_shader);
      cleanup_error = glGetError();
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE,
                                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY) {
      cleanup_ok &= eglTerminate(display);
      cleanup_egl_error = eglGetError();
   }
   passed &= cleanup_ok && cleanup_error == GL_NO_ERROR &&
             cleanup_egl_error == EGL_SUCCESS;
   printf("[%s] egl=%d.%d gl=%d.%d max=%d "
          "status=%x/%x/%x depth=%d/%d/%d pixels=%u/%u/%u/%u error=%x "
          "cleanup=%u/%x/%x result=%d\n",
          TEST_TAG, egl_major, egl_minor, major, minor, max_samples,
          mismatch_status, msaa_status, resolve_status,
          depth_samples, depth_bits, stencil_bits,
          red, blue, mixed, bad, draw_error,
          cleanup_ok, cleanup_error, cleanup_egl_error,
          passed ? 0 : 1);
   return passed ? 0 : 1;
}
