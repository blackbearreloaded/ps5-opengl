#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

int
main(void)
{
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
   uint8_t source[64];
   uint8_t initial[64];
   uint8_t actual[64];
   uint8_t before_invalid[64];
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint major = 0, minor = 0, count = 0;
   GLuint buffers[2] = {0};
   GLint64 size = 0;
   GLenum error = GL_NO_ERROR;
   int made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   for (unsigned i = 0; i < sizeof(source); ++i) {
      source[i] = (uint8_t)(i * 3u + 1u);
      initial[i] = UINT8_C(0xcc);
   }

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

   glGenBuffers(2, buffers);
   glBindBuffer(GL_COPY_READ_BUFFER, buffers[0]);
   glBufferData(GL_COPY_READ_BUFFER, sizeof(source), source, GL_STATIC_DRAW);
   glBindBuffer(GL_COPY_WRITE_BUFFER, buffers[1]);
   glBufferData(GL_COPY_WRITE_BUFFER, sizeof(initial), initial,
                GL_DYNAMIC_COPY);
   glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 8, 20, 24);
   glGetBufferParameteri64v(GL_COPY_WRITE_BUFFER, GL_BUFFER_SIZE, &size);
   glGetBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(actual), actual);
   error = glGetError();
   passed = size == (GLint64)sizeof(actual) && error == GL_NO_ERROR;
   for (unsigned i = 0; i < sizeof(actual); ++i) {
      uint8_t expected = i >= 20 && i < 44 ? source[i - 12] : initial[i];

      passed &= actual[i] == expected;
   }

   memcpy(before_invalid, actual, sizeof(actual));
   glBindBuffer(GL_COPY_READ_BUFFER, buffers[1]);
   glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 1, 16);
   error = glGetError();
   glGetBufferSubData(GL_COPY_WRITE_BUFFER, 0, sizeof(actual), actual);
   passed &= error == GL_INVALID_VALUE &&
             memcmp(actual, before_invalid, sizeof(actual)) == 0 &&
             glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-core33-buffer-copy] size=%lld overlap=0x%x result=%d\n",
          (long long)size, error, passed ? 0 : 1);

cleanup:
   if (buffers[0] || buffers[1])
      glDeleteBuffers(2, buffers);
   if (made_current) {
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
   printf("[ps5-egl-core33-buffer-copy] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
