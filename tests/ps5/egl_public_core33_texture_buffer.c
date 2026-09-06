#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define SIZE 64
#define WHITE UINT32_C(0xffffffff)
#define WHITE_HASH UINT32_C(0x4847ddc5)

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
      printf("[ps5-egl-texture-buffer] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static int
link_program(GLuint vertex, GLuint fragment, GLuint *result)
{
   GLint linked = GL_FALSE;
   GLuint program = glCreateProgram();

   if (!program)
      return 0;
   glAttachShader(program, vertex);
   glAttachShader(program, fragment);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) {
      glDeleteProgram(program);
      return 0;
   }
   *result = program;
   return 1;
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

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform samplerBuffer floats;\n"
      "uniform isamplerBuffer signed_values;\n"
      "uniform usamplerBuffer unsigned_values;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  bool sizes = textureSize(floats) == 4 &&\n"
      "               textureSize(signed_values) == 4 &&\n"
      "               textureSize(unsigned_values) == 4;\n"
      "  bool f = texelFetch(floats, 2) == vec4(0.25, 0.5, 0.75, 1.0);\n"
      "  bool i = texelFetch(signed_values, 2) == ivec4(1, -2, 3, -4);\n"
      "  bool u = texelFetch(unsigned_values, 2) == uvec4(5, 6, 7, 8);\n"
      "  color = sizes && f && i && u ? vec4(1.0) : vec4(1.0, 0.0, 1.0, 1.0);\n"
      "}\n";
   static const float vertices[] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static const float float_data[4][4] = {
      {0}, {0}, {0.25f, 0.5f, 0.75f, 1.0f}, {0},
   };
   static const int32_t signed_data[4][4] = {
      {0}, {0}, {1, -2, 3, -4}, {0},
   };
   static const uint32_t unsigned_data[4][4] = {
      {0}, {0}, {5, 6, 7, 8}, {0},
   };
   static uint32_t pixels[SIZE * SIZE];
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
   EGLint egl_major = 0, egl_minor = 0, count = 0;
   GLuint shaders[2] = {0, 0}, program = 0;
   GLuint vertex_array = 0, vertex_buffer = 0;
   GLuint buffers[3] = {0, 0, 0}, textures[3] = {0, 0, 0};
   GLuint framebuffer = 0, output = 0;
   GLint max_texels = 0, widths[3] = {0, 0, 0};
   GLenum status = 0, error = GL_NO_ERROR;
   unsigned matching = 0;
   uint32_t hash = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;
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
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;

   glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &max_texels);
   if (max_texels < 65536 ||
       !compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[1]) ||
       !link_program(shaders[0], shaders[1], &program))
      goto cleanup;

   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
   glGenBuffers(1, &vertex_buffer);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenBuffers(3, buffers);
   glGenTextures(3, textures);
   for (unsigned unit = 0; unit < 3; ++unit) {
      static const GLenum formats[3] = {GL_RGBA32F, GL_RGBA32I, GL_RGBA32UI};
      const void *data = unit == 0 ? (const void *)float_data
                         : unit == 1 ? (const void *)signed_data
                                     : (const void *)unsigned_data;

      glBindBuffer(GL_TEXTURE_BUFFER, buffers[unit]);
      glBufferData(GL_TEXTURE_BUFFER, sizeof(float_data), data,
                   GL_STATIC_DRAW);
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_BUFFER, textures[unit]);
      glTexBuffer(GL_TEXTURE_BUFFER, formats[unit], buffers[unit]);
      glGetTexLevelParameteriv(GL_TEXTURE_BUFFER, 0, GL_TEXTURE_WIDTH,
                               &widths[unit]);
   }

   glGenRenderbuffers(1, &output);
   glBindRenderbuffer(GL_RENDERBUFFER, output);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, SIZE, SIZE);
   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_RENDERBUFFER, output);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, SIZE, SIZE);
   glUseProgram(program);
   glUniform1i(glGetUniformLocation(program, "floats"), 0);
   glUniform1i(glGetUniformLocation(program, "signed_values"), 1);
   glUniform1i(glGetUniformLocation(program, "unsigned_values"), 2);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   error = glGetError();
   for (unsigned index = 0; index < SIZE * SIZE; ++index)
      matching += pixels[index] == WHITE;
   hash = hash32(pixels, sizeof(pixels));
   passed = egl_major == 1 && egl_minor == 4 && max_texels >= 65536 &&
            widths[0] == 4 && widths[1] == 4 && widths[2] == 4 &&
            status == GL_FRAMEBUFFER_COMPLETE && matching == SIZE * SIZE &&
            hash == WHITE_HASH && error == GL_NO_ERROR;

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (output)
         glDeleteRenderbuffers(1, &output);
      glDeleteTextures(3, textures);
      glDeleteBuffers(3, buffers);
      if (vertex_buffer)
         glDeleteBuffers(1, &vertex_buffer);
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
      if (program)
         glDeleteProgram(program);
      for (unsigned index = 0; index < 2; ++index)
         if (shaders[index])
            glDeleteShader(shaders[index]);
      cleanup_ok &= glGetError() == GL_NO_ERROR;
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE,
                                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf("[ps5-egl-texture-buffer] max=%d widths=%d/%d/%d status=%x "
          "pixels=%u:%08x hash=%08x error=%x cleanup=%u result=%d\n",
          max_texels, widths[0], widths[1], widths[2], status, matching,
          pixels[0], hash, error, cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
