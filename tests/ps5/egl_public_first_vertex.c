#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define CROP_WIDTH 64
#define CROP_HEIGHT 64
#define BLACK_PIXEL UINT32_C(0xff000000)
#define BLACK_HASH UINT32_C(0x1ec31dc5)
#define MAGENTA_PIXEL UINT32_C(0xffff00ff)
#define MAGENTA_HASH UINT32_C(0x64e31dc5)

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
      printf("[ps5-egl-first-vertex] shader type=0x%x compile=0 log=%.*s\n",
             type, length, log);
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
read_oracle(uint32_t *pixels, uint32_t expected_pixel,
            uint32_t expected_hash, const char *name)
{
   unsigned matching = 0, unexpected = 0;

   glFinish();
   glReadPixels((WIDTH - CROP_WIDTH) / 2, (HEIGHT - CROP_HEIGHT) / 2,
                CROP_WIDTH, CROP_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   for (unsigned i = 0; i < CROP_WIDTH * CROP_HEIGHT; ++i) {
      matching += pixels[i] == expected_pixel;
      unexpected += pixels[i] != expected_pixel;
   }
   uint32_t hash = hash32(pixels, CROP_WIDTH * CROP_HEIGHT * sizeof(*pixels));
   printf("[ps5-egl-first-vertex] %s matching=%u unexpected=%u hash=%08x error=0x%x\n",
          name, matching, unexpected, hash, error);
   return matching == CROP_WIDTH * CROP_HEIGHT && !unexpected &&
          hash == expected_hash && error == GL_NO_ERROR;
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
      "void main() { gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); }\n";
   static const float vertices[12] = {
      -2.0f, -2.0f,
      -2.0f, -2.0f,
      -2.0f, -2.0f,
      -0.5f, -0.5f,
       0.5f, -0.5f,
       0.0f,  0.5f,
   };
   static uint32_t pixels[CROP_WIDTH * CROP_HEIGHT];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0;
   EGLint width = 0, height = 0, context_version = 0;
   GLuint vertex_shader = 0, fragment_shader = 0, program = 0, vbo = 0;
   GLint linked = GL_FALSE;
   const GLubyte *gl_version = NULL, *glsl_version = NULL;
   GLenum setup_error = GL_NO_ERROR, cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int control_ok = 0, first_ok = 0, made_current = 0, passed = 0;

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

   if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
       !eglQuerySurface(display, surface, EGL_HEIGHT, &height) ||
       !eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION,
                        &context_version))
      goto cleanup;
   gl_version = glGetString(GL_VERSION);
   glsl_version = glGetString(GL_SHADING_LANGUAGE_VERSION);
   if (!gl_version || !glsl_version)
      goto cleanup;
   printf("[ps5-egl-first-vertex] egl=%d.%d surface=%dx%d context=%d gl=%s glsl=%s\n",
          egl_major, egl_minor, width, height, context_version, gl_version,
          glsl_version);

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex_shader) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fragment_shader))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vertex_shader);
   glAttachShader(program, fragment_shader);
   glBindAttribLocation(program, 0, "a_position");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);
   setup_error = glGetError();
   if (setup_error != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   control_ok = read_oracle(pixels, BLACK_PIXEL, BLACK_HASH, "control-first0");
   glDrawArrays(GL_TRIANGLES, 3, 3);
   first_ok = read_oracle(pixels, MAGENTA_PIXEL, MAGENTA_HASH, "first3");

   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && context_version == 2 &&
            strncmp((const char *)gl_version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl_version, "1.20", 4) == 0 &&
            setup_error == GL_NO_ERROR && control_ok && first_ok;

cleanup:
   if (vbo)
      glDeleteBuffers(1, &vbo);
   if (program)
      glDeleteProgram(program);
   if (fragment_shader)
      glDeleteShader(fragment_shader);
   if (vertex_shader)
      glDeleteShader(vertex_shader);
   if (made_current)
      cleanup_gl_error = glGetError();
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
      made_current = 0;
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_egl_error = eglGetError();
   passed = passed && cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
            cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-first-vertex] cleanup gl=0x%x egl=0x%x ok=%u result=%d\n",
          cleanup_gl_error, cleanup_egl_error, cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
