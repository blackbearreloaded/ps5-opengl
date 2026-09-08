// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define BLOCK_BYTES 8

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
   uint8_t compressed_1d[2 * BLOCK_BYTES];
   uint8_t expected_1d[2 * BLOCK_BYTES];
   uint8_t read_1d[2 * BLOCK_BYTES] = {0};
   uint8_t texels_1d[8] = {0};
   uint8_t compressed_3d[2 * BLOCK_BYTES];
   uint8_t expected_3d[2 * BLOCK_BYTES];
   uint8_t read_3d[2 * BLOCK_BYTES] = {0};
   uint8_t texels_3d[4 * 4 * 2] = {0};
   uint8_t replacement[BLOCK_BYTES];
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
   GLint compressed_size[2] = {0, 0};
   GLenum error = GL_NO_ERROR;
   int bytes_ok = 0, texels_ok = 1, made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   fill_block(compressed_1d, 32);
   fill_block(compressed_1d + BLOCK_BYTES, 64);
   memcpy(expected_1d, compressed_1d, sizeof(expected_1d));
   fill_block(replacement, 224);
   memcpy(expected_1d + BLOCK_BYTES, replacement, BLOCK_BYTES);
   fill_block(compressed_3d, 48);
   fill_block(compressed_3d + BLOCK_BYTES, 96);
   memcpy(expected_3d, compressed_3d, sizeof(expected_3d));
   memcpy(expected_3d + BLOCK_BYTES, replacement, BLOCK_BYTES);

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
   glBindTexture(GL_TEXTURE_1D, textures[0]);
   glCompressedTexImage1D(GL_TEXTURE_1D, 0, GL_COMPRESSED_RED_RGTC1,
                          8, 0, sizeof(compressed_1d), compressed_1d);
   glCompressedTexSubImage1D(GL_TEXTURE_1D, 0, 4, 4,
                             GL_COMPRESSED_RED_RGTC1,
                             sizeof(replacement), replacement);
   glGetTexLevelParameteriv(GL_TEXTURE_1D, 0,
                            GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                            &compressed_size[0]);
   glGetCompressedTexImage(GL_TEXTURE_1D, 0, read_1d);
   glGetTexImage(GL_TEXTURE_1D, 0, GL_RED, GL_UNSIGNED_BYTE, texels_1d);

   glBindTexture(GL_TEXTURE_3D, textures[1]);
   glCompressedTexImage3D(GL_TEXTURE_3D, 0, GL_COMPRESSED_RED_RGTC1,
                          4, 4, 2, 0, sizeof(compressed_3d), compressed_3d);
   glCompressedTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 1, 4, 4, 1,
                             GL_COMPRESSED_RED_RGTC1,
                             sizeof(replacement), replacement);
   glGetTexLevelParameteriv(GL_TEXTURE_3D, 0,
                            GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
                            &compressed_size[1]);
   glGetCompressedTexImage(GL_TEXTURE_3D, 0, read_3d);
   glGetTexImage(GL_TEXTURE_3D, 0, GL_RED, GL_UNSIGNED_BYTE, texels_3d);

   bytes_ok = compressed_size[0] == (GLint)sizeof(compressed_1d) &&
              compressed_size[1] == (GLint)sizeof(compressed_3d) &&
              !memcmp(read_1d, expected_1d, sizeof(expected_1d)) &&
              !memcmp(read_3d, expected_3d, sizeof(expected_3d));
   for (unsigned i = 0; i < 8; ++i)
      texels_ok &= texels_1d[i] == (i < 4 ? 32 : 224);
   for (unsigned i = 0; i < 4 * 4 * 2; ++i)
      texels_ok &= texels_3d[i] == (i < 16 ? 48 : 224);
   error = glGetError();
   passed = major == 1 && minor == 4 && bytes_ok && texels_ok &&
            error == GL_NO_ERROR;
   printf("[ps5-egl-compressed-dimensions] size=%d/%d bytes=%d texels=%d "
          "error=0x%x result=%d\n", compressed_size[0], compressed_size[1],
          bytes_ok, texels_ok, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
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
   printf("[ps5-egl-compressed-dimensions] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
