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

#define TAG "[gpu-blit-extended]"
#ifdef PS5_GPU_BLIT_EXTENDED_HOST_REFERENCE
#define SURFACE_TYPE EGL_PBUFFER_BIT
static int ps5_egl_current_draw_status(unsigned *calls) { *calls=0; return 0; }
#else
#define SURFACE_TYPE EGL_WINDOW_BIT
int ps5_egl_current_draw_status(unsigned *calls);
#endif

struct blit_case {
   const char *name;
   int w, h, src[4], dst[4], clip[4]; /* clip is x,y,width,height; width=0 disables it. */
   GLenum filter;
   unsigned samples, repeats;
};

/* Every sample differs in every channel; the exact average is 120,128,112,144.
 * Selecting any one sample, or ignoring sample masks while seeding, must fail. */
static const uint8_t sample_colors[4][4] = {
   {0,32,128,240}, {64,160,16,48}, {192,96,240,112}, {224,224,64,176}
};

static uint64_t now_ns(void)
{
   struct timespec t;
   return clock_gettime(CLOCK_MONOTONIC,&t) ? 0 :
      (uint64_t)t.tv_sec * UINT64_C(1000000000) + t.tv_nsec;
}

static int healthy(void)
{
   unsigned calls=0;
   GLenum error=glGetError();
   int status=ps5_egl_current_draw_status(&calls);
   if (error || status) printf(TAG " error=%x driver=%d draws=%u\n",error,status,calls);
   return !error && !status;
}

static uint8_t pattern(int x, int y, unsigned c)
{
   return (uint8_t)(x*(3+c) + y*(7+c) + c*53);
}

/* Same application shader and vertex-input convention as the basic GPU blit test. */
static GLuint make_program(void)
{
   const char *sources[]={
      "#version 330 core\nlayout(location=0) in vec2 p;\n"
      "void main(){ gl_Position=vec4(p,0,1); }\n",
      "#version 330 core\nuniform sampler2D image; uniform vec4 tint;\n"
      "out vec4 color; void main(){ color=texture(image,vec2(0.5))*tint; }\n"
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
            printf(TAG " shader=%u log=%.*s\n",i,length,log);
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
      printf(TAG " link log=%.*s\n",length,log);
      glDeleteProgram(program); return 0;
   }
   return program;
}

static int between(int value, int a, int b)
{
   return value >= (a<b ? a:b) && value < (a<b ? b:a);
}

static int lower(int a, int b) { return a<b ? a:b; }
static int upper(int a, int b) { return a>b ? a:b; }

static unsigned effective_pixels(const struct blit_case *t)
{
   int x0=lower(t->dst[0],t->dst[2]), x1=upper(t->dst[0],t->dst[2]);
   int y0=lower(t->dst[1],t->dst[3]), y1=upper(t->dst[1],t->dst[3]);
   if (t->clip[2]) {
      x0=upper(x0,t->clip[0]); y0=upper(y0,t->clip[1]);
      x1=lower(x1,t->clip[0]+t->clip[2]); y1=lower(y1,t->clip[1]+t->clip[3]);
   }
   return x1>x0 && y1>y0 ? (unsigned)(x1-x0)*(y1-y0) : 0;
}

static unsigned texel(const struct blit_case *t, int x, int y, unsigned c)
{
   x=x<0 ? 0 : x>=t->w ? t->w-1 : x;
   y=y<0 ? 0 : y>=t->h ? t->h-1 : y;
   return pattern(x,y,c);
}

static unsigned reference(const struct blit_case *t, int x, int y, unsigned c)
{
   if (t->samples) {
      unsigned sum=0;
      for (unsigned s=0; s<4; ++s) sum+=sample_colors[s][c];
      return (sum+2)/4;
   }
   /* Invert the original rectangles at destination pixel centers. Scissoring
    * only discards pixels: it must never rescale or shift this mapping. */
   double sx=t->src[0] + (x+0.5-t->dst[0])*(t->src[2]-t->src[0])/(t->dst[2]-t->dst[0]);
   double sy=t->src[1] + (y+0.5-t->dst[1])*(t->src[3]-t->src[1])/(t->dst[3]-t->dst[1]);
   if (t->filter==GL_NEAREST) return texel(t,(int)sx,(int)sy,c);
   sx-=0.5; sy-=0.5;
   int ix=(int)sx-(sx<(int)sx), iy=(int)sy-(sy<(int)sy); /* floor, including image edges */
   double a=sx-ix, b=sy-iy;
   double lo=(1-a)*texel(t,ix,iy,c) + a*texel(t,ix+1,iy,c);
   double hi=(1-a)*texel(t,ix,iy+1,c) + a*texel(t,ix+1,iy+1,c);
   return (unsigned)((1-b)*lo+b*hi+0.5);
}

static int oracle(const struct blit_case *t, uint8_t *pixels, int drawn)
{
   int vx=t->clip[2] ? t->clip[0]-9 : lower(t->dst[0],t->dst[2])+3;
   int vy=t->clip[2] ? t->clip[1]-9 : lower(t->dst[1],t->dst[3])+5;
   glReadPixels(0,0,t->w,t->h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
   if (!healthy()) return 0;
   for (int y=0; y<t->h; ++y) for (int x=0; x<t->w; ++x) {
      int visible=!t->clip[2] || (between(x,t->clip[0],t->clip[0]+t->clip[2]) &&
                                  between(y,t->clip[1],t->clip[1]+t->clip[3]));
      int copied=visible && between(x,t->dst[0],t->dst[2]) && between(y,t->dst[1],t->dst[3]);
      for (unsigned c=0; c<4; ++c) {
         unsigned expected=copied ? reference(t,x,y,c) : 0xa5;
         unsigned tolerance=copied && (t->samples || t->filter==GL_LINEAR);
         /* The post-blit draw reuses shader, uniforms, texture unit 3, VAO,
          * viewport, scissor and red-disabled color mask without rebinding. */
         if (drawn && visible && between(x,vx,vx+23) && between(y,vy,vy+19) && c) {
            expected=(32+64*c)/2; tolerance=0;
         }
         unsigned actual=pixels[((size_t)y*t->w+x)*4+c];
         if (abs((int)actual-(int)expected)>(int)tolerance) {
            printf(TAG " mismatch case=%s stage=%s xy=%d/%d c=%u got=%u want=%u tolerance=%u border=%d\n",
                   t->name,drawn ? "state":"blit",x,y,c,actual,expected,tolerance,!copied);
            return 0;
         }
      }
   }
   return 1;
}

static void blit(const struct blit_case *t)
{
   glBlitFramebuffer(t->src[0],t->src[1],t->src[2],t->src[3],
                     t->dst[0],t->dst[1],t->dst[2],t->dst[3],GL_COLOR_BUFFER_BIT,t->filter);
}

static int run_case(const struct blit_case *t, GLuint sample_texture, GLint tint)
{
   GLuint textures[2]={0}, fbos[2]={0};
   uint8_t *pixels=malloc((size_t)t->w*t->h*4);
   unsigned initial=0, before=0, after=0, total=0, copies=0;
   unsigned checks=0;
   uint64_t start=0, elapsed=0;
   int passed=0, state=0;
   if (!pixels) { printf(TAG " case=%s allocation failed result=1\n",t->name); return 0; }
   if (ps5_egl_current_draw_status(&initial)) goto cleanup;
   glGenTextures(2,textures); glGenFramebuffers(2,fbos);
   glActiveTexture(GL_TEXTURE0);
   for (unsigned i=0; i<2; ++i) {
      GLenum target=!i && t->samples ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
      glBindTexture(target,textures[i]);
      if (target==GL_TEXTURE_2D_MULTISAMPLE) {
         GLint samples=0;
         glTexImage2DMultisample(target,4,GL_RGBA8,t->w,t->h,GL_TRUE);
         glGetTexLevelParameteriv(target,0,GL_TEXTURE_SAMPLES,&samples);
         if (samples!=4) { printf(TAG " case=%s samples=%d expected=4\n",t->name,samples); goto cleanup; }
      } else {
         for (int y=0; y<t->h; ++y) for (int x=0; x<t->w; ++x) for (unsigned c=0; c<4; ++c)
            pixels[((size_t)y*t->w+x)*4+c]=i ? 0xa5 : pattern(x,y,c);
         glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,0);
         glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
         glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
         glTexImage2D(target,0,GL_RGBA8,t->w,t->h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
      }
      glBindFramebuffer(GL_FRAMEBUFFER,fbos[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,target,textures[i],0);
      GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status!=GL_FRAMEBUFFER_COMPLETE) {
         printf(TAG " case=%s fbo=%u status=%x\n",t->name,i,status); goto cleanup;
      }
   }
   glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D,sample_texture);
   if (t->samples) {
      glBindFramebuffer(GL_FRAMEBUFFER,fbos[0]);
      glViewport(0,0,t->w,t->h);
      glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);
      glEnable(GL_SAMPLE_MASK);
      for (unsigned s=0; s<4; ++s) {
         glSampleMaski(0,1u << s);
         glUniform4f(tint,sample_colors[s][0]/32.0f,sample_colors[s][1]/96.0f,
                         sample_colors[s][2]/160.0f,sample_colors[s][3]/224.0f);
         glDrawArrays(GL_TRIANGLES,0,3);
      }
      glDisable(GL_SAMPLE_MASK); glSampleMaski(0,~0u);
   }
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbos[1]);
   glUniform4f(tint,0.5f,0.5f,0.5f,0.5f);
   int vx=t->clip[2] ? t->clip[0]-9 : lower(t->dst[0],t->dst[2])+3;
   int vy=t->clip[2] ? t->clip[1]-9 : lower(t->dst[1],t->dst[3])+5;
   glViewport(vx,vy,23,19); glColorMask(GL_FALSE,GL_TRUE,GL_TRUE,GL_TRUE);
   if (t->clip[2]) {
      glEnable(GL_SCISSOR_TEST); glScissor(t->clip[0],t->clip[1],t->clip[2],t->clip[3]);
   }
   /* This draw lies within the subsequent blit's clipped destination in every
    * case, so the first oracle still expects an entirely untouched border. */
   glDrawArrays(GL_TRIANGLES,0,3);
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[0]);
   blit(t); glFinish();
   if (!healthy()) goto cleanup;
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[1]);
   if (!oracle(t,pixels,0)) goto cleanup;
   ++checks;
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[0]);
   blit(t); glFinish(); /* Warm caches; allocation, uploads and readback are not timed. */
   if (!healthy() || ps5_egl_current_draw_status(&before)) goto cleanup;
   start=now_ns(); if (!start) goto cleanup;
   for (; copies<t->repeats; ++copies) { blit(t); glFinish(); }
   uint64_t end=now_ns();
   if (end<=start) goto cleanup;
   elapsed=end-start;
   if (!healthy() || ps5_egl_current_draw_status(&after)) goto cleanup;
   /* No application-state rebinds between the final blit and this draw. */
   glDrawArrays(GL_TRIANGLES,0,3); glFinish();
   glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[1]);
   if (!oracle(t,pixels,1)) goto cleanup;
   ++checks; state=1; passed=1;
cleanup:
   glDisable(GL_SCISSOR_TEST); glDisable(GL_SAMPLE_MASK); glSampleMaski(0,~0u);
   glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
   glBindFramebuffer(GL_FRAMEBUFFER,0);
   glDeleteFramebuffers(2,fbos); glDeleteTextures(2,textures); free(pixels);
   int status=ps5_egl_current_draw_status(&total);
   if (!healthy() || status) passed=0;
   printf(TAG " case=%s size=%dx%d src=%d,%d,%d,%d dst=%d,%d,%d,%d scissor=%d,%d,%d,%d"
          " filter=%s samples=%u effective_dst_pixels=%u threshold=%s repeats=%u elapsed_ns=%llu ms_per_blit=%.3f"
          " driver=%d blit_gpu_draws=%u gpu_draws_total=%u oracle_pixels=%llu state=%d result=%d\n",
          t->name,t->w,t->h,t->src[0],t->src[1],t->src[2],t->src[3],
          t->dst[0],t->dst[1],t->dst[2],t->dst[3],t->clip[0],t->clip[1],t->clip[2],t->clip[3],
          t->filter==GL_LINEAR ? "linear":"nearest",t->samples,effective_pixels(t),
          effective_pixels(t)>=512u*512u ? "met":"below",copies,(unsigned long long)elapsed,
          copies ? (double)elapsed/copies/1e6 : 0,status,after-before,total-initial,
          (unsigned long long)checks*t->w*t->h,state,passed ? 0:1);
   return passed;
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
   unsigned successes=0;
   int current=0, passed=0; EGLBoolean cleanup=EGL_TRUE;
   static const struct blit_case cases[]={
      {"tiny-nearest",96,96,{8,8,40,40},{8,8,72,72},{0},GL_NEAREST,0,1},
      {"up-nearest",544,544,{16,16,272,272},{16,16,528,528},{0},GL_NEAREST,0,1},
      {"up-linear",544,544,{16,16,272,272},{16,16,528,528},{0},GL_LINEAR,0,1},
      {"down-nearest",544,544,{8,8,536,536},{16,16,528,528},{0},GL_NEAREST,0,1},
      {"down-linear",544,544,{8,8,536,536},{16,16,528,528},{0},GL_LINEAR,0,1},
      {"src-x-nearest",544,544,{272,16,16,272},{16,16,528,528},{0},GL_NEAREST,0,1},
      {"src-y-nearest",544,544,{16,272,272,16},{16,16,528,528},{0},GL_NEAREST,0,1},
      {"dst-x-nearest",544,544,{16,16,272,272},{528,16,16,528},{0},GL_NEAREST,0,1},
      {"dst-y-nearest",544,544,{16,16,272,272},{16,528,528,16},{0},GL_NEAREST,0,1},
      {"src-x-linear",544,544,{272,16,16,272},{16,16,528,528},{0},GL_LINEAR,0,1},
      {"src-y-linear",544,544,{16,272,272,16},{16,16,528,528},{0},GL_LINEAR,0,1},
      {"dst-x-linear",544,544,{16,16,272,272},{528,16,16,528},{0},GL_LINEAR,0,1},
      {"dst-y-linear",544,544,{16,16,272,272},{16,528,528,16},{0},GL_LINEAR,0,1},
      {"scissor-nearest",544,544,{8,8,272,272},{8,8,536,536},{16,16,512,512},GL_NEAREST,0,1},
      {"scissor-linear",544,544,{8,8,272,272},{8,8,536,536},{16,16,512,512},GL_LINEAR,0,1},
      {"combined-nearest",544,544,{272,8,8,536},{8,536,536,8},{16,16,512,512},GL_NEAREST,0,1},
      {"combined-linear",544,544,{8,536,272,8},{536,8,8,536},{16,16,512,512},GL_LINEAR,0,1},
      {"edge-linear",544,544,{0,0,256,256},{16,16,528,528},{0},GL_LINEAR,0,1},
      {"msaa4-average",544,544,{16,16,528,528},{16,16,528,528},{0},GL_NEAREST,4,1},
      {"msaa4-scissor",544,544,{8,8,536,536},{8,8,536,536},{16,16,512,512},GL_NEAREST,4,1},
      {"timing-up-nearest",1920,1080,{16,16,960,540},{16,16,1904,1064},{0},GL_NEAREST,0,8},
      {"timing-up-linear",1920,1080,{16,16,960,540},{16,16,1904,1064},{0},GL_LINEAR,0,8}
   };
   if (display==EGL_NO_DISPLAY || !eglInitialize(display,NULL,NULL) ||
       !eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(display,config_attrs,&config,1,&count) || count!=1)
      goto done;
#ifdef PS5_GPU_BLIT_EXTENDED_HOST_REFERENCE
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
#ifdef PS5_GPU_BLIT_EXTENDED_HOST_REFERENCE
          "host-stub"
#else
          "driver"
#endif
   );
   glDisable(GL_DITHER); glDisable(GL_BLEND); glDisable(GL_FRAMEBUFFER_SRGB);
   glEnable(GL_MULTISAMPLE);
   program=make_program(); if (!program) goto done;
   glUseProgram(program);
   GLint image=glGetUniformLocation(program,"image"), tint=glGetUniformLocation(program,"tint");
   if (image<0 || tint<0) goto done;
   glUniform1i(image,3);
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
   for (unsigned i=0; i<sizeof(cases)/sizeof(cases[0]); ++i) {
      if (!run_case(&cases[i],texture,tint)) goto done;
      ++successes;
   }
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
   EGLint error=eglGetError(); cleanup &= error==EGL_SUCCESS;
   printf(TAG " batch=%u/%u egl_error=%x\n",successes,(unsigned)(sizeof(cases)/sizeof(cases[0])),error);
   printf(TAG " cleanup=%u result=%d\n",cleanup,passed && cleanup ? 0:1);
   return passed && cleanup ? 0:1;
}
