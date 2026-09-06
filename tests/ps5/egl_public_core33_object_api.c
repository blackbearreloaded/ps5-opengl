#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

int
main(void)
{
   static const uint8_t zeros[64] = {0};
   static const GLint signed_border[4] = {-1, 2, -3, 4};
   static const GLuint unsigned_border[4] = {1, 2, 3, 4};
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
   GLuint buffer = 0, vertex_array = 0, texture = 0, sampler = 0;
   GLuint framebuffer = 0, renderbuffer = 0, queries[2] = {0, 0};
   GLint parameter_i = 0, query_bits = 0, current_query = -1;
   GLint query_i = -1, indexed_i = -1;
   GLint64 query_i64 = -1, indexed_i64 = -1, timestamp = -1;
   GLuint64 query_ui64 = 0, timestamp_query = 0;
   GLuint parameter_ui[4] = {0, 0, 0, 0};
   GLint parameter_si[4] = {0, 0, 0, 0};
   static const GLfloat expected_sample_positions[4][2] = {
      {0.375f, 0.125f},
      {0.875f, 0.375f},
      {0.125f, 0.625f},
      {0.625f, 0.875f},
   };
   GLfloat parameter_f = 0.0f, sample_positions[4][2] = {{0.0f}};
   GLboolean boolean_value = GL_FALSE, indexed_boolean[4] = {0};
   void *mapped = NULL, *queried_pointer = NULL;
   GLenum status = 0, error = GL_NO_ERROR;
   int identities = 0, queries_ok = 0, parameters_ok = 0;
   int made_current = 0, passed = 0;
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

   glGenBuffers(1, &buffer);
   glBindBuffer(GL_ARRAY_BUFFER, buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(zeros), zeros, GL_DYNAMIC_DRAW);
   mapped = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
   glGetBufferPointerv(GL_ARRAY_BUFFER, GL_BUFFER_MAP_POINTER,
                       &queried_pointer);
   if (!mapped || queried_pointer != mapped || !glUnmapBuffer(GL_ARRAY_BUFFER))
      goto cleanup;
   glBindBufferBase(GL_UNIFORM_BUFFER, 0, buffer);
   glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &indexed_i);
   glGetInteger64i_v(GL_UNIFORM_BUFFER_START, 0, &indexed_i64);

   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, zeros);
   {
      const GLint wrap = GL_CLAMP_TO_BORDER;
      const GLfloat max_lod = 2.0f;

      glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap);
      glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, &max_lod);
   }
   glTexParameterIiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, signed_border);
   glTexParameterIuiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR,
                      unsigned_border);
   glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &parameter_i);
   glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, &parameter_f);
   glGetTexParameterIiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, parameter_si);
   glGetTexParameterIuiv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, parameter_ui);
   glGetTexLevelParameterfv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &parameter_f);

   glGenSamplers(1, &sampler);
   glBindSampler(0, sampler);
   glSamplerParameterf(sampler, GL_TEXTURE_LOD_BIAS, 0.5f);
   {
      const GLint wrap = GL_CLAMP_TO_BORDER;
      glSamplerParameteriv(sampler, GL_TEXTURE_WRAP_S, &wrap);
   }
   glSamplerParameterIiv(sampler, GL_TEXTURE_BORDER_COLOR, signed_border);
   glSamplerParameterIuiv(sampler, GL_TEXTURE_BORDER_COLOR, unsigned_border);
   glGetSamplerParameterIiv(sampler, GL_TEXTURE_BORDER_COLOR, parameter_si);
   glGetSamplerParameterIuiv(sampler, GL_TEXTURE_BORDER_COLOR, parameter_ui);

   glGenRenderbuffers(1, &renderbuffer);
   glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 4, 4);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, renderbuffer);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   for (unsigned sample = 0; sample < 4; ++sample)
      glGetMultisamplefv(GL_SAMPLE_POSITION, sample,
                         sample_positions[sample]);

   glGenQueries(2, queries);
   glBeginQuery(GL_SAMPLES_PASSED, queries[0]);
   glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
   glClear(GL_COLOR_BUFFER_BIT);
   glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glEndQuery(GL_SAMPLES_PASSED);
   glGetQueryiv(GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &query_bits);
   glGetQueryiv(GL_SAMPLES_PASSED, GL_CURRENT_QUERY, &current_query);
   glGetQueryObjectiv(queries[0], GL_QUERY_RESULT_AVAILABLE, &query_i);
   glGetQueryObjecti64v(queries[0], GL_QUERY_RESULT, &query_i64);
   glGetQueryObjectui64v(queries[0], GL_QUERY_RESULT, &query_ui64);
   glQueryCounter(queries[1], GL_TIMESTAMP);
   glGetQueryObjectui64v(queries[1], GL_QUERY_RESULT, &timestamp_query);
   glGetInteger64v(GL_TIMESTAMP, &timestamp);

   glBlendEquation(GL_FUNC_ADD);
   glClampColor(GL_CLAMP_READ_COLOR, GL_FIXED_ONLY);
   glPixelStoref(GL_PACK_ALIGNMENT, 4.0f);
   glPointParameterf(GL_POINT_FADE_THRESHOLD_SIZE, 1.0f);
   {
      const GLfloat threshold = 1.0f;
      const GLint origin = GL_LOWER_LEFT;
      glPointParameterfv(GL_POINT_FADE_THRESHOLD_SIZE, &threshold);
      glPointParameteriv(GL_POINT_SPRITE_COORD_ORIGIN, &origin);
   }
   glGetBooleanv(GL_DEPTH_WRITEMASK, &boolean_value);
   glGetBooleani_v(GL_COLOR_WRITEMASK, 0, indexed_boolean);

   identities = glIsBuffer(buffer) && glIsVertexArray(vertex_array) &&
                glIsTexture(texture) && glIsSampler(sampler) &&
                glIsRenderbuffer(renderbuffer) &&
                glIsFramebuffer(framebuffer) && glIsQuery(queries[0]) &&
                glIsQuery(queries[1]);
   parameters_ok = parameter_i == GL_CLAMP_TO_BORDER &&
                   parameter_f == 4.0f &&
                   parameter_ui[0] == unsigned_border[0] &&
                   parameter_ui[1] == unsigned_border[1] &&
                   parameter_ui[2] == unsigned_border[2] &&
                   parameter_ui[3] == unsigned_border[3] &&
                   indexed_i == (GLint)buffer && indexed_i64 == 0 &&
                   boolean_value == GL_TRUE &&
                   indexed_boolean[0] == GL_TRUE &&
                   indexed_boolean[1] == GL_TRUE &&
                   indexed_boolean[2] == GL_TRUE &&
                   indexed_boolean[3] == GL_TRUE;
   queries_ok = query_bits > 0 && current_query == 0 && query_i == GL_TRUE &&
                query_i64 == 0 && query_ui64 == 0 &&
                timestamp_query > 0 && timestamp > 0;
   error = glGetError();
   passed = major == 1 && minor == 4 && identities && parameters_ok &&
            status == GL_FRAMEBUFFER_COMPLETE &&
            !memcmp(sample_positions, expected_sample_positions,
                    sizeof(sample_positions)) &&
            queries_ok && error == GL_NO_ERROR;
   printf("[ps5-egl-object-api] identity=%d parameter=%d fbo=0x%x "
          "sample=%.3f/%.3f query=%d/%d/%lld/%llu timestamp=%llu/%lld "
          "error=0x%x result=%d\n",
          identities, parameters_ok, status, sample_positions[0][0],
          sample_positions[0][1], query_bits, query_i, (long long)query_i64,
          (unsigned long long)query_ui64,
          (unsigned long long)timestamp_query, (long long)timestamp,
          error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      if (queries[0] || queries[1])
         glDeleteQueries(2, queries);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (renderbuffer)
         glDeleteRenderbuffers(1, &renderbuffer);
      if (sampler)
         glDeleteSamplers(1, &sampler);
      if (texture)
         glDeleteTextures(1, &texture);
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
      if (buffer)
         glDeleteBuffers(1, &buffer);
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
   printf("[ps5-egl-object-api] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
