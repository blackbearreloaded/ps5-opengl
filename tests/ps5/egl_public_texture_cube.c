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
#define SEAM_PIXEL UINT32_C(0xff400040)
#define SEAM_HASH UINT32_C(0xdec31dc5)

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
draw_oracle(GLint direction_uniform, GLint lod_uniform,
            const float direction[3], float lod,
            uint32_t expected, uint32_t expected_hash, uint32_t *pixels,
            const char *name)
{
   unsigned matching = 0;

   glUniform3fv(direction_uniform, 1, direction);
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
   printf("[ps5-egl-cube] %s lod=%.1f matching=%u hash=%08x error=0x%x\n",
          name, (double)lod, matching, hash, error);
   return matching == SIZE * SIZE && hash == expected_hash &&
          error == GL_NO_ERROR;
}

int
main(void)
{
#ifdef PS5_CORE33_CUBE_MIPMAP_TEST
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform samplerCube u_cube;\n"
      "uniform vec3 u_direction;\n"
      "uniform float u_lod;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = textureLod(u_cube, u_direction, u_lod); }\n";
#else
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 120\n"
      "uniform samplerCube u_cube;\n"
      "uniform vec3 u_direction;\n"
      "void main() { gl_FragColor = textureCube(u_cube, u_direction); }\n";
#endif
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static const GLenum targets[6] = {
      GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
      GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
      GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
   };
   static const uint32_t colors[6] = {
      UINT32_C(0xff000080), UINT32_C(0xff008000),
      UINT32_C(0xff800000), UINT32_C(0xff008080),
      UINT32_C(0xff800080), UINT32_C(0xff808000),
   };
   static const uint32_t hashes[6] = {
      UINT32_C(0x0ec31dc5), UINT32_C(0xcec31dc5),
      UINT32_C(0x8ec31dc5), UINT32_C(0xfec31dc5),
      UINT32_C(0x1ec31dc5), UINT32_C(0x3ec31dc5),
   };
   static const float directions[6][3] = {
      {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
      {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
   };
#ifndef PS5_SKIP_SEAMLESS_CUBE_TEST
   static const float xy_edge[3] = {1.0f, 1.0f, 0.0f};
#endif
   static const char *names[6] = {"+x", "-x", "+y", "-y", "+z", "-z"};
   static uint32_t face_texels[6][SIZE * SIZE];
   static uint32_t pixels[SIZE * SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
#ifdef PS5_CORE33_CUBE_MIPMAP_TEST
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
   GLint linked = GL_FALSE, sampler = -1, direction = -1, lod = -1;
   GLint max_cube_size = 0;
   GLboolean seamless = GL_FALSE, nonseamless = GL_TRUE;
   const GLubyte *version = NULL, *glsl = NULL;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, faces_ok = 1, mipmaps_ok = 1;
   int edge_ok = 1, passed = 0;

   for (unsigned face = 0; face < 6; ++face)
      for (unsigned i = 0; i < SIZE * SIZE; ++i)
         face_texels[face][i] = colors[face];

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
   glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &max_cube_size);
   if (!version || !glsl)
      goto cleanup;

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
   sampler = glGetUniformLocation(program, "u_cube");
   direction = glGetUniformLocation(program, "u_direction");
   lod = glGetUniformLocation(program, "u_lod");
   if (sampler < 0 || direction < 0
#ifdef PS5_CORE33_CUBE_MIPMAP_TEST
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
   glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
#ifdef PS5_CORE33_CUBE_MIPMAP_TEST
                   GL_NEAREST_MIPMAP_NEAREST);
#else
                   GL_NEAREST);
#endif
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL,
#ifdef PS5_CORE33_CUBE_MIPMAP_TEST
                   6);
#else
                   0);
#endif
#ifndef PS5_SKIP_SEAMLESS_CUBE_TEST
   glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
   seamless = glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);
#else
   seamless = GL_TRUE;
   nonseamless = GL_FALSE;
#endif
   for (unsigned face = 0; face < 6; ++face)
      glTexImage2D(targets[face], 0, GL_RGBA8, SIZE, SIZE, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, face_texels[face]);
#ifdef PS5_CORE33_CUBE_MIPMAP_TEST
   glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
#endif

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   for (unsigned face = 0; face < 6; ++face)
      faces_ok &= draw_oracle(direction, lod, directions[face], 0.0f,
                              colors[face], hashes[face], pixels, names[face]);
#ifdef PS5_CORE33_CUBE_MIPMAP_TEST
   for (unsigned face = 0; face < 6; ++face)
      mipmaps_ok &= draw_oracle(direction, lod, directions[face], 6.0f,
                                colors[face], hashes[face], pixels,
                                names[face]);
#endif
#ifndef PS5_SKIP_SEAMLESS_CUBE_TEST
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
   nonseamless = glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);
   edge_ok &= draw_oracle(direction, lod, xy_edge, 0.0f, colors[2], hashes[2],
                          pixels, "xy-edge-nonseamless");
   glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
   seamless = glIsEnabled(GL_TEXTURE_CUBE_MAP_SEAMLESS);
   edge_ok &= draw_oracle(direction, lod, xy_edge, 0.0f, SEAM_PIXEL,
                          SEAM_HASH, pixels, "xy-edge-seamless");
#endif
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && max_cube_size >= 2048 &&
            seamless == GL_TRUE && nonseamless == GL_FALSE &&
            faces_ok && mipmaps_ok && edge_ok &&
#ifdef PS5_CORE33_CUBE_MIPMAP_TEST
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
   printf("[ps5-egl-cube] max=%d faces=%d mipmaps=%d edge=%d state=%u/%u "
          "cleanup=%x/%x result=%d\n",
          max_cube_size, faces_ok, mipmaps_ok, edge_ok, nonseamless, seamless,
          cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
