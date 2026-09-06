#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define WIDTH 128
#define HEIGHT 128

#ifdef PS5_MSAA_SRGB_TEST
#define MSAA_COLOR_FORMAT GL_SRGB8_ALPHA8
#else
#define MSAA_COLOR_FORMAT GL_RGBA8
#endif

#if defined(PS5_MSAA_TEXTURE_TEST) || defined(PS5_MSAA_TEXTURE_FETCH_TEST) || \
    defined(PS5_MSAA_SRGB_TEST)
#define PS5_MSAA_TEXTURE_STORAGE_TEST 1
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
      printf("[ps5-egl-msaa4] shader=0x%x log=%.*s\n",
             type, length, log);
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
      printf("[ps5-egl-msaa4] link log=%.*s\n", length, log);
      glDeleteProgram(program);
      return -1;
   }
   *result = program;
   return 0;
}

static int
check_integer_sample_four(void)
{
   GLuint fbo = 0, texture = 0, renderbuffer = 0;
   GLint max_samples = 0, texture_samples = 0, renderbuffer_samples = 0;
   GLenum texture_status = 0, renderbuffer_status = 0;

   glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &max_samples);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, texture);
   glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8UI,
                           4, 4, GL_TRUE);
   glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE, 0,
                            GL_TEXTURE_SAMPLES, &texture_samples);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D_MULTISAMPLE, texture, 0);
   texture_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

   glGenRenderbuffers(1, &renderbuffer);
   glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8UI, 4, 4);
   glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES,
                                &renderbuffer_samples);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, renderbuffer);
   renderbuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   GLenum error = glGetError();

   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glDeleteFramebuffers(1, &fbo);
   glDeleteTextures(1, &texture);
   glDeleteRenderbuffers(1, &renderbuffer);
   printf("[ps5-egl-msaa4] integer max=%d samples=%d/%d status=%x/%x error=%x\n",
          max_samples, texture_samples, renderbuffer_samples,
          texture_status, renderbuffer_status, error);
   return max_samples >= 4 && texture_samples == 4 &&
          renderbuffer_samples == 4 &&
          texture_status == GL_FRAMEBUFFER_COMPLETE &&
          renderbuffer_status == GL_FRAMEBUFFER_COMPLETE &&
          error == GL_NO_ERROR ? 0 : -1;
}

#ifdef PS5_COLOR_TARGET_LIMITS_TEST
extern int sceKernelDebugOutText(int channel, const char *text);
int ps5_egl_current_draw_status(unsigned *draw_calls);

static const struct color_limit_case {
   GLenum format;
   unsigned width, height, samples, bytes_per_pixel;
} color_limit_cases[] = {
   {GL_RGBA8,   1920, 1080, 0,  4},
   {GL_RGBA8,   4096, 4096, 0,  4},
   {GL_RGBA8,   2048, 2048, 4,  4},
   {GL_R8,      2048, 2048, 4,  1},
   {GL_RG8,     2048, 2048, 4,  2},
   {GL_RGBA16F, 2048, 2048, 0,  8},
   {GL_RGBA16F, 1024, 1024, 4,  8},
   {GL_RGBA32F, 2048, 2048, 0, 16},
   {GL_RGBA32F, 1024, 1024, 4, 16},
   {GL_RGBA8,    128,  128, 0,  4}, /* Small-target recovery. */
};

static int
color_limit_pixel_matches(const float pixel[4], unsigned probe, unsigned samples)
{
   const float red = probe == 4 ? 0.0f : samples ? 0.25f : 1.0f;
   /* UNORM8 resolves to 64/255; floating-point targets resolve to 0.25. */
   return pixel[0] >= red - 0.002f && pixel[0] <= red + 0.002f &&
          pixel[1] == 0.0f && pixel[2] == 0.0f && pixel[3] == 1.0f;
}

static int
check_color_extent(const struct color_limit_case *test, int texture)
{
   const unsigned width = test->width, height = test->height;
   const unsigned samples = test->samples;
   const GLenum target = samples ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
   GLuint source = 0, resolve = 0, framebuffers[2] = {0, 0};
   GLenum status = 0, resolve_status = 0, error = GL_NO_ERROR;
   GLint actual_samples = 0;
   unsigned matches = 0, draws = 0;
   int draw_status = -1;
   const unsigned probes[5][2] = {
      {0, 0}, {width - 1, 0}, {0, height - 1},
      {width - 1, height - 1}, {width / 2, height / 2},
   };

   /* Bound each source, not total memory: staging, resolve and retained driver
    * references also consume storage. This is not an OOM stress test. */
   if (!width || !height || width > 8192 || height > 8192 ||
       (samples != 0 && samples != 4) ||
       (uint64_t)width * height * test->bytes_per_pixel *
          (samples ? samples : 1) > 64u * 1024u * 1024u)
      return 0;
   printf("[ps5-egl-color-limits] begin %s format=%x %ux%u samples=%u\n",
          texture ? "texture" : "renderbuffer", test->format, width, height, samples);
   glGenFramebuffers(2, framebuffers);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
   if (texture) {
      glGenTextures(1, &source);
      glBindTexture(target, source);
      if (samples) {
         glTexImage2DMultisample(target, samples, test->format, width, height, GL_TRUE);
         glGetTexLevelParameteriv(target, 0, GL_TEXTURE_SAMPLES, &actual_samples);
      } else {
         glTexImage2D(target, 0, test->format, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
      }
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, source, 0);
   } else {
      glGenRenderbuffers(1, &source);
      glBindRenderbuffer(GL_RENDERBUFFER, source);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, test->format, width, height);
      glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &actual_samples);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, source);
   }
   glDrawBuffer(GL_COLOR_ATTACHMENT0);
   glReadBuffer(GL_COLOR_ATTACHMENT0);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   error = glGetError();
   if (status != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR ||
       actual_samples != (GLint)samples)
      goto cleanup;
   if (samples) {
      glGenRenderbuffers(1, &resolve);
      glBindRenderbuffer(GL_RENDERBUFFER, resolve);
      glRenderbufferStorage(GL_RENDERBUFFER, test->format, width, height);
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[1]);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolve);
      resolve_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      error = glGetError();
      if (resolve_status != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR)
         goto cleanup;
   }

   glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[0]);
   glViewport(0, 0, width, height);
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_SAMPLE_MASK);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   if (samples) {
      glEnable(GL_SAMPLE_MASK);
      glSampleMaski(0, 1);
   }
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   draw_status = ps5_egl_current_draw_status(&draws);
   error = glGetError();
   if (draw_status != 0 || error != GL_NO_ERROR)
      goto cleanup;
   glDisable(GL_SAMPLE_MASK);
   glEnable(GL_SCISSOR_TEST);
   glScissor(width / 4, height / 4, width / 2, height / 2);
   glClear(GL_COLOR_BUFFER_BIT);
   glDisable(GL_SCISSOR_TEST);
   if (samples) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
      glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[1]);
   }
   glFinish();
   draw_status = ps5_egl_current_draw_status(&draws);
   error = glGetError();
   if (draw_status != 0 || error != GL_NO_ERROR)
      goto cleanup;
   for (unsigned i = 0; i < 5; ++i) {
      float pixel[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
      glReadPixels(probes[i][0], probes[i][1], 1, 1, GL_RGBA, GL_FLOAT, pixel);
      matches += color_limit_pixel_matches(pixel, i, samples);
      printf("[ps5-egl-color-limits] probe=%u rgba=%g,%g,%g,%g\n",
             i, pixel[0], pixel[1], pixel[2], pixel[3]);
   }
   error = glGetError();

cleanup:
   glDisable(GL_SCISSOR_TEST);
   glDisable(GL_SAMPLE_MASK);
   glSampleMaski(0, ~0u);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindTexture(target, 0);
   glBindRenderbuffer(GL_RENDERBUFFER, 0);
   glDeleteFramebuffers(2, framebuffers);
   if (texture)
      glDeleteTextures(1, &source);
   else
      glDeleteRenderbuffers(1, &source);
   glDeleteRenderbuffers(1, &resolve);
   GLenum cleanup_error = glGetError();
   printf("[ps5-egl-color-limits] %s format=%x %ux%u samples=%u/%d "
          "status=%x/%x draw=%d/%u probes=%u/5 error=%x cleanup=%x\n",
          texture ? "texture" : "renderbuffer", test->format, width, height,
          samples, actual_samples, status, resolve_status, draw_status, draws,
          matches, error, cleanup_error);
   return matches == 5 && draw_status == 0 &&
          error == GL_NO_ERROR && cleanup_error == GL_NO_ERROR;
}
#endif

int
main(void)
{
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { color = vec4(1.0, 0.0, 0.0, 1.0); }\n";
#ifdef PS5_MSAA_TEXTURE_FETCH_TEST
   static const char *fetch_fragment_source =
      "#version 330\n"
      "uniform sampler2DMS source;\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() {\n"
      "  ivec2 p = ivec2(gl_FragCoord.xy);\n"
      "  color = vec4(texelFetch(source, p, 0).r,\n"
      "               texelFetch(source, p, 1).r,\n"
      "               texelFetch(source, p, 2).r,\n"
      "               texelFetch(source, p, 3).r);\n"
      "}\n";
#endif
#if defined(PS5_MSAA_SAMPLE_MASK_TEST) || \
    defined(PS5_MSAA_TEXTURE_FETCH_TEST)
   static const float vertices[] = {
      -1.0f, -1.0f,
       3.0f, -1.0f,
      -1.0f,  3.0f,
   };
#else
   static const float vertices[] = {
      -1.0f, -1.0f,
       1.0f, -1.0f,
      -1.0f,  1.0f,
   };
#endif
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
#ifdef PS5_MSAA_TEXTURE_FETCH_TEST
   GLuint fetch_fragment_shader = 0, fetch_program = 0;
#endif
   GLuint vao = 0, vbo = 0, msaa_fbo = 0, resolve_fbo = 0;
   GLuint msaa_color = 0, resolve_color = 0;
   GLenum msaa_status = 0, resolve_status = 0;
   GLenum draw_error = GL_NO_ERROR, cleanup_error = GL_NO_ERROR;
   EGLBoolean cleanup_ok = EGL_TRUE;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   GLint major = 0, minor = 0, max_samples = 0;
#ifdef PS5_MSAA_TEXTURE_STORAGE_TEST
   GLint texture_samples = 0, fixed_locations = 0;
#endif
   unsigned full = 0, empty = 0, partial = 0, bad = 0;
   int made_current = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1,
                        &config_count) ||
       config_count != 1)
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
       check_integer_sample_four() ||
       compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex_shader) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source,
                      &fragment_shader) ||
       link_program(vertex_shader, fragment_shader, &program))
      goto cleanup;
#ifdef PS5_MSAA_TEXTURE_FETCH_TEST
   if (compile_shader(GL_FRAGMENT_SHADER, fetch_fragment_source,
                      &fetch_fragment_shader) ||
       link_program(vertex_shader, fetch_fragment_shader, &fetch_program))
      goto cleanup;
#endif

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

#ifdef PS5_MSAA_TEXTURE_STORAGE_TEST
   glGenTextures(1, &msaa_color);
   glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msaa_color);
   glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, MSAA_COLOR_FORMAT,
                           WIDTH, HEIGHT, GL_TRUE);
   glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE, 0,
                            GL_TEXTURE_SAMPLES, &texture_samples);
   glGetTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE, 0,
                            GL_TEXTURE_FIXED_SAMPLE_LOCATIONS,
                            &fixed_locations);
   printf("[ps5-egl-msaa4] texture=2dms samples=%d fixed=%d\n",
          texture_samples, fixed_locations);
#else
   glGenRenderbuffers(1, &msaa_color);
   glBindRenderbuffer(GL_RENDERBUFFER, msaa_color);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, MSAA_COLOR_FORMAT,
                                    WIDTH, HEIGHT);
#endif
   glGenFramebuffers(1, &msaa_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
#ifdef PS5_MSAA_TEXTURE_STORAGE_TEST
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D_MULTISAMPLE, msaa_color, 0);
#else
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, msaa_color);
#endif
   msaa_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

   glGenRenderbuffers(1, &resolve_color);
   glBindRenderbuffer(GL_RENDERBUFFER, resolve_color);
   glRenderbufferStorage(GL_RENDERBUFFER, MSAA_COLOR_FORMAT, WIDTH, HEIGHT);
   glGenFramebuffers(1, &resolve_fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, resolve_color);
   resolve_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (msaa_status != GL_FRAMEBUFFER_COMPLETE ||
       resolve_status != GL_FRAMEBUFFER_COMPLETE ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;

   glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
   glViewport(0, 0, WIDTH, HEIGHT);
   glEnable(GL_MULTISAMPLE);
#ifdef PS5_MSAA_SRGB_TEST
   glDisable(GL_FRAMEBUFFER_SRGB);
#endif
#if defined(PS5_MSAA_SAMPLE_MASK_TEST) || \
    defined(PS5_MSAA_TEXTURE_FETCH_TEST)
   glEnable(GL_SAMPLE_MASK);
   glSampleMaski(0, 1);
#endif
   glDisable(GL_BLEND);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_SCISSOR_TEST);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glUseProgram(program);
   glDrawArrays(GL_TRIANGLES, 0, 3);

#ifdef PS5_MSAA_TEXTURE_FETCH_TEST
   glDisable(GL_SAMPLE_MASK);
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msaa_color);
   glUseProgram(fetch_program);
   glUniform1i(glGetUniformLocation(fetch_program, "source"), 0);
   glDrawArrays(GL_TRIANGLES, 0, 3);
#else
   glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
   glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT,
                     GL_COLOR_BUFFER_BIT, GL_NEAREST);
#endif
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glFinish();
   glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   draw_error = glGetError();

   for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
      uint8_t *pixel = (uint8_t *)&pixels[i];

#ifdef PS5_MSAA_TEXTURE_FETCH_TEST
      if (pixel[0] == 255 && !pixel[1] && !pixel[2] && !pixel[3])
         full++;
      else
         bad++;
#else
      if (pixel[1] || pixel[2] || pixel[3] != 255) {
         bad++;
#ifdef PS5_MSAA_SAMPLE_MASK_TEST
      } else if (pixel[0] == 64) {
         partial++;
      } else {
         bad++;
#else
      } else if (pixel[0] == 255) {
         full++;
      } else if (pixel[0] == 0) {
         empty++;
      } else {
         partial++;
#endif
      }
#endif
   }
#ifdef PS5_MSAA_SAMPLE_MASK_TEST
   passed = egl_major == 1 && egl_minor == 4 && max_samples >= 4 &&
            msaa_status == GL_FRAMEBUFFER_COMPLETE &&
            resolve_status == GL_FRAMEBUFFER_COMPLETE &&
            !full && !empty && partial == WIDTH * HEIGHT && !bad &&
            draw_error == GL_NO_ERROR;
#elif defined(PS5_MSAA_TEXTURE_FETCH_TEST)
   passed = egl_major == 1 && egl_minor == 4 && max_samples >= 4 &&
            texture_samples == 4 && fixed_locations == GL_TRUE &&
            msaa_status == GL_FRAMEBUFFER_COMPLETE &&
            resolve_status == GL_FRAMEBUFFER_COMPLETE &&
            full == WIDTH * HEIGHT && !empty && !partial && !bad &&
            draw_error == GL_NO_ERROR;
#elif defined(PS5_MSAA_TEXTURE_TEST)
   passed = egl_major == 1 && egl_minor == 4 && max_samples >= 4 &&
            texture_samples == 4 && fixed_locations == GL_TRUE &&
            msaa_status == GL_FRAMEBUFFER_COMPLETE &&
            resolve_status == GL_FRAMEBUFFER_COMPLETE &&
            full > 7000 && empty > 7000 && partial >= 64 && !bad &&
            draw_error == GL_NO_ERROR;
#else
   passed = egl_major == 1 && egl_minor == 4 && max_samples >= 4 &&
            msaa_status == GL_FRAMEBUFFER_COMPLETE &&
            resolve_status == GL_FRAMEBUFFER_COMPLETE &&
            full > 7000 && empty > 7000 && partial >= 64 && !bad &&
            draw_error == GL_NO_ERROR;
#endif
   if (!eglSwapBuffers(display, surface))
      passed = 0;
#ifdef PS5_COLOR_TARGET_LIMITS_TEST
   if (passed) {
      static const float fullscreen[] = {-1, -1, 3, -1, -1, 3};
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(fullscreen), fullscreen);
      unsigned successes = 0;
      for (unsigned i = 0; i < sizeof(color_limit_cases) / sizeof(color_limit_cases[0]); ++i) {
         for (int texture = 0; texture < 2; ++texture) {
            if (!check_color_extent(&color_limit_cases[i], texture)) {
               passed = 0;
               goto cleanup;
            }
            ++successes;
         }
      }
      printf("[ps5-egl-color-limits] batch=%u/20\n", successes);
      passed = successes == 20;
   }
#endif

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (msaa_fbo)
         glDeleteFramebuffers(1, &msaa_fbo);
      if (resolve_fbo)
         glDeleteFramebuffers(1, &resolve_fbo);
      if (msaa_color)
#ifdef PS5_MSAA_TEXTURE_STORAGE_TEST
         glDeleteTextures(1, &msaa_color);
#else
         glDeleteRenderbuffers(1, &msaa_color);
#endif
      if (resolve_color)
         glDeleteRenderbuffers(1, &resolve_color);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      if (program)
         glDeleteProgram(program);
#ifdef PS5_MSAA_TEXTURE_FETCH_TEST
      if (fetch_program)
         glDeleteProgram(fetch_program);
      if (fetch_fragment_shader)
         glDeleteShader(fetch_fragment_shader);
#endif
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
   printf("[ps5-egl-msaa4] egl=%d.%d gl=%d.%d max=%d "
          "status=%x/%x pixels=%u/%u/%u/%u error=%x "
          "cleanup=%u/%x/%x result=%d\n",
          egl_major, egl_minor, major, minor, max_samples,
          msaa_status, resolve_status, full, empty, partial, bad,
          draw_error, cleanup_ok, cleanup_error, cleanup_egl_error,
          passed ? 0 : 1);
#ifdef PS5_COLOR_TARGET_LIMITS_TEST
   sceKernelDebugOutText(0, "[ps5-egl-color-limits] finished\n");
#endif
   return passed ? 0 : 1;
}
