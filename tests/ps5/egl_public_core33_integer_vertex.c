#include <stddef.h>
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
#define EXPECTED_PIXEL UINT32_C(0xffff00ff)
#define EXPECTED_HASH UINT32_C(0x64e31dc5)

struct vertex {
   int32_t position[2];
   uint32_t color[4];
};

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
      char log[512];
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-int-vertex] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   return shader;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in ivec2 a_position;\n"
      "layout(location=1) in uvec4 a_color;\n"
      "out vec4 v_color;\n"
      "void main() {\n"
      "  gl_Position = vec4(vec2(a_position) * 0.5, 0.0, 1.0);\n"
      "  v_color = vec4(a_color) / 255.0;\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "in vec4 v_color;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = v_color; }\n";
   static const struct vertex vertices[3] = {
      {{-1, -1}, {255, 0, 255, 255}},
      {{ 1, -1}, {255, 0, 255, 255}},
      {{ 0,  1}, {255, 0, 255, 255}},
   };
   static uint32_t pixels[SIZE * SIZE];
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
   GLuint vao = 0, vbo = 0, vs = 0, fs = 0, program = 0;
   GLint linked = GL_FALSE, integer[2] = {0, 0}, types[2] = {0, 0};
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   unsigned matching = 0;
   uint32_t hash = 0;
   int made_current = 0, passed = 0;

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

   vs = compile_shader(GL_VERTEX_SHADER, vertex_source);
   fs = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
   if (!vs || !fs)
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribIPointer(0, 2, GL_INT, sizeof(struct vertex),
                          (const void *)offsetof(struct vertex, position));
   glVertexAttribIPointer(1, 4, GL_UNSIGNED_INT, sizeof(struct vertex),
                          (const void *)offsetof(struct vertex, color));
   glEnableVertexAttribArray(0);
   glEnableVertexAttribArray(1);
   for (unsigned index = 0; index < 2; ++index) {
      glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_INTEGER,
                          &integer[index]);
      glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &types[index]);
   }

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   hash = hash32(pixels, sizeof(pixels));
   for (unsigned index = 0; index < SIZE * SIZE; ++index)
      matching += pixels[index] == EXPECTED_PIXEL;
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = major == 1 && minor == 4 &&
            profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            strncmp((const char *)glGetString(GL_VERSION), "3.3 ", 4) == 0 &&
            integer[0] == GL_TRUE && integer[1] == GL_TRUE &&
            types[0] == GL_INT && types[1] == GL_UNSIGNED_INT &&
            matching == SIZE * SIZE && hash == EXPECTED_HASH &&
            glGetError() == GL_NO_ERROR;

cleanup:
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
   printf("[ps5-egl-int-vertex] integer=%d/%d types=%x/%x pixels=%u "
          "hash=%08x cleanup=%x/%x result=%d\n",
          integer[0], integer[1], types[0], types[1], matching, hash,
          cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
