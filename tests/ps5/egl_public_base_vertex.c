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
#define RED_PIXEL UINT32_C(0xff0000ff)
#define RED_HASH UINT32_C(0xc40abdc5)
#define GREEN_PIXEL UINT32_C(0xff00ff00)
#define GREEN_HASH UINT32_C(0xc38d1dc5)
#define BLUE_PIXEL UINT32_C(0xffff0000)
#define BLUE_HASH UINT32_C(0xbdf93dc5)

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
      printf("[ps5-egl-base-vertex] shader type=0x%x compile=0 log=%.*s\n",
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
   printf("[ps5-egl-base-vertex] %s matching=%u unexpected=%u hash=%08x error=0x%x\n",
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
      "attribute vec3 a_color;\n"
      "varying vec3 v_color;\n"
      "void main() {\n"
      "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
      "  v_color = a_color;\n"
      "}\n";
   static const char *fragment_source =
      "#version 120\n"
      "varying vec3 v_color;\n"
      "void main() { gl_FragColor = vec4(v_color, 1.0); }\n";
   static const float positive_vertices[30] = {
      -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
       0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
       0.0f,  0.5f, 1.0f, 0.0f, 0.0f,
      -0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
       0.5f, -0.5f, 0.0f, 1.0f, 0.0f,
       0.0f,  0.5f, 0.0f, 1.0f, 0.0f,
   };
   static const float negative_vertices[30] = {
      -0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
       0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
       0.0f,  0.5f, 0.0f, 0.0f, 1.0f,
      -0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
       0.5f, -0.5f, 1.0f, 0.0f, 0.0f,
       0.0f,  0.5f, 1.0f, 0.0f, 0.0f,
   };
   static const uint16_t indices[6] = {0, 1, 2, 3, 4, 5};
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
   GLuint vertex_shader = 0, fragment_shader = 0, program = 0;
   GLuint vertex_buffers[2] = {0, 0}, index_buffer = 0;
   GLint linked = GL_FALSE;
   const GLubyte *gl_version = NULL, *glsl_version = NULL;
   const char *extensions = NULL;
   GLenum setup_error = GL_NO_ERROR, cleanup_gl_error = GL_NO_ERROR;
#ifdef PS5_DRAW_RANGE_TEST
   GLenum range_error = GL_NO_ERROR;
#endif
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int extension_present = 0, control_ok = 0, positive_ok = 0, negative_ok = 0;
   int made_current = 0, passed = 0;

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
   extension_present =
      has_extension(extensions, "GL_ARB_draw_elements_base_vertex");
   printf("[ps5-egl-base-vertex] egl=%d.%d surface=%dx%d context=%d gl=%s glsl=%s extension=%d\n",
          egl_major, egl_minor, width, height, context_version, gl_version,
          glsl_version, extension_present);

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex_shader) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fragment_shader))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vertex_shader);
   glAttachShader(program, fragment_shader);
   glBindAttribLocation(program, 0, "a_position");
   glBindAttribLocation(program, 1, "a_color");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);

   glGenBuffers(2, vertex_buffers);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[0]);
   glBufferData(GL_ARRAY_BUFFER, sizeof(positive_vertices), positive_vertices,
                GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), NULL);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                         (const void *)(2 * sizeof(float)));
   glEnableVertexAttribArray(0);
   glEnableVertexAttribArray(1);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[1]);
   glBufferData(GL_ARRAY_BUFFER, sizeof(negative_vertices), negative_vertices,
                GL_STATIC_DRAW);

   glGenBuffers(1, &index_buffer);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                GL_STATIC_DRAW);
   setup_error = glGetError();
   if (setup_error != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
#ifdef PS5_DRAW_RANGE_TEST
   glDrawRangeElements(GL_TRIANGLES, 2, 1, 3, GL_UNSIGNED_SHORT, NULL);
   range_error = glGetError();
#endif
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[0]);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), NULL);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                         (const void *)(2 * sizeof(float)));
#ifdef PS5_DRAW_RANGE_TEST
   glDrawRangeElements(GL_TRIANGLES, 0, 2, 3, GL_UNSIGNED_SHORT, NULL);
#else
   glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
#endif
   control_ok = read_oracle(pixels, RED_PIXEL, RED_HASH, "control-base0-red");

#ifdef PS5_DRAW_RANGE_TEST
   glDrawRangeElementsBaseVertex(GL_TRIANGLES, 0, 2, 3,
                                 GL_UNSIGNED_SHORT, NULL, 3);
#else
   glDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL, 3);
#endif
   positive_ok =
      read_oracle(pixels, GREEN_PIXEL, GREEN_HASH, "positive-base3-green");

   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffers[1]);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), NULL);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                         (const void *)(2 * sizeof(float)));
#ifdef PS5_DRAW_RANGE_TEST
   glDrawRangeElementsBaseVertex(
      GL_TRIANGLES, 3, 5, 3, GL_UNSIGNED_SHORT,
      (const void *)(3 * sizeof(uint16_t)), -3);
#else
   glDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
                            (const void *)(3 * sizeof(uint16_t)), -3);
#endif
   negative_ok =
      read_oracle(pixels, BLUE_PIXEL, BLUE_HASH, "negative-base3-blue");

   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && context_version == 2 && extension_present &&
            strncmp((const char *)gl_version, "2.1 ", 4) == 0 &&
            strncmp((const char *)glsl_version, "1.20", 4) == 0 &&
            setup_error == GL_NO_ERROR &&
#ifdef PS5_DRAW_RANGE_TEST
            range_error == GL_INVALID_VALUE &&
#endif
            control_ok && positive_ok &&
            negative_ok;

cleanup:
   if (index_buffer)
      glDeleteBuffers(1, &index_buffer);
   if (vertex_buffers[0] || vertex_buffers[1])
      glDeleteBuffers(2, vertex_buffers);
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
#ifdef PS5_DRAW_RANGE_TEST
   printf("[ps5-egl-base-vertex] draw-range invalid=0x%x valid=3\n",
          range_error);
#endif
   printf("[ps5-egl-base-vertex] cleanup gl=0x%x egl=0x%x ok=%u result=%d\n",
          cleanup_gl_error, cleanup_egl_error, cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
