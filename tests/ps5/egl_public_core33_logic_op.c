#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 32
#define HEIGHT 32

int ps5_egl_current_draw_status(unsigned *draw_calls);

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint ok = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-logic-op] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static uint8_t
logic_byte(GLenum operation, uint8_t source, uint8_t destination)
{
   switch (operation) {
   case GL_CLEAR:         return 0;
   case GL_AND:           return source & destination;
   case GL_AND_REVERSE:   return source & (uint8_t)~destination;
   case GL_COPY:          return source;
   case GL_AND_INVERTED:  return (uint8_t)~source & destination;
   case GL_NOOP:          return destination;
   case GL_XOR:           return source ^ destination;
   case GL_OR:            return source | destination;
   case GL_NOR:           return (uint8_t)~(source | destination);
   case GL_EQUIV:         return (uint8_t)~(source ^ destination);
   case GL_INVERT:        return (uint8_t)~destination;
   case GL_OR_REVERSE:    return source | (uint8_t)~destination;
   case GL_COPY_INVERTED: return (uint8_t)~source;
   case GL_OR_INVERTED:   return (uint8_t)~source | destination;
   case GL_NAND:          return (uint8_t)~(source & destination);
   case GL_SET:           return 0xff;
   default:               return 0;
   }
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position=vec4(position,0,1); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform vec4 source_color;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color=source_color; }\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static const GLenum operations[16] = {
      GL_CLEAR, GL_AND, GL_AND_REVERSE, GL_COPY,
      GL_AND_INVERTED, GL_NOOP, GL_XOR, GL_OR,
      GL_NOR, GL_EQUIV, GL_INVERT, GL_OR_REVERSE,
      GL_COPY_INVERTED, GL_OR_INVERTED, GL_NAND, GL_SET,
   };
   static const uint8_t source[4] = {0xf0, 0x0f, 0xaa, 0x55};
   static const uint8_t destination[4] = {0x0f, 0x33, 0x55, 0xaa};
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
   GLuint shaders[2] = {0, 0}, program = 0, vao = 0, vbo = 0;
   GLuint framebuffer = 0, color = 0;
   GLint linked = GL_FALSE, color_location = -1, logic_mode = 0;
   GLenum status = 0, error = GL_NO_ERROR;
   unsigned draw_calls = 0, matching = 0;
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
   color_location = glGetUniformLocation(program, "source_color");
   if (!linked || color_location < 0)
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenRenderbuffers(1, &color);
   glBindRenderbuffer(GL_RENDERBUFFER, color);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, color);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glUseProgram(program);
   glUniform4f(color_location, source[0] / 255.0f, source[1] / 255.0f,
               source[2] / 255.0f, source[3] / 255.0f);
   glDisable(GL_BLEND);
   glEnable(GL_COLOR_LOGIC_OP);
   for (unsigned operation = 0; operation < 16; ++operation) {
      uint8_t pixel[4] = {0, 0, 0, 0};
      uint8_t expected[4];
      int exact = 1;

      glClearColor(destination[0] / 255.0f, destination[1] / 255.0f,
                   destination[2] / 255.0f, destination[3] / 255.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      glLogicOp(operations[operation]);
      glGetIntegerv(GL_LOGIC_OP_MODE, &logic_mode);
      glDrawArrays(GL_TRIANGLES, 0, 3);
      glFinish();
      glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1,
                   GL_RGBA, GL_UNSIGNED_BYTE, pixel);
      for (unsigned channel = 0; channel < 4; ++channel) {
         expected[channel] = logic_byte(operations[operation],
                                        source[channel], destination[channel]);
         exact &= pixel[channel] == expected[channel];
      }
      exact &= logic_mode == (GLint)operations[operation] &&
               glGetError() == GL_NO_ERROR;
      matching += exact;
      printf("[ps5-egl-logic-op] op=0x%x pixel=%02x%02x%02x%02x "
             "expected=%02x%02x%02x%02x result=%d\n",
             operations[operation], pixel[0], pixel[1], pixel[2], pixel[3],
             expected[0], expected[1], expected[2], expected[3],
             exact ? 0 : 1);
   }
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   error = glGetError();
   passed = major == 1 && minor == 4 && matching == 16 &&
            draw_status == 0 && draw_calls == 16 && error == GL_NO_ERROR;
   printf("[ps5-egl-logic-op] status=0x%x matching=%u draw=%d/%u "
          "error=0x%x result=%d\n", status, matching, draw_status,
          draw_calls, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glDisable(GL_COLOR_LOGIC_OP);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (color)
         glDeleteRenderbuffers(1, &color);
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
   printf("[ps5-egl-logic-op] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
