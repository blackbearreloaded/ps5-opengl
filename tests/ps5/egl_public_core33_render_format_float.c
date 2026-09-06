#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 32
#define HEIGHT 32

struct format_case {
   const char *name;
   GLenum internal_format;
   unsigned channels;
   float tolerance;
   int signed_norm;
};

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
      printf("[ps5-egl-render-float] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static int
close_enough(float actual, float expected, float tolerance)
{
   float difference = actual - expected;

   if (difference < 0.0f)
      difference = -difference;
   return difference <= tolerance;
}

static int
check_pixel(const float actual[4], const float source[4],
            unsigned channels, float tolerance)
{
   for (unsigned channel = 0; channel < 4; ++channel) {
      float expected = channel < channels ? source[channel]
                                          : (channel == 3 ? 1.0f : 0.0f);

      if (!close_enough(actual[channel], expected, tolerance))
         return 0;
   }
   return 1;
}

static int
run_case(const struct format_case *test, GLuint program, GLint color_location,
         GLuint framebuffer, GLuint renderbuffer)
{
   static const float clear_color[4] = {0.25f, 0.5f, 0.75f, 1.0f};
   static const float masked_color[4] = {0.75f, 0.25f, 0.5f, 0.0f};
   static const float masked_expected[4] = {0.75f, 0.5f, 0.5f, 1.0f};
   static const float draw_color[4] = {0.125f, 0.375f, 0.625f,
                                       0.6666667f};
   static const float signed_color[4] = {-0.5f, 0.25f, -0.75f, 0.5f};
   static const float wide_color[4] = {100000.0f, -2.0f, 0.625f, 0.6666667f};
   const int wide_float = test->internal_format == GL_R32F ||
                         test->internal_format == GL_RG32F ||
                         test->internal_format == GL_RGBA32F;
   const float *draw = test->signed_norm ? signed_color :
                       wide_float ? wide_color : draw_color;
   float clear_pixel[4] = {0};
   float draw_pixel[4] = {0};
   GLenum status;
   GLenum error;
   int passed;

   glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
   glRenderbufferStorage(GL_RENDERBUFFER, test->internal_format,
                         WIDTH, HEIGHT);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, renderbuffer);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE) {
      printf("[ps5-egl-render-float] case=%s fbo=0x%x result=1\n",
             test->name, status);
      return 0;
   }

   glClearColor(clear_color[0], clear_color[1], clear_color[2],
                clear_color[3]);
   glClear(GL_COLOR_BUFFER_BIT);
   glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
   glClearColor(masked_color[0], masked_color[1], masked_color[2],
                masked_color[3]);
   glClear(GL_COLOR_BUFFER_BIT);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA, GL_FLOAT,
                clear_pixel);

   glUseProgram(program);
   glUniform4fv(color_location, 1, draw);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA, GL_FLOAT,
                draw_pixel);
   error = glGetError();
   passed = error == GL_NO_ERROR &&
            check_pixel(clear_pixel, masked_expected, test->channels,
                        test->tolerance) &&
            check_pixel(draw_pixel, draw, test->channels, test->tolerance);
   printf("[ps5-egl-render-float] case=%s clear=%.4f/%.4f/%.4f/%.4f "
          "draw=%.4f/%.4f/%.4f/%.4f error=0x%x result=%d\n",
          test->name, clear_pixel[0], clear_pixel[1], clear_pixel[2],
          clear_pixel[3], draw_pixel[0], draw_pixel[1], draw_pixel[2],
          draw_pixel[3], error, passed ? 0 : 1);
   return passed;
}

static int
run_mrt_clears(GLuint framebuffer)
{
   static const GLenum formats[3] = {GL_RGBA8, GL_RGBA16F, GL_RGBA32F};
   static const GLenum full[3] = {
      GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2,
   };
   static const GLenum sparse[3] = {
      GL_NONE, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT0,
   };
   static const float initial[4] = {0.125f, 0.5f, 0.875f, 1};
   static const float color[4] = {0.75f, 0.25f, 0.5f, 0.125f};
   float pixels[WIDTH * HEIGHT * 4];
   GLuint buffers[3] = {0};
   int passed = 1;

   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glGenRenderbuffers(3, buffers);
   for (unsigned i = 0; i < 3; ++i) {
      glBindRenderbuffer(GL_RENDERBUFFER, buffers[i]);
      glRenderbufferStorage(GL_RENDERBUFFER, formats[i], WIDTH, HEIGHT);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, full[i], GL_RENDERBUFFER,
                                buffers[i]);
   }
   glDrawBuffers(3, full);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      printf("[ps5-egl-render-float] mrt=incomplete result=1\n");
      passed = 0;
      goto cleanup;
   }
   for (unsigned phase = 0; phase < 3; ++phase) {
      unsigned mismatches = 0;
      glDisable(GL_SCISSOR_TEST);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glDrawBuffers(3, full);
      glClearBufferfv(GL_COLOR, 0, initial);
      glClearBufferfv(GL_COLOR, 1, initial);
      glClearBufferfv(GL_COLOR, 2, initial);
      glDrawBuffers(3, phase ? sparse : full);
      for (unsigned slot = 0; slot < 3; ++slot)
         glColorMaski(slot, !(slot & 1), slot & 1, !(slot & 1), slot & 1);
      if (phase) {
         glEnable(GL_SCISSOR_TEST);
         glScissor(8, 8, 16, 16);
      }
      if (phase == 2) {
         glClearBufferfv(GL_COLOR, 1, color);
      } else {
         glClearColor(color[0], color[1], color[2], color[3]);
         glClear(GL_COLOR_BUFFER_BIT);
      }
      for (unsigned attachment = 0; attachment < 3; ++attachment) {
         int slot = phase ? (attachment == 0 ? 2 : attachment == 2 ? 1 : -1)
                          : (int)attachment;
         glReadBuffer(full[attachment]);
         glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_FLOAT, pixels);
         for (unsigned y = 0; y < HEIGHT; ++y)
            for (unsigned x = 0; x < WIDTH; ++x)
               for (unsigned c = 0; c < 4; ++c) {
                  int inside = !phase || (x >= 8 && x < 24 && y >= 8 && y < 24);
                  int written = inside && slot >= 0 &&
                                (phase != 2 || slot == 1) &&
                                (c & 1) == ((unsigned)slot & 1);
                  float expected = written ? color[c] : initial[c];
                  mismatches += !close_enough(pixels[(y * WIDTH + x) * 4 + c],
                                              expected, 0.004f);
               }
      }
      GLenum error = glGetError();
      passed &= !mismatches && error == GL_NO_ERROR;
      printf("[ps5-egl-render-float] mrt-phase=%u mismatches=%u error=0x%x result=%d\n",
             phase, mismatches, error, !mismatches && error == GL_NO_ERROR ? 0 : 1);
   }
cleanup:
   glDisable(GL_SCISSOR_TEST);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glDrawBuffers(1, full);
   glReadBuffer(GL_COLOR_ATTACHMENT0);
   glDeleteRenderbuffers(3, buffers);
   return passed;
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
      "uniform vec4 source_color;\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){color=source_color;}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static const struct format_case cases[] = {
      {"rgba4", GL_RGBA4, 4, 0.07f, 0},
      {"rgb5-a1", GL_RGB5_A1, 4, 0.34f, 0},
      {"rgb565", GL_RGB565, 3, 0.04f, 0},
      {"r8-snorm", GL_R8_SNORM, 1, 0.012f, 1},
      {"rg8-snorm", GL_RG8_SNORM, 2, 0.012f, 1},
      {"rgba8-snorm", GL_RGBA8_SNORM, 4, 0.012f, 1},
      {"r16", GL_R16, 1, 0.0001f, 0},
      {"rg16", GL_RG16, 2, 0.0001f, 0},
      {"rgba16", GL_RGBA16, 4, 0.0001f, 0},
      {"r16-snorm", GL_R16_SNORM, 1, 0.0001f, 1},
      {"rg16-snorm", GL_RG16_SNORM, 2, 0.0001f, 1},
      {"rgba16-snorm", GL_RGBA16_SNORM, 4, 0.0001f, 1},
      {"r16f", GL_R16F, 1, 0.001f, 0},
      {"rg16f", GL_RG16F, 2, 0.001f, 0},
      {"rgba16f", GL_RGBA16F, 4, 0.001f, 0},
      {"r32f", GL_R32F, 1, 0.00001f, 0},
      {"rg32f", GL_RG32F, 2, 0.00001f, 0},
      {"rgba32f", GL_RGBA32F, 4, 0.00001f, 0},
      {"rgb10-a2", GL_RGB10_A2, 4, 0.003f, 0},
      {"r11g11b10f", GL_R11F_G11F_B10F, 3, 0.012f, 0},
   };
#ifdef PS5_FORMAT_HOST_REFERENCE
   const EGLint surface_type = EGL_PBUFFER_BIT;
#else
   const EGLint surface_type = EGL_WINDOW_BIT;
#endif
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, surface_type,
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
   GLuint framebuffer = 0, renderbuffer = 0;
   GLint linked = GL_FALSE, color_location = -1;
   unsigned matching = 0;
   int made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
#ifdef PS5_FORMAT_HOST_REFERENCE
   const EGLint pbuffer_attributes[] = {EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE};
   surface = eglCreatePbufferSurface(display, config, pbuffer_attributes);
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

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[1]))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   color_location = glGetUniformLocation(program, "source_color");
   if (!linked || color_location < 0)
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glGenFramebuffers(1, &framebuffer);
   glGenRenderbuffers(1, &renderbuffer);
   glViewport(0, 0, WIDTH, HEIGHT);
   glDisable(GL_BLEND);
   /* The default FIXED_ONLY read clamp would hide negative SNORM values. */
   glClampColor(GL_CLAMP_READ_COLOR, GL_FALSE);
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
      matching += run_case(&cases[i], program, color_location,
                           framebuffer, renderbuffer);

   /* Masked clears may legitimately add internal draws. Pixel oracles above
    * validate the public API without assuming a driver-internal draw count. */
   int mrt_passed = run_mrt_clears(framebuffer);
   passed = major == 1 && minor >= 4 && mrt_passed &&
            matching == sizeof(cases) / sizeof(cases[0]) &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-render-float] matching=%u result=%d\n",
          matching, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (renderbuffer)
         glDeleteRenderbuffers(1, &renderbuffer);
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
   printf("[ps5-egl-render-float] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
