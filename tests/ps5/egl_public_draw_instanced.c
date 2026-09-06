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
#define CROP_Y ((HEIGHT - CROP_HEIGHT) / 2)
#define LEFT_X (WIDTH / 4 - CROP_WIDTH / 2)
#define RIGHT_X (3 * WIDTH / 4 - CROP_WIDTH / 2)
#define RED_PIXEL UINT32_C(0xff0000ff)
#define RED_HASH UINT32_C(0xc40abdc5)
#define GREEN_PIXEL UINT32_C(0xff00ff00)
#define GREEN_HASH UINT32_C(0xc38d1dc5)

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
      printf("[ps5-egl-instanced] shader type=0x%x compile=0 log=%.*s\n",
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
read_oracle(uint32_t *pixels, int x, uint32_t expected_pixel,
            uint32_t expected_hash, const char *name)
{
   unsigned matching = 0, unexpected = 0;

   glFinish();
   glReadPixels(x, CROP_Y, CROP_WIDTH, CROP_HEIGHT,
                GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   for (unsigned i = 0; i < CROP_WIDTH * CROP_HEIGHT; ++i) {
      matching += pixels[i] == expected_pixel;
      unexpected += pixels[i] != expected_pixel;
   }
   uint32_t hash = hash32(pixels, CROP_WIDTH * CROP_HEIGHT * sizeof(*pixels));
   printf("[ps5-egl-instanced] %s matching=%u unexpected=%u hash=%08x error=0x%x\n",
          name, matching, unexpected, hash, error);
   return matching == CROP_WIDTH * CROP_HEIGHT && !unexpected &&
          hash == expected_hash && error == GL_NO_ERROR;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 120\n"
      "#extension GL_ARB_draw_instanced : require\n"
      "attribute vec2 a_position;\n"
      "varying vec3 v_color;\n"
      "void main() {\n"
      "  float i = float(gl_InstanceIDARB);\n"
      "  gl_Position = vec4(a_position + vec2(i - 0.5, 0.0), 0.0, 1.0);\n"
      "  v_color = i < 0.5 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);\n"
      "}\n";
   static const char *fragment_source =
      "#version 120\n"
      "varying vec3 v_color;\n"
      "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
   static const float vertices[6] = {
      -0.2f, -0.2f,
       0.2f, -0.2f,
       0.0f,  0.2f,
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
   const char *extensions = NULL;
   GLenum setup_error = GL_NO_ERROR, cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int extension_present = 0, control_red = 0;
   int instance_red = 0, instance_green = 0, made_current = 0, passed = 0;

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
   extensions = (const char *)glGetString(GL_EXTENSIONS);
   if (!gl_version || !glsl_version || !extensions)
      goto cleanup;
   extension_present = has_extension(extensions, "GL_ARB_draw_instanced");
   printf("[ps5-egl-instanced] egl=%d.%d surface=%dx%d context=%d gl=%s glsl=%s extension=%d\n",
          egl_major, egl_minor, width, height, context_version, gl_version,
          glsl_version, extension_present);

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
   glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
   control_red = read_oracle(pixels, LEFT_X, RED_PIXEL, RED_HASH,
                             "control-left-red");

   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 2);
   instance_red = read_oracle(pixels, LEFT_X, RED_PIXEL, RED_HASH,
                              "instance0-red");
   instance_green = read_oracle(pixels, RIGHT_X, GREEN_PIXEL, GREEN_HASH,
                                "instance1-green");

   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && context_version == 2 && extension_present &&
            strncmp((const char *)gl_version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl_version, "1.20", 4) == 0 &&
            setup_error == GL_NO_ERROR && control_red &&
            instance_red && instance_green;

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
   printf("[ps5-egl-instanced] cleanup gl=0x%x egl=0x%x ok=%u result=%d\n",
          cleanup_gl_error, cleanup_egl_error, cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
