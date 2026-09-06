#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define WIDTH 4
#define HEIGHT 3
#define UNPACK_OFFSET 16
#define UNPACK_ROW_LENGTH 6
#define UNPACK_SKIP_PIXELS 1
#define UNPACK_SKIP_ROWS 1
#define UNPACK_STRIDE 24
#define PACK_OFFSET 32
#define PACK_ROW_LENGTH 7
#define PACK_SKIP_PIXELS 2
#define PACK_SKIP_ROWS 1
#define PACK_STRIDE 32
#define PACK_BYTES 160

static void
make_pixel(unsigned x, unsigned y, uint8_t pixel[4])
{
   pixel[0] = (uint8_t)(17u + x * 31u + y * 7u);
   pixel[1] = (uint8_t)(29u + x * 5u + y * 41u);
   pixel[2] = (uint8_t)(43u + x * 13u + y * 19u);
   pixel[3] = 255;
}

int
main(void)
{
   uint8_t unpack[UNPACK_OFFSET + (UNPACK_SKIP_ROWS + HEIGHT) *
                                  UNPACK_STRIDE];
   uint8_t tight[WIDTH * HEIGHT * 4];
   uint8_t initial_pack[PACK_BYTES];
   uint8_t expected_pack[PACK_BYTES];
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
   GLuint buffers[2] = {0}, texture = 0, framebuffer = 0;
   GLint unpack_alignment = 0, unpack_row_length = 0;
   GLint unpack_skip_pixels = 0, unpack_skip_rows = 0;
   GLint pack_alignment = 0, pack_row_length = 0;
   GLint pack_skip_pixels = 0, pack_skip_rows = 0;
   GLenum framebuffer_status = 0, error = GL_NO_ERROR;
   const uint8_t *mapped = NULL;
   int tight_matches = 1, pack_matches = 0, unmapped = 0;
   int made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   memset(unpack, 0xa5, sizeof(unpack));
   memset(initial_pack, 0xcd, sizeof(initial_pack));
   memcpy(expected_pack, initial_pack, sizeof(expected_pack));
   for (unsigned y = 0; y < HEIGHT; ++y) {
      for (unsigned x = 0; x < WIDTH; ++x) {
         uint8_t pixel[4];
         size_t unpack_index = UNPACK_OFFSET +
            (UNPACK_SKIP_ROWS + y) * UNPACK_STRIDE +
            (UNPACK_SKIP_PIXELS + x) * 4u;
         size_t pack_index = PACK_OFFSET +
            (PACK_SKIP_ROWS + y) * PACK_STRIDE +
            (PACK_SKIP_PIXELS + x) * 4u;

         make_pixel(x, y, pixel);
         memcpy(unpack + unpack_index, pixel, sizeof(pixel));
         memcpy(expected_pack + pack_index, pixel, sizeof(pixel));
      }
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
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffers[0]);
   glBufferData(GL_PIXEL_UNPACK_BUFFER, sizeof(unpack), unpack,
                GL_STATIC_DRAW);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, UNPACK_ROW_LENGTH);
   glPixelStorei(GL_UNPACK_SKIP_PIXELS, UNPACK_SKIP_PIXELS);
   glPixelStorei(GL_UNPACK_SKIP_ROWS, UNPACK_SKIP_ROWS);
   glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
   glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length);
   glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpack_skip_pixels);
   glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpack_skip_rows);

   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0,
                GL_RGBA, GL_UNSIGNED_BYTE,
                (const void *)(uintptr_t)UNPACK_OFFSET);
   glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
   glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
   glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);

   glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, tight);
   for (unsigned y = 0; y < HEIGHT; ++y) {
      for (unsigned x = 0; x < WIDTH; ++x) {
         uint8_t expected[4];

         make_pixel(x, y, expected);
         tight_matches &= !memcmp(tight + (y * WIDTH + x) * 4u,
                                  expected, sizeof(expected));
      }
   }

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
   glReadBuffer(GL_COLOR_ATTACHMENT0);
   framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

   glBindBuffer(GL_PIXEL_PACK_BUFFER, buffers[1]);
   glBufferData(GL_PIXEL_PACK_BUFFER, sizeof(initial_pack), initial_pack,
                GL_STREAM_READ);
   glPixelStorei(GL_PACK_ALIGNMENT, 8);
   glPixelStorei(GL_PACK_ROW_LENGTH, PACK_ROW_LENGTH);
   glPixelStorei(GL_PACK_SKIP_PIXELS, PACK_SKIP_PIXELS);
   glPixelStorei(GL_PACK_SKIP_ROWS, PACK_SKIP_ROWS);
   glGetIntegerv(GL_PACK_ALIGNMENT, &pack_alignment);
   glGetIntegerv(GL_PACK_ROW_LENGTH, &pack_row_length);
   glGetIntegerv(GL_PACK_SKIP_PIXELS, &pack_skip_pixels);
   glGetIntegerv(GL_PACK_SKIP_ROWS, &pack_skip_rows);
   glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
                (void *)(uintptr_t)PACK_OFFSET);
   mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, sizeof(initial_pack),
                             GL_MAP_READ_BIT);
   if (mapped)
      pack_matches = !memcmp(mapped, expected_pack, sizeof(expected_pack));
   if (mapped)
      unmapped = glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
   glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
   glPixelStorei(GL_PACK_ALIGNMENT, 4);
   glPixelStorei(GL_PACK_ROW_LENGTH, 0);
   glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
   glPixelStorei(GL_PACK_SKIP_ROWS, 0);
   error = glGetError();

   passed = major == 1 && minor == 4 &&
            unpack_alignment == 8 &&
            unpack_row_length == UNPACK_ROW_LENGTH &&
            unpack_skip_pixels == UNPACK_SKIP_PIXELS &&
            unpack_skip_rows == UNPACK_SKIP_ROWS &&
            pack_alignment == 8 && pack_row_length == PACK_ROW_LENGTH &&
            pack_skip_pixels == PACK_SKIP_PIXELS &&
            pack_skip_rows == PACK_SKIP_ROWS &&
            framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
            tight_matches && pack_matches && unmapped &&
            error == GL_NO_ERROR;
   printf("[ps5-egl-core33-pixel-buffer] unpack=%d/%d/%d/%d "
          "pack=%d/%d/%d/%d fbo=0x%x tight=%d packed=%d unmap=%d "
          "error=0x%x result=%d\n",
          unpack_alignment, unpack_row_length, unpack_skip_pixels,
          unpack_skip_rows, pack_alignment, pack_row_length,
          pack_skip_pixels, pack_skip_rows, framebuffer_status,
          tight_matches, pack_matches, unmapped, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      if (texture)
         glDeleteTextures(1, &texture);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
      if (buffers[0] || buffers[1])
         glDeleteBuffers(2, buffers);
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
   printf("[ps5-egl-core33-pixel-buffer] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
