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
#define WHITE_PIXEL UINT32_C(0xffffffff)
#define WHITE_HASH UINT32_C(0x4847ddc5)
#define GREEN_PIXEL UINT32_C(0xff00ff00)
#define GREEN_HASH UINT32_C(0xc38d1dc5)

static uint32_t
hash32(const void *data, size_t size)
{
   const uint8_t *bytes = data;
   uint32_t hash = UINT32_C(2166136261);

   while (size--)
      hash = (hash ^ *bytes++) * UINT32_C(16777619);
   return hash;
}

static GLuint
compile_shader(GLenum type, const char *source)
{
   GLuint shader = glCreateShader(type);
   GLint compiled = GL_FALSE;

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[1024];
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-core33-glsl-suite] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   return shader;
}

static GLuint
link_program(const char *vertex_source, const char *fragment_source,
             GLuint *vertex_shader, GLuint *fragment_shader)
{
   GLuint program;
   GLint linked = GL_FALSE;

   *vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
   *fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
   if (!*vertex_shader || !*fragment_shader)
      return 0;
   program = glCreateProgram();
   if (!program)
      return 0;
   glAttachShader(program, *vertex_shader);
   glAttachShader(program, *fragment_shader);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      char log[1024];
      GLsizei length = 0;

      glGetProgramInfoLog(program, sizeof(log), &length, log);
      printf("[ps5-egl-core33-glsl-suite] link log=%.*s\n", length, log);
      glDeleteProgram(program);
      return 0;
   }
   return program;
}

static int
read_oracle(uint32_t expected, uint32_t expected_hash, uint32_t *pixels,
            const char *name)
{
   unsigned matching = 0;
   GLenum error;
   uint32_t hash;

   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   error = glGetError();
   hash = hash32(pixels, SIZE * SIZE * sizeof(*pixels));
   for (unsigned index = 0; index < SIZE * SIZE; ++index)
      matching += pixels[index] == expected;
   printf("[ps5-egl-core33-glsl-suite] %s matching=%u hash=%08x error=0x%x\n",
          name, matching, hash, error);
   return matching == SIZE * SIZE && hash == expected_hash &&
          error == GL_NO_ERROR;
}

static int
read_split_oracle(uint32_t *pixels)
{
   unsigned left = 0, right = 0;
   GLenum error;
   uint32_t hash;

   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   error = glGetError();
   hash = hash32(pixels, SIZE * SIZE * sizeof(*pixels));
   for (unsigned y = 0; y < SIZE; ++y) {
      for (unsigned x = 0; x < SIZE; ++x) {
         if (pixels[y * SIZE + x] != WHITE_PIXEL)
            continue;
         if (x < SIZE / 2)
            ++left;
         else
            ++right;
      }
   }
   printf("[ps5-egl-core33-glsl-suite] math-groups left=%u right=%u "
          "pixels=%08x/%08x hash=%08x error=0x%x\n",
          left, right, pixels[0], pixels[SIZE / 2], hash, error);
   return left == SIZE * SIZE / 2 && right == SIZE * SIZE / 2 &&
          hash == WHITE_HASH && error == GL_NO_ERROR;
}

int
main(void)
{
   static const char *math_vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 a_position;\n"
      "uniform mat3 u_transform;\n"
      "uniform uvec4 u_bits;\n"
      "flat out uvec4 v_bits;\n"
      "noperspective out vec2 v_uv;\n"
      "void main() {\n"
      "  vec3 p = u_transform * vec3(a_position, 1.0);\n"
      "  gl_Position = vec4(p.xy, 0.0, 1.0);\n"
      "  v_bits = u_bits;\n"
      "  v_uv = a_position * 0.25 + vec2(0.5);\n"
      "}\n";
   static const char *math_fragment_source =
      "#version 330 core\n"
      "flat in uvec4 v_bits;\n"
      "noperspective in vec2 v_uv;\n"
      "uniform vec4 u_math;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  bool bits = (((v_bits.x << v_bits.y) & 0xffu) == 0x20u) &&\n"
      "              ((v_bits.x | v_bits.z) == 0x13u) &&\n"
      "              ((v_bits.x ^ v_bits.z) == 0x11u) &&\n"
      "              ((v_bits.w >> 4u) == 0x0fu);\n"
      "  bool round_ok = abs(roundEven(u_math.x) - 2.0) < 0.001;\n"
      "  bool trunc_ok = abs(trunc(u_math.y) + 1.0) < 0.001;\n"
      "  bool smooth_ok =\n"
      "     abs(smoothstep(0.0, 1.0, u_math.z) - 0.15625) < 0.001;\n"
      "  bool inverse_ok = abs(inversesqrt(u_math.w) - 0.5) < 0.001;\n"
      "  bool dx_ok = abs(dFdx(gl_FragCoord.x) - 1.0) < 0.01;\n"
      "  bool dy_ok = abs(abs(dFdy(gl_FragCoord.y)) - 1.0) < 0.01;\n"
      "  bool interp = all(greaterThan(v_uv, vec2(0.45))) &&\n"
      "                all(lessThan(v_uv, vec2(0.55)));\n"
      "  vec4 left = vec4(bits, round_ok, trunc_ok, smooth_ok);\n"
      "  vec4 right = vec4(inverse_ok, dx_ok, dy_ok, interp);\n"
      "  color = gl_FragCoord.x < 960.0 ? left : right;\n"
      "}\n";
   static const char *texture_vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *texture_fragment_source =
      "#version 330 core\n"
      "uniform sampler2D u_texture;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  ivec2 extent = textureSize(u_texture, 0);\n"
      "  vec4 texel = texelFetch(u_texture, ivec2(1, 0), 0);\n"
      "  bool ok = all(equal(extent, ivec2(2, 2))) &&\n"
      "            texel.g > 0.99 && texel.r < 0.01 && texel.b < 0.01;\n"
      "  color = ok ? vec4(0.0, 1.0, 0.0, 1.0)\n"
      "             : vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static const uint8_t texels[16] = {
      0, 0, 0, 255, 0, 255, 0, 255,
      0, 0, 0, 255, 0, 0, 0, 255,
   };
   static uint32_t pixels[SIZE * SIZE];
   static const float identity[9] = {
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 1.0f,
   };
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
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
   EGLint major = 0, minor = 0, count = 0, profile = 0;
   GLuint vao = 0, vbo = 0, texture = 0;
   GLuint math_vs = 0, math_fs = 0, math_program = 0;
   GLuint texture_vs = 0, texture_fs = 0, texture_program = 0;
   GLint transform_location, bits_location, math_location, texture_location;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, math_ok = 0, texture_ok = 0, passed = 0;

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
       !eglSwapInterval(display, 0) ||
       !eglQueryContext(display, context,
                        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, &profile))
      goto cleanup;
   made_current = 1;

   math_program = link_program(math_vertex_source, math_fragment_source,
                               &math_vs, &math_fs);
   texture_program = link_program(texture_vertex_source,
                                  texture_fragment_source,
                                  &texture_vs, &texture_fs);
   if (!math_program || !texture_program)
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);
   glViewport(0, 0, WIDTH, HEIGHT);

   transform_location = glGetUniformLocation(math_program, "u_transform");
   bits_location = glGetUniformLocation(math_program, "u_bits");
   math_location = glGetUniformLocation(math_program, "u_math");
   if (transform_location < 0 || bits_location < 0 || math_location < 0)
      goto cleanup;
   glUseProgram(math_program);
   glUniformMatrix3fv(transform_location, 1, GL_FALSE, identity);
   glUniform4ui(bits_location, 0x12u, 4u, 3u, 0xffu);
   glUniform4f(math_location, 1.5f, -1.5f, 0.25f, 4.0f);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   math_ok = read_split_oracle(pixels);

   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, texels);
   texture_location = glGetUniformLocation(texture_program, "u_texture");
   if (texture_location < 0 || glGetError() != GL_NO_ERROR)
      goto cleanup;
   glUseProgram(texture_program);
   glUniform1i(texture_location, 0);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   texture_ok = read_oracle(GREEN_PIXEL, GREEN_HASH, pixels,
                            "texture-size-texel-fetch");

   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = major == 1 && minor == 4 &&
            profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            strncmp((const char *)glGetString(GL_VERSION), "3.3 ", 4) == 0 &&
            strncmp((const char *)glGetString(GL_SHADING_LANGUAGE_VERSION),
                    "3.30", 4) == 0 && math_ok && texture_ok;

cleanup:
   if (texture)
      glDeleteTextures(1, &texture);
   if (vbo)
      glDeleteBuffers(1, &vbo);
   if (vao)
      glDeleteVertexArrays(1, &vao);
   if (texture_program)
      glDeleteProgram(texture_program);
   if (math_program)
      glDeleteProgram(math_program);
   if (texture_fs)
      glDeleteShader(texture_fs);
   if (texture_vs)
      glDeleteShader(texture_vs);
   if (math_fs)
      glDeleteShader(math_fs);
   if (math_vs)
      glDeleteShader(math_vs);
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
   printf("[ps5-egl-core33-glsl-suite] math=%u texture=%u cleanup=%x/%x result=%d\n",
          math_ok, texture_ok, cleanup_gl_error, cleanup_egl_error,
          passed ? 0 : 1);
   return passed ? 0 : 1;
}
