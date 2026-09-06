#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 128
#define HEIGHT 64
#define SAMPLE_SIZE 8
#define RED UINT32_C(0xff0000ff)
#define GREEN UINT32_C(0xff00ff00)

int ps5_egl_current_draw_status(unsigned *draw_calls);

struct vertex {
   float position[2];
   float color[3];
};

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
      printf("[ps5-egl-draw-matrix] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static unsigned
matching_sample(int x, uint32_t expected)
{
   uint32_t pixels[SAMPLE_SIZE * SAMPLE_SIZE];
   unsigned matching = 0;

   glReadPixels(x - SAMPLE_SIZE / 2, HEIGHT / 2 - SAMPLE_SIZE / 2,
                SAMPLE_SIZE, SAMPLE_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   for (unsigned i = 0; i < SAMPLE_SIZE * SAMPLE_SIZE; ++i)
      matching += pixels[i] == expected;
   return matching;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "layout(location=1) in vec3 vertex_color;\n"
      "layout(location=2) in vec4 instance_data;\n"
      "out vec3 color_value;\n"
      "void main() {\n"
      "  gl_Position=vec4(position+vec2(instance_data.x,0),0,1);\n"
      "  color_value=vertex_color*instance_data.yzw;\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "in vec3 color_value;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color=vec4(color_value,1); }\n";
   static const struct vertex vertices[16] = {
      {{2.0f, 2.0f}, {0.0f, 0.0f, 0.0f}},
      {{2.1f, 2.0f}, {0.0f, 0.0f, 0.0f}},
      {{2.0f, 2.1f}, {0.0f, 0.0f, 0.0f}},
      {{2.1f, 2.1f}, {0.0f, 0.0f, 0.0f}},
      {{-0.2f, -0.4f}, {1.0f, 1.0f, 1.0f}},
      {{ 0.2f, -0.4f}, {1.0f, 1.0f, 1.0f}},
      {{-0.2f,  0.4f}, {1.0f, 1.0f, 1.0f}},
      {{ 0.2f,  0.4f}, {1.0f, 1.0f, 1.0f}},
      {{-0.7f, -0.4f}, {1.0f, 0.0f, 0.0f}},
      {{-0.3f, -0.4f}, {1.0f, 0.0f, 0.0f}},
      {{-0.7f,  0.4f}, {1.0f, 0.0f, 0.0f}},
      {{-0.3f,  0.4f}, {1.0f, 0.0f, 0.0f}},
      {{ 0.3f, -0.4f}, {0.0f, 1.0f, 0.0f}},
      {{ 0.7f, -0.4f}, {0.0f, 1.0f, 0.0f}},
      {{ 0.3f,  0.4f}, {0.0f, 1.0f, 0.0f}},
      {{ 0.7f,  0.4f}, {0.0f, 1.0f, 0.0f}},
   };
   static const float instances[8] = {
      -0.5f, 1.0f, 0.0f, 0.0f,
       0.5f, 0.0f, 1.0f, 0.0f,
   };
   static const uint16_t indices[9] = {
      0, 1, 2, 3, UINT16_MAX, 0, 1, 2, 3,
   };
   static const GLsizei multi_counts[2] = {4, 4};
   static const void *const multi_offsets[2] = {
      NULL, (const void *)(5 * sizeof(uint16_t)),
   };
   static const GLint multi_bases[2] = {8, 12};
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
   GLuint shaders[2] = {0, 0}, program = 0, vao = 0;
   GLuint buffers[3] = {0, 0, 0}, framebuffer = 0, color = 0;
   GLint linked = GL_FALSE, restart_index = 0;
   GLenum status = 0, instance_error = 0, multi_error = 0, error = 0;
   unsigned phase1_red = 0, phase1_green = 0;
   unsigned phase2_red = 0, phase2_green = 0, draw_calls = 0;
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
   glGenBuffers(3, buffers);
   glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                         (const void *)offsetof(struct vertex, position));
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                         (const void *)offsetof(struct vertex, color));
   glEnableVertexAttribArray(0);
   glEnableVertexAttribArray(1);
   glBindBuffer(GL_ARRAY_BUFFER, buffers[1]);
   glBufferData(GL_ARRAY_BUFFER, sizeof(instances), instances, GL_STATIC_DRAW);
   glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), NULL);
   glEnableVertexAttribArray(2);
   glVertexAttribDivisor(2, 1);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[2]);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                GL_STATIC_DRAW);

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

   glUseProgram(program);
   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glEnable(GL_PRIMITIVE_RESTART);
   glPrimitiveRestartIndex(UINT16_MAX);
   glGetIntegerv(GL_PRIMITIVE_RESTART_INDEX, &restart_index);

   glDrawElementsInstancedBaseVertex(GL_TRIANGLE_STRIP, 4,
                                     GL_UNSIGNED_SHORT, NULL, -1, 4);
   instance_error = glGetError();
   glMultiDrawElementsBaseVertex(GL_TRIANGLE_STRIP, multi_counts,
                                 GL_UNSIGNED_SHORT, multi_offsets, -1,
                                 multi_bases);
   multi_error = glGetError();

   glClear(GL_COLOR_BUFFER_BIT);
   glDrawElementsInstancedBaseVertex(GL_TRIANGLE_STRIP, 9,
                                     GL_UNSIGNED_SHORT, NULL, 2, 4);
   glFinish();
   phase1_red = matching_sample(WIDTH / 4, RED);
   phase1_green = matching_sample(3 * WIDTH / 4, GREEN);

   glClear(GL_COLOR_BUFFER_BIT);
   glDisableVertexAttribArray(2);
   glVertexAttribDivisor(2, 0);
   glVertexAttrib4f(2, 0.0f, 1.0f, 1.0f, 1.0f);
   glDrawElementsInstanced(GL_TRIANGLE_STRIP, 4,
                           GL_UNSIGNED_SHORT, NULL, 1);
   glMultiDrawElementsBaseVertex(GL_TRIANGLE_STRIP, multi_counts,
                                 GL_UNSIGNED_SHORT, multi_offsets, 2,
                                 multi_bases);
   glFinish();
   phase2_red = matching_sample(WIDTH / 4, RED);
   phase2_green = matching_sample(3 * WIDTH / 4, GREEN);
   glDrawArrays(GL_LINES, 0, 3);
   glDrawArrays(GL_TRIANGLES, 0, 4);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            restart_index == (GLint)UINT16_MAX &&
            glIsEnabled(GL_PRIMITIVE_RESTART) == GL_TRUE &&
            instance_error == GL_INVALID_VALUE &&
            multi_error == GL_INVALID_VALUE &&
            phase1_red == SAMPLE_SIZE * SAMPLE_SIZE &&
            phase1_green == SAMPLE_SIZE * SAMPLE_SIZE &&
            phase2_red == SAMPLE_SIZE * SAMPLE_SIZE &&
            phase2_green == SAMPLE_SIZE * SAMPLE_SIZE &&
            draw_status == 0 && draw_calls == 7 && error == GL_NO_ERROR;
   printf("[ps5-egl-draw-matrix] restart=%d invalid=%x/%x "
          "phase1=%u/%u phase2=%u/%u draw=%d/%u error=0x%x result=%d\n",
          restart_index, instance_error, multi_error,
          phase1_red, phase1_green, phase2_red, phase2_green,
          draw_status, draw_calls, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glDisable(GL_PRIMITIVE_RESTART);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (color)
         glDeleteRenderbuffers(1, &color);
      if (buffers[0] || buffers[1] || buffers[2])
         glDeleteBuffers(3, buffers);
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
   printf("[ps5-egl-draw-matrix] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
