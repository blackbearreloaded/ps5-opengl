/* Bounded clear regression: exact RGBA8 sweep, shader-state reuse, query exclusion. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

#define WIDTH 64
#define HEIGHT 32
static uint8_t pixels[WIDTH * HEIGHT * 4];

static int read_pixels(void)
{
   glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   return glGetError() == GL_NO_ERROR;
}

static GLuint shader(GLenum type, const char *text)
{
   GLuint result = glCreateShader(type);
   GLint ok = GL_FALSE;
   glShaderSource(result, 1, &text, NULL);
   glCompileShader(result);
   glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[512];
      glGetShaderInfoLog(result, sizeof(log), NULL, log);
      printf("[ps5-gpu-clear] shader failed: %s\n", log);
      glDeleteShader(result);
      return 0;
   }
   return result;
}

int main(void)
{
   const EGLint config_attrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
   const EGLint context_attrs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE};
   const EGLint surface_attrs[] = {EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE};
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLContext context = EGL_NO_CONTEXT;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLConfig config = NULL;
   EGLint count = 0;
   EGLBoolean clean = EGL_TRUE;
   GLuint vs = 0, fs = 0, program = 0, vao = 0, query = 0;
   int current = 0, passed = 0;
   unsigned completed = 0;
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attrs, &config, 1, &count) || count != 1)
      goto cleanup;
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
   surface = eglCreatePbufferSurface(display, config, surface_attrs);
   if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
       !eglMakeCurrent(display, surface, surface, context))
      goto cleanup;
   current = 1;

   for (unsigned value = 0; value < 256; ++value) {
      uint8_t expected[] = {value, 255 - value, (value * 37) & 255, (value * 11) & 255};
      glClearColor(expected[0] / 255.0f, expected[1] / 255.0f,
                   expected[2] / 255.0f, expected[3] / 255.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      if (!read_pixels()) goto cleanup;
      for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
         if (memcmp(pixels + i * 4, expected, 4)) {
            printf("[ps5-gpu-clear] value=%u pixel=%u got=%u,%u,%u,%u\n", value, i,
                   pixels[i*4], pixels[i*4+1], pixels[i*4+2], pixels[i*4+3]);
            goto cleanup;
         }
      }
      ++completed;
   }
   printf("[ps5-gpu-clear] rgba8-sweep=%u pixels=%u PASS\n", completed, completed * WIDTH * HEIGHT);

   vs = shader(GL_VERTEX_SHADER, "#version 330 core\n"
      "void main(){vec2 p[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));"
      "gl_Position=vec4(p[gl_VertexID],0,1);}");
   fs = shader(GL_FRAGMENT_SHADER, "#version 330 core\n"
      "uniform vec4 tint[2]; out vec4 result; void main(){result=tint[0]+tint[1];}");
   if (!vs || !fs) goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glLinkProgram(program);
   GLint linked = GL_FALSE;
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) goto cleanup;
   glUseProgram(program);
   GLint tint = glGetUniformLocation(program, "tint[0]");
   const GLfloat uniforms[] = {0.2f, 0.4f, 0.6f, 0.8f, 0, 0, 0, 0};
   if (tint < 0) goto cleanup;
   glUniform4fv(tint, 2, uniforms);
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glViewport(4, 4, WIDTH - 8, HEIGHT - 8);
   glDrawArrays(GL_TRIANGLES, 0, 3); /* Make inline FS uniforms live in the driver. */
   glClearColor(0, 0, 0, 0);
   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3); /* No uniform/program/viewport rebind after clear. */
   if (!read_pixels()) goto cleanup;
   for (unsigned y = 0; y < HEIGHT; ++y) {
      for (unsigned x = 0; x < WIDTH; ++x) {
         int inside = x >= 4 && x < WIDTH - 4 && y >= 4 && y < HEIGHT - 4;
         const uint8_t expected[] = {inside ? 51 : 0, inside ? 102 : 0,
                                    inside ? 153 : 0, inside ? 204 : 0};
         if (memcmp(pixels + (y * WIDTH + x) * 4, expected, 4)) goto cleanup;
      }
   }
   printf("[ps5-gpu-clear] inline-uniforms/program/viewport restored PASS\n");

   glGenQueries(1, &query);
   glBeginQuery(GL_SAMPLES_PASSED, query);
   glClearColor(1, 0, 0, 1);
   glClear(GL_COLOR_BUFFER_BIT); /* Clears must not count as visible draw samples. */
   glEndQuery(GL_SAMPLES_PASSED);
   GLuint samples = 1;
   glGetQueryObjectuiv(query, GL_QUERY_RESULT, &samples);
   if (samples || !read_pixels()) goto cleanup;
   for (unsigned i = 0; i < WIDTH * HEIGHT; ++i) {
      const uint8_t expected[] = {255, 0, 0, 255};
      if (memcmp(pixels + i * 4, expected, 4)) goto cleanup;
   }
   printf("[ps5-gpu-clear] active-query fallback samples=%u PASS\n", samples);
   passed = 1;
cleanup:
   if (current) {
      glUseProgram(0);
      if (query) glDeleteQueries(1, &query);
      if (vao) glDeleteVertexArrays(1, &vao);
      if (program) glDeleteProgram(program);
      if (fs) glDeleteShader(fs);
      if (vs) glDeleteShader(vs);
      clean &= glGetError() == GL_NO_ERROR;
   }
   if (display != EGL_NO_DISPLAY) {
      clean &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (surface != EGL_NO_SURFACE) clean &= eglDestroySurface(display, surface);
      if (context != EGL_NO_CONTEXT) clean &= eglDestroyContext(display, context);
      clean &= eglTerminate(display);
   }
   clean &= eglGetError() == EGL_SUCCESS;
   printf("[ps5-gpu-clear] completed=%u cleanup=%u result=%d\n", completed, clean, passed && clean ? 0 : 1);
   return passed && clean ? 0 : 1;
}
