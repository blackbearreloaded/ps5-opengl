#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define SIZE 64
#define LEVEL0_PIXEL UINT32_C(0xff000020)
#define LEVEL0_HASH UINT32_C(0xab831dc5)
#define LEVEL1_PIXEL UINT32_C(0xff008000)
#define LEVEL1_HASH UINT32_C(0xcec31dc5)
#define GENERATED_PIXEL UINT32_C(0xff800040)
#define GENERATED_HASH UINT32_C(0xdac31dc5)

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

static int
draw_oracle(GLint lod, float lod_value, uint32_t expected,
            uint32_t expected_hash,
            uint32_t *pixels)
{
   unsigned matching = 0;

   glUniform1f(lod, lod_value);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   uint32_t hash = hash32(pixels, SIZE * SIZE * sizeof(*pixels));

   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      matching += pixels[i] == expected;
   printf("[ps5-egl-mipmap] lod=%.1f matching=%u hash=%08x error=0x%x\n",
          (double)lod_value, matching, hash, error);
   return matching == SIZE * SIZE && hash == expected_hash &&
          error == GL_NO_ERROR;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 120\n"
      "#extension GL_ARB_shader_texture_lod : require\n"
      "uniform sampler2D u_texture;\n"
      "uniform float u_lod;\n"
      "void main() {\n"
      "  gl_FragColor = texture2DLod(u_texture, vec2(0.5), u_lod);\n"
      "}\n";
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static uint32_t level0[SIZE * SIZE];
   static uint32_t level1[(SIZE / 2) * (SIZE / 2)];
   static uint32_t generated_level0[SIZE * SIZE];
   static uint32_t pixels[SIZE * SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0, width = 0, height = 0;
   GLuint vs = 0, fs = 0, program = 0, vbo = 0, textures[2] = {0, 0};
   GLint linked = GL_FALSE, sampler = -1, lod = -1;
   const GLubyte *version = NULL, *glsl = NULL;
   const char *extensions = NULL;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, extension_present = 0;
   GLfloat max_lod_bias = 0.0f;
   int level0_ok = 0, level1_ok = 0, clamp_ok = 0, generated_ok = 0;
   int passed = 0;

   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      level0[i] = LEVEL0_PIXEL;
   for (unsigned i = 0; i < (SIZE / 2) * (SIZE / 2); ++i)
      level1[i] = LEVEL1_PIXEL;
   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      generated_level0[i] = GENERATED_PIXEL;

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
   glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!version || !glsl || !extensions)
      goto cleanup;
   extension_present = has_extension(extensions, "GL_ARB_shader_texture_lod");

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
   lod = glGetUniformLocation(program, "u_lod");
   if (sampler < 0 || lod < 0)
      goto cleanup;
   glUniform1i(sampler, 0);

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(2, textures);
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                   GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, level0);
   glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, SIZE / 2, SIZE / 2, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, level1);

   glBindTexture(GL_TEXTURE_2D, textures[1]);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                   GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 6);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, SIZE, SIZE, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, generated_level0);
   glGenerateMipmap(GL_TEXTURE_2D);

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glGetFloatv(GL_MAX_TEXTURE_LOD_BIAS, &max_lod_bias);
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   level0_ok = draw_oracle(lod, 0.0f, LEVEL0_PIXEL, LEVEL0_HASH, pixels);
   level1_ok = draw_oracle(lod, 1.0f, LEVEL1_PIXEL, LEVEL1_HASH, pixels);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, 1.0f);
   glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 1.0f);
   clamp_ok = draw_oracle(lod, 0.0f, LEVEL1_PIXEL, LEVEL1_HASH, pixels);
   glBindTexture(GL_TEXTURE_2D, textures[1]);
   generated_ok = draw_oracle(lod, 6.0f, GENERATED_PIXEL, GENERATED_HASH,
                              pixels);
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
             height == HEIGHT && extension_present && max_lod_bias >= 16.0f &&
            level0_ok && level1_ok && clamp_ok && generated_ok &&
            strncmp((const char *)version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl, "1.20", 4) == 0;

cleanup:
   if (textures[0] || textures[1])
      glDeleteTextures(2, textures);
   if (vbo)
      glDeleteBuffers(1, &vbo);
   if (program)
      glDeleteProgram(program);
   if (fs)
      glDeleteShader(fs);
   if (vs)
      glDeleteShader(vs);
   if (made_current)
      cleanup_gl_error = glGetError();
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_egl_error = eglGetError();
   passed = passed && cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
            cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-mipmap] ext=%d max-bias=%.1f levels=%d/%d clamp=%d "
          "generated=%d cleanup=%x/%x result=%d\n", extension_present,
          (double)max_lod_bias, level0_ok, level1_ok, clamp_ok, generated_ok,
          cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
