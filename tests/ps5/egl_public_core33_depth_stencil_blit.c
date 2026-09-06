#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#ifdef PS5_DEPTH_STENCIL_SCALED_TEST
#define TEST_TAG "ps5-egl-depth-stencil-blit-scaled"
#define SRC_X 128
#define SRC_Y 192
#define SRC_WIDTH 256
#define SRC_HEIGHT 256
#define DST_X 640
#define DST_Y 320
#define READ_WIDTH 128
#define READ_HEIGHT 128
#else
#define TEST_TAG "ps5-egl-depth-stencil-blit"
#define SIZE 64
#define READ_WIDTH SIZE
#define READ_HEIGHT SIZE
#endif
#define PIXEL_COUNT (READ_WIDTH * READ_HEIGHT)
#define BLACK UINT32_C(0xff000000)
#define GREEN UINT32_C(0xff00ff00)
#define MAGENTA UINT32_C(0xffff00ff)
#ifndef PS5_DEPTH_STENCIL_SCALED_TEST
#define GREEN_HASH UINT32_C(0xc38d1dc5)
#define MAGENTA_HASH UINT32_C(0x64e31dc5)
#endif

int ps5_egl_current_draw_status(unsigned *draw_calls);

static uint32_t
hash32(const void *data, size_t size)
{
   const uint8_t *bytes = data;
   uint32_t hash = UINT32_C(2166136261);

   while (size--)
      hash = (hash ^ *bytes++) * UINT32_C(16777619);
   return hash;
}

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
      printf("[%s] shader=0x%x log=%.*s\n", TEST_TAG, type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static unsigned
matching_pixels(const uint32_t *pixels, uint32_t expected)
{
   unsigned matching = 0;

   for (unsigned i = 0; i < PIXEL_COUNT; ++i)
      matching += pixels[i] == expected;
   return matching;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "uniform float depth;\n"
      "void main() { gl_Position = vec4(position, depth, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform vec4 color;\n"
      "layout(location=0) out vec4 output_color;\n"
      "void main() { output_color = color; }\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static uint32_t pixels[PIXEL_COUNT];
#ifdef PS5_DEPTH_STENCIL_SCALED_TEST
   static uint32_t expected[PIXEL_COUNT];
#endif
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
   GLuint shaders[2] = {0, 0};
   GLuint program = 0, vertex_array = 0, vertex_buffer = 0;
   GLuint framebuffers[2] = {0, 0};
   GLuint colors[2] = {0, 0};
   GLuint depth_stencils[2] = {0, 0};
   GLint linked = GL_FALSE, depth_location = -1, color_location = -1;
   GLint internal_format = 0, depth_bits = 0, stencil_bits = 0;
   GLenum status[2] = {0, 0};
   GLenum error = GL_NO_ERROR;
   EGLBoolean cleanup_ok = EGL_TRUE;
   unsigned draw_calls = 0, depth_matching = 0, stencil_matching = 0;
   int draw_status[4] = {-1, -1, -1, -1};
   int made_current = 0, passed = 0;

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
   if (!linked)
      goto cleanup;
   glUseProgram(program);
   depth_location = glGetUniformLocation(program, "depth");
   color_location = glGetUniformLocation(program, "color");
   if (depth_location < 0 || color_location < 0)
      goto cleanup;

   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
   glGenBuffers(1, &vertex_buffer);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenFramebuffers(2, framebuffers);
   glGenRenderbuffers(2, colors);
   glGenRenderbuffers(2, depth_stencils);
   for (unsigned i = 0; i < 2; ++i) {
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[i]);
      glBindRenderbuffer(GL_RENDERBUFFER, colors[i]);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, colors[i]);
      glBindRenderbuffer(GL_RENDERBUFFER, depth_stencils[i]);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH32F_STENCIL8,
                            WIDTH, HEIGHT);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                GL_RENDERBUFFER, depth_stencils[i]);
      status[i] = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   }
   glBindRenderbuffer(GL_RENDERBUFFER, depth_stencils[1]);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                GL_RENDERBUFFER_INTERNAL_FORMAT,
                                &internal_format);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                GL_RENDERBUFFER_DEPTH_SIZE, &depth_bits);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER,
                                GL_RENDERBUFFER_STENCIL_SIZE, &stencil_bits);
   error = glGetError();
   if (status[0] != GL_FRAMEBUFFER_COMPLETE ||
       status[1] != GL_FRAMEBUFFER_COMPLETE ||
       internal_format != GL_DEPTH32F_STENCIL8 || depth_bits != 32 ||
       stencil_bits != 8 || error != GL_NO_ERROR)
      goto cleanup;

#ifdef PS5_DEPTH_STENCIL_SCALED_TEST
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
   glViewport(0, 0, WIDTH, HEIGHT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_ALWAYS);
   glDepthMask(GL_TRUE);
   glEnable(GL_STENCIL_TEST);
   glStencilMask(0xff);
   glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
   glClearDepth(0.0);
   glClearStencil(0);
   glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
   glEnable(GL_SCISSOR_TEST);

   glScissor(SRC_X, SRC_Y, SRC_WIDTH / 2, SRC_HEIGHT);
   glStencilFunc(GL_ALWAYS, 0x2a, 0xff);
   glUniform1f(depth_location, -0.5f);
   glUniform4f(color_location, 1.0f, 0.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[0] = ps5_egl_current_draw_status(&draw_calls);

   glScissor(SRC_X + SRC_WIDTH / 2, SRC_Y, SRC_WIDTH / 2, SRC_HEIGHT);
   glStencilFunc(GL_ALWAYS, 0x5a, 0xff);
   glUniform1f(depth_location, 0.5f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[1] = ps5_egl_current_draw_status(&draw_calls);
   glDisable(GL_SCISSOR_TEST);
   glFinish();

   glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[1]);
   glDepthMask(GL_TRUE);
   glStencilMask(0xff);
   glClearDepth(0.0);
   glClearStencil(0);
   glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
   glBlitFramebuffer(SRC_X, SRC_Y, SRC_X + SRC_WIDTH,
                     SRC_Y + SRC_HEIGHT, DST_X, DST_Y,
                     DST_X + READ_WIDTH, DST_Y + READ_HEIGHT,
                     GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                     GL_NEAREST);

   glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[1]);
   glViewport(DST_X, DST_Y, READ_WIDTH, READ_HEIGHT);
   glDisable(GL_STENCIL_TEST);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glDepthMask(GL_FALSE);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glUniform1f(depth_location, 0.0f);
   glUniform4f(color_location, 0.0f, 1.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[2] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels(DST_X, DST_Y, READ_WIDTH, READ_HEIGHT,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   for (unsigned y = 0; y < READ_HEIGHT; ++y) {
      for (unsigned x = 0; x < READ_WIDTH; ++x)
         expected[y * READ_WIDTH + x] = x < READ_WIDTH / 2 ? BLACK : GREEN;
   }
   depth_matching = matching_pixels(pixels, GREEN);
   const uint32_t depth_hash = hash32(pixels, sizeof(pixels));
   const uint32_t expected_depth_hash = hash32(expected, sizeof(expected));
   const int depth_equal = memcmp(pixels, expected, sizeof(expected)) == 0;

   glDisable(GL_DEPTH_TEST);
   glEnable(GL_STENCIL_TEST);
   glStencilMask(0x00);
   glStencilFunc(GL_EQUAL, 0x2a, 0xff);
   glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
   glClear(GL_COLOR_BUFFER_BIT);
   glUniform4f(color_location, 1.0f, 0.0f, 1.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[3] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels(DST_X, DST_Y, READ_WIDTH, READ_HEIGHT,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   for (unsigned y = 0; y < READ_HEIGHT; ++y) {
      for (unsigned x = 0; x < READ_WIDTH; ++x)
         expected[y * READ_WIDTH + x] = x < READ_WIDTH / 2 ? MAGENTA : BLACK;
   }
   stencil_matching = matching_pixels(pixels, MAGENTA);
   const uint32_t stencil_hash = hash32(pixels, sizeof(pixels));
   const uint32_t expected_stencil_hash = hash32(expected, sizeof(expected));
   const int stencil_equal = memcmp(pixels, expected, sizeof(expected)) == 0;
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            draw_status[0] == 0 && draw_status[1] == 0 &&
            draw_status[2] == 0 && draw_status[3] == 0 && draw_calls == 4 &&
            depth_equal && depth_hash == expected_depth_hash &&
            stencil_equal && stencil_hash == expected_stencil_hash &&
            error == GL_NO_ERROR;
   printf("[%s] status=%x/%x format=0x%x bits=%d/%d "
          "draw=%d/%d/%d/%d/%u depth-green=%u/%u hash=%08x/%08x "
          "stencil-magenta=%u/%u hash=%08x/%08x error=0x%x result=%d\n",
          TEST_TAG, status[0], status[1], internal_format, depth_bits,
          stencil_bits, draw_status[0], draw_status[1], draw_status[2],
          draw_status[3], draw_calls, depth_matching, PIXEL_COUNT / 2,
          depth_hash, expected_depth_hash, stencil_matching, PIXEL_COUNT / 2,
          stencil_hash, expected_stencil_hash, error, passed ? 0 : 1);
#else
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
   glViewport(0, 0, WIDTH, HEIGHT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_ALWAYS);
   glDepthMask(GL_TRUE);
   glEnable(GL_STENCIL_TEST);
   glStencilMask(0xff);
   glStencilFunc(GL_ALWAYS, 0x5a, 0xff);
   glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
   glUniform1f(depth_location, 0.5f);
   glUniform4f(color_location, 1.0f, 0.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[0] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();

   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
   glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT,
                     GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                     GL_NEAREST);

   glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[1]);
   glDisable(GL_STENCIL_TEST);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glDepthMask(GL_FALSE);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glUniform1f(depth_location, 0.0f);
   glUniform4f(color_location, 0.0f, 1.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[1] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   depth_matching = matching_pixels(pixels, GREEN);
   const uint32_t depth_hash = hash32(pixels, sizeof(pixels));

   glDisable(GL_DEPTH_TEST);
   glEnable(GL_STENCIL_TEST);
   glStencilMask(0x00);
   glStencilFunc(GL_EQUAL, 0x5a, 0xff);
   glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
   glClear(GL_COLOR_BUFFER_BIT);
   glUniform4f(color_location, 1.0f, 0.0f, 1.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[2] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   stencil_matching = matching_pixels(pixels, MAGENTA);
   const uint32_t stencil_hash = hash32(pixels, sizeof(pixels));
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            draw_status[0] == 0 && draw_status[1] == 0 &&
            draw_status[2] == 0 && draw_calls == 3 &&
            depth_matching == SIZE * SIZE && depth_hash == GREEN_HASH &&
            stencil_matching == SIZE * SIZE &&
            stencil_hash == MAGENTA_HASH && error == GL_NO_ERROR;
   printf("[%s] status=%x/%x format=0x%x "
          "bits=%d/%d draw=%d/%d/%d/%u depth=%u/%08x "
          "stencil=%u/%08x error=0x%x result=%d\n",
          TEST_TAG, status[0], status[1], internal_format, depth_bits,
          stencil_bits,
          draw_status[0], draw_status[1], draw_status[2], draw_calls,
          depth_matching, depth_hash, stencil_matching, stencil_hash,
          error, passed ? 0 : 1);
#endif

cleanup:
   if (made_current) {
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_STENCIL_TEST);
      glDisable(GL_SCISSOR_TEST);
      glDepthMask(GL_TRUE);
      glStencilMask(0xff);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (depth_stencils[0] || depth_stencils[1])
         glDeleteRenderbuffers(2, depth_stencils);
      if (colors[0] || colors[1])
         glDeleteRenderbuffers(2, colors);
      if (framebuffers[0] || framebuffers[1])
         glDeleteFramebuffers(2, framebuffers);
      if (vertex_buffer)
         glDeleteBuffers(1, &vertex_buffer);
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
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
   printf("[%s] cleanup=%u result=%d\n", TEST_TAG, cleanup_ok,
          passed ? 0 : 1);
   return passed ? 0 : 1;
}
