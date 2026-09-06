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
#define CASE_COUNT 7
#define OUTPUT_BYTES 512
#define CAPTURE_OFFSET 256

struct topology_case {
   const char *name;
   GLenum draw_mode;
   GLenum feedback_mode;
   GLsizei draw_count;
   const float *expected;
   size_t expected_count;
   GLuint expected_primitives;
};

_Static_assert(CAPTURE_OFFSET % sizeof(GLuint) == 0,
               "transform-feedback range must be dword aligned");

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
      printf("[ps5-egl-xfb-topologies] compile type=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
   return 0;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 a_position;\n"
      "out float captured_id;\n"
      "void main() {\n"
      "  captured_id = float(gl_VertexID);\n"
      "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
      "  gl_PointSize = 1.0;\n"
      "}\n";
   static const char *fragment_source =
      "#version 330\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }\n";
   static const float vertices[12] = {
      -0.8f, -0.8f, -0.4f, -0.8f, -0.6f, -0.4f,
       0.0f, -0.8f,  0.4f, -0.4f,  0.8f, -0.8f,
   };
   static const float points[] = {0, 1, 2};
   static const float lines[] = {0, 1, 2, 3};
   static const float line_strip[] = {0, 1, 1, 2, 2, 3};
   static const float line_loop[] = {0, 1, 1, 2, 2, 3, 3, 0};
   static const float triangles[] = {0, 1, 2, 3, 4, 5};
   static const float triangle_strip[] = {
      /* The alternating A/B replacement preserves strip winding. */
      0, 1, 2, 2, 1, 3, 2, 3, 4,
   };
   static const float triangle_fan[] = {
      0, 1, 2, 0, 2, 3, 0, 3, 4,
   };
   static const struct topology_case cases[CASE_COUNT] = {
      {"points", GL_POINTS, GL_POINTS, 3, points, 3, 3},
      {"lines", GL_LINES, GL_LINES, 4, lines, 4, 2},
      {"line-strip", GL_LINE_STRIP, GL_LINES, 4, line_strip, 6, 3},
      {"line-loop", GL_LINE_LOOP, GL_LINES, 4, line_loop, 8, 4},
      {"triangles", GL_TRIANGLES, GL_TRIANGLES, 6, triangles, 6, 2},
      {"triangle-strip", GL_TRIANGLE_STRIP, GL_TRIANGLES, 5,
       triangle_strip, 9, 3},
      {"triangle-fan", GL_TRIANGLE_FAN, GL_TRIANGLES, 5,
       triangle_fan, 9, 3},
   };
   static const char *varying = "captured_id";
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
   GLuint vs = 0, fs = 0, program = 0, vao = 0, vbo = 0;
   GLuint output[CASE_COUNT] = {0};
   GLuint query[CASE_COUNT][2] = {{0}};
   GLuint query_result[CASE_COUNT][2] = {{0}};
   GLuint initial[OUTPUT_BYTES / sizeof(GLuint)];
   GLint linked = GL_FALSE, context_major = 0, context_minor = 0;
   int case_ok[CASE_COUNT] = {0};
   float first[CASE_COUNT] = {0}, last[CASE_COUNT] = {0};
   GLenum error = GL_NO_ERROR;
   int draw_status = -100;
   unsigned draw_calls = 0;
   unsigned passed_cases = 0;
   int made_current = 0;
   int passed = 0;

   for (unsigned i = 0; i < OUTPUT_BYTES / sizeof(GLuint); ++i)
      initial[i] = UINT32_C(0x5a5a5a5a);

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
   if (context_major != 3 || context_minor != 3 ||
       compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) != 0 ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fs) != 0)
      goto cleanup;

   program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glTransformFeedbackVaryings(program, 1, &varying, GL_INTERLEAVED_ATTRIBS);
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

   glGenBuffers(CASE_COUNT, output);
   for (unsigned i = 0; i < CASE_COUNT; ++i) {
      glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, output[i]);
      glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER, OUTPUT_BYTES, initial,
                   GL_DYNAMIC_READ);
   }
   glGenQueries(CASE_COUNT * 2, &query[0][0]);

   glViewport(0, 0, WIDTH, HEIGHT);
   for (unsigned i = 0; i < CASE_COUNT; ++i) {
      glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, output[i],
                        CAPTURE_OFFSET,
                        cases[i].expected_count * sizeof(float));
      glBeginQuery(GL_PRIMITIVES_GENERATED, query[i][0]);
      glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query[i][1]);
      glBeginTransformFeedback(cases[i].feedback_mode);
      glDrawArrays(cases[i].draw_mode, 0, cases[i].draw_count);
      draw_status = ps5_egl_current_draw_status(&draw_calls);
      glEndTransformFeedback();
      glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
      glEndQuery(GL_PRIMITIVES_GENERATED);
      if (draw_status != 0)
         break;
   }
   glFinish();
   error = glGetError();

   for (unsigned i = 0; i < CASE_COUNT; ++i) {
      const GLubyte *mapped;
      size_t bytes = cases[i].expected_count * sizeof(float);
      int prefix_ok = 0, values_ok = 0, suffix_ok = 0;
      GLenum local_error;

      glGetQueryObjectuiv(query[i][0], GL_QUERY_RESULT,
                          &query_result[i][0]);
      glGetQueryObjectuiv(query[i][1], GL_QUERY_RESULT,
                          &query_result[i][1]);
      glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, output[i]);
      mapped = glMapBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, OUTPUT_BYTES,
                                GL_MAP_READ_BIT);
      local_error = glGetError();
      if (error == GL_NO_ERROR)
         error = local_error;
      if (mapped) {
         prefix_ok = !memcmp(mapped, initial, CAPTURE_OFFSET);
         values_ok = !memcmp(mapped + CAPTURE_OFFSET, cases[i].expected,
                             bytes);
         suffix_ok = !memcmp(mapped + CAPTURE_OFFSET + bytes,
                             (const GLubyte *)initial + CAPTURE_OFFSET + bytes,
                             OUTPUT_BYTES - CAPTURE_OFFSET - bytes);
         memcpy(&first[i], mapped + CAPTURE_OFFSET, sizeof(float));
         memcpy(&last[i], mapped + CAPTURE_OFFSET + bytes - sizeof(float),
                sizeof(float));
      }
      case_ok[i] = prefix_ok && values_ok && suffix_ok &&
                   query_result[i][0] == cases[i].expected_primitives &&
                   query_result[i][1] == cases[i].expected_primitives;
      printf("[ps5-egl-xfb-topologies] case=%s canary=%d/%d values=%d "
             "query=%u/%u first=%g last=%g\n",
             cases[i].name, prefix_ok, suffix_ok, values_ok,
             query_result[i][0], query_result[i][1], first[i], last[i]);
      if (mapped && glUnmapBuffer(GL_TRANSFORM_FEEDBACK_BUFFER) != GL_TRUE)
         case_ok[i] = 0;
      if (case_ok[i])
         passed_cases++;
   }

   if (passed_cases == CASE_COUNT && draw_status == 0 &&
       draw_calls == CASE_COUNT && error == GL_NO_ERROR)
      passed = 1;
   printf("[ps5-egl-xfb-topologies] core=%d.%d cases=%u/%d "
          "draw_status=%d submissions=%u error=0x%x\n",
          context_major, context_minor, passed_cases, CASE_COUNT,
          draw_status, draw_calls, error);
   if (!eglSwapBuffers(display, surface))
      passed = 0;

cleanup:
   if (query[0][0])
      glDeleteQueries(CASE_COUNT * 2, &query[0][0]);
   if (output[0])
      glDeleteBuffers(CASE_COUNT, output);
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
   printf("[ps5-egl-xfb-topologies] result=%d\n", passed ? 0 : 1);
   return passed ? 0 : 1;
}
