#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define SIZE 64

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

#ifndef PS5_CORE33_ARRAY_MIPMAP_TEST
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
#endif

static int
draw_oracle(GLint layer_uniform, GLint lod_uniform, float layer, float lod,
            uint32_t expected,
            uint32_t expected_hash, uint32_t *pixels)
{
   unsigned matching = 0;

   glUniform1f(layer_uniform, layer);
   if (lod_uniform >= 0)
      glUniform1f(lod_uniform, lod);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   uint32_t hash = hash32(pixels, SIZE * SIZE * sizeof(*pixels));

   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      matching += pixels[i] == expected;
   printf("[ps5-egl-array] layer=%.1f lod=%.1f matching=%u hash=%08x error=0x%x\n",
          (double)layer, (double)lod, matching, hash, error);
   return matching == SIZE * SIZE && hash == expected_hash &&
          error == GL_NO_ERROR;
}

int
main(void)
{
#ifdef PS5_CORE33_ARRAY_MIPMAP_TEST
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform sampler2DArray u_array;\n"
      "uniform float u_layer;\n"
      "uniform float u_lod;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  color = textureLod(u_array, vec3(0.5, 0.5, u_layer), u_lod);\n"
      "}\n";
#else
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 120\n"
      "#extension GL_EXT_texture_array : require\n"
      "uniform sampler2DArray u_array;\n"
      "uniform float u_layer;\n"
      "void main() {\n"
      "  gl_FragColor = texture2DArray(u_array, vec3(0.5, 0.5, u_layer));\n"
      "}\n";
#endif
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static const uint32_t colors[3] = {
      UINT32_C(0xff000080), UINT32_C(0xff008000), UINT32_C(0xff800000),
   };
   static const uint32_t hashes[3] = {
      UINT32_C(0x0ec31dc5), UINT32_C(0xcec31dc5), UINT32_C(0x8ec31dc5),
   };
   static uint32_t texels[3][SIZE * SIZE];
   static uint32_t pixels[SIZE * SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
#ifdef PS5_CORE33_ARRAY_MIPMAP_TEST
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
#else
   const EGLint *context_attributes = NULL;
#endif
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0, width = 0, height = 0;
   GLuint vs = 0, fs = 0, program = 0, vao = 0, vbo = 0, texture = 0;
   GLint linked = GL_FALSE, sampler = -1, layer = -1, lod = -1;
   GLint max_layers = 0;
   const GLubyte *version = NULL, *glsl = NULL;
#ifndef PS5_CORE33_ARRAY_MIPMAP_TEST
   const char *extensions = NULL;
#endif
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, extension_present = 0, layers_ok = 1;
   int mipmaps_ok = 1, passed = 0;

   for (unsigned z = 0; z < 3; ++z)
      for (unsigned i = 0; i < SIZE * SIZE; ++i)
         texels[z][i] = colors[z];

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
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
   eglQuerySurface(display, surface, EGL_WIDTH, &width);
   eglQuerySurface(display, surface, EGL_HEIGHT, &height);
   version = glGetString(GL_VERSION);
   glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
   glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS_EXT, &max_layers);
   if (!version || !glsl)
      goto cleanup;
#ifdef PS5_CORE33_ARRAY_MIPMAP_TEST
   extension_present = 1;
#else
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!extensions)
      goto cleanup;
   extension_present = has_extension(extensions, "GL_EXT_texture_array");
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
   sampler = glGetUniformLocation(program, "u_array");
   layer = glGetUniformLocation(program, "u_layer");
   lod = glGetUniformLocation(program, "u_lod");
   if (sampler < 0 || layer < 0
#ifdef PS5_CORE33_ARRAY_MIPMAP_TEST
       || lod < 0
#endif
      )
      goto cleanup;
   glUniform1i(sampler, 0);

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D_ARRAY_EXT, texture);
   glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_MIN_FILTER,
#ifdef PS5_CORE33_ARRAY_MIPMAP_TEST
                   GL_NEAREST_MIPMAP_NEAREST);
#else
                   GL_NEAREST);
#endif
   glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D_ARRAY_EXT, GL_TEXTURE_MAX_LEVEL,
#ifdef PS5_CORE33_ARRAY_MIPMAP_TEST
                   6);
#else
                   0);
#endif
   glTexImage3D(GL_TEXTURE_2D_ARRAY_EXT, 0, GL_RGBA8, SIZE, SIZE, 3, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, texels);
#ifdef PS5_CORE33_ARRAY_MIPMAP_TEST
   glGenerateMipmap(GL_TEXTURE_2D_ARRAY_EXT);
#endif

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   for (unsigned z = 0; z < 3; ++z)
      layers_ok &= draw_oracle(layer, lod, (float)z, 0.0f,
                               colors[z], hashes[z], pixels);
#ifdef PS5_CORE33_ARRAY_MIPMAP_TEST
   for (unsigned z = 0; z < 3; ++z)
      mipmaps_ok &= draw_oracle(layer, lod, (float)z, 6.0f,
                                colors[z], hashes[z], pixels);
#endif
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && extension_present && max_layers >= 256 &&
            layers_ok && mipmaps_ok &&
#ifdef PS5_CORE33_ARRAY_MIPMAP_TEST
            strncmp((const char *)version, "3.3 ", 4) == 0 &&
            strncmp((const char *)glsl, "3.30", 4) == 0;
#else
            strncmp((const char *)version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl, "1.20", 4) == 0;
#endif

cleanup:
   if (texture)
      glDeleteTextures(1, &texture);
   if (vbo)
      glDeleteBuffers(1, &vbo);
   if (vao)
      glDeleteVertexArrays(1, &vao);
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
   printf("[ps5-egl-array] ext=%d max=%d layers=%d mipmaps=%d cleanup=%x/%x result=%d\n",
          extension_present, max_layers, layers_ok, mipmaps_ok,
          cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
