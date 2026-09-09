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

#define TAG "[native-color-formats]"
#ifdef PS5_NATIVE_COLOR_FORMATS_HOST_REFERENCE
#define SURFACE_TYPE EGL_PBUFFER_BIT
static int ps5_egl_current_draw_status(unsigned *calls) { *calls=0; return 0; }
#else
#define SURFACE_TYPE EGL_WINDOW_BIT
int ps5_egl_current_draw_status(unsigned *calls);
#endif

struct format_case { const char *name; GLenum storage, upload; unsigned channels; };
enum phase { UPLOADED, DRAWN, CLEARED_RB, STATE_RB, CLEARED_GA, STATE_GA, TIMED };
enum color_id { DRAW_A, DRAW_B, CLEAR_A, PROBE_A, CLEAR_B, PROBE_B, TIMING_COLOR, BORDER };
static const uint8_t byte_colors[][4]={
   {51,102,153,204}, {187,34,221,85}, {17,238,119,68}, {85,153,34,221},
   {221,68,187,17}, {119,204,51,153}, {34,187,85,238}, {17,85,153,221}
};
/* All components, including alpha, are exact half-floats. No tolerance can
 * conceal accidental UNORM clamping, swapped channels, NaNs or address errors. */
static const float float_colors[][4]={
   {-.5f,1.5f,2,-.25f}, {3,-2,.125f,1.75f}, {4,-3,.5f,2.5f}, {-1.25f,2.25f,3.5f,-.75f},
   {-2.5f,5,-4,.375f}, {.25f,-1.5f,6,2}, {-4,2.5f,.75f,1.25f}, {-3,4,.5f,-1}
};
/* Rectangles are x,y,width,height. Both clear masks cross 128-pixel tile edges;
 * the probe viewport straddles its scissor so restoration affects pixels. */
static const int clear_rects[2][4]={{125,127,257,119},{127,125,259,121}};
static const int probe_rects[2][4]={{121,123,29,23},{123,121,31,25}};

static int healthy(const char *stage, unsigned *calls)
{
   unsigned ignored=0;
   if (!calls) calls=&ignored;
   GLenum error=glGetError();
   int status=ps5_egl_current_draw_status(calls);
   if (error || status) printf(TAG " stage=%s error=%x driver=%d draws=%u\n",stage,error,status,*calls);
   return !error && !status;
}

static uint64_t now_ns(void)
{
   struct timespec t;
   return clock_gettime(CLOCK_MONOTONIC,&t) ? 0 :
      (uint64_t)t.tv_sec * UINT64_C(1000000000) + t.tv_nsec;
}

/* Reuse the public GLSL330/VBO harness; only the two fragment operations differ. */
static GLuint make_program(int sampling)
{
   const char *sources[]={
      "#version 330 core\nlayout(location=0) in vec2 p;\n"
      "void main(){gl_Position=vec4(p,0,1);}\n",
      sampling ?
      "#version 330 core\nuniform sampler2D image; uniform vec2 extent; out vec4 color;\n"
      "void main(){color=texture(image,gl_FragCoord.xy/extent);}\n" :
      "#version 330 core\nuniform vec4 tint; out vec4 color;\n"
      "void main(){color=tint;}\n"
   };
   GLuint program=glCreateProgram();
   if (!program) return 0;
   for (unsigned i=0; i<2; ++i) {
      GLuint shader=glCreateShader(i ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
      GLint ok=0;
      if (shader) {
         glShaderSource(shader,1,&sources[i],NULL); glCompileShader(shader);
         glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
         if (ok) glAttachShader(program,shader);
         else {
            char log[512]; GLsizei length=0;
            glGetShaderInfoLog(shader,sizeof(log),&length,log);
            printf(TAG " sampling=%d shader=%u log=%.*s\n",sampling,i,length,log);
         }
         glDeleteShader(shader);
      }
      if (!ok) { glDeleteProgram(program); return 0; }
   }
   GLint ok=0;
   glLinkProgram(program); glGetProgramiv(program,GL_LINK_STATUS,&ok);
   if (!ok) {
      char log[512]; GLsizei length=0;
      glGetProgramInfoLog(program,sizeof(log),&length,log);
      printf(TAG " sampling=%d link log=%.*s\n",sampling,length,log);
      glDeleteProgram(program); return 0;
   }
   return program;
}

static float value(const struct format_case *t, unsigned id, unsigned c)
{
   return t->storage==GL_RGBA16F ? float_colors[id][c] : byte_colors[id][c];
}

static void uniform_color(const struct format_case *t, GLint tint, unsigned id)
{
   float color[4];
   for (unsigned c=0; c<4; ++c)
      color[c]=value(t,id,c)/(t->storage==GL_RGBA16F ? 1.0f : 255.0f);
   glUniform4fv(tint,1,color);
}

static float upload_value(const struct format_case *t, int x, int y, unsigned c)
{
   int n=x*(3+(int)c)+y*(7+(int)c)+(int)c*53;
   return t->storage==GL_RGBA16F ? ((n%65)-32)*.125f : (uint8_t)n;
}

static int inside(int x, int y, int rx, int ry, int rw, int rh)
{
   return x>=rx && x<rx+rw && y>=ry && y<ry+rh;
}

static float expected_source(const struct format_case *t, int w, int h,
                             enum phase phase, int x, int y, unsigned c)
{
   if (c>=t->channels) return c==3 ? 255.0f : 0.0f;
   if (phase==TIMED) return value(t,TIMING_COLOR,c);
   float expected=upload_value(t,x,y,c);
   if (phase>=DRAWN) {
      if (inside(x,y,11,17,w-34,h-46)) expected=value(t,DRAW_A,c);
      if (inside(x,y,125,127,131,113)) expected=value(t,DRAW_B,c);
   }
   for (unsigned k=0; k<2; ++k) {
      const int *r=clear_rects[k], *p=probe_rects[k];
      if ((unsigned)phase>=CLEARED_RB+2*k && (c&1)==k && inside(x,y,r[0],r[1],r[2],r[3])) {
         expected=value(t,CLEAR_A+2*k,c);
         if ((unsigned)phase>=STATE_RB+2*k && inside(x,y,p[0],p[1],p[2],p[3]))
            expected=value(t,PROBE_A+2*k,c);
      }
   }
   return expected;
}

static int oracle(const struct format_case *t, int w, int h, enum phase phase,
                  int sampled, const char *stage, void *pixels)
{
   int floating=t->storage==GL_RGBA16F;
   unsigned calls=0;
   glReadPixels(0,0,w,h,GL_RGBA,floating ? GL_FLOAT : GL_UNSIGNED_BYTE,pixels);
   if (!healthy(stage,&calls)) return 0;
   for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) for (unsigned c=0; c<4; ++c) {
      int border=sampled && !inside(x,y,7,9,w-20,h-24);
      float expected=border ? value(t,BORDER,c) : expected_source(t,w,h,phase,x,y,c);
      size_t i=((size_t)y*w+x)*4+c;
      float actual=floating ? ((float *)pixels)[i] : ((uint8_t *)pixels)[i];
      if (actual!=expected) {
         printf(TAG " mismatch format=%s size=%dx%d stage=%s xy=%d/%d c=%u got=%.9g want=%.9g border=%d\n",
                t->name,w,h,stage,x,y,c,actual,expected,border);
         return 0;
      }
   }
   printf(TAG " format=%s size=%dx%d stage=%s pixels=%u driver=0 draws=%u result=0\n",
          t->name,w,h,stage,(unsigned)w*h,calls);
   return 1;
}

static void sample(GLuint fbo, GLuint texture, GLuint program, GLint extent, int w, int h)
{
   /* No finish/readback between the preceding FBO draw and this sampling draw. */
   glBindFramebuffer(GL_FRAMEBUFFER,fbo);
   glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,texture);
   glUseProgram(program); glUniform2f(extent,(float)w,(float)h);
   glViewport(0,0,w,h);
   glEnable(GL_SCISSOR_TEST); glScissor(7,9,w-20,h-24);
   glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
   glDrawArrays(GL_TRIANGLES,0,3);
}

static int run_case(const struct format_case *t, int w, int h,
                    GLuint render, GLint tint, GLuint sampling, GLint extent, int timed)
{
   GLuint textures[2]={0}, fbos[2]={0};
   int passed=0, floating=t->storage==GL_RGBA16F;
   void *pixels=malloc((size_t)w*h*4*(floating ? sizeof(float) : sizeof(uint8_t)));
   unsigned initial=0, before=0, after=0, total=0;
   if (!pixels) { printf(TAG " format=%s allocation failed result=1\n",t->name); return 0; }
   if (!healthy("begin",&initial)) goto cleanup;
   glGenTextures(2,textures); glGenFramebuffers(2,fbos);
   glActiveTexture(GL_TEXTURE0);
   for (unsigned target=0; target<2; ++target) {
      unsigned channels=target ? 4 : t->channels;
      for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) for (unsigned c=0; c<channels; ++c) {
         size_t i=((size_t)y*w+x)*channels+c;
         float v=target ? value(t,BORDER,c) : upload_value(t,x,y,c);
         if (floating) ((float *)pixels)[i]=v;
         else ((uint8_t *)pixels)[i]=(uint8_t)v;
      }
      glBindTexture(GL_TEXTURE_2D,textures[target]);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glTexImage2D(GL_TEXTURE_2D,0,target ? (floating ? GL_RGBA16F : GL_RGBA8) : t->storage,
                   w,h,0,target ? GL_RGBA : t->upload,floating ? GL_FLOAT : GL_UNSIGNED_BYTE,pixels);
      glBindFramebuffer(GL_FRAMEBUFFER,fbos[target]);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[target],0);
      GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status!=GL_FRAMEBUFFER_COMPLETE) {
         printf(TAG " format=%s size=%dx%d fbo=%u status=%x\n",t->name,w,h,target,status); goto cleanup;
      }
   }
   glBindFramebuffer(GL_FRAMEBUFFER,fbos[0]);
   if (!oracle(t,w,h,UPLOADED,0,"upload-readback",pixels)) goto cleanup;
   glUseProgram(render); glDisable(GL_SCISSOR_TEST);
   glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
   uniform_color(t,tint,DRAW_A); glViewport(11,17,w-34,h-46); glDrawArrays(GL_TRIANGLES,0,3);
   uniform_color(t,tint,DRAW_B); glViewport(125,127,131,113); glDrawArrays(GL_TRIANGLES,0,3);
   sample(fbos[1],textures[0],sampling,extent,w,h);
   if (!oracle(t,w,h,DRAWN,1,"draw-sample",pixels)) goto cleanup;
   glBindFramebuffer(GL_FRAMEBUFFER,fbos[0]);
   if (!oracle(t,w,h,DRAWN,0,"draw-readback",pixels)) goto cleanup;
   for (unsigned k=0; k<2; ++k) {
      const int *r=clear_rects[k], *p=probe_rects[k];
      float clear[4];
      for (unsigned c=0; c<4; ++c) clear[c]=value(t,CLEAR_A+2*k,c)/(floating ? 1.0f : 255.0f);
      glBindFramebuffer(GL_FRAMEBUFFER,fbos[0]);
      glUseProgram(render); uniform_color(t,tint,PROBE_A+2*k);
      glViewport(p[0],p[1],p[2],p[3]);
      glEnable(GL_SCISSOR_TEST); glScissor(r[0],r[1],r[2],r[3]);
      glColorMask(!k,k,!k,k);
      glDrawArrays(GL_TRIANGLES,0,3); /* Establish the state, overwritten by the following clear. */
      glClearColor(clear[0],clear[1],clear[2],clear[3]); glClear(GL_COLOR_BUFFER_BIT);
      if (!oracle(t,w,h,(enum phase)(CLEARED_RB+2*k),0,k ? "clear-ga":"clear-rb",pixels)) goto cleanup;
      /* Reissue the clear after readback, then draw without rebinding program,
       * uniforms, VBO/VAO, viewport, scissor or color mask. */
      glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLES,0,3);
      sample(fbos[1],textures[0],sampling,extent,w,h);
      if (!oracle(t,w,h,(enum phase)(STATE_RB+2*k),1,k ? "state-ga-sample":"state-rb-sample",pixels)) goto cleanup;
      glBindFramebuffer(GL_FRAMEBUFFER,fbos[0]);
      if (!oracle(t,w,h,(enum phase)(STATE_RB+2*k),0,k ? "state-ga-readback":"state-rb-readback",pixels)) goto cleanup;
   }
   if (timed) {
      glUseProgram(render); uniform_color(t,tint,TIMING_COLOR);
      glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
      glViewport(0,0,w,h); glDrawArrays(GL_TRIANGLES,0,3); glFinish();
      if (!healthy("timing-warmup",&before)) goto cleanup;
      uint64_t start=now_ns(); if (!start) goto cleanup;
      for (unsigned i=0; i<8; ++i) {
         glDrawArrays(GL_TRIANGLES,0,3); glFinish();
         if (!healthy("timing-draw",&after)) goto cleanup;
      }
      uint64_t end=now_ns(); if (end<=start) goto cleanup;
      /* Timing excludes allocations, uploads, sampling and all pixel checks. */
      sample(fbos[1],textures[0],sampling,extent,w,h);
      if (!oracle(t,w,h,TIMED,1,"timing-sample",pixels)) goto cleanup;
      glBindFramebuffer(GL_FRAMEBUFFER,fbos[0]);
      if (!oracle(t,w,h,TIMED,0,"timing-readback",pixels)) goto cleanup;
      printf(TAG " timing format=%s size=%dx%d repeats=8 elapsed_ns=%llu ms_per_draw=%.3f"
             " driver=0 draws=%u->%u delta=%u result=0\n",t->name,w,h,
             (unsigned long long)(end-start),(double)(end-start)/8e6,before,after,after-before);
   }
   passed=1;
cleanup:
   glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
   glBindFramebuffer(GL_FRAMEBUFFER,0);
   glDeleteFramebuffers(2,fbos); glDeleteTextures(2,textures); free(pixels);
   int status=ps5_egl_current_draw_status(&total);
   if (!healthy("case-cleanup",NULL) || status) passed=0;
   printf(TAG " case=%s size=%dx%d mip=0 layers=1 driver=%d draws=%u->%u delta=%u result=%d\n",
          t->name,w,h,status,initial,total,total-initial,passed ? 0:1);
   return passed;
}

int main(void)
{
   const EGLint config_attrs[]={EGL_SURFACE_TYPE,SURFACE_TYPE,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
      EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
   const EGLint context_attrs[]={EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
   static const struct format_case formats[]={
      {"R8",GL_R8,GL_RED,1}, {"RG8",GL_RG8,GL_RG,2}, {"RGBA16F",GL_RGBA16F,GL_RGBA,4}
   };
   EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLSurface surface=EGL_NO_SURFACE; EGLContext context=EGL_NO_CONTEXT;
   EGLConfig config; EGLint count=0;
   GLuint programs[2]={0}, vao=0, vbo=0;
   unsigned cases=0;
   int current=0, initialized=0, passed=0; EGLBoolean cleanup=EGL_TRUE;
   if (display==EGL_NO_DISPLAY || !eglInitialize(display,NULL,NULL)) goto done;
   initialized=1;
   if (!eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(display,config_attrs,&config,1,&count) || count!=1) goto done;
#ifdef PS5_NATIVE_COLOR_FORMATS_HOST_REFERENCE
   const EGLint pb[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
   surface=eglCreatePbufferSurface(display,config,pb);
#else
   surface=eglCreateWindowSurface(display,config,(EGLNativeWindowType)0,NULL);
#endif
   context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
   if (surface==EGL_NO_SURFACE || context==EGL_NO_CONTEXT ||
       !eglMakeCurrent(display,surface,surface,context)) goto done;
   current=1;
   printf(TAG " renderer=%s counters=%s\n",(const char *)glGetString(GL_RENDERER),
#ifdef PS5_NATIVE_COLOR_FORMATS_HOST_REFERENCE
          "host-stub"
#else
          "native-driver"
#endif
   );
   for (unsigned i=0; i<2; ++i) { programs[i]=make_program(i); if (!programs[i]) goto done; }
   GLint tint=glGetUniformLocation(programs[0],"tint");
   GLint image=glGetUniformLocation(programs[1],"image"), extent=glGetUniformLocation(programs[1],"extent");
   if (tint<0 || image<0 || extent<0) goto done;
   glUseProgram(programs[1]); glUniform1i(image,3);
   const GLfloat vertices[]={-1,-1,3,-1,-1,3};
   glGenVertexArrays(1,&vao); glBindVertexArray(vao);
   glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
   glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
   glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL); glEnableVertexAttribArray(0);
   glPixelStorei(GL_PACK_ALIGNMENT,1); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
   glDisable(GL_DITHER); glDisable(GL_BLEND); glDisable(GL_FRAMEBUFFER_SRGB);
   glClampColor(GL_CLAMP_READ_COLOR,GL_FALSE);
   if (!healthy("setup",NULL)) goto done;
   for (unsigned i=0; i<sizeof(formats)/sizeof(formats[0]); ++i) for (int timed=0; timed<2; ++timed) {
      if (!run_case(&formats[i],timed ? 1024:513,timed ? 768:259,
                    programs[0],tint,programs[1],extent,timed)) goto done;
      ++cases;
   }
   passed=1;
done:
   if (current) {
      glUseProgram(0); glDeleteProgram(programs[0]); glDeleteProgram(programs[1]);
      glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
      cleanup &= healthy("cleanup",NULL);
      cleanup &= eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
   }
   if (context!=EGL_NO_CONTEXT) cleanup &= eglDestroyContext(display,context);
   if (surface!=EGL_NO_SURFACE) cleanup &= eglDestroySurface(display,surface);
   if (initialized) cleanup &= eglTerminate(display);
   EGLint error=eglGetError(); cleanup &= error==EGL_SUCCESS;
   printf(TAG " batch=%u/6 egl_error=%x\n",cases,error);
   printf(TAG " cleanup=%u result=%d\n",cleanup,passed && cleanup ? 0:1);
   return passed && cleanup ? 0:1;
}
