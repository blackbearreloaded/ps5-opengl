#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define SIZE 8
#define BLOCK_BYTES 8
#define COMPRESSED_BYTES 32

static void
fill_block(uint8_t block[BLOCK_BYTES], uint8_t value)
{
   memset(block, 0, BLOCK_BYTES);
   block[0] = value;
   block[1] = value;
}

int
main(void)
{
   uint8_t compressed[COMPRESSED_BYTES];
   uint8_t expected[COMPRESSED_BYTES];
   uint8_t replacement[BLOCK_BYTES];
   uint8_t compressed_read[COMPRESSED_BYTES];
   uint8_t red_read[SIZE * SIZE];
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
   GLuint texture = 0;
   GLint is_compressed = GL_FALSE, compressed_size = 0;
   GLenum error = GL_NO_ERROR;
   int bytes_ok = 0, texels_ok = 1, made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   for (unsigned block = 0; block < 4; ++block)
      fill_block(compressed + block * BLOCK_BYTES, 32);
   memcpy(expected, compressed, sizeof(expected));
   fill_block(replacement, 224);
   memcpy(expected + 3 * BLOCK_BYTES, replacement, sizeof(replacement));

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

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RED_RGTC1,
                          SIZE, SIZE, 0, sizeof(compressed), compressed);
   glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 4, 4, 4, 4,
                             GL_COMPRESSED_RED_RGTC1,
                             sizeof(replacement), replacement);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED,
                            &is_compressed);
   glGetTexLevelParameteriv(GL_TEXTURE_2D, 0,
                            GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                            &compressed_size);
   glGetCompressedTexImage(GL_TEXTURE_2D, 0, compressed_read);
   bytes_ok = !memcmp(compressed_read, expected, sizeof(expected));
   glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE, red_read);
   for (unsigned y = 0; y < SIZE; ++y) {
      for (unsigned x = 0; x < SIZE; ++x) {
         uint8_t value = x >= 4 && y >= 4 ? 224 : 32;

         texels_ok &= red_read[y * SIZE + x] == value;
      }
   }
   error = glGetError();
   passed = major == 1 && minor == 4 && is_compressed == GL_TRUE &&
            compressed_size == COMPRESSED_BYTES && bytes_ok && texels_ok &&
            error == GL_NO_ERROR;
   printf("[ps5-egl-core33-rgtc-transfer] compressed=%d/%d bytes=%d "
          "texels=%d error=0x%x result=%d\n",
          is_compressed, compressed_size, bytes_ok, texels_ok,
          error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      if (texture)
         glDeleteTextures(1, &texture);
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
   printf("[ps5-egl-core33-rgtc-transfer] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
