#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

#ifndef PS5_NARROW_FIRST_CASE
#define PS5_NARROW_FIRST_CASE 0
#endif
#ifndef PS5_NARROW_CASE_COUNT
#define PS5_NARROW_CASE_COUNT 4
#endif
#ifndef PS5_NARROW_RUN_RENDER
#define PS5_NARROW_RUN_RENDER 1
#endif
#ifndef PS5_NARROW_RUN_UPLOAD
#define PS5_NARROW_RUN_UPLOAD 1
#endif

_Static_assert(PS5_NARROW_FIRST_CASE < 4 && PS5_NARROW_CASE_COUNT > 0 &&
               PS5_NARROW_FIRST_CASE + PS5_NARROW_CASE_COUNT <= 4,
               "invalid narrow-integer case range");

int ps5_egl_current_draw_status(unsigned *draw_calls);

struct case_desc {
   const char *name;
   GLenum internal_format;
   GLenum type;
   const char *producer_source;
   const char *consumer_source;
   uint32_t expected_pixel;
   uint32_t expected_hash;
};

struct case_result {
   GLint internal_format;
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
      printf("[ps5-egl-texture-integer-narrow] shader=0x%x log=%.*s\n",
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
      printf("[ps5-egl-texture-integer-narrow] link=%.*s\n", length, log);
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
run_case(const struct case_desc *desc, GLuint vertex, uint32_t *pixels,
         struct case_result *result)
{
   GLuint producer_fs = 0, consumer_fs = 0;
   GLuint producer = 0, consumer = 0, texture = 0, framebuffer = 0;
   GLint sampler = -1;
   int passed = 0;

   memset(result, 0, sizeof(*result));
   result->producer_status = -100;
   result->consumer_status = -100;
   if (compile_shader(GL_FRAGMENT_SHADER, desc->producer_source,
                      &producer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, desc->consumer_source,
                      &consumer_fs) ||
       link_program(vertex, producer_fs, &producer) ||
       link_program(vertex, consumer_fs, &consumer))
      goto cleanup;
   sampler = glGetUniformLocation(consumer, "u_texture");
   if (sampler < 0)
      goto cleanup;

   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, desc->internal_format,
                TARGET_SIZE, TARGET_SIZE, 0,
                GL_RGBA_INTEGER, desc->type, NULL);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                            &result->internal_format);
   result->setup_error = glGetError();
   glTexSubImage2D(GL_TEXTURE_2D, 0, TARGET_SIZE, 0, 1, 1,
                   GL_RGBA_INTEGER, desc->type, NULL);
   result->bounds_error = glGetError();

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   result->framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (result->setup_error != GL_NO_ERROR ||
       result->bounds_error != GL_INVALID_VALUE ||
       result->internal_format != (GLint)desc->internal_format ||
       result->framebuffer_status != GL_FRAMEBUFFER_COMPLETE)
      goto cleanup;

   glViewport(0, 0, TARGET_SIZE, TARGET_SIZE);
   glUseProgram(producer);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   result->producer_status =
      ps5_egl_current_draw_status(&result->producer_calls);

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
         result->matching += pixels[i] == desc->expected_pixel;
      passed = result->producer_status == 0 &&
               result->consumer_status == 0 &&
               result->matching == CROP_SIZE * CROP_SIZE &&
               result->hash == desc->expected_hash;
   }

cleanup:
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   if (framebuffer)
      glDeleteFramebuffers(1, &framebuffer);
   if (texture)
      glDeleteTextures(1, &texture);
   if (consumer)
      glDeleteProgram(consumer);
   if (producer)
      glDeleteProgram(producer);
   if (consumer_fs)
      glDeleteShader(consumer_fs);
   if (producer_fs)
      glDeleteShader(producer_fs);
   return passed;
}

static int
run_upload_case(const struct case_desc *desc, GLuint vertex, uint32_t *pixels,
                struct case_result *result)
{
   GLuint consumer_fs = 0, consumer = 0, texture = 0;
   const unsigned component_size =
      desc->type == GL_BYTE || desc->type == GL_UNSIGNED_BYTE ? 1 : 2;
   const unsigned pixel_index = (64 * UPLOAD_SIZE + 64) * 4;
   void *texels = calloc(UPLOAD_SIZE * UPLOAD_SIZE * 4, component_size);
   GLint sampler = -1;
   int passed = 0;

   memset(result, 0, sizeof(*result));
   result->producer_status = -100;
   result->consumer_status = -100;
   if (!texels)
      goto cleanup;
   switch (desc->internal_format) {
   case GL_RGBA8UI: {
      const uint8_t value[4] = {255, 85, 17, 2};
      memcpy((uint8_t *)texels + pixel_index, value, sizeof(value));
      break;
   }
   case GL_RGBA8I: {
      const int8_t value[4] = {-128, 63, -7, 1};
      memcpy((int8_t *)texels + pixel_index, value, sizeof(value));
      break;
   }
   case GL_RGBA16UI: {
      const uint16_t value[4] = {65535, 21845, 4369, 2};
      memcpy((uint16_t *)texels + pixel_index, value, sizeof(value));
      break;
   }
   case GL_RGBA16I: {
      const int16_t value[4] = {-32768, 16383, -257, 1};
      memcpy((int16_t *)texels + pixel_index, value, sizeof(value));
      break;
   }
   default:
      goto cleanup;
   }
   if (compile_shader(GL_FRAGMENT_SHADER, desc->consumer_source,
                      &consumer_fs) ||
       link_program(vertex, consumer_fs, &consumer))
      goto cleanup;
   sampler = glGetUniformLocation(consumer, "u_texture");
   if (sampler < 0)
      goto cleanup;

   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, desc->internal_format,
                UPLOAD_SIZE, UPLOAD_SIZE, 0,
                GL_RGBA_INTEGER, desc->type, texels);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                            &result->internal_format);
   result->setup_error = glGetError();
   glTexSubImage2D(GL_TEXTURE_2D, 0, UPLOAD_SIZE, 0, 1, 1,
                   GL_RGBA_INTEGER, desc->type, texels);
   result->bounds_error = glGetError();
   if (result->setup_error != GL_NO_ERROR ||
       result->bounds_error != GL_INVALID_VALUE ||
       result->internal_format != (GLint)desc->internal_format)
      goto cleanup;

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
         result->matching += pixels[i] == desc->expected_pixel;
      passed = result->producer_status == 0 &&
               result->matching == CROP_SIZE * CROP_SIZE &&
               result->hash == desc->expected_hash;
   }

cleanup:
   if (texture)
      glDeleteTextures(1, &texture);
   if (consumer)
      glDeleteProgram(consumer);
   if (consumer_fs)
      glDeleteShader(consumer_fs);
   free(texels);
   return passed;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const struct case_desc cases[] = {
      {
         "rgba8ui", GL_RGBA8UI, GL_UNSIGNED_BYTE,
         "#version 120\n#extension GL_EXT_gpu_shader4 : require\n"
         "varying out uvec4 frag_value;\n"
         "void main() { frag_value = uvec4(255u, 85u, 17u, 2u); }\n",
         "#version 120\n#extension GL_EXT_gpu_shader4 : require\n"
         "uniform usampler2D u_texture;\nvoid main() {\n"
         " uvec4 v=texture2D(u_texture,vec2(0.5));\n"
         " gl_FragColor=all(equal(v,uvec4(255u,85u,17u,2u)))?"
         "vec4(1,0,1,1):vec4(1,0,0,1);\n}\n",
         UINT32_C(0xffff00ff), UINT32_C(0x64e31dc5),
      },
      {
         "rgba8i", GL_RGBA8I, GL_BYTE,
         "#version 120\n#extension GL_EXT_gpu_shader4 : require\n"
         "varying out ivec4 frag_value;\n"
         "void main() { frag_value = ivec4(-128, 63, -7, 1); }\n",
         "#version 120\n#extension GL_EXT_gpu_shader4 : require\n"
         "uniform isampler2D u_texture;\nvoid main() {\n"
         " ivec4 v=texture2D(u_texture,vec2(0.5));\n"
         " gl_FragColor=all(equal(v,ivec4(-128,63,-7,1)))?"
         "vec4(0,1,0,1):vec4(1,0,0,1);\n}\n",
         UINT32_C(0xff00ff00), UINT32_C(0xc38d1dc5),
      },
      {
         "rgba16ui", GL_RGBA16UI, GL_UNSIGNED_SHORT,
         "#version 120\n#extension GL_EXT_gpu_shader4 : require\n"
         "varying out uvec4 frag_value;\n"
         "void main(){frag_value=uvec4(65535u,21845u,4369u,2u);}\n",
         "#version 120\n#extension GL_EXT_gpu_shader4 : require\n"
         "uniform usampler2D u_texture;\nvoid main(){\n"
         " uvec4 v=texture2D(u_texture,vec2(0.5));\n"
         " gl_FragColor=all(equal(v,uvec4(65535u,21845u,4369u,2u)))?"
         "vec4(0,1,1,1):vec4(1,0,0,1);}\n",
         UINT32_C(0xffffff00), UINT32_C(0xf1461dc5),
      },
      {
         "rgba16i", GL_RGBA16I, GL_SHORT,
         "#version 120\n#extension GL_EXT_gpu_shader4 : require\n"
         "varying out ivec4 frag_value;\n"
         "void main(){frag_value=ivec4(-32768,16383,-257,1);}\n",
         "#version 120\n#extension GL_EXT_gpu_shader4 : require\n"
         "uniform isampler2D u_texture;\nvoid main(){\n"
         " ivec4 v=texture2D(u_texture,vec2(0.5));\n"
         " gl_FragColor=all(equal(v,ivec4(-32768,16383,-257,1)))?"
         "vec4(1,1,0,1):vec4(1,0,0,1);}\n",
         UINT32_C(0xff00ffff), UINT32_C(0x5f3a1dc5),
      },
   };
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
   GLuint vertex = 0, vbo = 0;
   struct case_result results[sizeof(cases) / sizeof(cases[0])];
   struct case_result upload_results[sizeof(cases) / sizeof(cases[0])];
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   const char *extensions = NULL;
   unsigned passed_cases = 0, passed_uploads = 0;
   int extension_present = 0, shader4_present = 0;
   int made_current = 0, passed = 0;

   memset(results, 0, sizeof(results));
   memset(upload_results, 0, sizeof(upload_results));
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
   extension_present = has_extension(extensions, "GL_EXT_texture_integer");
   shader4_present = has_extension(extensions, "GL_EXT_gpu_shader4");
   if (!extension_present || !shader4_present ||
       compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex))
      goto cleanup;

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);
   for (unsigned i = PS5_NARROW_FIRST_CASE;
        PS5_NARROW_RUN_RENDER &&
        i < PS5_NARROW_FIRST_CASE + PS5_NARROW_CASE_COUNT; ++i) {
      passed_cases += run_case(&cases[i], vertex, pixels, &results[i]);
      printf("[ps5-egl-texture-integer-narrow] %s "
             "internal=%x setup=%x bounds=%x fbo=%x pixels=%u/%08x "
             "draw=%d/%u,%d/%u\n",
             cases[i].name, results[i].internal_format,
             results[i].setup_error, results[i].bounds_error,
             results[i].framebuffer_status, results[i].matching,
             results[i].hash, results[i].producer_status,
             results[i].producer_calls, results[i].consumer_status,
             results[i].consumer_calls);
   }
   for (unsigned i = PS5_NARROW_FIRST_CASE;
        PS5_NARROW_RUN_UPLOAD &&
        i < PS5_NARROW_FIRST_CASE + PS5_NARROW_CASE_COUNT; ++i) {
      passed_uploads +=
         run_upload_case(&cases[i], vertex, pixels, &upload_results[i]);
      printf("[ps5-egl-texture-integer-narrow] %s-upload "
             "internal=%x setup=%x bounds=%x pixels=%u/%08x draw=%d/%u\n",
             cases[i].name, upload_results[i].internal_format,
             upload_results[i].setup_error, upload_results[i].bounds_error,
             upload_results[i].matching, upload_results[i].hash,
             upload_results[i].producer_status,
             upload_results[i].producer_calls);
   }
   passed = egl_major == 1 && egl_minor == 4 &&
            width == DISPLAY_WIDTH && height == DISPLAY_HEIGHT &&
            (!PS5_NARROW_RUN_RENDER ||
             passed_cases == PS5_NARROW_CASE_COUNT) &&
            (!PS5_NARROW_RUN_UPLOAD ||
             passed_uploads == PS5_NARROW_CASE_COUNT) &&
            eglSwapBuffers(display, surface);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vertex)
         glDeleteShader(vertex);
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
   printf("[ps5-egl-texture-integer-narrow] egl=%d.%d size=%dx%d "
          "ext=%d/%d cases=%u/%u uploads=%u/%u "
          "cleanup=%u/0x%x/0x%x result=%d\n",
          egl_major, egl_minor, width, height,
          extension_present, shader4_present,
          passed_cases,
          (unsigned)(PS5_NARROW_RUN_RENDER ? PS5_NARROW_CASE_COUNT : 0),
          passed_uploads,
          (unsigned)(PS5_NARROW_RUN_UPLOAD ? PS5_NARROW_CASE_COUNT : 0),
          cleanup_ok, cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
