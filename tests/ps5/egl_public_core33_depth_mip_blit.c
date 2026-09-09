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
#include <GL/glext.h>

#define TAG "[depth-mip-blit]"
#define SIZE 32
#define PIXELS (SIZE * SIZE)
#define LEVELS 3
#define LAYERS 4

#ifdef PS5_DEPTH_MIP_BLIT_HOST_REFERENCE
#define SURFACE_TYPE EGL_PBUFFER_BIT
#else
#define SURFACE_TYPE EGL_WINDOW_BIT
int ps5_egl_current_draw_status(unsigned *calls);
#endif

struct pixel { float depth; uint8_t stencil; };
/* Resources: two mip chains, one single-level texture, one MSAA4 texture.
 * All expected pixels are CPU-owned; no readback feeds the oracle. */
static struct pixel expected[4][LEVELS][LAYERS][PIXELS];
static GLuint textures[4], fbos[3], readback;
static GLenum targets[4], attachment, all_mask;
static int layers, packed;
static unsigned cases, passed_cases, pixels, stencil_pixels;
static unsigned depth_errors, stencil_errors, gl_errors, fbo_errors, driver_errors;

struct blit_case {
   const char *name;
   int src, src_level, src_layer, dst, dst_level, dst_layer;
   int from[4], to[4], scissor[4];
};

/* Layer 2/3 become layer 0 for 2D. Distinct images, including same-texture
 * different levels, avoid undefined overlapping copies. All rectangles fit.
 * Desktop GL permits 1x <-> 4x with matching formats and equal extents;
 * only 1x cases flip/scale/scissor. Depth/stencil always use GL_NEAREST. */
static const struct blit_case batch[] = {
   {"mip0-mip1", 0,0,2, 1,1,3, {3,5,15,17}, {1,2,13,14}, {0}},
   {"mip1-mip2", 0,1,3, 1,2,2, {2,3,8,9}, {1,1,7,7}, {0}},
   {"mip2-mip0", 0,2,2, 1,0,3, {0,0,8,8}, {9,11,17,19}, {0}},
   {"same-chain-0-1", 0,0,2, 0,1,3, {3,5,15,17}, {1,2,13,14}, {0}},
   {"same-chain-1-0", 0,1,3, 0,0,2, {1,2,13,14}, {9,11,21,23}, {0}},
   {"single-mip", 2,0,2, 1,2,3, {5,9,11,15}, {1,1,7,7}, {0}},
   {"mip-single", 0,1,3, 2,0,2, {1,2,15,14}, {5,7,19,19}, {0}},
   {"flip-source-x", 0,1,2, 1,1,3, {15,1,1,15}, {1,1,15,15}, {0}},
   {"flip-dest-y", 0,1,3, 1,1,2, {1,1,15,15}, {1,15,15,1}, {0}},
   {"scale-up", 0,2,2, 1,1,3, {0,0,8,8}, {0,0,16,16}, {0}},
   {"scale-down", 0,0,3, 1,2,2, {1,3,29,31}, {0,0,8,8}, {0}},
   {"flip-scale-scissor", 0,1,2, 1,2,3, {15,0,1,14}, {0,0,8,8}, {1,2,5,3}},
   {"msaa4-mip", 3,0,3, 1,1,2, {7,5,21,19}, {1,1,15,15}, {0}},
   {"mip-msaa4", 0,2,2, 3,0,3, {1,1,7,7}, {9,11,15,17}, {0}},
};

static int
check_errors(const char *phase)
{
   unsigned before = gl_errors + driver_errors;
   GLenum error;
   while ((error = glGetError()) != GL_NO_ERROR) {
      ++gl_errors;
      printf(TAG " phase=%s gl_error=0x%x\n", phase, error);
   }
#ifndef PS5_DEPTH_MIP_BLIT_HOST_REFERENCE
   unsigned calls = 0;
   int status = ps5_egl_current_draw_status(&calls);
   if (status) {
      ++driver_errors;
      printf(TAG " phase=%s driver_status=%d calls=%u\n", phase, status, calls);
   }
#endif
   return before == gl_errors + driver_errors;
}

static int
check_framebuffer(int samples)
{
   GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   GLint actual = -1;
   glGetIntegerv(GL_SAMPLES, &actual);
   int ok = check_errors("framebuffer");
   if (status != GL_FRAMEBUFFER_COMPLETE || actual != samples) {
      ++fbo_errors;
      printf(TAG " fbo=0x%x samples=%d expected=%d\n", status, actual, samples);
      ok = 0;
   }
   return ok;
}

static int
attach(GLuint fbo, int resource, int level, int layer)
{
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   if (layers == 1)
      glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, targets[resource],
                             textures[resource], level);
   else
      glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, textures[resource], level, layer);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   return check_framebuffer(resource == 3 ? 4 : 0);
}

static struct pixel
pattern(unsigned seed, int x, int y)
{
   unsigned tile = (unsigned)(y / 4 * 8 + x / 4);
   struct pixel value = {(seed * 128 + tile + 1) / 32768.0f,
                        (uint8_t)(seed * 37 + tile * 13)};
   return value;
}

static int
initialize_image(int resource, int level, int layer, unsigned seed)
{
   int size = SIZE >> level;
   if (!attach(fbos[0], resource, level, layer))
      return 0;
   glDepthMask(GL_TRUE);
   glStencilMask(0xff);
   glEnable(GL_SCISSOR_TEST);
   /* Spatially varying, exactly representable depths and distinct stencil bytes.
    * ponytail: 4x4 blocks bound native clear work; use 1x1 for sub-block faults.
    * Clears initialize every MS sample equally; no sample-isolation claim. */
   for (int y = 0; y < size; y += 4) {
      for (int x = 0; x < size; x += 4) {
         struct pixel value = pattern(seed, x, y);
         glScissor(x, y, 4, 4);
         glClearDepth(value.depth);
         glClearStencil(value.stencil);
         glClear(all_mask);
      }
   }
   glDisable(GL_SCISSOR_TEST);
   glFinish();
   if (!check_errors("initialize-image"))
      return 0;
   for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x)
         expected[resource][level][layer][y * size + x] = pattern(seed, x, y);
   return 1;
}

static int
verify_all(void)
{
   float depths[PIXELS];
   uint8_t stencils[PIXELS];
   unsigned before = depth_errors + stencil_errors;
   glDisable(GL_SCISSOR_TEST);
   /* Read both chains at all levels and all layers, plus single-level and MS
    * images. This checks source preservation, neighboring images/rectangles,
    * and BOTH packed channels even when the blit requests only one. */
   for (int resource = 0; resource < 4; ++resource) {
      for (int level = 0; level < (resource < 2 ? LEVELS : 1); ++level) {
         int size = SIZE >> level;
         for (int layer = 0; layer < layers; ++layer) {
            if (!attach(fbos[0], resource, level, layer))
               return 0;
            if (resource == 3) {
               glBindFramebuffer(GL_FRAMEBUFFER, fbos[2]);
               if (!check_framebuffer(0))
                  return 0;
               glClearDepth(1);
               glClearStencil(0xee);
               glClear(all_mask);
               if (!check_errors("readback-initialize"))
                  return 0;
               glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
               glBlitFramebuffer(0, 0, SIZE, SIZE, 0, 0, SIZE, SIZE, all_mask, GL_NEAREST);
               if (!check_errors("readback-resolve"))
                  return 0;
               glBindFramebuffer(GL_FRAMEBUFFER, fbos[2]);
            }
            glFinish();
            for (int i = 0; i < size * size; ++i)
               depths[i] = -1;
            memset(stencils, 0xff, sizeof(stencils));
            /* glReadPixels is only issued against a single-sample framebuffer. */
            glReadPixels(0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
            if (packed)
               glReadPixels(0, 0, size, size, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencils);
            if (!check_errors("read-pixels"))
               return 0;
            for (int i = 0; i < size * size; ++i) {
               const struct pixel *value = &expected[resource][level][layer][i];
               float expected_depth = value->depth;
               int bad_depth = depths[i] != expected_depth;
               int bad_stencil = packed && stencils[i] != value->stencil;
               if ((bad_depth || bad_stencil) && before == depth_errors + stencil_errors)
                  printf(TAG " mismatch case=%u resource=%d level=%d layer=%d xy=%d,%d "
                         "depth=%g expected=%g stencil=%u expected=%u\n",
                         cases, resource, level, layer, i % size, i / size,
                         (double)depths[i], (double)expected_depth, stencils[i], value->stencil);
               depth_errors += bad_depth;
               stencil_errors += bad_stencil;
            }
            pixels += (unsigned)(size * size);
            if (packed)
               stencil_pixels += (unsigned)(size * size);
         }
      }
   }
   return before == depth_errors + stencil_errors;
}

static void
apply_oracle(const struct blit_case *c, GLbitfield mask, int sl, int dl)
{
   int size = SIZE >> c->dst_level, source_size = SIZE >> c->src_level;
   /* Inverse destination-pixel-center mapping, independently of driver tiling
    * and integer blit loops. Chosen ratios avoid source-texel boundary ties. */
   for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
         double u = (x + 0.5 - c->to[0]) / (c->to[2] - c->to[0]);
         double v = (y + 0.5 - c->to[1]) / (c->to[3] - c->to[1]);
         if (u < 0 || u >= 1 || v < 0 || v >= 1 ||
             (c->scissor[2] && (x < c->scissor[0] || y < c->scissor[1] ||
              x >= c->scissor[0] + c->scissor[2] || y >= c->scissor[1] + c->scissor[3])))
            continue;
         int sx = (int)(c->from[0] + u * (c->from[2] - c->from[0]));
         int sy = (int)(c->from[1] + v * (c->from[3] - c->from[1]));
         struct pixel source = expected[c->src][c->src_level][sl][sy * source_size + sx];
         struct pixel *dest = &expected[c->dst][c->dst_level][dl][y * size + x];
         if (mask & GL_DEPTH_BUFFER_BIT)
            dest->depth = source.depth;
         if (mask & GL_STENCIL_BUFFER_BIT)
            dest->stencil = source.stencil;
      }
   }
}

static int
run_case(const struct blit_case *c, GLbitfield mask)
{
   int sl = layers == 1 ? 0 : c->src_layer, dl = layers == 1 ? 0 : c->dst_layer;
   unsigned before_pixels = pixels, before_depth = depth_errors, before_stencil = stencil_errors;
   int ok = 0;
   ++cases;
   /* Fresh destination sentinels keep repeated masks/copies observable. Other
    * images retain their CPU history, including earlier same-chain copies. */
   if (!initialize_image(c->dst, c->dst_level, dl, 64 + cases) ||
       !attach(fbos[0], c->src, c->src_level, sl) ||
       !attach(fbos[1], c->dst, c->dst_level, dl))
      goto done;
   if (c->scissor[2]) {
      glEnable(GL_SCISSOR_TEST);
      glScissor(c->scissor[0], c->scissor[1], c->scissor[2], c->scissor[3]);
   }
   glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
   glBlitFramebuffer(c->from[0], c->from[1], c->from[2], c->from[3],
                     c->to[0], c->to[1], c->to[2], c->to[3], mask, GL_NEAREST);
   glDisable(GL_SCISSOR_TEST);
   glFinish();
   if (!check_errors("blit"))
      goto done;
   apply_oracle(c, mask, sl, dl);
   ok = verify_all();
done:
   passed_cases += ok;
   printf(TAG " case=%u format=%s target=%s stage=%s mask=%s src=%d:%d:%d dst=%d:%d:%d "
          "samples=%d->%d pixels=%u depth_errors=%u stencil_errors=%u result=%d\n",
          cases, packed ? "D32S8" : "D32", layers == 1 ? "2D" : "2D-array", c->name,
          mask == GL_DEPTH_BUFFER_BIT ? "depth" : mask == GL_STENCIL_BUFFER_BIT ? "stencil" : "both",
          c->src, c->src_level, sl, c->dst, c->dst_level, dl,
          c->src == 3 ? 4 : 1, c->dst == 3 ? 4 : 1,
          pixels - before_pixels, depth_errors - before_depth, stencil_errors - before_stencil, !ok);
   return ok;
}

static int
test_variant(GLenum format, int array)
{
   int ok = 0, cleanup_ok;
   packed = format == GL_DEPTH32F_STENCIL8;
   layers = array ? LAYERS : 1;
   attachment = packed ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
   all_mask = GL_DEPTH_BUFFER_BIT | (packed ? GL_STENCIL_BUFFER_BIT : 0);
   glGenTextures(4, textures);
   glGenFramebuffers(3, fbos);
   glGenRenderbuffers(1, &readback);
   for (int resource = 0; resource < 4; ++resource) {
      targets[resource] = resource == 3
         ? (array ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY : GL_TEXTURE_2D_MULTISAMPLE)
         : (array ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D);
      GLenum target = targets[resource];
      glBindTexture(target, textures[resource]);
      if (resource == 3) {
         if (array)
            glTexImage3DMultisample(target, 4, format, SIZE, SIZE, layers, GL_TRUE);
         else
            glTexImage2DMultisample(target, 4, format, SIZE, SIZE, GL_TRUE);
      } else {
         glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, 0);
         glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, resource < 2 ? LEVELS - 1 : 0);
         glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
         glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         for (int level = 0; level < (resource < 2 ? LEVELS : 1); ++level) {
            int size = SIZE >> level;
            GLenum external = packed ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT;
            GLenum type = packed ? GL_FLOAT_32_UNSIGNED_INT_24_8_REV : GL_FLOAT;
            if (array)
               glTexImage3D(target, level, format, size, size, layers, 0, external, type, NULL);
            else
               glTexImage2D(target, level, format, size, size, 0, external, type, NULL);
         }
      }
      if (!check_errors("allocate-texture"))
         goto cleanup;
   }
   /* On the native driver, the single-level textures exercise tiled storage;
    * OpenGL itself does not specify layout, and llvmpipe cannot validate it. */
   glBindFramebuffer(GL_FRAMEBUFFER, fbos[2]);
   glBindRenderbuffer(GL_RENDERBUFFER, readback);
   glRenderbufferStorage(GL_RENDERBUFFER, format, SIZE, SIZE);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, readback);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   if (!check_framebuffer(0))
      goto cleanup;
   for (int resource = 0; resource < 4; ++resource)
      for (int level = 0; level < (resource < 2 ? LEVELS : 1); ++level)
         for (int layer = 0; layer < layers; ++layer)
            if (!initialize_image(resource, level, layer, resource * 16 + level * 4 + layer + 1))
               goto cleanup;
   unsigned before = pixels;
   ok = verify_all();
   printf(TAG " format=%s target=%s stage=initial pixels=%u result=%d\n",
          packed ? "D32S8" : "D32", array ? "2D-array" : "2D", pixels - before, !ok);
   if (!ok)
      goto cleanup;
   const GLbitfield masks[] = {GL_DEPTH_BUFFER_BIT, GL_STENCIL_BUFFER_BIT, all_mask};
   for (unsigned m = 0; m < (packed ? 3u : 1u); ++m) {
      for (unsigned i = 0; i < sizeof(batch) / sizeof(batch[0]); ++i) {
         ok = run_case(&batch[i], masks[m]);
         if (!ok)
            goto cleanup;
      }
   }
cleanup:
   glDisable(GL_SCISSOR_TEST);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glBindRenderbuffer(GL_RENDERBUFFER, 0);
   glDeleteFramebuffers(3, fbos);
   glDeleteRenderbuffers(1, &readback);
   glDeleteTextures(4, textures);
   cleanup_ok = check_errors("variant-cleanup");
   ok &= cleanup_ok;
   printf(TAG " format=%s target=%s cleanup=%d result=%d\n",
          packed ? "D32S8" : "D32", array ? "2D-array" : "2D", cleanup_ok, !ok);
   return ok;
}

int
main(void)
{
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, SURFACE_TYPE, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE,
   };
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL;
   EGLint count = 0;
   int current = 0, ok = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) || count != 1)
      goto cleanup;
#ifdef PS5_DEPTH_MIP_BLIT_HOST_REFERENCE
   const EGLint surface_attributes[] = {EGL_WIDTH, SIZE, EGL_HEIGHT, SIZE, EGL_NONE};
   surface = eglCreatePbufferSurface(display, config, surface_attributes);
#else
   surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, NULL);
#endif
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context))
      goto cleanup;
   current = 1;
   printf(TAG " mode=%s renderer=%s version=%s size=32 levels=0/1/2 array_layers=2/3 "
          "uniform_samples=1 sample_isolation=0\n",
#ifdef PS5_DEPTH_MIP_BLIT_HOST_REFERENCE
          "host",
#else
          "native",
#endif
          glGetString(GL_RENDERER), glGetString(GL_VERSION));
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glEnable(GL_MULTISAMPLE);
   if (!check_errors("setup"))
      goto cleanup;
   ok = test_variant(GL_DEPTH_COMPONENT32F, 0) && test_variant(GL_DEPTH_COMPONENT32F, 1) &&
        test_variant(GL_DEPTH32F_STENCIL8, 0) && test_variant(GL_DEPTH32F_STENCIL8, 1);
   ok &= check_errors("final");
cleanup:
   if (current)
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   EGLint egl_error = eglGetError();
   cleanup_ok &= egl_error == EGL_SUCCESS;
   ok &= cleanup_ok;
   unsigned errors = depth_errors + stencil_errors + gl_errors + fbo_errors + driver_errors;
   printf(TAG " summary cases=%u passed=%u pixels=%u depth_pixels=%u stencil_pixels=%u "
          "errors=%u depth_errors=%u stencil_errors=%u gl_errors=%u fbo_errors=%u "
          "driver_errors=%u egl_error=0x%x cleanup=%u result=%d\n",
          cases, passed_cases, pixels, pixels, stencil_pixels, errors + !cleanup_ok,
          depth_errors, stencil_errors, gl_errors, fbo_errors, driver_errors,
          egl_error, cleanup_ok, !ok);
   return ok ? 0 : 1;
}
