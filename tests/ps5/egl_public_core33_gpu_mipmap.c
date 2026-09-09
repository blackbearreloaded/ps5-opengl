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
#define TAG "[gpu-mipmap]"
#ifdef PS5_GPU_MIPMAP_HOST_REFERENCE
#define SURFACE_TYPE EGL_PBUFFER_BIT
static int ps5_egl_current_draw_status(unsigned *calls) { *calls=0; return 0; }
#else
#define SURFACE_TYPE EGL_WINDOW_BIT
int ps5_egl_current_draw_status(unsigned *calls);
#endif

struct test { const char *name; int w,h,layers; GLenum format,upload; unsigned channels; int npot,timed; };
static int dim(int size, int level) { int n=size>>level; return n ? n:1; }
static uint64_t now_ns(void)
{
   struct timespec t;
   return clock_gettime(CLOCK_MONOTONIC,&t) ? 0 : (uint64_t)t.tv_sec*UINT64_C(1000000000)+t.tv_nsec;
}
static int healthy(const char *stage, unsigned *calls)
{
   unsigned ignored=0; if (!calls) calls=&ignored;
   GLenum error=glGetError(); int status=ps5_egl_current_draw_status(calls);
   if (error || status) printf(TAG " stage=%s error=%x driver=%d draws=%u\n",stage,error,status,*calls);
   return !error && !status;
}
static int observed(const char *name, const char *op, unsigned iteration, unsigned before)
{
   unsigned after=0; if (!healthy(op,&after)) return 0;
   printf(TAG " operation case=%s op=%s iteration=%u driver=0 draws=%u->%u delta=%u observation=%s result=0\n",
          name,op,iteration,before,after,after-before,
#ifdef PS5_GPU_MIPMAP_HOST_REFERENCE
          "host-reference"
#else
          after!=before ? "positive-driver-delta":"no-driver-delta"
#endif
   );
   return 1;
}

/* The existing public GLSL330/VBO convention, with explicit LOD 1 sampling. */
static GLuint make_program(int array)
{
   const char *sources[]={
      "#version 330 core\nlayout(location=0) in vec2 p; void main(){gl_Position=vec4(p,0,1);}\n",
      array ?
      "#version 330 core\nuniform sampler2DArray image; uniform vec2 extent; uniform vec4 tint; out vec4 color;\n"
      "void main(){color=textureLod(image,vec3((gl_FragCoord.xy-vec2(5,7))/extent,1),1.0)*tint;}\n" :
      "#version 330 core\nuniform sampler2D image; uniform vec2 extent; uniform vec4 tint; out vec4 color;\n"
      "void main(){color=textureLod(image,(gl_FragCoord.xy-vec2(5,7))/extent,1.0)*tint;}\n"
   };
   GLuint program=glCreateProgram(); if (!program) return 0;
   for (unsigned i=0; i<2; ++i) {
      GLuint shader=glCreateShader(i ? GL_FRAGMENT_SHADER:GL_VERTEX_SHADER); GLint ok=0;
      if (shader) {
         glShaderSource(shader,1,&sources[i],NULL); glCompileShader(shader);
         glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
         if (ok) glAttachShader(program,shader);
         else { char log[512]={0}; glGetShaderInfoLog(shader,sizeof(log),NULL,log); printf(TAG " shader=%u log=%s\n",i,log); }
         glDeleteShader(shader);
      }
      if (!ok) { glDeleteProgram(program); return 0; }
   }
   GLint ok=0; glLinkProgram(program); glGetProgramiv(program,GL_LINK_STATUS,&ok);
   if (!ok) {
      char log[512]={0}; glGetProgramInfoLog(program,sizeof(log),NULL,log); printf(TAG " link log=%s\n",log);
      glDeleteProgram(program); return 0;
   }
   return program;
}

static float poison(const struct test *t, int level, int layer, int x, int y, unsigned c)
{
   return t->format==GL_RGBA16F ? 9.0f+c+level*.5f+(x&1)*.25f :
      (uint8_t)(193+level*7+layer*11+x*3+y*5+c*13);
}
static float reference(const struct test *t, int level, int layer, int x, int y, unsigned c, int generated)
{
   if (c>=t->channels) return c==3 ? 255.0f:0.0f;
   if (!generated && level) {
      if (level==1 && layer==1) layer=2; /* Selected level-0/layer-2 -> level-1/layer-1 blit. */
      else return poison(t,level,layer,x,y,c);
   }
   int n=16+24*(int)c+12*layer;
   /* Closed-form box averages, independent of readback and driver helpers.
    * Each 2x2 base block contributes 0,4,8,12 (mean 6); macro quadrants
    * contribute 0,32,64,96. Every reduction remains integral, even at 2x1.
    * The NPOT control is constant because its reconstruction filter is implementation-dependent. */
   if (!t->npot) {
      n+=level ? 6 : (x&1)*4+(y&1)*8;
      n+=dim(t->w,level)==1 ? 16 : 32*(x>=dim(t->w,level)/2);
      n+=dim(t->h,level)==1 ? 32 : 64*(y>=dim(t->h,level)/2);
   }
   return t->format==GL_RGBA16F ? (n-96)/16.0f : (float)n;
}
static int attach(const struct test *t, GLuint fbo, GLenum binding, GLuint texture, int level, int layer)
{
   glBindFramebuffer(binding,fbo);
   if (t->layers>1) glFramebufferTextureLayer(binding,GL_COLOR_ATTACHMENT0,texture,level,layer);
   else glFramebufferTexture2D(binding,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,level);
   GLenum status=glCheckFramebufferStatus(binding);
   if (status!=GL_FRAMEBUFFER_COMPLETE) printf(TAG " case=%s level=%d layer=%d fbo=%x\n",t->name,level,layer,status);
   return healthy("attach",NULL) && status==GL_FRAMEBUFFER_COMPLETE;
}
static int read_chain(const struct test *t, GLuint texture, GLuint fbo, int levels, int generated, void *pixels)
{
   uint64_t checked=0;
   for (int level=0; level<levels; ++level) for (int layer=0; layer<t->layers; ++layer) {
      int w=dim(t->w,level), h=dim(t->h,level);
      if (!attach(t,fbo,GL_READ_FRAMEBUFFER,texture,level,layer)) return 0;
      glReadPixels(0,0,w,h,GL_RGBA,t->format==GL_RGBA16F ? GL_FLOAT:GL_UNSIGNED_BYTE,pixels);
      if (!healthy("read-chain",NULL)) return 0;
      for (int y=0; y<h; ++y) for (int x=0; x<w; ++x) for (unsigned c=0; c<4; ++c) {
         size_t i=((size_t)y*w+x)*4+c;
         float actual=t->format==GL_RGBA16F ? ((float *)pixels)[i]:((uint8_t *)pixels)[i];
         float expected=reference(t,level,layer,x,y,c,generated);
         if (actual!=expected) {
            printf(TAG " mismatch case=%s stage=%s level=%d layer=%d xy=%d/%d c=%u got=%.9g want=%.9g\n",
                   t->name,generated ? "chain":"selected-preservation",level,layer,x,y,c,actual,expected); return 0;
         }
      }
      checked+=(uint64_t)w*h;
   }
   printf(TAG " oracle case=%s stage=%s levels=%d layers=%d pixels=%llu result=0\n",
          t->name,generated ? "chain":"selected-preservation",levels,t->layers,(unsigned long long)checked);
   return 1;
}
static int sample_oracle(const struct test *t, void *pixels, int all_channels)
{
   int w=dim(t->w,1), h=dim(t->h,1), sw=w+16, sh=h+16, floating=t->format==GL_RGBA16F;
   glReadPixels(0,0,sw,sh,GL_RGBA,floating ? GL_FLOAT:GL_UNSIGNED_BYTE,pixels);
   if (!healthy("sample-read",NULL)) return 0;
   for (int y=0; y<sh; ++y) for (int x=0; x<sw; ++x) for (unsigned c=0; c<4; ++c) {
      int written=x>=9 && x<w+1 && y>=11 && y<h+3 && (all_channels || c!=1);
      float expected=floating ? -9.0f:165.0f;
      if (written) {
         expected=reference(t,1,t->layers>1 ? 1:0,x-5,y-7,c,1);
         expected=floating ? expected*.5f : (float)(((unsigned)expected+1)/2);
      }
      size_t i=((size_t)y*sw+x)*4+c;
      float actual=floating ? ((float *)pixels)[i]:((uint8_t *)pixels)[i];
      if (actual!=expected) {
         printf(TAG " mismatch case=%s stage=sample-%s xy=%d/%d c=%u got=%.9g want=%.9g border=%d\n",
                t->name,all_channels ? "all":"state",x,y,c,actual,expected,!written); return 0;
      }
   }
   printf(TAG " oracle case=%s stage=sample-%s level=1 layer=%d pixels=%u result=0\n",
          t->name,all_channels ? "all":"state",t->layers>1 ? 1:0,(unsigned)sw*sh);
   return 1;
}

static int run(const struct test *t, GLuint program, unsigned *rows)
{
   GLuint textures[2]={0}, fbos[3]={0};
   GLenum target=t->layers>1 ? GL_TEXTURE_2D_ARRAY:GL_TEXTURE_2D;
   int levels=1, floating=t->format==GL_RGBA16F, passed=0;
   while (dim(t->w,levels-1)>1 || dim(t->h,levels-1)>1) ++levels;
   int w=dim(t->w,1), h=dim(t->h,1), sw=w+16, sh=h+16;
   void *pixels=malloc((size_t)t->w*t->h*4*(floating ? sizeof(float):1));
   unsigned before=0, initial=0, total=0; uint64_t elapsed=0;
   if (!pixels) { printf(TAG " case=%s allocation failed result=1\n",t->name); return 0; }
   if (!healthy("begin",&initial)) goto cleanup;
   printf(TAG " begin case=%s size=%dx%d levels=%d layers=%d first_dst_pixels=%u pattern=%s\n",
          t->name,t->w,t->h,levels,t->layers,(unsigned)w*h,t->npot ? "constant-NPOT":"quadrant-and-2x2");
   glGenTextures(2,textures); glGenFramebuffers(3,fbos);
   glActiveTexture(GL_TEXTURE3); glBindTexture(target,textures[0]);
   glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,0); glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,levels-1);
   glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST); glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
   for (int level=0; level<levels; ++level) {
      int lw=dim(t->w,level), lh=dim(t->h,level);
      if (t->layers>1) glTexImage3D(target,level,t->format,lw,lh,t->layers,0,t->upload,GL_UNSIGNED_BYTE,NULL);
      else glTexImage2D(target,level,t->format,lw,lh,0,t->upload,floating ? GL_FLOAT:GL_UNSIGNED_BYTE,NULL);
      for (int layer=0; layer<t->layers; ++layer) {
         for (int y=0; y<lh; ++y) for (int x=0; x<lw; ++x) for (unsigned c=0; c<t->channels; ++c) {
            size_t i=((size_t)y*lw+x)*t->channels+c;
            float v=level ? poison(t,level,layer,x,y,c):reference(t,0,layer,x,y,c,1);
            if (floating) ((float *)pixels)[i]=v; else ((uint8_t *)pixels)[i]=(uint8_t)v;
         }
         if (t->layers>1) glTexSubImage3D(target,level,0,0,layer,lw,lh,1,t->upload,GL_UNSIGNED_BYTE,pixels);
         else glTexSubImage2D(target,level,0,0,lw,lh,t->upload,floating ? GL_FLOAT:GL_UNSIGNED_BYTE,pixels);
      }
   }
   if (t->layers>1) {
      glDisable(GL_SCISSOR_TEST);
      if (!attach(t,fbos[0],GL_READ_FRAMEBUFFER,textures[0],0,2) ||
          !attach(t,fbos[1],GL_DRAW_FRAMEBUFFER,textures[0],1,1) || !healthy("blit-before",&before)) goto cleanup;
      glBlitFramebuffer(0,0,t->w,t->h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_LINEAR); glFinish();
      if (!observed("array-RGBA8-layer-blit","selected-layer-blit",0,before) ||
          !read_chain(t,textures[0],fbos[0],levels,0,pixels)) goto cleanup;
      printf(TAG " case=array-RGBA8-layer-blit src_level=0 src_layer=2 dst_level=1 dst_layer=1 dst=512x512 preservation=all-levels-all-layers result=0\n");
      ++*rows;
   }
   /* Independent RGBA output retains a border and the masked green channel. */
   for (size_t i=0; i<(size_t)sw*sh*4; ++i) {
      if (floating) ((float *)pixels)[i]=-9.0f; else ((uint8_t *)pixels)[i]=165;
   }
   glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,textures[1]);
   glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
   glTexImage2D(GL_TEXTURE_2D,0,floating ? GL_RGBA16F:GL_RGBA8,sw,sh,0,GL_RGBA,floating ? GL_FLOAT:GL_UNSIGNED_BYTE,pixels);
   glBindFramebuffer(GL_FRAMEBUFFER,fbos[2]);
   glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) goto cleanup;
   glActiveTexture(GL_TEXTURE3); glBindTexture(target,textures[0]); glUseProgram(program);
   GLint image=glGetUniformLocation(program,"image"), extent=glGetUniformLocation(program,"extent"), tint=glGetUniformLocation(program,"tint");
   if (image<0 || extent<0 || tint<0) goto cleanup;
   glUniform1i(image,3); glUniform2f(extent,(float)w,(float)h); glUniform4f(tint,.5f,.5f,.5f,.5f);
   glViewport(5,7,w,h); glEnable(GL_SCISSOR_TEST); glScissor(9,11,w-8,h-8);
   glColorMask(GL_TRUE,GL_FALSE,GL_TRUE,GL_TRUE); glDrawArrays(GL_TRIANGLES,0,3);
   glFinish(); /* Retire the setup draw before taking the operation's counter baseline. */
   if (!healthy("generate-before",&before)) goto cleanup;
   glGenerateMipmap(target);
   if (t->timed) glFinish();
   if (!observed(t->name,t->timed ? "warmup":"generate",0,before)) goto cleanup;
   if (t->timed) {
      uint64_t batch_start=now_ns(); if (!batch_start) goto cleanup;
      for (unsigned i=0; i<4; ++i) {
         if (!healthy("timing-before",&before)) goto cleanup;
         uint64_t start=now_ns(); if (!start) goto cleanup;
         glGenerateMipmap(target); glFinish(); /* Only completed API calls are measured. */
         uint64_t end=now_ns();
         if (end<=start || end<batch_start || end-batch_start>UINT64_C(5000000000)) {
            printf(TAG " case=%s timing clock/deadline failure result=1\n",t->name); goto cleanup;
         }
         elapsed+=end-start;
         if (!observed(t->name,"timed-generate",i,before)) goto cleanup;
      }
   }
   /* No program/uniform/texture/VAO/viewport/scissor/mask/FBO rebind after generation.
    * Ordinary cases consume mip 1 before any subsequent finish or source readback. */
   glDrawArrays(GL_TRIANGLES,0,3);
   if (!sample_oracle(t,pixels,0)) goto cleanup;
   glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDrawArrays(GL_TRIANGLES,0,3);
   if (!sample_oracle(t,pixels,1) || !read_chain(t,textures[0],fbos[0],levels,1,pixels)) goto cleanup;
   passed=1;
cleanup:
   glDisable(GL_SCISSOR_TEST); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
   glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(3,fbos); glDeleteTextures(2,textures); free(pixels);
   int status=ps5_egl_current_draw_status(&total);
   if (!healthy("case-cleanup",NULL) || status) passed=0;
   printf(TAG " case=%s size=%dx%d levels=%d layers=%d driver=%d draws=%u->%u delta=%u repeats=%u completed_api_wall_ns=%llu ms_per_generation=%.3f result=%d\n",
          t->name,t->w,t->h,levels,t->layers,status,initial,total,total-initial,t->timed ? 4:0,
          (unsigned long long)elapsed,t->timed ? (double)elapsed/4e6:0,passed ? 0:1);
   if (passed) ++*rows;
   return passed;
}

int main(void)
{
   const EGLint cfg[]={EGL_SURFACE_TYPE,SURFACE_TYPE,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
      EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
   const EGLint ctx[]={EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
   const struct test tests[]={
      {"chain-RGBA8",1024,1024,1,GL_RGBA8,GL_RGBA,4,0,0},
      {"chain-R8",1024,1024,1,GL_R8,GL_RED,1,0,0},
      {"chain-RG8",1024,1024,1,GL_RG8,GL_RG,2,0,0},
      {"chain-RGBA16F",1024,1024,1,GL_RGBA16F,GL_RGBA,4,0,0},
      {"npot-RGBA8",127,65,1,GL_RGBA8,GL_RGBA,4,1,0},
      {"array-RGBA8-generate",1024,1024,4,GL_RGBA8,GL_RGBA,4,0,0},
      {"timing-RGBA8",2048,1024,1,GL_RGBA8,GL_RGBA,4,0,1}
   };
   EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLSurface surface=EGL_NO_SURFACE; EGLContext context=EGL_NO_CONTEXT; EGLConfig config; EGLint count=0;
   GLuint programs[2]={0}, vao=0,vbo=0; unsigned rows=0;
   int initialized=0,current=0,passed=0; EGLBoolean cleanup=EGL_TRUE;
   if (display==EGL_NO_DISPLAY || !eglInitialize(display,NULL,NULL)) goto done;
   initialized=1;
   if (!eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(display,cfg,&config,1,&count) || count!=1) goto done;
#ifdef PS5_GPU_MIPMAP_HOST_REFERENCE
   const EGLint pb[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE}; surface=eglCreatePbufferSurface(display,config,pb);
#else
   surface=eglCreateWindowSurface(display,config,(EGLNativeWindowType)0,NULL);
#endif
   context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);
   if (surface==EGL_NO_SURFACE || context==EGL_NO_CONTEXT || !eglMakeCurrent(display,surface,surface,context)) goto done;
   current=1;
   printf(TAG " renderer=%s counters=%s\n",(const char *)glGetString(GL_RENDERER),
#ifdef PS5_GPU_MIPMAP_HOST_REFERENCE
          "host-stub"
#else
          "native-driver"
#endif
   );
   for (int i=0; i<2; ++i) { programs[i]=make_program(i); if (!programs[i]) goto done; }
   const GLfloat vertices[]={-1,-1,3,-1,-1,3};
   glGenVertexArrays(1,&vao); glBindVertexArray(vao); glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
   glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
   glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL); glEnableVertexAttribArray(0);
   glPixelStorei(GL_PACK_ALIGNMENT,1); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
   glDisable(GL_DITHER); glDisable(GL_BLEND); glDisable(GL_FRAMEBUFFER_SRGB); glClampColor(GL_CLAMP_READ_COLOR,GL_FALSE);
   if (!healthy("setup",NULL)) goto done;
   for (unsigned i=0; i<sizeof(tests)/sizeof(tests[0]); ++i)
      if (!run(&tests[i],programs[tests[i].layers>1],&rows)) goto done;
   passed=rows==8;
done:
   if (current) {
      glUseProgram(0); glDeleteProgram(programs[0]); glDeleteProgram(programs[1]);
      glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao); cleanup &= healthy("cleanup",NULL);
      cleanup &= eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
   }
   if (context!=EGL_NO_CONTEXT) cleanup &= eglDestroyContext(display,context);
   if (surface!=EGL_NO_SURFACE) cleanup &= eglDestroySurface(display,surface);
   if (initialized) cleanup &= eglTerminate(display);
   EGLint error=eglGetError(); cleanup &= error==EGL_SUCCESS;
   printf(TAG " batch=%u/8 egl_error=%x\n",rows,error);
   printf(TAG " cleanup=%u result=%d\n",cleanup,passed && cleanup ? 0:1);
   return passed && cleanup ? 0:1;
}
