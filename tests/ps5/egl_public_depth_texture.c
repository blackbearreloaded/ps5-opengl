#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define DISPLAY_WIDTH 1920
#define DISPLAY_HEIGHT 1080
#define TARGET_WIDTH 128
#define TARGET_HEIGHT 96
#define CROP_SIZE 64
#define GREEN_PIXEL UINT32_C(0xff00ff00)
#define GREEN_HASH UINT32_C(0xc38d1dc5)

#ifndef PS5_CORE_33_TEST
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
#else
static int
has_core_extension(const char *name)
{
   GLint count = 0;

   glGetIntegerv(GL_NUM_EXTENSIONS, &count);
   for (GLint i = 0; i < count; ++i) {
      const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, i);

      if (extension && strcmp(extension, name) == 0)
         return 1;
   }
   return 0;
}
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
      printf("[ps5-egl-depth-texture] shader=0x%x log=%.*s\n",
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
      printf("[ps5-egl-depth-texture] link log=%.*s\n", length, log);
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

static unsigned
matching_pixels(const uint32_t *pixels)
{
   unsigned matching = 0;

   for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
      matching += pixels[i] == GREEN_PIXEL;
   return matching;
}

int
main(void)
{
#ifdef PS5_CORE_33_TEST
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 a_position;\n"
      "uniform float u_ndc_depth;\n"
      "void main() { gl_Position = vec4(a_position, u_ndc_depth, 1.0); }\n";
   static const char *producer_source =
      "#version 330\n"
      "out vec4 frag_color;\n"
      "void main() { frag_color = vec4(0.0, 0.0, 1.0, 1.0); }\n";
   static const char *raw_source =
      "#version 330\n"
      "uniform sampler2D u_depth_texture;\n"
      "out vec4 frag_color;\n"
      "void main() {\n"
      "  float d = texture(u_depth_texture, vec2(0.5)).r;\n"
      "  frag_color = abs(d - 0.25) < 0.01\n"
      "      ? vec4(0.0, 1.0, 0.0, 1.0)\n"
      "      : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const char *shadow_source =
      "#version 330\n"
      "uniform sampler2DShadow u_depth_texture;\n"
      "out vec4 frag_color;\n"
      "void main() {\n"
      "  float visible = texture(u_depth_texture, vec3(0.5, 0.5, 0.125));\n"
      "  frag_color = visible > 0.5\n"
      "      ? vec4(0.0, 1.0, 0.0, 1.0)\n"
      "      : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
#else
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "uniform float u_ndc_depth;\n"
      "void main() { gl_Position = vec4(a_position, u_ndc_depth, 1.0); }\n";
   static const char *producer_source =
      "#version 120\n"
      "void main() { gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0); }\n";
   static const char *raw_source =
      "#version 120\n"
      "uniform sampler2D u_depth_texture;\n"
      "void main() {\n"
      "  float d = texture2D(u_depth_texture, vec2(0.5)).r;\n"
      "  gl_FragColor = abs(d - 0.25) < 0.01\n"
      "      ? vec4(0.0, 1.0, 0.0, 1.0)\n"
      "      : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const char *shadow_source =
      "#version 120\n"
      "uniform sampler2DShadow u_depth_texture;\n"
      "void main() {\n"
      "  float visible = shadow2D(u_depth_texture, vec3(0.5, 0.5, 0.125)).r;\n"
      "  gl_FragColor = visible > 0.5\n"
      "      ? vec4(0.0, 1.0, 0.0, 1.0)\n"
      "      : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
#endif
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
#ifdef PS5_CORE_33_TEST
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
#endif
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0;
   EGLint width = 0, height = 0;
#ifdef PS5_CORE_33_TEST
   EGLint context_version = 0, profile_mask = 0;
#endif
   GLuint vs = 0, producer_fs = 0, raw_fs = 0, shadow_fs = 0;
   GLuint producer = 0, raw = 0, shadow = 0, vbo = 0;
#ifdef PS5_CORE_33_TEST
   GLuint vertex_array = 0;
#endif
   GLuint depth_texture = 0, framebuffer = 0, color = 0;
   GLint producer_depth = -1, raw_depth = -1, shadow_depth = -1;
   GLint raw_sampler = -1, shadow_sampler = -1, internal_format = 0;
   GLenum framebuffer_status = 0, setup_error = GL_NO_ERROR;
   GLenum raw_error = GL_NO_ERROR, shadow_error = GL_NO_ERROR;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
#ifndef PS5_CORE_33_TEST
   const char *extensions = NULL;
#endif
#ifdef PS5_CORE_33_TEST
   const GLubyte *version = NULL, *glsl = NULL;
   char version_text[64] = {0}, glsl_text[64] = {0};
#endif
   uint32_t raw_hash = 0, shadow_hash = 0;
   unsigned raw_matching = 0, shadow_matching = 0;
   int depth_float = 0, fbo_extension = 0, made_current = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
#ifdef PS5_CORE_33_TEST
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
#else
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
#endif
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;
   eglQuerySurface(display, surface, EGL_WIDTH, &width);
   eglQuerySurface(display, surface, EGL_HEIGHT, &height);
#ifdef PS5_CORE_33_TEST
   eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION,
                   &context_version);
   eglQueryContext(display, context, EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                   &profile_mask);
#endif
#ifdef PS5_CORE_33_TEST
   version = glGetString(GL_VERSION);
   glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
   if (!version || !glsl)
      goto cleanup;
   snprintf(version_text, sizeof(version_text), "%s", version);
   snprintf(glsl_text, sizeof(glsl_text), "%s", glsl);
   depth_float = has_core_extension("GL_ARB_depth_buffer_float");
   fbo_extension = has_core_extension("GL_ARB_framebuffer_object");
#else
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!extensions)
      goto cleanup;
   depth_float = has_extension(extensions, "GL_ARB_depth_buffer_float");
   fbo_extension = has_extension(extensions, "GL_ARB_framebuffer_object") ||
                   has_extension(extensions, "GL_EXT_framebuffer_object");
#endif
   if (!depth_float || !fbo_extension)
      goto cleanup;

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, producer_source, &producer_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, raw_source, &raw_fs) ||
       compile_shader(GL_FRAGMENT_SHADER, shadow_source, &shadow_fs) ||
       link_program(vs, producer_fs, &producer) ||
       link_program(vs, raw_fs, &raw) ||
       link_program(vs, shadow_fs, &shadow))
      goto cleanup;
   producer_depth = glGetUniformLocation(producer, "u_ndc_depth");
   raw_depth = glGetUniformLocation(raw, "u_ndc_depth");
   shadow_depth = glGetUniformLocation(shadow, "u_ndc_depth");
   raw_sampler = glGetUniformLocation(raw, "u_depth_texture");
   shadow_sampler = glGetUniformLocation(shadow, "u_depth_texture");
   if (producer_depth < 0 || raw_depth < 0 || shadow_depth < 0 ||
       raw_sampler < 0 || shadow_sampler < 0)
      goto cleanup;

#ifdef PS5_CORE_33_TEST
   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
#endif
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &depth_texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, depth_texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                TARGET_WIDTH, TARGET_HEIGHT, 0,
                GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT,
                            &internal_format);

   glGenRenderbuffers(1, &color);
   glBindRenderbuffer(GL_RENDERBUFFER, color);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8,
                         TARGET_WIDTH, TARGET_HEIGHT);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, color);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                          GL_TEXTURE_2D, depth_texture, 0);
   framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   setup_error = glGetError();
   if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE ||
       internal_format != GL_DEPTH_COMPONENT32F ||
       setup_error != GL_NO_ERROR)
      goto cleanup;

   /* Prime the native runner's persistent display target before offscreen use. */
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
   glDisable(GL_DEPTH_TEST);
   glUseProgram(producer);
   glUniform1f(producer_depth, 0.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   if (glGetError() != GL_NO_ERROR)
      goto cleanup;

   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glViewport(0, 0, TARGET_WIDTH, TARGET_HEIGHT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_ALWAYS);
   glDepthMask(GL_TRUE);
   glClearDepth(1.0);
   glClear(GL_DEPTH_BUFFER_BIT);
   glUseProgram(producer);
   glUniform1f(producer_depth, -0.5f);
   glDrawArrays(GL_TRIANGLES, 0, 3);

   /* The next draw samples the just-written D32 plane without CPU staging. */
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
   glDisable(GL_DEPTH_TEST);
   glUseProgram(raw);
   glUniform1f(raw_depth, 0.0f);
   glUniform1i(raw_sampler, 0);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((DISPLAY_WIDTH - CROP_SIZE) / 2,
                (DISPLAY_HEIGHT - CROP_SIZE) / 2,
                CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   raw_error = glGetError();
   raw_hash = hash32(pixels, sizeof(pixels));
   raw_matching = matching_pixels(pixels);

   glBindTexture(GL_TEXTURE_2D, depth_texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                   GL_COMPARE_R_TO_TEXTURE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LESS);
   glUseProgram(shadow);
   glUniform1f(shadow_depth, 0.0f);
   glUniform1i(shadow_sampler, 0);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((DISPLAY_WIDTH - CROP_SIZE) / 2,
                (DISPLAY_HEIGHT - CROP_SIZE) / 2,
                CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   shadow_error = glGetError();
   shadow_hash = hash32(pixels, sizeof(pixels));
   shadow_matching = matching_pixels(pixels);

   passed = egl_major == 1 && egl_minor == 4 &&
            width == DISPLAY_WIDTH && height == DISPLAY_HEIGHT &&
            depth_float && fbo_extension &&
            framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
            internal_format == GL_DEPTH_COMPONENT32F &&
            raw_matching == CROP_SIZE * CROP_SIZE &&
            raw_hash == GREEN_HASH && raw_error == GL_NO_ERROR &&
            shadow_matching == CROP_SIZE * CROP_SIZE &&
            shadow_hash == GREEN_HASH && shadow_error == GL_NO_ERROR;
#ifdef PS5_CORE_33_TEST
   passed &= context_version == 3 &&
             profile_mask == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
             strncmp(version_text, "3.3 ", 4) == 0 &&
             strncmp(glsl_text, "3.30", 4) == 0;
#endif
   if (!eglSwapBuffers(display, surface))
      passed = 0;

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (color)
         glDeleteRenderbuffers(1, &color);
      if (depth_texture)
         glDeleteTextures(1, &depth_texture);
      if (vbo)
         glDeleteBuffers(1, &vbo);
#ifdef PS5_CORE_33_TEST
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
#endif
      if (producer)
         glDeleteProgram(producer);
      if (raw)
         glDeleteProgram(raw);
      if (shadow)
         glDeleteProgram(shadow);
      if (producer_fs)
         glDeleteShader(producer_fs);
      if (raw_fs)
         glDeleteShader(raw_fs);
      if (shadow_fs)
         glDeleteShader(shadow_fs);
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
#ifdef PS5_CORE_33_TEST
   printf("[ps5-egl-depth-texture] egl=%d.%d size=%dx%d target=%dx%d "
          "context=%d/%04x version=%s glsl=%s "
          "ext=%d/%d status=0x%x internal=0x%x setup=0x%x "
          "raw=%u/%08x/0x%x shadow=%u/%08x/0x%x "
          "cleanup=%u/0x%x/0x%x result=%d\n",
          egl_major, egl_minor, width, height, TARGET_WIDTH, TARGET_HEIGHT,
          context_version, profile_mask, version_text, glsl_text,
          depth_float, fbo_extension, framebuffer_status, internal_format,
          setup_error, raw_matching, raw_hash, raw_error,
          shadow_matching, shadow_hash, shadow_error,
          cleanup_ok, cleanup_gl_error, cleanup_egl_error,
          passed ? 0 : 1);
#else
   printf("[ps5-egl-depth-texture] egl=%d.%d size=%dx%d target=%dx%d "
          "ext=%d/%d status=0x%x internal=0x%x setup=0x%x "
          "raw=%u/%08x/0x%x shadow=%u/%08x/0x%x "
          "cleanup=%u/0x%x/0x%x result=%d\n",
          egl_major, egl_minor, width, height, TARGET_WIDTH, TARGET_HEIGHT,
          depth_float, fbo_extension, framebuffer_status, internal_format,
          setup_error, raw_matching, raw_hash, raw_error,
          shadow_matching, shadow_hash, shadow_error,
          cleanup_ok, cleanup_gl_error, cleanup_egl_error,
          passed ? 0 : 1);
#endif
   return passed ? 0 : 1;
}
