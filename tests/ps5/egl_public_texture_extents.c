#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define TEX_WIDTH 37
#define TEX_HEIGHT 23
#define CROP_SIZE 64

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
draw_oracle(GLint coord, float s, float t, uint32_t expected,
            uint32_t expected_hash, uint32_t *pixels, const char *name)
{
   unsigned matching = 0;

   glUniform2f(coord, s, t);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((WIDTH - CROP_SIZE) / 2, (HEIGHT - CROP_SIZE) / 2,
                CROP_SIZE, CROP_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   uint32_t hash = hash32(pixels, CROP_SIZE * CROP_SIZE * sizeof(*pixels));
   for (unsigned i = 0; i < CROP_SIZE * CROP_SIZE; ++i)
      matching += pixels[i] == expected;
   printf("[ps5-egl-extents] %s matching=%u hash=%08x error=0x%x\n",
          name, matching, hash, error);
   return matching == CROP_SIZE * CROP_SIZE && hash == expected_hash &&
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
   static uint8_t texels[TEX_WIDTH * TEX_HEIGHT * 4];
   static const uint8_t filter_texels[16] = {
      0x00, 0x00, 0x00, 0xff, 0x40, 0x40, 0x40, 0xff,
      0x80, 0x80, 0x80, 0xff, 0xc0, 0xc0, 0xc0, 0xff,
   };
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
   GLuint vs = 0, fs = 0, program = 0, vbo = 0, texture = 0;
   GLuint samplers[2] = {0, 0};
   GLint linked = GL_FALSE, sampler = -1, coord = -1;
   const GLubyte *version = NULL, *glsl = NULL;
   const char *extensions = NULL;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, first_ok = 0, second_ok = 0;
   int repeat_ok = 0, mirror_a_ok = 0, mirror_b_ok = 0;
   int linear_ok = 0, sampler_ext = 0, sampler_repeat_a_ok = 0;
   int sampler_linear_ok = 0, sampler_repeat_b_ok = 0, passed = 0;

   for (unsigned y = 0; y < TEX_HEIGHT; ++y) {
      for (unsigned x = 0; x < TEX_WIDTH; ++x) {
         unsigned offset = 4 * (y * TEX_WIDTH + x);
         texels[offset] = x;
         texels[offset + 1] = y;
         texels[offset + 2] = x ^ y;
         texels[offset + 3] = 0xff;
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
   sampler_ext = has_extension(extensions, "GL_ARB_sampler_objects");

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
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TEX_WIDTH, TEX_HEIGHT, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, texels);
   if (glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   first_ok = draw_oracle(coord, 0.75f, 0.75f, UINT32_C(0xff0a111b),
                          UINT32_C(0x1a767dc5), pixels, "27x17");
   second_ok = draw_oracle(coord, 0.125f, 0.125f, UINT32_C(0xff060204),
                           UINT32_C(0xb77dfdc5), pixels, "4x2");
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, filter_texels);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   repeat_ok = draw_oracle(coord, -0.25f, 0.25f, UINT32_C(0xff404040),
                           UINT32_C(0x9d431dc5), pixels, "repeat");
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
   mirror_a_ok = draw_oracle(coord, 1.25f, 0.25f, UINT32_C(0xff404040),
                             UINT32_C(0x9d431dc5), pixels, "mirror-1.25");
   mirror_b_ok = draw_oracle(coord, 2.25f, 0.25f, UINT32_C(0xff000000),
                             UINT32_C(0x1ec31dc5), pixels, "mirror-2.25");
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   linear_ok = draw_oracle(coord, 0.5f, 0.5f, UINT32_C(0xff606060),
                           UINT32_C(0x9b831dc5), pixels, "linear");
   glGenSamplers(2, samplers);
   glSamplerParameteri(samplers[0], GL_TEXTURE_WRAP_S, GL_REPEAT);
   glSamplerParameteri(samplers[0], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glSamplerParameteri(samplers[0], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glSamplerParameteri(samplers[0], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glSamplerParameteri(samplers[1], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glSamplerParameteri(samplers[1], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glSamplerParameteri(samplers[1], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glSamplerParameteri(samplers[1], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glBindSampler(0, samplers[0]);
   sampler_repeat_a_ok =
      draw_oracle(coord, -0.25f, 0.25f, UINT32_C(0xff404040),
                  UINT32_C(0x9d431dc5), pixels, "sampler-repeat-a");
   glBindSampler(0, samplers[1]);
   sampler_linear_ok =
      draw_oracle(coord, 0.5f, 0.5f, UINT32_C(0xff606060),
                  UINT32_C(0x9b831dc5), pixels, "sampler-linear");
   glBindSampler(0, samplers[0]);
   sampler_repeat_b_ok =
      draw_oracle(coord, -0.25f, 0.25f, UINT32_C(0xff404040),
                  UINT32_C(0x9d431dc5), pixels, "sampler-repeat-b");
   glBindSampler(0, 0);
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && first_ok && second_ok && repeat_ok &&
            mirror_a_ok && mirror_b_ok && linear_ok && sampler_ext &&
            sampler_repeat_a_ok && sampler_linear_ok && sampler_repeat_b_ok &&
            strncmp((const char *)version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl, "1.20", 4) == 0;

cleanup:
   if (samplers[0] || samplers[1])
      glDeleteSamplers(2, samplers);
   if (texture)
      glDeleteTextures(1, &texture);
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
   printf("[ps5-egl-extents] size=%dx%d cleanup=%x/%x result=%d\n",
          TEX_WIDTH, TEX_HEIGHT, cleanup_gl_error, cleanup_egl_error,
          passed ? 0 : 1);
   return passed ? 0 : 1;
}
