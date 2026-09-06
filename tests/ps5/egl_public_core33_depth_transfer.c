#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define SIZE 8
#define PIXELS (SIZE * SIZE)

struct depth_stencil_pixel {
   float depth;
   uint32_t stencil;
};

static int
check_depth(const float *values, int with_patch)
{
   for (unsigned y = 0; y < SIZE; ++y) {
      for (unsigned x = 0; x < SIZE; ++x) {
         float expected = with_patch && x >= 3 && x < 5 &&
                          y >= 2 && y < 4 ? 0.75f : 0.25f;

         if (values[y * SIZE + x] != expected)
            return 0;
      }
   }
   return 1;
}

int
main(void)
{
   static float depth_source[PIXELS];
   static const float depth_patch[4] = {0.75f, 0.75f, 0.75f, 0.75f};
   static float depth_texture_read[PIXELS];
   static float depth_framebuffer_read[PIXELS];
   static struct depth_stencil_pixel packed_source[PIXELS];
   static struct depth_stencil_pixel packed_texture_read[PIXELS];
   static float packed_depth_read[PIXELS];
   static uint8_t packed_stencil_read[PIXELS];
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
   GLuint textures[2] = {0}, framebuffer = 0;
   GLenum depth_status = 0, packed_status = 0, error = GL_NO_ERROR;
   int depth_texture_ok = 0, depth_framebuffer_ok = 0;
   int packed_texture_ok = 1, packed_depth_ok = 1, packed_stencil_ok = 1;
   int made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   for (unsigned i = 0; i < PIXELS; ++i) {
      depth_source[i] = 0.25f;
      packed_source[i].depth = 0.625f;
      packed_source[i].stencil = UINT32_C(0x5a);
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

   glGenTextures(2, textures);
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                SIZE, SIZE, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depth_source);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 2,
                   GL_DEPTH_COMPONENT, GL_FLOAT, depth_patch);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
                 depth_texture_read);
   depth_texture_ok = check_depth(depth_texture_read, 1);

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                          GL_TEXTURE_2D, textures[0], 0);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   depth_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glReadPixels(0, 0, SIZE, SIZE, GL_DEPTH_COMPONENT, GL_FLOAT,
                depth_framebuffer_read);
   depth_framebuffer_ok = check_depth(depth_framebuffer_read, 1);

   glBindTexture(GL_TEXTURE_2D, textures[1]);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH32F_STENCIL8,
                SIZE, SIZE, 0, GL_DEPTH_STENCIL,
                GL_FLOAT_32_UNSIGNED_INT_24_8_REV, packed_source);
   glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL,
                 GL_FLOAT_32_UNSIGNED_INT_24_8_REV, packed_texture_read);
   for (unsigned i = 0; i < PIXELS; ++i) {
      packed_texture_ok &= packed_texture_read[i].depth == 0.625f &&
                           (packed_texture_read[i].stencil & 0xffu) == 0x5au;
   }

   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                          GL_TEXTURE_2D, textures[1], 0);
   packed_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   glReadPixels(0, 0, SIZE, SIZE, GL_DEPTH_COMPONENT, GL_FLOAT,
                packed_depth_read);
   glReadPixels(0, 0, SIZE, SIZE, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE,
                packed_stencil_read);
   for (unsigned i = 0; i < PIXELS; ++i) {
      packed_depth_ok &= packed_depth_read[i] == 0.625f;
      packed_stencil_ok &= packed_stencil_read[i] == 0x5a;
   }
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            depth_status == GL_FRAMEBUFFER_COMPLETE &&
            packed_status == GL_FRAMEBUFFER_COMPLETE &&
            depth_texture_ok && depth_framebuffer_ok &&
            packed_texture_ok && packed_depth_ok && packed_stencil_ok &&
            error == GL_NO_ERROR;
   printf("[ps5-egl-core33-depth-transfer] fbo=0x%x/0x%x "
          "depth=%d/%d packed=%d/%d/%d error=0x%x result=%d\n",
          depth_status, packed_status, depth_texture_ok,
          depth_framebuffer_ok, packed_texture_ok, packed_depth_ok,
          packed_stencil_ok, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
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
   printf("[ps5-egl-core33-depth-transfer] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
