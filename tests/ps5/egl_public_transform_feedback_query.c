#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define WIDTH 1920
#define HEIGHT 1080
#define OUTPUT_BYTES 512
#define CAPTURE_BYTES (6 * sizeof(float))
#define X_OFFSET 256
#define Y_OFFSET 320

_Static_assert(X_OFFSET % sizeof(GLuint) == 0 &&
               Y_OFFSET % sizeof(GLuint) == 0,
               "transform-feedback ranges must be dword aligned");

int ps5_egl_current_draw_status(unsigned *draw_calls);

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
      GLchar log[512];
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-xfb-query] compile type=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
   return 0;
}

static int
validate_capture(GLuint buffer, GLintptr offset, const GLuint *initial,
                 const float expected[6], float observed[6],
                 int *prefix_ok, int *values_ok, int *suffix_ok,
                 GLenum *error)
{
   const GLubyte *captured;
   GLenum local_error;
   GLboolean unmapped;

   glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer);
   captured = glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, OUTPUT_BYTES,
                               GL_MAP_READ_BIT);
   local_error = glGetError();
   if (*error == GL_NO_ERROR)
      *error = local_error;
   if (!captured || local_error != GL_NO_ERROR)
      return -1;

   *prefix_ok = !memcmp(captured, initial, (size_t)offset);
   *values_ok = !memcmp(captured + offset, expected, CAPTURE_BYTES);
   *suffix_ok = !memcmp(captured + offset + CAPTURE_BYTES,
                        (const GLubyte *)initial + offset + CAPTURE_BYTES,
                        OUTPUT_BYTES - (size_t)offset - CAPTURE_BYTES);
   memcpy(observed, captured + offset, CAPTURE_BYTES);
   unmapped = glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER);
   local_error = glGetError();
   if (*error == GL_NO_ERROR)
      *error = local_error;
   return unmapped == GL_TRUE && local_error == GL_NO_ERROR ? 0 : -1;
}

int
main(void)
{
#ifdef PS5_GEOMETRY_XFB_TEST
   const unsigned expected_draw_calls = 1;
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location = 0) in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *geometry_source =
      "#version 330 core\n"
      "layout(triangles) in;\n"
      "layout(triangle_strip, max_vertices = 6) out;\n"
      "out float captured_x;\n"
      "out float captured_y;\n"
      "void main() {\n"
      "  for (int primitive = 0; primitive < 2; ++primitive) {\n"
      "    vec2 offset = vec2(float(primitive) * 0.25,\n"
      "                       float(primitive) * 0.5);\n"
      "    for (int i = 0; i < 3; ++i) {\n"
      "      gl_Position = gl_in[i].gl_Position + vec4(offset, 0.0, 0.0);\n"
      "      captured_x = gl_Position.x;\n"
      "      captured_y = gl_Position.y;\n"
      "      EmitVertex();\n"
      "    }\n"
      "    EndPrimitive();\n"
      "  }\n"
      "}\n";
#else
   const unsigned expected_draw_calls = 2;
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 a_position;\n"
      "out float captured_x;\n"
      "out float captured_y;\n"
      "void main() {\n"
      "  captured_x = a_position.x + 0.25;\n"
      "  captured_y = a_position.y + 0.5;\n"
      "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
      "}\n";
#endif
   static const char *fragment_source =
      "#version 330\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }\n";
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
#ifdef PS5_GEOMETRY_XFB_TEST
   static const float expected_x[6] = {
      -0.5f, 0.5f, 0.0f, -0.25f, 0.75f, 0.25f,
   };
   static const float expected_y[6] = {
      -0.5f, -0.5f, 0.5f, 0.0f, 0.0f, 1.0f,
   };
#else
   static const float expected_x[6] = {
      -0.25f, 0.75f, 0.25f, -0.25f, 0.75f, 0.25f,
   };
   static const float expected_y[6] = {
      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
   };
#endif
   static const char *varyings[2] = {"captured_x", "captured_y"};
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
   EGLint major = 0, minor = 0, count = 0;
   GLuint vs = 0, gs = 0, fs = 0, program = 0, vao = 0, vbo = 0;
   GLuint output[2] = {0};
   GLuint query[2] = {0};
   GLuint query_result[2] = {0};
   GLuint initial[2][OUTPUT_BYTES / sizeof(GLuint)];
   float observed[2][6] = {{0}};
   GLint linked = GL_FALSE, context_major = 0, context_minor = 0;
   GLint limits[3] = {0};
   int prefix_ok[2] = {0}, values_ok[2] = {0}, suffix_ok[2] = {0};
   GLenum error = GL_NO_ERROR;
   int draw_status = -100;
   unsigned draw_calls = 0;
   int made_current = 0;
   int passed = 0;

   for (unsigned i = 0; i < OUTPUT_BYTES / sizeof(GLuint); ++i) {
      initial[0][i] = UINT32_C(0x5a5a5a5a);
      initial[1][i] = UINT32_C(0xa5a5a5a5);
   }

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
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;

   glGetIntegerv(GL_MAJOR_VERSION, &context_major);
   glGetIntegerv(GL_MINOR_VERSION, &context_minor);
   glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, &limits[0]);
   glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS, &limits[1]);
   glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS, &limits[2]);
   if (context_major != 3 || context_minor != 3 || limits[0] < 4 ||
       limits[1] < 4 || limits[2] < 64 ||
       compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) != 0 ||
#ifdef PS5_GEOMETRY_XFB_TEST
       compile_shader(GL_GEOMETRY_SHADER, geometry_source, &gs) != 0 ||
#endif
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fs) != 0)
      goto cleanup;

   program = glCreateProgram();
   glAttachShader(program, vs);
#ifdef PS5_GEOMETRY_XFB_TEST
   glAttachShader(program, gs);
#endif
   glAttachShader(program, fs);
   glTransformFeedbackVaryings(program, 2, varyings, GL_SEPARATE_ATTRIBS);
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
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   glGenBuffers(2, output);
   for (unsigned i = 0; i < 2; ++i) {
      glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, output[i]);
      glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, OUTPUT_BYTES, initial[i],
                   GL_DYNAMIC_READ);
   }
   glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, output[0], X_OFFSET,
                     CAPTURE_BYTES);
   glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 1, output[1], Y_OFFSET,
                     CAPTURE_BYTES);

   glGenQueries(2, query);
   glBeginQuery(GL_PRIMITIVES_GENERATED, query[0]);
   glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query[1]);
   glViewport(0, 0, WIDTH, HEIGHT);
   glBeginTransformFeedback(GL_TRIANGLES);
   glDrawArrays(GL_TRIANGLES, 0, 3);
#ifndef PS5_GEOMETRY_XFB_TEST
   glDrawArrays(GL_TRIANGLES, 0, 3);
#endif
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glEndTransformFeedback();
   glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
   glEndQuery(GL_PRIMITIVES_GENERATED);
   glFinish();
   glGetQueryObjectuiv(query[0], GL_QUERY_RESULT, &query_result[0]);
   glGetQueryObjectuiv(query[1], GL_QUERY_RESULT, &query_result[1]);
   error = glGetError();

   if (validate_capture(output[0], X_OFFSET, initial[0], expected_x,
                        observed[0], &prefix_ok[0], &values_ok[0],
                        &suffix_ok[0], &error) == 0 &&
       validate_capture(output[1], Y_OFFSET, initial[1], expected_y,
                        observed[1], &prefix_ok[1], &values_ok[1],
                        &suffix_ok[1], &error) == 0 &&
       prefix_ok[0] && prefix_ok[1] && values_ok[0] && values_ok[1] &&
       suffix_ok[0] && suffix_ok[1] && query_result[0] == 2 &&
       query_result[1] == 2 && draw_status == 0 &&
       draw_calls == expected_draw_calls && error == GL_NO_ERROR)
      passed = 1;

   printf("[ps5-egl-xfb-query] core=%d.%d limits=%d/%d/%d "
          "offsets=%d/%d canary=%d/%d/%d/%d values=%d/%d queries=%u/%u "
          "x=%g,%g,%g,%g,%g,%g y=%g,%g,%g,%g,%g,%g "
          "draw_status=%d submissions=%u error=0x%x\n",
          context_major, context_minor, limits[0], limits[1], limits[2],
          X_OFFSET, Y_OFFSET, prefix_ok[0], suffix_ok[0], prefix_ok[1],
          suffix_ok[1], values_ok[0], values_ok[1], query_result[0],
          query_result[1], observed[0][0], observed[0][1], observed[0][2],
          observed[0][3], observed[0][4], observed[0][5], observed[1][0],
          observed[1][1], observed[1][2], observed[1][3], observed[1][4],
          observed[1][5], draw_status, draw_calls, error);
   if (!eglSwapBuffers(display, surface))
      passed = 0;

cleanup:
   if (query[0] || query[1])
      glDeleteQueries(2, query);
   if (output[0] || output[1])
      glDeleteBuffers(2, output);
   if (vbo)
      glDeleteBuffers(1, &vbo);
   if (vao)
      glDeleteVertexArrays(1, &vao);
   if (program)
      glDeleteProgram(program);
   if (fs)
      glDeleteShader(fs);
   if (gs)
      glDeleteShader(gs);
   if (vs)
      glDeleteShader(vs);
   if (made_current && glGetError() != GL_NO_ERROR)
      passed = 0;
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      eglTerminate(display);
   printf("[ps5-egl-xfb-query] result=%d\n", passed ? 0 : 1);
   return passed ? 0 : 1;
}
