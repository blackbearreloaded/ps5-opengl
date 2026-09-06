#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define CROP_SIZE 64
#define R_WIDTH 300
#define R_HEIGHT 300
#define RG_WIDTH 300
#define RG_HEIGHT 200

struct sample {
   unsigned x, y;
   uint32_t pixel, hash;
   const char *name;
};

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
draw_oracle(GLuint texture, GLint coord, unsigned texture_width,
            unsigned texture_height, const struct sample *sample,
            uint32_t *pixels)
{
   unsigned matching = 0;

   glBindTexture(GL_TEXTURE_2D, texture);
   glUniform2f(coord, ((float)sample->x + 0.5f) / texture_width,
                      ((float)sample->y + 0.5f) / texture_height);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((WIDTH - CROP_SIZE) / 2, (HEIGHT - CROP_SIZE) / 2,
                CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   uint32_t hash = hash32(pixels, CROP_SIZE * CROP_SIZE * sizeof(*pixels));

   for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
      matching += pixels[i] == sample->pixel;
   printf("[ps5-egl-rg-multitile] %s matching=%u hash=%08x error=0x%x\n",
          sample->name, matching, hash, error);
   return matching == CROP_SIZE * CROP_SIZE && hash == sample->hash &&
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
      "uniform sampler2D u_texture;\n"
      "uniform vec2 u_coord;\n"
      "void main() { gl_FragColor = texture2D(u_texture, u_coord); }\n";
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static const uint8_t r_values[4] = {0x11, 0x55, 0x99, 0xdd};
   static const uint8_t rg_values[4][2] = {
      {0x12, 0x34}, {0x56, 0x78}, {0x9a, 0xbc}, {0xde, 0xf0},
   };
   static const struct sample r_samples[4] = {
      {64, 64, UINT32_C(0xff000011), UINT32_C(0x72399dc5), "r8-q00"},
      {280, 64, UINT32_C(0xff000055), UINT32_C(0x85a59dc5), "r8-q10"},
      {64, 280, UINT32_C(0xff000099), UINT32_C(0x36a79dc5), "r8-q01"},
      {280, 280, UINT32_C(0xff0000dd), UINT32_C(0x7ed39dc5), "r8-q11"},
   };
   static const struct sample rg_samples[4] = {
      {64, 64, UINT32_C(0xff003412), UINT32_C(0x66587dc5), "rg8-q00"},
      {280, 64, UINT32_C(0xff007856), UINT32_C(0xf1007dc5), "rg8-q10"},
      {64, 170, UINT32_C(0xff00bc9a), UINT32_C(0x2ee87dc5), "rg8-q01"},
      {280, 170, UINT32_C(0xff00f0de), UINT32_C(0x8ca07dc5), "rg8-q11"},
   };
   static uint8_t r_texels[R_WIDTH * R_HEIGHT];
   static uint8_t rg_texels[RG_WIDTH * RG_HEIGHT * 2];
   static uint32_t pixels[CROP_SIZE * CROP_SIZE];
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
   GLuint vs = 0, fs = 0, program = 0, vbo = 0, textures[2] = {0};
   GLint linked = GL_FALSE, sampler = -1, coord = -1;
   const GLubyte *version = NULL, *glsl = NULL;
   const char *extensions = NULL;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, extension_present = 0, r_ok = 1, rg_ok = 1;
   int passed = 0;

   for (unsigned y = 0; y < R_HEIGHT; ++y) {
      for (unsigned x = 0; x < R_WIDTH; ++x)
         r_texels[y * R_WIDTH + x] = r_values[(y >= 256) * 2 + (x >= 256)];
   }
   for (unsigned y = 0; y < RG_HEIGHT; ++y) {
      for (unsigned x = 0; x < RG_WIDTH; ++x) {
         unsigned quadrant = (y >= 128) * 2 + (x >= 256);
         unsigned offset = 2 * (y * RG_WIDTH + x);

         rg_texels[offset] = rg_values[quadrant][0];
         rg_texels[offset + 1] = rg_values[quadrant][1];
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
   version = glGetString(GL_VERSION);
   glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!version || !glsl || !extensions)
      goto cleanup;
   extension_present = has_extension(extensions, "GL_ARB_texture_rg");

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
   coord = glGetUniformLocation(program, "u_coord");
   if (sampler < 0 || coord < 0)
      goto cleanup;
   glUniform1i(sampler, 0);

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);
   glGenTextures(2, textures);
   for (unsigned i = 0; i < 2; ++i) {
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   }
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, R_WIDTH, R_HEIGHT, 0,
                GL_RED, GL_UNSIGNED_BYTE, r_texels);
   glBindTexture(GL_TEXTURE_2D, textures[1]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, RG_WIDTH, RG_HEIGHT, 0,
                GL_RG, GL_UNSIGNED_BYTE, rg_texels);
   if (glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   for (unsigned i = 0; i < 4; ++i)
      r_ok &= draw_oracle(textures[0], coord, R_WIDTH, R_HEIGHT,
                          &r_samples[i], pixels);
   for (unsigned i = 0; i < 4; ++i)
      rg_ok &= draw_oracle(textures[1], coord, RG_WIDTH, RG_HEIGHT,
                           &rg_samples[i], pixels);
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && extension_present && r_ok && rg_ok &&
            strncmp((const char *)version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl, "1.20", 4) == 0 &&
            eglSwapBuffers(display, surface);

cleanup:
   if (textures[0])
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
   printf("[ps5-egl-rg-multitile] ext=%d cases=%d/%d "
          "cleanup=%u/0x%x/0x%x result=%d\n",
          extension_present, r_ok, rg_ok, cleanup_ok, cleanup_gl_error,
          cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
