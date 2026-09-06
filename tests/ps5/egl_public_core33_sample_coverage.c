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
      printf("[ps5-egl-sample-coverage] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static int
draw_and_resolve(GLuint msaa_fbo, GLuint resolve_fbo, uint8_t *red)
{
   uint32_t pixel = 0;

   glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
   glBlitFramebuffer(0, 0, WIDTH, HEIGHT, 0, 0, WIDTH, HEIGHT,
                     GL_COLOR_BUFFER_BIT, GL_NEAREST);
   glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
   glFinish();
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1, GL_RGBA,
                GL_UNSIGNED_BYTE, &pixel);
   *red = (uint8_t)pixel;
   return glGetError() == GL_NO_ERROR;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position=vec4(position,0,1); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color=vec4(1,0,0,1); }\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
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
   GLuint shaders[2] = {0, 0}, program = 0, vao = 0, vbo = 0;
   GLuint fbos[2] = {0, 0}, buffers[2] = {0, 0};
   GLint linked = GL_FALSE, max_samples = 0;
   uint8_t full = 0, quarter = 0, inverted = 0;
   uint8_t intersection = 0, explicit_mask = 0;
   GLenum error = GL_NO_ERROR;
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

   glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
   if (max_samples < 4 ||
       !compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
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

   glGenRenderbuffers(2, buffers);
   glBindRenderbuffer(GL_RENDERBUFFER, buffers[0]);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8,
                                    WIDTH, HEIGHT);
   glBindRenderbuffer(GL_RENDERBUFFER, buffers[1]);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
   glGenFramebuffers(2, fbos);
   glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, buffers[0]);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      goto cleanup;
   glBindFramebuffer(GL_FRAMEBUFFER, fbos[1]);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, buffers[1]);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glUseProgram(program);
   glEnable(GL_MULTISAMPLE);
   glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
   glDisable(GL_SAMPLE_ALPHA_TO_ONE);
   glDisable(GL_SAMPLE_COVERAGE);
   glDisable(GL_SAMPLE_MASK);
   if (!draw_and_resolve(fbos[0], fbos[1], &full))
      goto cleanup;

   glEnable(GL_SAMPLE_COVERAGE);
   glSampleCoverage(0.25f, GL_FALSE);
   if (!draw_and_resolve(fbos[0], fbos[1], &quarter))
      goto cleanup;
   glSampleCoverage(0.25f, GL_TRUE);
   if (!draw_and_resolve(fbos[0], fbos[1], &inverted))
      goto cleanup;

   glSampleCoverage(0.75f, GL_FALSE);
   glEnable(GL_SAMPLE_MASK);
   glSampleMaski(0, 0x5u);
   if (!draw_and_resolve(fbos[0], fbos[1], &intersection))
      goto cleanup;

   glDisable(GL_SAMPLE_COVERAGE);
   glSampleMaski(0, 0x2u);
   if (!draw_and_resolve(fbos[0], fbos[1], &explicit_mask))
      goto cleanup;

   draw_status = ps5_egl_current_draw_status(&draw_calls);
   error = glGetError();
   passed = major == 1 && minor == 4 && max_samples >= 4 &&
            full == 255 && quarter >= 63 && quarter <= 64 &&
            inverted >= 191 && inverted <= 192 &&
            intersection >= 127 && intersection <= 128 &&
            explicit_mask >= 63 && explicit_mask <= 64 &&
            draw_status == 0 && draw_calls == 5 && error == GL_NO_ERROR;
   printf("[ps5-egl-sample-coverage] max=%d values=%u,%u,%u,%u,%u "
          "draw=%d/%u error=0x%x result=%d\n",
          max_samples, full, quarter, inverted, intersection, explicit_mask,
          draw_status, draw_calls, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glDeleteFramebuffers(2, fbos);
      glDeleteRenderbuffers(2, buffers);
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
   printf("[ps5-egl-sample-coverage] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
