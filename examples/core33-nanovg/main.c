// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

// Unmodified NanoVG GL3 renderer; native platform glue and public-API oracles.
#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"

static int check(int ok, const char *stage)
{
   if (!ok)
      printf("[ps5-nanovg] FAIL %s gl=0x%x egl=0x%x\n", stage, glGetError(), eglGetError());
   return ok;
}

static void rectangle(NVGcontext *vg, float x, float y, float w, float h, NVGcolor color)
{
   nvgBeginPath(vg);
   nvgRect(vg, x, y, w, h);
   nvgFillColor(vg, color);
   nvgFill(vg);
}

static int render_frames(EGLDisplay display, EGLSurface surface)
{
   static const unsigned char texels[16] = {
      255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255
   };
   static const int probes[][5] = {
      {16,24,0,0,255}, {40,24,128,0,127}, {96,16,255,255,0},
      {120,40,8,12,20}, {176,36,255,0,255}, {200,36,8,12,20},
      {16,112,8,12,20}, {40,112,0,255,0}, {104,96,255,0,0},
      {144,96,0,255,0}, {104,136,0,0,255}, {144,136,255,255,255},
      {208,112,130,130,130}, {200,190,4,134,138}, {280,200,8,12,20}
   };
   unsigned char *pixels = malloc(640 * 480 * 4);
   unsigned char *stencil = malloc(640 * 480);
   GLuint framebuffer = 0, color = 0, depth = 0;
   NVGcontext *vg = NULL;
   int ok = pixels && stencil, all_frames = 1;
   glGenFramebuffers(1, &framebuffer);
   glGenRenderbuffers(1, &color);
   glGenRenderbuffers(1, &depth);
   for (int frame = 0; frame < 3 && ok; ++frame) {
      const int scale = frame == 1 ? 2 : 1;
      const int width = 320 * scale, height = 240 * scale;
      if (frame == 2) {
         nvgDeleteGL3(vg);
         vg = NULL;
      }
      if (!vg)
         vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
      if (!check(vg != NULL, "renderer/shader initialization")) { ok = 0; break; }
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
      glBindRenderbuffer(GL_RENDERBUFFER, color);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
      glBindRenderbuffer(GL_RENDERBUFFER, depth);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
      if (!check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
                 "complete color/stencil framebuffer")) { ok = 0; break; }
      glViewport(0, 0, width, height);
      glDisable(GL_SCISSOR_TEST);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glStencilMask(255);
      glClearColor(8.0f/255, 12.0f/255, 20.0f/255, 1);
      glClearStencil(0);
      glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
      int image = nvgCreateImageRGBA(vg, 2, 2, NVG_IMAGE_NEAREST, texels);
      if (!check(image != 0 && glGetError() == GL_NO_ERROR, "image/setup")) { ok = 0; break; }
      nvgBeginFrame(vg, 320, 240, (float)scale);
      rectangle(vg, 8, 8, 64, 48, nvgRGB(0,0,255));
      rectangle(vg, 24, 16, 32, 32, nvgRGBA(255,0,0,128));
      nvgBeginPath(vg);
      nvgRect(vg, 88, 8, 64, 64);
      nvgCircle(vg, 120, 40, 12);
      nvgPathWinding(vg, NVG_HOLE);
      nvgFillColor(vg, nvgRGB(255,255,0));
      nvgFill(vg);
      nvgBeginPath(vg);
      nvgRect(vg, 176, 16, 48, 40);
      nvgStrokeWidth(vg, 8);
      nvgStrokeColor(vg, nvgRGB(255,0,255));
      nvgStroke(vg);
      nvgSave(vg);
      nvgScissor(vg, 24, 96, 32, 24);
      rectangle(vg, 8, 88, 64, 48, nvgRGB(0,255,0));
      nvgRestore(vg);
      nvgBeginPath(vg);
      nvgRect(vg, 96, 88, 64, 64);
      nvgFillPaint(vg, nvgImagePattern(vg, 96, 88, 64, 64, 0, image, 1));
      nvgFill(vg);
      nvgBeginPath(vg);
      nvgRect(vg, 184, 88, 48, 48);
      nvgFillPaint(vg, nvgLinearGradient(vg, 184, 88, 232, 88, nvgRGB(0,0,0), nvgRGB(255,255,255)));
      nvgFill(vg);
      nvgBeginPath(vg);
      nvgMoveTo(vg, 180, 170); nvgLineTo(vg, 220, 210);
      nvgLineTo(vg, 180, 210); nvgLineTo(vg, 220, 170);
      nvgStrokeWidth(vg, 8);
      nvgStrokeColor(vg, nvgRGBA(0,255,255,128));
      nvgStroke(vg);
      nvgEndFrame(vg);
      glFinish();
      glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      glReadPixels(0, 0, width, height, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencil);
      ok = check(glGetError() == GL_NO_ERROR, "render/readback");
      int frame_ok = ok;
      for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
         const int *p = probes[i];
         const unsigned char *actual = pixels + ((height - 1 - p[1] * scale) * width + p[0] * scale) * 4;
         int match = actual[3] == 255;
         for (unsigned c = 0; c < 3; ++c) match &= abs(actual[c] - p[c + 2]) <= 3;
         if (!match)
            printf("[ps5-nanovg] pixel=%d,%d got=%u/%u/%u/%u expected=%d/%d/%d/255\n",
                   p[0], p[1], actual[0], actual[1], actual[2], actual[3], p[2], p[3], p[4]);
         frame_ok &= match;
      }
      unsigned dirty_stencil = 0;
      for (int i = 0; i < width * height; ++i) dirty_stencil += stencil[i] != 0;
      frame_ok &= dirty_stencil == 0;
      nvgDeleteImage(vg, image);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
      glBlitFramebuffer(0, 0, width, height, 0, 0, 1280, 960, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      ok = check(glGetError() == GL_NO_ERROR && eglSwapBuffers(display, surface), "present") && ok;
      all_frames &= frame_ok;
      printf("[ps5-nanovg] frame=%d scale=%d probes=15 dirty-stencil=%u %s\n",
             frame, scale, dirty_stencil, frame_ok && ok ? "PASS" : "FAIL");
   }
   if (vg) nvgDeleteGL3(vg);
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glDeleteFramebuffers(1, &framebuffer);
   glDeleteRenderbuffers(1, &color);
   glDeleteRenderbuffers(1, &depth);
   free(stencil); free(pixels);
   return check(glGetError() == GL_NO_ERROR, "resource cleanup") && ok && all_frames;
}

int main(void)
{
#ifdef PS5_NANOVG_HOST_REFERENCE
   const EGLint surface_type = EGL_PBUFFER_BIT;
#else
   const EGLint surface_type = EGL_WINDOW_BIT;
#endif
   const EGLint attributes[] = {EGL_SURFACE_TYPE, surface_type, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
   const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE};
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLContext context = EGL_NO_CONTEXT;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLConfig config = NULL;
   EGLint count = 0;
   int result = 1;
   if (check(display != EGL_NO_DISPLAY && eglInitialize(display, NULL, NULL) && eglBindAPI(EGL_OPENGL_API) &&
             eglChooseConfig(display, attributes, &config, 1, &count) && count == 1, "EGL initialization")) {
#ifdef PS5_NANOVG_HOST_REFERENCE
      const EGLint pbuffer[] = {EGL_WIDTH, 1920, EGL_HEIGHT, 1080, EGL_NONE};
      surface = eglCreatePbufferSurface(display, config, pbuffer);
#else
      surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, NULL);
#endif
      context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
      if (check(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
                eglMakeCurrent(display, surface, surface, context), "EGL context")) {
         printf("[ps5-nanovg] upstream=ce3bf745eb2d2dbc14a50bf2446783f691ac4353 GL=%s GLSL=%s renderer=%s\n",
                glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION), glGetString(GL_RENDERER));
         result = render_frames(display, surface) ? 0 : 1;
      }
   }
   EGLBoolean cleanup = EGL_TRUE;
   if (display != EGL_NO_DISPLAY) {
      cleanup &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (context != EGL_NO_CONTEXT) cleanup &= eglDestroyContext(display, context);
      if (surface != EGL_NO_SURFACE) cleanup &= eglDestroySurface(display, surface);
      cleanup &= eglTerminate(display);
   }
   if (!check(cleanup && eglGetError() == EGL_SUCCESS, "EGL cleanup")) result = 1;
   printf("[ps5-nanovg] finished status=%d\n", result);
   return result;
}
