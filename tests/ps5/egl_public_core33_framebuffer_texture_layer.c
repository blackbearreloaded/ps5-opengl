#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define TARGET_SIZE 128
#define READ_SIZE 64
#define ZERO UINT32_C(0x00000000)
#define GREEN UINT32_C(0xff00ff00)
#define WHITE UINT32_C(0xffffffff)

int ps5_egl_current_draw_status(unsigned *draw_calls);

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
      printf("[ps5-egl-framebuffer-layer] shader=0x%x log=%.*s\n",
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

static unsigned
matching_pixels(const uint32_t *pixels, uint32_t expected)
{
   unsigned matching = 0;

   for (unsigned i = 0; i < READ_SIZE * READ_SIZE; ++i)
      matching += pixels[i] == expected;
   return matching;
}

static void
read_target(uint32_t *pixels)
{
   glReadPixels((TARGET_SIZE - READ_SIZE) / 2,
                (TARGET_SIZE - READ_SIZE) / 2,
                READ_SIZE, READ_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location=0) in vec2 position;\n"
      "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";
   static const char *render_fragment_source =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }\n";
   static const char *sample_fragment_source =
      "#version 330 core\n"
      "uniform sampler2DArray layers;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  vec4 a = texture(layers, vec3(0.5, 0.5, 0.0));\n"
      "  vec4 b = texture(layers, vec3(0.5, 0.5, 1.0));\n"
      "  vec4 c = texture(layers, vec3(0.5, 0.5, 2.0));\n"
      "  color = vec4(1.0 - a.r, b.g, 1.0 - c.b, 1.0);\n"
      "}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static uint32_t pixels[READ_SIZE * READ_SIZE];
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
   EGLint major = 0, minor = 0, count = 0, profile = 0;
   GLuint shaders[3] = {0, 0, 0};
   GLuint programs[2] = {0, 0};
   GLuint vertex_array = 0, vertex_buffer = 0;
   GLuint texture = 0, framebuffer = 0;
   GLenum status[3] = {0, 0, 0};
   GLenum negative_error = GL_NO_ERROR, outside_error = GL_NO_ERROR;
   GLenum outside_status = 0, error = GL_NO_ERROR;
   GLint attached_layer = -1, max_layers = 0, sampler = -1;
   unsigned draw_calls = 0;
   int draw_status[2] = {-1, -1};
   unsigned layer_matches[3] = {0, 0, 0}, sample_matches = 0;
   uint32_t layer_hashes[3] = {0, 0, 0}, sample_hash = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;
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

   glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_layers);
   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, render_fragment_source,
                       &shaders[1]) ||
       !compile_shader(GL_FRAGMENT_SHADER, sample_fragment_source,
                       &shaders[2]) ||
       !link_program(shaders[0], shaders[1], &programs[0]) ||
       !link_program(shaders[0], shaders[2], &programs[1]))
      goto cleanup;

   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
   glGenBuffers(1, &vertex_buffer);
   glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TARGET_SIZE, TARGET_SIZE,
                3, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             texture, 0, -1);
   negative_error = glGetError();
   glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             texture, 0, 3);
   outside_error = glGetError();
   outside_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             texture, 0, 1);
   glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER, &attached_layer);
   status[1] = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status[1] != GL_FRAMEBUFFER_COMPLETE || attached_layer != 1 ||
       negative_error != GL_INVALID_VALUE || outside_error != GL_NO_ERROR ||
       outside_status == GL_FRAMEBUFFER_COMPLETE ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, TARGET_SIZE, TARGET_SIZE);
   glUseProgram(programs[0]);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[0] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   read_target(pixels);
   layer_matches[1] = matching_pixels(pixels, GREEN);
   layer_hashes[1] = hash32(pixels, sizeof(pixels));

   for (unsigned layer = 0; layer < 3; layer += 2) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                texture, 0, layer);
      status[layer] = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      read_target(pixels);
      layer_matches[layer] = matching_pixels(pixels, ZERO);
      layer_hashes[layer] = hash32(pixels, sizeof(pixels));
   }

   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glViewport(0, 0, WIDTH, HEIGHT);
   glUseProgram(programs[1]);
   sampler = glGetUniformLocation(programs[1], "layers");
   if (sampler < 0)
      goto cleanup;
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
   glUniform1i(sampler, 0);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[1] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels((WIDTH - READ_SIZE) / 2, (HEIGHT - READ_SIZE) / 2,
                READ_SIZE, READ_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   sample_matches = matching_pixels(pixels, WHITE);
   sample_hash = hash32(pixels, sizeof(pixels));
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            max_layers >= 256 && attached_layer == 1 &&
            negative_error == GL_INVALID_VALUE &&
            outside_error == GL_NO_ERROR &&
            outside_status != GL_FRAMEBUFFER_COMPLETE &&
            status[0] == GL_FRAMEBUFFER_COMPLETE &&
            status[1] == GL_FRAMEBUFFER_COMPLETE &&
            status[2] == GL_FRAMEBUFFER_COMPLETE &&
            draw_status[0] == 0 && draw_status[1] == 0 && draw_calls == 2 &&
            layer_matches[0] == READ_SIZE * READ_SIZE &&
            layer_matches[1] == READ_SIZE * READ_SIZE &&
            layer_matches[2] == READ_SIZE * READ_SIZE &&
            sample_matches == READ_SIZE * READ_SIZE && error == GL_NO_ERROR;
   printf("[ps5-egl-framebuffer-layer] status=%x/%x/%x layer=%d "
          "negative=0x%x outside=%x/0x%x max=%d draw=%d/%d/%u "
          "layers=%u:%08x/%u:%08x/%u:%08x sample=%u:%08x "
          "error=0x%x result=%d\n",
          status[0], status[1], status[2], attached_layer, negative_error,
          outside_status, outside_error, max_layers,
          draw_status[0], draw_status[1], draw_calls,
          layer_matches[0], layer_hashes[0], layer_matches[1],
          layer_hashes[1], layer_matches[2], layer_hashes[2],
          sample_matches, sample_hash, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (texture)
         glDeleteTextures(1, &texture);
      if (vertex_buffer)
         glDeleteBuffers(1, &vertex_buffer);
      if (vertex_array)
         glDeleteVertexArrays(1, &vertex_array);
      for (unsigned i = 0; i < 2; ++i)
         if (programs[i])
            glDeleteProgram(programs[i]);
      for (unsigned i = 0; i < 3; ++i)
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
   printf("[ps5-egl-framebuffer-layer] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
