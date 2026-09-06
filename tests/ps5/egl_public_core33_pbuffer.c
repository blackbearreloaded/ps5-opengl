#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 96
#define HEIGHT 64

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
      printf("[ps5-egl-core33-pbuffer] shader=0x%x log=%.*s\n",
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
      "void main(){gl_Position=vec4(position,0,1);}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "layout(location=0) out vec4 color;\n"
      "void main(){color=vec4(1,0,1,1);}\n";
   static const float vertices[6] = {
      -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f,
   };
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
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
   const EGLint pbuffer_attributes[] = {
      EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT,
      EGL_TEXTURE_FORMAT, EGL_NO_TEXTURE,
      EGL_TEXTURE_TARGET, EGL_NO_TEXTURE,
      EGL_MIPMAP_TEXTURE, EGL_FALSE,
      EGL_NONE,
   };
   const EGLint negative_attributes[] = {
      EGL_WIDTH, -1, EGL_HEIGHT, HEIGHT, EGL_NONE,
   };
   const EGLint oversize_attributes[] = {
      EGL_WIDTH, 1921, EGL_HEIGHT, HEIGHT, EGL_NONE,
   };
   const EGLint texture_attributes[] = {
      EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT,
      EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
      EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface pbuffer = EGL_NO_SURFACE;
   EGLSurface invalid[3] = {EGL_NO_SURFACE};
   EGLContext context = EGL_NO_CONTEXT;
   EGLint invalid_error[3] = {EGL_SUCCESS};
   EGLint major = 0, minor = 0, count = 0;
   EGLint surface_type = 0, max_width = 0, max_height = 0;
   EGLint width = 0, height = 0, texture_format = -1;
   GLuint shaders[2] = {0}, program = 0, vao = 0, vbo = 0;
   GLint linked = GL_FALSE;
   uint8_t pixel[4] = {0};
   GLenum error = GL_NO_ERROR;
   unsigned draw_calls = 0;
   int draw_status = -1, made_current = 0, passed = 0;
   EGLBoolean swapped = EGL_FALSE, cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1 ||
       !eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE, &surface_type) ||
       !eglGetConfigAttrib(display, config, EGL_MAX_PBUFFER_WIDTH,
                           &max_width) ||
       !eglGetConfigAttrib(display, config, EGL_MAX_PBUFFER_HEIGHT,
                           &max_height))
      goto cleanup;

   invalid[0] = eglCreatePbufferSurface(display, config,
                                        negative_attributes);
   invalid_error[0] = eglGetError();
   invalid[1] = eglCreatePbufferSurface(display, config,
                                        oversize_attributes);
   invalid_error[1] = eglGetError();
   invalid[2] = eglCreatePbufferSurface(display, config,
                                        texture_attributes);
   invalid_error[2] = eglGetError();
   pbuffer = eglCreatePbufferSurface(display, config, pbuffer_attributes);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (pbuffer == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglQuerySurface(display, pbuffer, EGL_WIDTH, &width) ||
       !eglQuerySurface(display, pbuffer, EGL_HEIGHT, &height) ||
       !eglQuerySurface(display, pbuffer, EGL_TEXTURE_FORMAT,
                        &texture_format) ||
       !eglMakeCurrent(display, pbuffer, pbuffer, context) ||
       !eglSwapInterval(display, 1))
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
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
   glEnableVertexAttribArray(0);
   glUseProgram(program);
   glViewport(0, 0, WIDTH, HEIGHT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1,
                GL_RGBA, GL_UNSIGNED_BYTE, pixel);
   swapped = eglSwapBuffers(display, pbuffer);
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            (surface_type & EGL_PBUFFER_BIT) &&
            max_width == 1920 && max_height == 1080 &&
            invalid[0] == EGL_NO_SURFACE &&
            invalid_error[0] == EGL_BAD_PARAMETER &&
            invalid[1] == EGL_NO_SURFACE &&
            invalid_error[1] == EGL_BAD_ALLOC &&
            invalid[2] == EGL_NO_SURFACE &&
            invalid_error[2] == EGL_BAD_MATCH &&
            width == WIDTH && height == HEIGHT &&
            texture_format == EGL_NO_TEXTURE && swapped &&
            pixel[0] == 255 && pixel[1] == 0 &&
            pixel[2] == 255 && pixel[3] == 255 &&
            draw_status == 0 && draw_calls == 1 && error == GL_NO_ERROR;
   printf("[ps5-egl-core33-pbuffer] config=0x%x/%dx%d invalid=%x/%x/%x "
          "surface=%dx%d/%x pixel=%u/%u/%u/%u draw=%d/%u swap=%u "
          "error=0x%x result=%d\n",
          surface_type, max_width, max_height,
          invalid_error[0], invalid_error[1], invalid_error[2],
          width, height, texture_format, pixel[0], pixel[1], pixel[2],
          pixel[3], draw_status, draw_calls, swapped, error,
          passed ? 0 : 1);

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
   if (display != EGL_NO_DISPLAY && pbuffer != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, pbuffer);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf("[ps5-egl-core33-pbuffer] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
