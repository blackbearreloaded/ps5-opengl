#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define WIDTH 1920
#define HEIGHT 1080
#define SIZE 64
#define R_PIXEL UINT32_C(0xff000040)
#define R_HASH UINT32_C(0x4bc31dc5)
#define RG_PIXEL UINT32_C(0xff008020)
#define RG_HASH UINT32_C(0x11831dc5)
#define SNORM_PIXEL UINT32_C(0xff0000ff)
#define SNORM_HASH UINT32_C(0xc40abdc5)
#define FLOAT16_PIXEL UINT32_C(0xffffff00)
#define FLOAT16_HASH UINT32_C(0xf1461dc5)
#define FLOAT32_PIXEL UINT32_C(0xff00ffff)
#define FLOAT32_HASH UINT32_C(0x5f3a1dc5)
#define SHARED_EXP_PIXEL UINT32_C(0xffff00ff)
#define SHARED_EXP_HASH UINT32_C(0x64e31dc5)
#ifdef PS5_RGTC_FALLBACK_TEST
#define TEXTURE_COUNT 10
#define RGTC1_PIXEL UINT32_C(0xff0000ff)
#define RGTC1_HASH UINT32_C(0xc40abdc5)
#define RGTC2_PIXEL UINT32_C(0xff00ffff)
#define RGTC2_HASH UINT32_C(0x5f3a1dc5)
#elif defined(PS5_TEXTURE_RG_DEFAULT_ONLY)
#define TEXTURE_COUNT 2
#else
#define TEXTURE_COUNT 6
#endif

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint ok = GL_FALSE;
   GLuint shader = glCreateShader(type);
   if (!shader)
      return -1;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
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

#ifndef PS5_CORE_33_TEST
static int
has_extension(const char *extensions, const char *name)
{
   size_t length = strlen(name);
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
   for (GLint index = 0; index < count; ++index) {
      const char *extension = (const char *)glGetStringi(
         GL_EXTENSIONS, (GLuint)index);
      if (extension && strcmp(extension, name) == 0)
         return 1;
   }
   return 0;
}
#endif

static int
draw_oracle(GLuint texture, uint32_t expected, uint32_t expected_hash,
            uint32_t *pixels, const char *name)
{
   unsigned matching = 0;

   glBindTexture(GL_TEXTURE_2D, texture);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   uint32_t hash = hash32(pixels, SIZE * SIZE * sizeof(*pixels));
   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      matching += pixels[i] == expected;
   printf("[ps5-egl-rg] %s matching=%u hash=%08x error=0x%x\n",
          name, matching, hash, error);
   return matching == SIZE * SIZE && hash == expected_hash &&
          error == GL_NO_ERROR;
}

int
main(void)
{
#ifdef PS5_CORE_33_TEST
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330\n"
      "uniform sampler2D u_texture;\n"
      "out vec4 color;\n"
      "void main() { color = textureLod(u_texture, vec2(0.5), 0.0); }\n";
#else
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 120\n"
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
      "#extension GL_ARB_shader_texture_lod : require\n"
#endif
      "uniform sampler2D u_texture;\n"
#ifdef PS5_TEXTURE_RG_DEFAULT_ONLY
      "void main() { gl_FragColor = texture2D(u_texture, vec2(0.5)); }\n";
#else
      "void main() { gl_FragColor = texture2DLod(u_texture, vec2(0.5), 0.0); }\n";
#endif
#endif
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static uint8_t r_texels[SIZE * SIZE];
   static uint8_t rg_texels[SIZE * SIZE * 2];
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
   static int8_t snorm_texels[SIZE * SIZE * 4];
   static float float16_texels[SIZE * SIZE * 4];
   static float float32_texels[SIZE * SIZE * 4];
   static float shared_exp_texels[SIZE * SIZE * 3];
#endif
#ifdef PS5_RGTC_FALLBACK_TEST
   static uint8_t rgtc1_unorm[SIZE * SIZE / 2];
   static uint8_t rgtc1_snorm[SIZE * SIZE / 2];
   static uint8_t rgtc2_unorm[SIZE * SIZE];
   static uint8_t rgtc2_snorm[SIZE * SIZE];
#endif
   static uint32_t pixels[SIZE * SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
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
   EGLint egl_major = 0, egl_minor = 0, count = 0, width = 0, height = 0;
   EGLint context_version = 0, profile_mask = 0;
   GLuint vs = 0, fs = 0, program = 0, vbo = 0;
#ifdef PS5_CORE_33_TEST
   GLuint vertex_array = 0;
#endif
   GLuint textures[TEXTURE_COUNT] = {0};
   GLint linked = GL_FALSE, sampler = -1;
   const GLubyte *version = NULL, *glsl = NULL;
#ifndef PS5_CORE_33_TEST
   const char *extensions = NULL;
#endif
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   EGLBoolean swap_ok = EGL_FALSE;
   EGLBoolean unbind_ok = EGL_TRUE;
   EGLBoolean destroy_context_ok = EGL_TRUE;
   EGLBoolean destroy_surface_ok = EGL_TRUE;
   EGLBoolean terminate_ok = EGL_TRUE;
   int made_current = 0, extension_present = 0;
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
   int snorm_extension_present = 0;
   int float_extension_present = 0;
   int shared_exp_extension_present = 0;
#endif
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
   int texture_lod_extension_present = 0;
   int rgtc_extension_present = 1;
#ifdef PS5_RGTC_FALLBACK_TEST
   int snorm_ok = 1, float16_ok = 1, float32_ok = 1;
   int shared_exp_ok = 1;
#else
   int snorm_ok = 0, float16_ok = 0, float32_ok = 0;
   int shared_exp_ok = 0;
#endif
   int rgtc1_unorm_ok = 1, rgtc1_snorm_ok = 1;
   int rgtc2_unorm_ok = 1, rgtc2_snorm_ok = 1;
#endif
#ifdef PS5_RGTC_FALLBACK_TEST
   int r_ok = 1, rg_ok = 1;
#else
   int r_ok = 0, rg_ok = 0;
#endif
   int passed = 0;

   memset(r_texels, 0x40, sizeof(r_texels));
   for (unsigned i = 0; i < SIZE * SIZE; ++i) {
      rg_texels[2 * i] = 0x20;
      rg_texels[2 * i + 1] = 0x80;
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
      snorm_texels[4 * i] = 127;
      snorm_texels[4 * i + 1] = -127;
      snorm_texels[4 * i + 2] = 0;
      snorm_texels[4 * i + 3] = 127;
      float16_texels[4 * i] = 0.0f;
      float16_texels[4 * i + 1] = 1.0f;
      float16_texels[4 * i + 2] = 1.0f;
      float16_texels[4 * i + 3] = 1.0f;
      float32_texels[4 * i] = 1.0f;
      float32_texels[4 * i + 1] = 1.0f;
      float32_texels[4 * i + 2] = 0.0f;
      float32_texels[4 * i + 3] = 1.0f;
      shared_exp_texels[3 * i] = 1.0f;
      shared_exp_texels[3 * i + 1] = 0.0f;
      shared_exp_texels[3 * i + 2] = 1.0f;
#endif
   }
#ifdef PS5_RGTC_FALLBACK_TEST
   for (unsigned block = 0; block < SIZE * SIZE / 16; ++block) {
      uint8_t *r1u = rgtc1_unorm + block * 8;
      uint8_t *r1s = rgtc1_snorm + block * 8;
      uint8_t *r2u = rgtc2_unorm + block * 16;
      uint8_t *r2s = rgtc2_snorm + block * 16;

      r1u[0] = 255;
      r1u[1] = 0;
      memset(r1u + 2, 0, 6);
      r1s[0] = 127;
      r1s[1] = 0x81;
      memset(r1s + 2, 0, 6);
      memcpy(r2u, r1u, 8);
      memcpy(r2u + 8, r1u, 8);
      memcpy(r2s, r1s, 8);
      memcpy(r2s + 8, r1s, 8);
   }
#endif

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
   eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION,
                   &context_version);
#ifdef PS5_CORE_33_TEST
   eglQueryContext(display, context, EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                   &profile_mask);
#endif
   version = glGetString(GL_VERSION);
   glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
#ifdef PS5_CORE_33_TEST
   if (!version || !glsl)
      goto cleanup;
   extension_present = has_core_extension("GL_ARB_texture_rg");
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
   snorm_extension_present = has_core_extension("GL_EXT_texture_snorm");
   float_extension_present = has_core_extension("GL_ARB_texture_float");
   shared_exp_extension_present =
      has_core_extension("GL_EXT_texture_shared_exponent");
   texture_lod_extension_present =
      has_core_extension("GL_ARB_shader_texture_lod");
#endif
#ifdef PS5_RGTC_FALLBACK_TEST
   rgtc_extension_present =
      has_core_extension("GL_ARB_texture_compression_rgtc");
#endif
#else
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!version || !glsl || !extensions)
      goto cleanup;
   extension_present = has_extension(extensions, "GL_ARB_texture_rg");
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
   snorm_extension_present = has_extension(extensions, "GL_EXT_texture_snorm");
   float_extension_present = has_extension(extensions, "GL_ARB_texture_float");
   shared_exp_extension_present =
      has_extension(extensions, "GL_EXT_texture_shared_exponent");
#endif
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
   texture_lod_extension_present =
      has_extension(extensions, "GL_ARB_shader_texture_lod");
#endif
#ifdef PS5_RGTC_FALLBACK_TEST
   rgtc_extension_present =
      has_extension(extensions, "GL_ARB_texture_compression_rgtc");
#endif
#endif

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fs))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glBindAttribLocation(program, 0, "a_position");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);
   sampler = glGetUniformLocation(program, "u_texture");
   if (sampler < 0)
      goto cleanup;
   glUniform1i(sampler, 0);

#ifdef PS5_CORE_33_TEST
   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
#endif
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);
   glGenTextures(TEXTURE_COUNT, textures);
   for (unsigned i = 0; i < TEXTURE_COUNT; ++i) {
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   }
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, SIZE, SIZE, 0,
                GL_RED, GL_UNSIGNED_BYTE, r_texels);
   glBindTexture(GL_TEXTURE_2D, textures[1]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, SIZE, SIZE, 0,
                GL_RG, GL_UNSIGNED_BYTE, rg_texels);
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
   glBindTexture(GL_TEXTURE_2D, textures[2]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_SNORM, SIZE, SIZE, 0,
                GL_RGBA, GL_BYTE, snorm_texels);
   glBindTexture(GL_TEXTURE_2D, textures[3]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, SIZE, SIZE, 0,
                GL_RGBA, GL_FLOAT, float16_texels);
   glBindTexture(GL_TEXTURE_2D, textures[4]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, SIZE, SIZE, 0,
                GL_RGBA, GL_FLOAT, float32_texels);
   glBindTexture(GL_TEXTURE_2D, textures[5]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB9_E5, SIZE, SIZE, 0,
                GL_RGB, GL_FLOAT, shared_exp_texels);
#endif
#ifdef PS5_RGTC_FALLBACK_TEST
   glBindTexture(GL_TEXTURE_2D, textures[6]);
   glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RED_RGTC1,
                          SIZE, SIZE, 0, sizeof(rgtc1_unorm), rgtc1_unorm);
   glBindTexture(GL_TEXTURE_2D, textures[7]);
   glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_SIGNED_RED_RGTC1,
                          SIZE, SIZE, 0, sizeof(rgtc1_snorm), rgtc1_snorm);
   glBindTexture(GL_TEXTURE_2D, textures[8]);
   glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RG_RGTC2,
                          SIZE, SIZE, 0, sizeof(rgtc2_unorm), rgtc2_unorm);
   glBindTexture(GL_TEXTURE_2D, textures[9]);
   glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_SIGNED_RG_RGTC2,
                          SIZE, SIZE, 0, sizeof(rgtc2_snorm), rgtc2_snorm);
#endif

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
#ifndef PS5_RGTC_FALLBACK_TEST
   r_ok = draw_oracle(textures[0], R_PIXEL, R_HASH, pixels, "r8");
   rg_ok = draw_oracle(textures[1], RG_PIXEL, RG_HASH, pixels, "rg8");
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
   snorm_ok = draw_oracle(textures[2], SNORM_PIXEL, SNORM_HASH, pixels,
                          "rgba8-snorm");
   float16_ok = draw_oracle(textures[3], FLOAT16_PIXEL, FLOAT16_HASH, pixels,
                            "rgba16-float");
   float32_ok = draw_oracle(textures[4], FLOAT32_PIXEL, FLOAT32_HASH, pixels,
                            "rgba32-float");
   shared_exp_ok = draw_oracle(textures[5], SHARED_EXP_PIXEL, SHARED_EXP_HASH,
                               pixels, "rgb9-e5");
#endif
#endif
#ifdef PS5_RGTC_FALLBACK_TEST
   rgtc1_unorm_ok = draw_oracle(textures[6], RGTC1_PIXEL, RGTC1_HASH,
                                pixels, "rgtc1-unorm-fallback");
   rgtc1_snorm_ok = draw_oracle(textures[7], RGTC1_PIXEL, RGTC1_HASH,
                                pixels, "rgtc1-snorm-fallback");
   rgtc2_unorm_ok = draw_oracle(textures[8], RGTC2_PIXEL, RGTC2_HASH,
                                pixels, "rgtc2-unorm-fallback");
   rgtc2_snorm_ok = draw_oracle(textures[9], RGTC2_PIXEL, RGTC2_HASH,
                                pixels, "rgtc2-snorm-fallback");
#endif
   swap_ok = eglSwapBuffers(display, surface);
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && extension_present && r_ok && rg_ok &&
#ifndef PS5_TEXTURE_RG_DEFAULT_ONLY
            texture_lod_extension_present &&
            snorm_extension_present &&
            float_extension_present && shared_exp_extension_present &&
            snorm_ok && float16_ok && float32_ok &&
            shared_exp_ok && rgtc_extension_present &&
            rgtc1_unorm_ok && rgtc1_snorm_ok &&
            rgtc2_unorm_ok && rgtc2_snorm_ok &&
#endif
#ifdef PS5_CORE_33_TEST
            context_version == 3 &&
            profile_mask == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            strncmp((const char *)version, "3.3 ", 4) == 0 &&
            strncmp((const char *)glsl, "3.30", 4) == 0 && swap_ok;
#else
            strncmp((const char *)version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl, "1.20", 4) == 0 && swap_ok;
#endif
#ifdef PS5_TEXTURE_RG_DEFAULT_ONLY
   printf("[ps5-egl-rg] state egl=%d.%d size=%dx%d context=%d/%04x "
          "version=%s glsl=%s "
          "cases=%d/%d swap=%u pre=%d\n",
          egl_major, egl_minor, width, height, context_version, profile_mask,
          version, glsl,
          r_ok, rg_ok, swap_ok, passed);
#else
   printf("[ps5-egl-rg] state egl=%d.%d size=%dx%d context=%d/%04x "
          "version=%s glsl=%s "
          "cases=%d/%d/%d/%d/%d/%d swap=%u pre=%d\n",
          egl_major, egl_minor, width, height, context_version, profile_mask,
          version, glsl,
          r_ok, rg_ok, snorm_ok, float16_ok, float32_ok, shared_exp_ok,
          swap_ok, passed);
#endif

cleanup:
   if (textures[0])
      glDeleteTextures(TEXTURE_COUNT, textures);
   if (vbo)
      glDeleteBuffers(1, &vbo);
#ifdef PS5_CORE_33_TEST
   if (vertex_array)
      glDeleteVertexArrays(1, &vertex_array);
#endif
   if (program)
      glDeleteProgram(program);
   if (fs)
      glDeleteShader(fs);
   if (vs)
      glDeleteShader(vs);
   if (made_current)
      cleanup_gl_error = glGetError();
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
      unbind_ok = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                 EGL_NO_CONTEXT);
      cleanup_ok &= unbind_ok;
      destroy_context_ok = eglDestroyContext(display, context);
      cleanup_ok &= destroy_context_ok;
   }
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE) {
      destroy_surface_ok = eglDestroySurface(display, surface);
      cleanup_ok &= destroy_surface_ok;
   }
   if (display != EGL_NO_DISPLAY) {
      terminate_ok = eglTerminate(display);
      cleanup_ok &= terminate_ok;
   }
   cleanup_egl_error = eglGetError();
   passed = passed && cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
            cleanup_egl_error == EGL_SUCCESS;
#ifdef PS5_TEXTURE_RG_DEFAULT_ONLY
   printf("[ps5-egl-rg] ext=%d "
          "cleanup=%u/%u/%u/%u/%u/0x%x/0x%x result=%d\n",
          extension_present, cleanup_ok, unbind_ok, destroy_context_ok,
          destroy_surface_ok, terminate_ok, cleanup_gl_error, cleanup_egl_error,
          passed ? 0 : 1);
#else
   printf("[ps5-egl-rg] ext=%d/%d/%d/%d/%d/%d "
          "cleanup=%u/%u/%u/%u/%u/0x%x/0x%x result=%d\n",
          extension_present, snorm_extension_present, float_extension_present,
          shared_exp_extension_present, texture_lod_extension_present,
          rgtc_extension_present, cleanup_ok, unbind_ok, destroy_context_ok,
          destroy_surface_ok, terminate_ok, cleanup_gl_error, cleanup_egl_error,
          passed ? 0 : 1);
#endif
   return passed ? 0 : 1;
}
