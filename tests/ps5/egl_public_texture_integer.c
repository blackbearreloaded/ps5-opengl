#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#define TARGET_SIZE 64
#define UPLOAD_SIZE 128
#define CROP_SIZE 64
#define UINT_PIXEL UINT32_C(0xffff00ff)
#define UINT_HASH UINT32_C(0x64e31dc5)
#define SINT_PIXEL UINT32_C(0xff00ff00)
#define SINT_HASH UINT32_C(0xc38d1dc5)

int ps5_egl_current_draw_status(unsigned *draw_calls);

struct case_result {
   GLenum internal_format;
   GLenum framebuffer_status;
   GLenum setup_error;
   GLenum bounds_error;
   int producer_status;
   int consumer_status;
   unsigned producer_calls;
   unsigned consumer_calls;
   unsigned matching;
   uint32_t hash;
};

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
      printf("[ps5-egl-texture-integer] shader=0x%x log=%.*s\n",
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
      char log[512];
      GLsizei length = 0;

      glGetProgramInfoLog(program, sizeof(log), &length, log);
      printf("[ps5-egl-texture-integer] link=%.*s\n", length, log);
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
run_case(GLenum internal_format, GLenum type, GLuint producer,
         GLuint consumer, uint32_t expected_pixel, uint32_t expected_hash,
         uint32_t *pixels, struct case_result *result)
{
   GLuint texture = 0, framebuffer = 0;
   GLint actual_format = 0;
   GLint sampler = glGetUniformLocation(consumer, "u_texture");
   int passed = 0;

   memset(result, 0, sizeof(*result));
   result->internal_format = internal_format;
   result->producer_status = -100;
   result->consumer_status = -100;
   if (sampler < 0)
      return 0;

   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, internal_format, TARGET_SIZE, TARGET_SIZE,
                0, GL_RGBA_INTEGER, type, NULL);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                            &actual_format);
   result->setup_error = glGetError();
   glTexSubImage2D(GL_TEXTURE_2D, 0, TARGET_SIZE, 0, 1, 1,
                   GL_RGBA_INTEGER, type, NULL);
   result->bounds_error = glGetError();

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   result->framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (result->setup_error == GL_NO_ERROR &&
       result->bounds_error == GL_INVALID_VALUE &&
       actual_format == (GLint)internal_format &&
       result->framebuffer_status == GL_FRAMEBUFFER_COMPLETE) {
      glViewport(0, 0, TARGET_SIZE, TARGET_SIZE);
      glUseProgram(producer);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      result->producer_status =
         ps5_egl_current_draw_status(&result->producer_calls);

      /* Rebinding the default target makes the integer render sampleable. */
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      glUseProgram(consumer);
      glUniform1i(sampler, 0);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      result->consumer_status =
         ps5_egl_current_draw_status(&result->consumer_calls);
      glFinish();
      glReadPixels((DISPLAY_WIDTH - CROP_SIZE) / 2,
                   (DISPLAY_HEIGHT - CROP_SIZE) / 2,
                   CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      if (glGetError() == GL_NO_ERROR) {
         result->hash = hash32(pixels, CROP_SIZE * CROP_SIZE * sizeof(*pixels));
         for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
            result->matching += pixels[i] == expected_pixel;
         passed = result->producer_status == 0 &&
                  result->consumer_status == 0 &&
                  result->matching == CROP_SIZE * CROP_SIZE &&
                  result->hash == expected_hash;
      }
   }

   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   if (framebuffer)
      glDeleteFramebuffers(1, &framebuffer);
   if (texture)
      glDeleteTextures(1, &texture);
   return passed;
}

static int
run_upload_case(GLenum internal_format, GLenum type, const void *texels,
                GLuint consumer, uint32_t expected_pixel,
                uint32_t expected_hash, uint32_t *pixels,
                struct case_result *result)
{
   GLuint texture = 0;
   GLint actual_format = 0;
   GLint sampler = glGetUniformLocation(consumer, "u_texture");
   int passed = 0;

   memset(result, 0, sizeof(*result));
   result->internal_format = internal_format;
   result->producer_status = -100;
   if (sampler < 0)
      return 0;

   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, internal_format, UPLOAD_SIZE, UPLOAD_SIZE,
                0, GL_RGBA_INTEGER, type, texels);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                            &actual_format);
   result->setup_error = glGetError();
   glTexSubImage2D(GL_TEXTURE_2D, 0, UPLOAD_SIZE, 0, 1, 1,
                   GL_RGBA_INTEGER, type, texels);
   result->bounds_error = glGetError();

   if (result->setup_error == GL_NO_ERROR &&
       result->bounds_error == GL_INVALID_VALUE &&
       actual_format == (GLint)internal_format) {
      glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      glUseProgram(consumer);
      glUniform1i(sampler, 0);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      result->producer_status =
         ps5_egl_current_draw_status(&result->producer_calls);
      glFinish();
      glReadPixels((DISPLAY_WIDTH - CROP_SIZE) / 2,
                   (DISPLAY_HEIGHT - CROP_SIZE) / 2,
                   CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      if (glGetError() == GL_NO_ERROR) {
         result->hash = hash32(pixels, CROP_SIZE * CROP_SIZE * sizeof(*pixels));
         for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
            result->matching += pixels[i] == expected_pixel;
         passed = result->producer_status == 0 &&
                  result->matching == CROP_SIZE * CROP_SIZE &&
                  result->hash == expected_hash;
      }
   }

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
   static const char *uint_producer_source =
      "#version 120\n"
      "#extension GL_EXT_gpu_shader4 : require\n"
      "varying out uvec4 frag_value;\n"
      "void main() { frag_value = uvec4(255u, 0u, 255u, 1u); }\n";
   static const char *uint_consumer_source =
      "#version 120\n"
      "#extension GL_EXT_gpu_shader4 : require\n"
      "uniform usampler2D u_texture;\n"
      "void main() {\n"
      "  uvec4 value = texture2D(u_texture, vec2(0.5));\n"
      "  gl_FragColor = all(equal(value, uvec4(255u, 0u, 255u, 1u)))\n"
      "     ? vec4(1.0, 0.0, 1.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const char *sint_producer_source =
      "#version 120\n"
      "#extension GL_EXT_gpu_shader4 : require\n"
      "varying out ivec4 frag_value;\n"
      "void main() { frag_value = ivec4(-1, 7, -3, 1); }\n";
   static const char *sint_consumer_source =
      "#version 120\n"
      "#extension GL_EXT_gpu_shader4 : require\n"
      "uniform isampler2D u_texture;\n"
      "void main() {\n"
      "  ivec4 value = texture2D(u_texture, vec2(0.5));\n"
      "  gl_FragColor = all(equal(value, ivec4(-1, 7, -3, 1)))\n"
      "     ? vec4(0.0, 1.0, 0.0, 1.0) : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const char *uint_upload_consumer_source =
      "#version 120\n"
      "#extension GL_EXT_gpu_shader4 : require\n"
      "uniform usampler2D u_texture;\n"
      "void main() {\n"
      "  bool ok =\n"
      "    all(equal(texture2D(u_texture, vec2(0.25, 0.25)), "
      "uvec4(255u, 0u, 255u, 1u))) &&\n"
      "    all(equal(texture2D(u_texture, vec2(0.75, 0.25)), "
      "uvec4(7u, 11u, 13u, 17u))) &&\n"
      "    all(equal(texture2D(u_texture, vec2(0.25, 0.75)), "
      "uvec4(19u, 23u, 29u, 31u))) &&\n"
      "    all(equal(texture2D(u_texture, vec2(0.75, 0.75)), "
      "uvec4(37u, 41u, 43u, 47u)));\n"
      "  gl_FragColor = ok ? vec4(1.0, 0.0, 1.0, 1.0) "
      ": vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const char *sint_upload_consumer_source =
      "#version 120\n"
      "#extension GL_EXT_gpu_shader4 : require\n"
      "uniform isampler2D u_texture;\n"
      "void main() {\n"
      "  bool ok =\n"
      "    all(equal(texture2D(u_texture, vec2(0.25, 0.25)), "
      "ivec4(-1, 7, -3, 1))) &&\n"
      "    all(equal(texture2D(u_texture, vec2(0.75, 0.25)), "
      "ivec4(-5, 11, -13, 17))) &&\n"
      "    all(equal(texture2D(u_texture, vec2(0.25, 0.75)), "
      "ivec4(19, -23, 29, -31))) &&\n"
      "    all(equal(texture2D(u_texture, vec2(0.75, 0.75)), "
      "ivec4(-37, 41, -43, 47)));\n"
      "  gl_FragColor = ok ? vec4(0.0, 1.0, 0.0, 1.0) "
      ": vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static uint32_t pixels[CROP_SIZE * CROP_SIZE];
   static uint32_t uint_texels[UPLOAD_SIZE * UPLOAD_SIZE * 4];
   static int32_t sint_texels[UPLOAD_SIZE * UPLOAD_SIZE * 4];
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
   GLuint vs = 0, uint_producer_fs = 0, uint_consumer_fs = 0;
   GLuint sint_producer_fs = 0, sint_consumer_fs = 0;
   GLuint uint_upload_consumer_fs = 0, sint_upload_consumer_fs = 0;
   GLuint uint_producer = 0, uint_consumer = 0, uint_upload_consumer = 0;
   GLuint sint_producer = 0, sint_consumer = 0, sint_upload_consumer = 0;
   GLuint vbo = 0;
   struct case_result uint_result, sint_result;
   struct case_result uint_upload_result, sint_upload_result;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   const char *extensions = NULL;
   int texture_integer_extension = 0, gpu_shader4_extension = 0;
   int uint_ok = 0, sint_ok = 0, uint_upload_ok = 0, sint_upload_ok = 0;
   int made_current = 0, passed = 0;

   memset(&uint_result, 0, sizeof(uint_result));
   memset(&sint_result, 0, sizeof(sint_result));
   memset(&uint_upload_result, 0, sizeof(uint_upload_result));
   memset(&sint_upload_result, 0, sizeof(sint_upload_result));
   for (unsigned y = 0; y < UPLOAD_SIZE; ++y) {
      for (unsigned x = 0; x < UPLOAD_SIZE; ++x) {
         unsigned quadrant = (x >= 64) | ((y >= 64) << 1);
         unsigned i = (y * UPLOAD_SIZE + x) * 4;
         static const uint32_t uint_values[4][4] = {
            {255, 0, 255, 1}, {7, 11, 13, 17},
            {19, 23, 29, 31}, {37, 41, 43, 47},
         };
         static const int32_t sint_values[4][4] = {
            {-1, 7, -3, 1}, {-5, 11, -13, 17},
            {19, -23, 29, -31}, {-37, 41, -43, 47},
         };

         memcpy(&uint_texels[i], uint_values[quadrant],
                sizeof(uint_values[quadrant]));
         memcpy(&sint_texels[i], sint_values[quadrant],
                sizeof(sint_values[quadrant]));
      }
   }
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
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!extensions)
      goto cleanup;
   texture_integer_extension =
      has_extension(extensions, "GL_EXT_texture_integer");
   gpu_shader4_extension = has_extension(extensions, "GL_EXT_gpu_shader4");
   if (!texture_integer_extension || !gpu_shader4_extension)
      goto cleanup;

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, uint_producer_source,
                      &uint_producer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, uint_consumer_source,
                      &uint_consumer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, sint_producer_source,
                      &sint_producer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, sint_consumer_source,
                      &sint_consumer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, uint_upload_consumer_source,
                      &uint_upload_consumer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, sint_upload_consumer_source,
                      &sint_upload_consumer_fs) ||
       link_program(vs, uint_producer_fs, &uint_producer) ||
       link_program(vs, uint_consumer_fs, &uint_consumer) ||
       link_program(vs, sint_producer_fs, &sint_producer) ||
       link_program(vs, sint_consumer_fs, &sint_consumer) ||
       link_program(vs, uint_upload_consumer_fs, &uint_upload_consumer) ||
       link_program(vs, sint_upload_consumer_fs, &sint_upload_consumer))
      goto cleanup;

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   uint_ok = run_case(GL_RGBA32UI, GL_UNSIGNED_INT, uint_producer,
                      uint_consumer, UINT_PIXEL, UINT_HASH,
                      pixels, &uint_result);
   sint_ok = run_case(GL_RGBA32I, GL_INT, sint_producer, sint_consumer,
                      SINT_PIXEL, SINT_HASH, pixels, &sint_result);
   uint_upload_ok = run_upload_case(
      GL_RGBA32UI, GL_UNSIGNED_INT, uint_texels, uint_upload_consumer,
      UINT_PIXEL, UINT_HASH, pixels, &uint_upload_result);
   sint_upload_ok = run_upload_case(
      GL_RGBA32I, GL_INT, sint_texels, sint_upload_consumer,
      SINT_PIXEL, SINT_HASH, pixels, &sint_upload_result);
   passed = egl_major == 1 && egl_minor == 4 &&
            width == DISPLAY_WIDTH && height == DISPLAY_HEIGHT &&
            texture_integer_extension && gpu_shader4_extension &&
            uint_ok && sint_ok && uint_upload_ok && sint_upload_ok &&
            eglSwapBuffers(display, surface);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (uint_producer)
         glDeleteProgram(uint_producer);
      if (uint_consumer)
         glDeleteProgram(uint_consumer);
      if (sint_producer)
         glDeleteProgram(sint_producer);
      if (sint_consumer)
         glDeleteProgram(sint_consumer);
      if (uint_upload_consumer)
         glDeleteProgram(uint_upload_consumer);
      if (sint_upload_consumer)
         glDeleteProgram(sint_upload_consumer);
      if (uint_producer_fs)
         glDeleteShader(uint_producer_fs);
      if (uint_consumer_fs)
         glDeleteShader(uint_consumer_fs);
      if (sint_producer_fs)
         glDeleteShader(sint_producer_fs);
      if (sint_consumer_fs)
         glDeleteShader(sint_consumer_fs);
      if (uint_upload_consumer_fs)
         glDeleteShader(uint_upload_consumer_fs);
      if (sint_upload_consumer_fs)
         glDeleteShader(sint_upload_consumer_fs);
      if (vs)
         glDeleteShader(vs);
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
   printf("[ps5-egl-texture-integer] egl=%d.%d size=%dx%d ext=%d/%d "
          "uint=%u/%08x/%x/%x/%x/%d/%u,%d/%u "
          "sint=%u/%08x/%x/%x/%x/%d/%u,%d/%u "
          "uint-upload=%u/%08x/%x/%x/%d/%u "
          "sint-upload=%u/%08x/%x/%x/%d/%u "
          "cleanup=%u/0x%x/0x%x result=%d\n",
          egl_major, egl_minor, width, height,
          texture_integer_extension, gpu_shader4_extension,
          uint_result.matching, uint_result.hash, uint_result.setup_error,
          uint_result.bounds_error, uint_result.framebuffer_status,
          uint_result.producer_status, uint_result.producer_calls,
          uint_result.consumer_status, uint_result.consumer_calls,
          sint_result.matching, sint_result.hash, sint_result.setup_error,
          sint_result.bounds_error, sint_result.framebuffer_status,
          sint_result.producer_status, sint_result.producer_calls,
          sint_result.consumer_status, sint_result.consumer_calls,
          uint_upload_result.matching, uint_upload_result.hash,
          uint_upload_result.setup_error, uint_upload_result.bounds_error,
          uint_upload_result.producer_status,
          uint_upload_result.producer_calls,
          sint_upload_result.matching, sint_upload_result.hash,
          sint_upload_result.setup_error, sint_upload_result.bounds_error,
          sint_upload_result.producer_status,
          sint_upload_result.producer_calls,
          cleanup_ok, cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
