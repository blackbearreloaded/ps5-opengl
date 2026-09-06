#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#define TARGET_WIDTH 64
#define TARGET_HEIGHT 64
#define CROP_SIZE 64
#define R_PIXEL UINT32_C(0xff000040)
#define R_HASH UINT32_C(0x4bc31dc5)
#define RG_PIXEL UINT32_C(0xff008040)
#define RG_HASH UINT32_C(0xebc31dc5)

int ps5_egl_current_draw_status(unsigned *draw_calls);

static int
has_extension(const char *extensions, const char *name)
{
   const size_t length = strlen(name);
   const char *match = extensions;

   while (match && (match = strstr(match, name))) {
      if ((match == extensions || match[-1] == ' ') &&
          (match[length] == '\0' || match[length] == ' '))
         return 1;
      match += length;
   }
   return 0;
}

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
      printf("[ps5-egl-rg-render] shader=0x%x log=%.*s\n",
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
   glBindAttribLocation(program, 0, "a_position");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      glDeleteProgram(program);
      return -1;
   }
   *result = program;
   return 0;
}

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
run_case(GLenum internal_format, GLenum upload_format, const char *name,
         GLuint producer, GLuint consumer, GLint sampler,
         uint32_t expected_pixel, uint32_t expected_hash,
         uint32_t *pixels)
{
   GLuint texture = 0;
   GLuint framebuffer = 0;
   GLenum status = 0;
   GLenum error;
   uint32_t hash = 0;
   unsigned matching = 0;
   unsigned producer_calls = 0;
   unsigned consumer_calls = 0;
   int producer_status = -100;
   int consumer_status = -100;
   int passed;

   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, internal_format,
                TARGET_WIDTH, TARGET_HEIGHT, 0, upload_format,
                GL_UNSIGNED_BYTE, NULL);

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   error = glGetError();
   if (status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR) {
      glViewport(0, 0, TARGET_WIDTH, TARGET_HEIGHT);
      glUseProgram(producer);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      producer_status = ps5_egl_current_draw_status(&producer_calls);

      /* This transition must be implemented by the native GPU barrier. */
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      glUseProgram(consumer);
      glUniform1i(sampler, 0);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      consumer_status = ps5_egl_current_draw_status(&consumer_calls);
      glFinish();
      glReadPixels((DISPLAY_WIDTH - CROP_SIZE) / 2,
                   (DISPLAY_HEIGHT - CROP_SIZE) / 2,
                   CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      error = glGetError();
      hash = hash32(pixels, CROP_SIZE * CROP_SIZE * sizeof(*pixels));
      for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
         matching += pixels[i] == expected_pixel;
   }
   passed = status == GL_FRAMEBUFFER_COMPLETE && producer_status == 0 &&
            consumer_status == 0 && error == GL_NO_ERROR &&
            matching == CROP_SIZE * CROP_SIZE && hash == expected_hash;
   printf("[ps5-egl-rg-render] %s status=0x%x matching=%u hash=%08x "
          "error=0x%x draw=%d/%u,%d/%u result=%d\n",
          name, status, matching, hash, error,
          producer_status, producer_calls, consumer_status, consumer_calls,
          passed ? 0 : 1);

   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   if (framebuffer)
      glDeleteFramebuffers(1, &framebuffer);
   if (texture)
      glDeleteTextures(1, &texture);
   return passed;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *producer_source =
      "#version 120\n"
      "void main() {\n"
      "  gl_FragColor = vec4(0.2509803922, 0.5019607843, 0.0, 1.0);\n"
      "}\n";
   static const char *consumer_source =
      "#version 120\n"
      "uniform sampler2D u_texture;\n"
      "void main() { gl_FragColor = texture2D(u_texture, vec2(0.5)); }\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static uint32_t pixels[CROP_SIZE * CROP_SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0, width = 0, height = 0;
   GLuint vs = 0, producer_fs = 0, consumer_fs = 0;
   GLuint producer = 0, consumer = 0, vbo = 0;
   GLint sampler = -1;
   const GLubyte *version = NULL;
   const char *extensions = NULL;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0;
   int fbo_extension = 0, rg_extension = 0;
   int r_ok = 0, rg_ok = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;
   eglQuerySurface(display, surface, EGL_WIDTH, &width);
   eglQuerySurface(display, surface, EGL_HEIGHT, &height);
   version = glGetString(GL_VERSION);
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!version || !extensions)
      goto cleanup;
   fbo_extension = has_extension(extensions, "GL_ARB_framebuffer_object") ||
                   has_extension(extensions, "GL_EXT_framebuffer_object");
   rg_extension = has_extension(extensions, "GL_ARB_texture_rg");

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, producer_source, &producer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, consumer_source, &consumer_fs) ||
       link_program(vs, producer_fs, &producer) ||
       link_program(vs, consumer_fs, &consumer))
      goto cleanup;
   sampler = glGetUniformLocation(consumer, "u_texture");
   if (sampler < 0)
      goto cleanup;

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   r_ok = run_case(GL_R8, GL_RED, "r8", producer, consumer, sampler,
                   R_PIXEL, R_HASH, pixels);
   rg_ok = run_case(GL_RG8, GL_RG, "rg8", producer, consumer, sampler,
                    RG_PIXEL, RG_HASH, pixels);
   passed = egl_major == 1 && egl_minor == 4 &&
            width == DISPLAY_WIDTH && height == DISPLAY_HEIGHT &&
            fbo_extension && rg_extension && r_ok && rg_ok &&
            strncmp((const char *)version, "2.1 ", 4) == 0 &&
            eglSwapBuffers(display, surface);

cleanup:
   if (vbo)
      glDeleteBuffers(1, &vbo);
   if (producer)
      glDeleteProgram(producer);
   if (consumer)
      glDeleteProgram(consumer);
   if (producer_fs)
      glDeleteShader(producer_fs);
   if (consumer_fs)
      glDeleteShader(consumer_fs);
   if (vs)
      glDeleteShader(vs);
   if (made_current) {
      cleanup_gl_error = glGetError();
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
   passed &= cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
             cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-rg-render] egl=%d.%d size=%dx%d ext=%d/%d cases=%d/%d "
          "cleanup=%u/0x%x/0x%x result=%d\n",
          egl_major, egl_minor, width, height, fbo_extension, rg_extension,
          r_ok, rg_ok, cleanup_ok, cleanup_gl_error, cleanup_egl_error,
          passed ? 0 : 1);
   return passed ? 0 : 1;
}
