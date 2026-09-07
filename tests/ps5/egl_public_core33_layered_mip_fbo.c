#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 1920
#define HEIGHT 1080
#define BASE_SIZE 128
#define MIP_SIZE 64
#define READ_SIZE 32
#define RED UINT32_C(0xff0000ff)
#define GREEN UINT32_C(0xff00ff00)
#define WHITE UINT32_C(0xffffffff)

#ifdef PS5_LAYERED_MIP_HOST_REFERENCE
/* Host validates pixels; only the native build checks retired driver draws. */
static int ps5_egl_current_draw_status(unsigned *draw_calls)
{
   static unsigned calls;
   *draw_calls = ++calls;
   return 0;
}
#define SURFACE_TYPE EGL_PBUFFER_BIT
#else
int ps5_egl_current_draw_status(unsigned *draw_calls);
#define SURFACE_TYPE EGL_WINDOW_BIT
#endif

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
      printf("[ps5-egl-layered-mip-fbo] shader=0x%x log=%.*s\n",
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
   GLint ok = GL_FALSE;
   GLuint program = glCreateProgram();

   if (!program)
      return 0;
   glAttachShader(program, vertex);
   glAttachShader(program, fragment);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &ok);
   if (!ok) {
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

static unsigned
read_center(uint32_t *pixels, unsigned width, unsigned height,
            uint32_t expected)
{
   glReadPixels((width - READ_SIZE) / 2, (height - READ_SIZE) / 2,
                READ_SIZE, READ_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   return matching_pixels(pixels, expected);
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
      "uniform vec4 u_color;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() { color = u_color; }\n";
   static const char *sample_fragment_source =
      "#version 330 core\n"
      "uniform samplerCube u_cube;\n"
      "uniform sampler3D u_volume;\n"
      "layout(location=0) out vec4 color;\n"
      "void main() {\n"
      "  vec4 c = textureLod(u_cube, vec3(0.0, 1.0, 0.0), 1.0);\n"
      "  vec4 v = textureLod(u_volume, vec3(0.5, 0.5, 0.75), 1.0);\n"
      "  color = vec4(c.r, v.g, 1.0 - c.b - v.b, 1.0);\n"
      "}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   static uint32_t pixels[READ_SIZE * READ_SIZE];
   static const uint32_t zeroes[BASE_SIZE * BASE_SIZE * 4];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, SURFACE_TYPE,
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
   GLuint shaders[3] = {0, 0, 0};
   GLuint programs[2] = {0, 0};
   GLuint vao = 0, vbo = 0, fbo = 0, cube = 0, volume = 0;
   GLenum cube_status = 0, volume_status = 0, error = GL_NO_ERROR;
   GLint cube_face = 0, volume_layer = -1;
   GLint color_location = -1, cube_location = -1, volume_location = -1;
   int draw_status[3] = {-1, -1, -1};
   unsigned draw_calls = 0;
   unsigned cube_matches = 0, volume_matches = 0, sample_matches = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
#ifdef PS5_LAYERED_MIP_HOST_REFERENCE
   const EGLint surface_attributes[] = {EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE};
   surface = eglCreatePbufferSurface(display, config, surface_attributes);
#else
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
#endif
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, render_fragment_source,
                       &shaders[1]) ||
       !compile_shader(GL_FRAGMENT_SHADER, sample_fragment_source,
                       &shaders[2]) ||
       !link_program(shaders[0], shaders[1], &programs[0]) ||
       !link_program(shaders[0], shaders[2], &programs[1]))
      goto cleanup;

   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);

   glGenTextures(1, &cube);
   glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
   /* textureLod still obeys the minification filter: select mip level 1. */
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 1);
   for (unsigned face = 0; face < 6; ++face) {
      GLenum target = GL_TEXTURE_CUBE_MAP_POSITIVE_X + face;

      glTexImage2D(target, 0, GL_RGBA8, BASE_SIZE, BASE_SIZE, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, zeroes);
      glTexImage2D(target, 1, GL_RGBA8, MIP_SIZE, MIP_SIZE, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, zeroes);
   }

   glGenTextures(1, &volume);
   glBindTexture(GL_TEXTURE_3D, volume);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, 1);
   glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, BASE_SIZE, BASE_SIZE, 4, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, zeroes);
   glTexImage3D(GL_TEXTURE_3D, 1, GL_RGBA8, MIP_SIZE, MIP_SIZE, 2, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, zeroes);

   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_CUBE_MAP_POSITIVE_Y, cube, 1);
   cube_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE, &cube_face);
   if (cube_status != GL_FRAMEBUFFER_COMPLETE ||
       cube_face != GL_TEXTURE_CUBE_MAP_POSITIVE_Y ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;
   glViewport(0, 0, MIP_SIZE, MIP_SIZE);
   glUseProgram(programs[0]);
   color_location = glGetUniformLocation(programs[0], "u_color");
   if (color_location < 0)
      goto cleanup;
   glUniform4f(color_location, 1.0f, 0.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[0] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   cube_matches = read_center(pixels, MIP_SIZE, MIP_SIZE, RED);

   glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             volume, 1, 1);
   volume_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
      GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER, &volume_layer);
   if (volume_status != GL_FRAMEBUFFER_COMPLETE || volume_layer != 1 ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;
   glUniform4f(color_location, 0.0f, 1.0f, 0.0f, 1.0f);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[1] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   volume_matches = read_center(pixels, MIP_SIZE, MIP_SIZE, GREEN);

   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glViewport(0, 0, WIDTH, HEIGHT);
   glUseProgram(programs[1]);
   cube_location = glGetUniformLocation(programs[1], "u_cube");
   volume_location = glGetUniformLocation(programs[1], "u_volume");
   if (cube_location < 0 || volume_location < 0)
      goto cleanup;
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
   glUniform1i(cube_location, 0);
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_3D, volume);
   glUniform1i(volume_location, 1);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status[2] = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   sample_matches = read_center(pixels, WIDTH, HEIGHT, WHITE);
   error = glGetError();

   passed = major == 1 && minor >= 4 &&
            cube_status == GL_FRAMEBUFFER_COMPLETE &&
            volume_status == GL_FRAMEBUFFER_COMPLETE &&
            cube_face == GL_TEXTURE_CUBE_MAP_POSITIVE_Y &&
            volume_layer == 1 && draw_status[0] == 0 &&
            draw_status[1] == 0 && draw_status[2] == 0 && draw_calls == 3 &&
            cube_matches == READ_SIZE * READ_SIZE &&
            volume_matches == READ_SIZE * READ_SIZE &&
            sample_matches == READ_SIZE * READ_SIZE && error == GL_NO_ERROR;
   printf("[ps5-egl-layered-mip-fbo] status=%x/%x face=%x layer=%d "
          "draw=%d/%d/%d/%u match=%u/%u/%u error=0x%x result=%d\n",
          cube_status, volume_status, cube_face, volume_layer,
          draw_status[0], draw_status[1], draw_status[2], draw_calls,
          cube_matches, volume_matches, sample_matches, error,
          passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (fbo)
         glDeleteFramebuffers(1, &fbo);
      if (cube)
         glDeleteTextures(1, &cube);
      if (volume)
         glDeleteTextures(1, &volume);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (vao)
         glDeleteVertexArrays(1, &vao);
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
   printf("[ps5-egl-layered-mip-fbo] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
