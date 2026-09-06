#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLuint shader = glCreateShader(type);
   GLint compiled = GL_FALSE;

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   printf("[ps5-egl-cts-ubo-link] compile type=%x status=%d error=%x\n",
          type, compiled, glGetError());
   if (!compiled) {
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
      "#version 140\n"
      "uniform data2 { vec4 temp2; };\n"
      "void main() { gl_Position = temp2; }\n";
   static const char *fragment_source =
      "#version 140\n"
      "uniform data { vec4 temp; };\n"
      "out vec4 result;\n"
      "void main() { result = temp; }\n";
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   const EGLint surface_attributes[] = {
      EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE,
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
   EGLint count = 0;
   GLuint vertex = 0, fragment = 0, program = 0;
   GLint linked = GL_FALSE, name_length = 0;
   GLenum error = GL_NO_ERROR;
   int current = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreatePbufferSurface(display, config, surface_attributes);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context))
      goto cleanup;
   current = 1;

   if (!compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fragment) ||
       !compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, fragment);
   glAttachShader(program, vertex);
   printf("[ps5-egl-cts-ubo-link] link begin program=%u error=%x\n",
          program, glGetError());
   glLinkProgram(program);
   printf("[ps5-egl-cts-ubo-link] link returned error=%x\n", glGetError());
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   printf("[ps5-egl-cts-ubo-link] query link=%d error=%x\n",
          linked, glGetError());
   if (!linked)
      goto cleanup;
   glGetProgramiv(program, GL_ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH,
                  &name_length);
   error = glGetError();
   passed = name_length == 6 && error == GL_NO_ERROR;
   printf("[ps5-egl-cts-ubo-link] finish begin\n");
   glFinish();
   printf("[ps5-egl-cts-ubo-link] finish returned error=%x\n", glGetError());

cleanup:
   if (current) {
      if (fragment) {
         printf("[ps5-egl-cts-ubo-link] delete fragment begin\n");
         glDeleteShader(fragment);
         printf("[ps5-egl-cts-ubo-link] delete fragment returned\n");
      }
      if (program) {
         printf("[ps5-egl-cts-ubo-link] delete program begin\n");
         glDeleteProgram(program);
         printf("[ps5-egl-cts-ubo-link] delete program returned\n");
      }
      if (vertex) {
         printf("[ps5-egl-cts-ubo-link] delete vertex begin\n");
         glDeleteShader(vertex);
         printf("[ps5-egl-cts-ubo-link] delete vertex returned\n");
      }
      passed &= glGetError() == GL_NO_ERROR;
      passed &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                               EGL_NO_CONTEXT);
   }
   if (context != EGL_NO_CONTEXT)
      passed &= eglDestroyContext(display, context);
   if (surface != EGL_NO_SURFACE)
      passed &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      passed &= eglTerminate(display);
   passed &= eglGetError() == EGL_SUCCESS;
   printf("[ps5-egl-cts-ubo-link] linked=%d name=%d error=%x result=%d\n",
          linked, name_length, error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
