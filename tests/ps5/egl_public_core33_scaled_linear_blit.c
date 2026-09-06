#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

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
   static const uint8_t source[2 * 2 * 4] = {
      255,   0,   0, 255,   0, 255,   0, 255,
        0,   0, 255, 255, 255, 255, 255, 255,
   };
   static const uint8_t expected[4 * 4 * 4] = {
      255,   0,   0, 255, 191,  64,   0, 255,
       64, 191,   0, 255,   0, 255,   0, 255,
      191,   0,  64, 255, 159,  64,  64, 255,
       96, 191,  64, 255,  64, 255,  64, 255,
       64,   0, 191, 255,  96,  64, 191, 255,
      159, 191, 191, 255, 191, 255, 191, 255,
        0,   0, 255, 255,  64,  64, 255, 255,
      191, 191, 255, 255, 255, 255, 255, 255,
   };
   static uint8_t pixels[sizeof(expected)];
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
   GLuint textures[2] = {0, 0};
   GLuint framebuffers[2] = {0, 0};
   GLenum error = GL_NO_ERROR;
   GLenum source_status = 0, destination_status = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0;
   int passed = 0;

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

   glGenTextures(2, textures);
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, source);
   glBindTexture(GL_TEXTURE_2D, textures[1]);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   glGenFramebuffers(2, framebuffers);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
   glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, textures[0], 0);
   source_status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
   glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, textures[1], 0);
   destination_status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
   if (source_status != GL_FRAMEBUFFER_COMPLETE ||
       destination_status != GL_FRAMEBUFFER_COMPLETE)
      goto cleanup;

   glBlitFramebuffer(0, 0, 2, 2, 0, 0, 4, 4,
                     GL_COLOR_BUFFER_BIT, GL_LINEAR);
   glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[1]);
   glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   error = glGetError();
   passed = major == 1 && minor == 4 && error == GL_NO_ERROR &&
            memcmp(pixels, expected, sizeof(expected)) == 0;
   printf("[ps5-egl-scaled-linear-blit] hash=%08x expected=%08x "
          "status=%x/%x error=0x%x result=%d\n",
          hash32(pixels, sizeof(pixels)), hash32(expected, sizeof(expected)),
          source_status, destination_status, error, passed ? 0 : 1);
   if (!passed) {
      for (unsigned y = 0; y < 4; ++y) {
         printf("[ps5-egl-scaled-linear-blit] row%u", y);
         for (unsigned x = 0; x < 4; ++x) {
            const uint8_t *pixel = pixels + (y * 4u + x) * 4u;

            printf(" %02x%02x%02x%02x", pixel[0], pixel[1],
                   pixel[2], pixel[3]);
         }
         printf("\n");
      }
   }

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffers[0] || framebuffers[1])
         glDeleteFramebuffers(2, framebuffers);
      if (textures[0] || textures[1])
         glDeleteTextures(2, textures);
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
   printf("[ps5-egl-scaled-linear-blit] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
