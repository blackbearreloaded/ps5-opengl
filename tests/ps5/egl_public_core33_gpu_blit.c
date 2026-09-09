// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define TAG "[gpu-blit]"
#ifdef PS5_GPU_BLIT_HOST_REFERENCE
#define SURFACE_TYPE EGL_PBUFFER_BIT
static int ps5_egl_current_draw_status(unsigned *calls) { *calls=0; return 0; }
#else
#define SURFACE_TYPE EGL_WINDOW_BIT
int ps5_egl_current_draw_status(unsigned *calls);
#endif

static uint64_t now_ns(void)
{
   struct timespec t;
   return clock_gettime(CLOCK_MONOTONIC, &t) ? 0 :
      (uint64_t)t.tv_sec * UINT64_C(1000000000) + t.tv_nsec;
}

static int healthy(void)
{
   unsigned calls;
   GLenum error = glGetError();
   int status = ps5_egl_current_draw_status(&calls);
   if (error || status) printf(TAG " error=%x driver=%d draws=%u\n", error, status, calls);
   return !error && !status;
}

static uint8_t pattern(int x, int y, unsigned c)
{
   return (uint8_t)(x * (3 + c) + y * (7 + c) + c * 53);
}

static GLuint make_program(void)
{
   const char *sources[] = {
      "#version 330 core\nlayout(location=0) in vec2 p;\n"
      "void main(){ gl_Position=vec4(p,0,1); }\n",
      "#version 330 core\nuniform sampler2D image; uniform vec4 tint;\n"
      "out vec4 color; void main(){ color=texture(image,vec2(0.5))*tint; }\n"
   };
   GLuint program = glCreateProgram();
   if (!program) return 0;
   for (unsigned i=0; i<2; ++i) {
      GLuint shader=glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
      GLint ok=0;
      if (shader) {
         glShaderSource(shader,1,&sources[i],NULL); glCompileShader(shader);
         glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
         if (ok) glAttachShader(program,shader);
         glDeleteShader(shader);
      }
      if (!ok) { glDeleteProgram(program); return 0; }
   }
   GLint ok=0;
   glLinkProgram(program); glGetProgramiv(program,GL_LINK_STATUS,&ok);
   if (!ok) { glDeleteProgram(program); return 0; }
   return program;
}

static void copy(int width, int height, int inset)
{
   glBlitFramebuffer(inset,inset,width-inset,height-inset,
                     inset+1,inset+2,width-inset+1,height-inset+2,
                     GL_COLOR_BUFFER_BIT,GL_NEAREST);
}

static int oracle(uint8_t *pixels, int w, int h, int inset, int drawn, int solid)
{
   glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
   if (!healthy()) return 0;
   for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) for (unsigned c=0; c<4; ++c) {
      uint8_t expected=0xa5;
      if (x>=inset+1 && y>=inset+2 && x<w-inset+1 && y<h-inset+2)
         expected=solid ? (c == 1 || c == 3 ? 255 : 0) : pattern(x-1,y-2,c);
      /* Unchanged application shader, sampler unit 3, uniforms, vertex input,
       * viewport and color mask must work immediately after the internal blit. */
      if (drawn && x>=inset+3 && x<inset+16 && y>=inset+5 && y<inset+16 && c)
         expected=(uint8_t)((32+64*c)/2);
      uint8_t actual=pixels[((size_t)y*w+x)*4+c];
      if (actual != expected) {
         printf(TAG " mismatch size=%dx%d inset=%d drawn=%d solid=%d xy=%d/%d c=%u got=%u want=%u\n",
                w,h,inset,drawn,solid,x,y,c,actual,expected);
         return 0;
      }
   }
   return 1;
}

static int run_case(int w, int h, int inset, GLuint sample_texture)
{
   GLuint textures[2]={0}, fbos[2]={0};
   uint8_t *pixels=malloc((size_t)w*h*4);
   int passed=0;
   unsigned copies=0, before=0, after=0;
   uint64_t start=0, elapsed=0;
   if (!pixels) return 0;
   glGenTextures(2,textures); glGenFramebuffers(2,fbos);
   glActiveTexture(GL_TEXTURE0);
   for (unsigned i=0; i<2; ++i) {
      for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) for (unsigned c=0; c<4; ++c)
         pixels[((size_t)y*w+x)*4+c]=i ? 0xa5 : pattern(x,y,c);
      glBindTexture(GL_TEXTURE_2D,textures[i]);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
      glBindFramebuffer(GL_FRAMEBUFFER,fbos[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) goto cleanup;
      glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
   }
   glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,sample_texture);
   glViewport(inset+3,inset+5,13,11); glColorMask(GL_FALSE,GL_TRUE,GL_TRUE,GL_TRUE);
   glDrawArrays(GL_TRIANGLES,0,3); /* Establish the application's driver state. */
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[0]);
   copy(w,h,inset); glFinish();
   if (!healthy()) goto cleanup;
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[1]);
   if (!oracle(pixels,w,h,inset,0,0)) goto cleanup;
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[0]);
   /* Warm shader/upload caches, then time completed copies only; no upload,
    * readback, presentation or oracle work is inside the measurement. */
   copy(w,h,inset); glFinish();
   if (!healthy() || ps5_egl_current_draw_status(&before)) goto cleanup;
   start=now_ns(); if (!start) goto cleanup;
   do {
      copy(w,h,inset); glFinish(); ++copies;
      if (!healthy()) goto cleanup;
      uint64_t end=now_ns(); if (end<=start) goto cleanup;
      elapsed=end-start;
      if (elapsed>UINT64_C(5000000000)) goto cleanup;
   } while (elapsed<UINT64_C(1000000000) && copies<10000);
   if (ps5_egl_current_draw_status(&after)) goto cleanup;
   /* No rebind/reupload of shader, uniforms, vertex input, sampler or viewport. */
   glDrawArrays(GL_TRIANGLES,0,3); glFinish();
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[1]);
   if (!oracle(pixels,w,h,inset,1,0)) goto cleanup;
   /* A GPU-produced source must also be visible to the next copy. */
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbos[0]);
   glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
   glClearColor(0,1,0,1); glClear(GL_COLOR_BUFFER_BIT);
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[0]);
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbos[1]);
   copy(w,h,inset); glFinish();
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[1]);
   if (!oracle(pixels,w,h,inset,0,1)) goto cleanup;
   printf(TAG " size=%dx%d copy=%dx%d copies=%u elapsed_ns=%llu ms_per_copy=%.3f MiB_per_s=%.2f gpu_draws=%u pixels=%llu result=0\n",
          w,h,w-2*inset,h-2*inset,copies,(unsigned long long)elapsed,
          (double)elapsed/copies/1e6,
          (double)(w-2*inset)*(h-2*inset)*4*copies*1e9/(elapsed*1048576.0),after-before,
          (unsigned long long)w*h*3);
   passed=1;
cleanup:
   glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
   glBindFramebuffer(GL_FRAMEBUFFER,0);
   glDeleteFramebuffers(2,fbos); glDeleteTextures(2,textures); free(pixels);
   return healthy() && passed;
}

int main(void)
{
   const EGLint config_attrs[]={EGL_SURFACE_TYPE,SURFACE_TYPE,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
      EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
   const EGLint context_attrs[]={EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
   EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLSurface surface=EGL_NO_SURFACE; EGLContext context=EGL_NO_CONTEXT;
   EGLConfig config; EGLint count=0;
   GLuint program=0, vao=0, vbo=0, texture=0;
   int current=0, passed=0; EGLBoolean cleanup=EGL_TRUE;
   if (display==EGL_NO_DISPLAY || !eglInitialize(display,NULL,NULL) ||
       !eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(display,config_attrs,&config,1,&count) || count!=1)
      goto done;
#ifdef PS5_GPU_BLIT_HOST_REFERENCE
   const EGLint pb[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
   surface=eglCreatePbufferSurface(display,config,pb);
#else
   surface=eglCreateWindowSurface(display,config,(EGLNativeWindowType)0,NULL);
#endif
   context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
   if (surface==EGL_NO_SURFACE || context==EGL_NO_CONTEXT ||
       !eglMakeCurrent(display,surface,surface,context)) goto done;
   current=1;
   program=make_program(); if (!program) goto done;
   glUseProgram(program);
   GLint image=glGetUniformLocation(program,"image"), tint=glGetUniformLocation(program,"tint");
   if (image<0 || tint<0) goto done;
   glUniform1i(image,3); glUniform4f(tint,0.5f,0.5f,0.5f,0.5f);
   const GLfloat vertices[]={-1,-1,3,-1,-1,3};
   glGenVertexArrays(1,&vao); glBindVertexArray(vao);
   glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
   glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
   glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL); glEnableVertexAttribArray(0);
   const uint8_t color[]={32,96,160,224};
   glGenTextures(1,&texture); glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,texture);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
   glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,color);
   if (!healthy()) goto done;
   /* Includes a tiny CPU-preferred copy, threshold crossing and two large
    * offscreen footprints. All run in one 4K window, not separate HDMI modes. */
   const int sizes[][3]={{96,96,8},{544,544,16},{1920,1080,8},{3840,2160,8}};
   for (unsigned i=0; i<sizeof(sizes)/sizeof(sizes[0]); ++i)
      if (!run_case(sizes[i][0],sizes[i][1],sizes[i][2],texture)) goto done;
   passed=1;
done:
   if (current) {
      glUseProgram(0); glDeleteProgram(program); glDeleteTextures(1,&texture);
      glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
      cleanup &= healthy();
      cleanup &= eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
   }
   if (context!=EGL_NO_CONTEXT) cleanup &= eglDestroyContext(display,context);
   if (surface!=EGL_NO_SURFACE) cleanup &= eglDestroySurface(display,surface);
   if (display!=EGL_NO_DISPLAY) cleanup &= eglTerminate(display);
   cleanup &= eglGetError()==EGL_SUCCESS;
   printf(TAG " cleanup=%u result=%d\n",cleanup,passed && cleanup ? 0 : 1);
   return passed && cleanup ? 0 : 1;
}
