#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define READ_SIZE 32

int ps5_egl_current_draw_status(unsigned *draw_calls);

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-core33-current-attrib] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "layout(location=1) in vec4 current_float;\n"
      "layout(location=2) in uvec4 current_uint;\n"
      "flat out vec4 value;\n"
      "void main() {\n"
      "  gl_Position = vec4(position, 0.0, 1.0);\n"
      "  value = 0.5 * current_float +\n"
      "          0.5 * vec4(current_uint) / 255.0;\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "flat in vec4 value;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = value; }\n";
   static const float positions[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   uint8_t pixels[READ_SIZE * READ_SIZE * 4];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
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
   GLuint shaders[2] = {0}, program = 0, vao = 0, vbo = 0;
   GLint linked = GL_FALSE, float_enabled = GL_TRUE, uint_enabled = GL_TRUE;
   GLfloat float_value[4] = {0};
   GLuint uint_value[4] = {0};
   void *pointer = (void *)(uintptr_t)1;
   GLenum error = GL_NO_ERROR;
   unsigned matching = 0, draw_calls = 0;
   int draw_status = -1, made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

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

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[1]))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glVertexAttrib4f(1, 1.0f, 0.0f, 0.0f, 1.0f);
   glVertexAttribI4ui(2, 0, 255, 0, 255);
   glDisableVertexAttribArray(1);
   glDisableVertexAttribArray(2);
   glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &float_enabled);
   glGetVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &uint_enabled);
   glGetVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, float_value);
   glGetVertexAttribIuiv(2, GL_CURRENT_VERTEX_ATTRIB, uint_value);
   glGetVertexAttribPointerv(1, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);

   glUseProgram(program);
   glViewport(0, 0, WIDTH, HEIGHT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels((WIDTH - READ_SIZE) / 2, (HEIGHT - READ_SIZE) / 2,
                READ_SIZE, READ_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   for (unsigned i = 0; i < READ_SIZE * READ_SIZE; ++i) {
      const uint8_t *pixel = pixels + i * 4u;

      matching += pixel[0] == 128 && pixel[1] == 128 &&
                  pixel[2] == 0 && pixel[3] == 255;
   }
   error = glGetError();
   passed = major == 1 && minor == 4 && !float_enabled && !uint_enabled &&
            float_value[0] == 1.0f && float_value[1] == 0.0f &&
            float_value[2] == 0.0f && float_value[3] == 1.0f &&
            uint_value[0] == 0 && uint_value[1] == 255 &&
            uint_value[2] == 0 && uint_value[3] == 255 && !pointer &&
            draw_status == 0 && draw_calls == 1 &&
            matching == READ_SIZE * READ_SIZE && error == GL_NO_ERROR;
   printf("[ps5-egl-core33-current-attrib] enabled=%d/%d "
          "draw=%d/%u matching=%u error=0x%x result=%d\n",
          float_enabled, uint_enabled, draw_status, draw_calls,
          matching, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
      if (program)
         glDeleteProgram(program);
      for (unsigned i = 0; i < 2; ++i)
         if (shaders[i])
            glDeleteShader(shaders[i]);
      cleanup_ok &= glGetError() == GL_NO_ERROR;
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf("[ps5-egl-core33-current-attrib] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
