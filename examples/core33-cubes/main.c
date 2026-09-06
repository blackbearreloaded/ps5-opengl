/* Low-poly 3D frame benchmark using only public EGL/OpenGL interfaces. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

enum { WIDTH = 1920, HEIGHT = 1080, WARMUP = 2, FRAMES = 8 };
static const unsigned workloads[] = {1, 8, 32};
static const uint8_t texels[2][16] = {
   {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255},
   {255,255,0,255, 0,255,255,255, 255,0,255,255, 255,255,255,255}
};
struct vertex { float p[3], n[3], uv[2]; };
static struct vertex vertices[36];
static GLuint textures[2];
static GLint placement, rotation, instanced, object_index;
#ifdef PS5_NATIVE_CUBES_TEST
extern int ps5_egl_current_draw_status(unsigned *);
#endif

static int check(int ok, const char *stage)
{
   if (!ok) printf("[ps5-cubes] FAIL stage=%s gl=%x egl=%x\n", stage, glGetError(), eglGetError());
   return ok;
}

static int64_t now_ns(void)
{
   struct timespec t;
   return clock_gettime(CLOCK_MONOTONIC, &t) == 0 ? (int64_t)t.tv_sec * 1000000000 + t.tv_nsec : 0;
}

static GLuint compile(GLenum type, const char *text)
{
   GLuint s = glCreateShader(type);
   GLint ok = 0;
   glShaderSource(s, 1, &text, NULL); glCompileShader(s); glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[1024]; GLsizei len = 0;
      glGetShaderInfoLog(s, sizeof(log), &len, log);
      printf("[ps5-cubes] shader: %.*s\n", len, log); glDeleteShader(s); return 0;
   }
   return s;
}

static void geometry(void)
{
   const float corners[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
      {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
   /* Front first, back last: disabling depth must fail the foreground oracle. */
   const unsigned faces[6][4] = {{4,5,6,7},{5,1,2,6},{0,4,7,3},{7,6,2,3},{0,1,5,4},{1,0,3,2}};
   const float normals[6][3] = {{0,0,1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,-1}};
   const unsigned triangle[6] = {0,1,2,0,2,3};
   const float uv[4][2] = {{0,0},{1,0},{1,1},{0,1}};
   for (unsigned face = 0; face < 6; ++face) for (unsigned v = 0; v < 6; ++v) {
      struct vertex *out = &vertices[face * 6 + v];
      memcpy(out->p, corners[faces[face][triangle[v]]], sizeof(out->p));
      memcpy(out->n, normals[face], sizeof(out->n));
      memcpy(out->uv, uv[triangle[v]], sizeof(out->uv));
   }
}

static void object_position(unsigned count, unsigned i, float out[4])
{
   unsigned cols = count == 1 ? 1 : count == 8 ? 4 : 8;
   unsigned rows = count / cols;
   out[0] = ((float)(i % cols) - (cols - 1) * .5f) * .8f;
   out[1] = ((float)(i / cols) - (rows - 1) * .5f) * .8f;
   out[2] = -6;
   out[3] = count == 1 ? .8f : .25f;
}

static int draw(unsigned count, unsigned mode, float angle, int64_t times[3])
{
   times[0] = now_ns();
   glClearColor(8 / 255.0f, 12 / 255.0f, 20 / 255.0f, 1);
   glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   times[1] = now_ns();
   glUniform4f(rotation, cosf(angle), sinf(angle), cosf(angle * .6f), sinf(angle * .6f));
   glUniform1i(instanced, mode);
   for (unsigned i = 0; i < (mode ? 1u : count); ++i) {
      if (mode) {
         glDrawArraysInstanced(GL_TRIANGLES, 0, 36, count);
      } else {
         float position[4]; object_position(count, i, position);
         glUniform4fv(placement, 1, position);
         glUniform1i(object_index, i);
         glDrawArrays(GL_TRIANGLES, 0, 36);
      }
#ifdef PS5_NATIVE_CUBES_TEST
      if (!check(ps5_egl_current_draw_status(NULL) == 0, "native draw")) return 0;
#endif
   }
   /* One completion boundary per frame; never time unfinished work as FPS. */
   glFinish();
   times[2] = now_ns();
   return check(times[0] > 0 && times[1] > times[0] && times[2] > times[1] &&
                glGetError() == GL_NO_ERROR, "draw");
}

static int pixel(int x, int y, const uint8_t expected[4])
{
   uint8_t got[4] = {0};
   glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, got);
   for (unsigned c = 0; c < 4; ++c) if (abs((int)got[c] - expected[c]) > 1) {
      printf("[ps5-cubes] pixel=%d,%d actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n", x, y,
         got[0],got[1],got[2],got[3],expected[0],expected[1],expected[2],expected[3]);
      return 0;
   }
   return glGetError() == GL_NO_ERROR;
}

static int oracle(unsigned count, unsigned mode)
{
   const uint8_t background[4] = {8,12,20,255};
   int passed = pixel(5, 5, background);
   for (unsigned i = 0; i < count; ++i) {
      float p[4]; object_position(count, i, p);
      for (unsigned v = 0; v < 2; ++v) for (unsigned u = 0; u < 2; ++u) {
         const float x = p[0] + (u ? .5f : -.5f) * p[3];
         const float y = p[1] + (v ? .5f : -.5f) * p[3];
         const float z = -p[2] - p[3];
         int sx = (int)((1 + x * 1.5f * HEIGHT / WIDTH / z) * WIDTH * .5f);
         int sy = (int)((1 + y * 1.5f / z) * HEIGHT * .5f);
         passed &= pixel(sx, sy, &texels[i % 2][4 * (v * 2 + u)]);
      }
   }
   printf("[ps5-cubes] oracle mode=%u objects=%u probes=%u %s\n", mode, count, 1 + count * 4, passed ? "PASS" : "FAIL");
   return passed;
}

int main(void)
{
#ifdef PS5_CUBES_HOST_REFERENCE
   const EGLint surface_type = EGL_PBUFFER_BIT;
#else
   const EGLint surface_type = EGL_WINDOW_BIT;
#endif
   const EGLint ca[] = {EGL_SURFACE_TYPE,surface_type,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,
      EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_NONE};
   const EGLint ctx[] = {EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL; EGLint count = 0;
   GLuint vs = 0, fs = 0, program = 0, vao = 0, vbo = 0, instances = 0;
   int current = 0, passed = 0, clean = 1;
   unsigned completed = 0;
   if (!check(display != EGL_NO_DISPLAY && eglInitialize(display,NULL,NULL) && eglBindAPI(EGL_OPENGL_API) &&
       eglChooseConfig(display,ca,&config,1,&count) && count == 1, "EGL init")) goto cleanup;
#ifdef PS5_CUBES_HOST_REFERENCE
   const EGLint sa[] = {EGL_WIDTH,WIDTH,EGL_HEIGHT,HEIGHT,EGL_NONE};
   surface = eglCreatePbufferSurface(display,config,sa);
#else
   surface = eglCreateWindowSurface(display,config,(EGLNativeWindowType)0,NULL);
#endif
   context = eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);
   if (!check(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
       eglMakeCurrent(display,surface,surface,context), "EGL current")) goto cleanup;
   current = 1;
   EGLint width = 0, height = 0;
   if (!check(eglQuerySurface(display,surface,EGL_WIDTH,&width) &&
       eglQuerySurface(display,surface,EGL_HEIGHT,&height) && width == WIDTH && height == HEIGHT,
       "surface dimensions")) goto cleanup;
   vs = compile(GL_VERTEX_SHADER, "#version 330 core\n"
      "layout(location=0) in vec3 p; layout(location=1) in vec3 normal; layout(location=2) in vec2 uv;"
      "layout(location=3) in vec4 instance_placement; uniform int instanced; uniform int object_index;"
      "uniform vec4 placement; uniform vec4 rotation; out vec2 texcoord; out vec3 n; flat out int material;"
      "vec3 rotate(vec3 q){vec3 a=vec3(q.x,rotation.z*q.y-rotation.w*q.z,rotation.w*q.y+rotation.z*q.z);"
      "return vec3(rotation.x*a.x+rotation.y*a.z,a.y,-rotation.y*a.x+rotation.x*a.z);}"
      "void main(){vec4 place=instanced!=0?instance_placement:placement; vec3 q=rotate(p)*place.w+place.xyz;"
      "gl_Position=vec4(q.x*0.84375,q.y*1.5,-1.002002*q.z-0.2002002,-q.z);"
      "n=rotate(normal);texcoord=uv;material=(instanced!=0?gl_InstanceID:object_index)%2;}");
   fs = compile(GL_FRAGMENT_SHADER, "#version 330 core\n"
      "in vec2 texcoord; in vec3 n; flat in int material; uniform sampler2D albedo0; uniform sampler2D albedo1; out vec4 color;"
      "void main(){vec3 c=material==0?texture(albedo0,texcoord).rgb:texture(albedo1,texcoord).rgb;"
      "color=vec4(c*(0.25+0.75*max(normalize(n).z,0.0)),1);}");
   if (!vs || !fs) goto cleanup;
   program = glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program);
   GLint linked = 0; glGetProgramiv(program,GL_LINK_STATUS,&linked);
   if (!check(linked, "link")) goto cleanup;
   glUseProgram(program);
   placement = glGetUniformLocation(program,"placement"); rotation = glGetUniformLocation(program,"rotation");
   instanced = glGetUniformLocation(program,"instanced"); object_index = glGetUniformLocation(program,"object_index");
   GLint albedo0 = glGetUniformLocation(program,"albedo0"), albedo1 = glGetUniformLocation(program,"albedo1");
   if (!check(placement >= 0 && rotation >= 0 && albedo0 >= 0 && albedo1 >= 0 && instanced >= 0 && object_index >= 0,
       "uniforms")) goto cleanup;
   glUniform1i(albedo0,0); glUniform1i(albedo1,1);
   glGenTextures(2,textures);
   for (unsigned i = 0; i < 2; ++i) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D,textures[i]);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,texels[i]);
   }
   geometry(); glGenVertexArrays(1,&vao); glBindVertexArray(vao);
   glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
   glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
   glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(struct vertex),(void *)offsetof(struct vertex,p));
   glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(struct vertex),(void *)offsetof(struct vertex,n));
   glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(struct vertex),(void *)offsetof(struct vertex,uv));
   glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
   glGenBuffers(1,&instances); glBindBuffer(GL_ARRAY_BUFFER,instances);
   glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,4*sizeof(float),NULL);
   glEnableVertexAttribArray(3); glVertexAttribDivisor(3,1);
   glViewport(0,0,WIDTH,HEIGHT); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
   glDisable(GL_DITHER); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
   printf("[ps5-cubes] start width=%u height=%u warmup=%u frames=%u triangles_per_object=12 modes=2\n", WIDTH,HEIGHT,WARMUP,FRAMES);
   for (unsigned mode = 0; mode < 2; ++mode) for (unsigned w = 0; w < sizeof(workloads) / sizeof(workloads[0]); ++w) {
      unsigned objects = workloads[w];
      float positions[32][4];
      for (unsigned i = 0; i < objects; ++i) object_position(objects,i,positions[i]);
      glBufferData(GL_ARRAY_BUFFER,objects*sizeof(positions[0]),positions,GL_STATIC_DRAW);
      int64_t t[3];
      if (!draw(objects,mode,0,t) || !oracle(objects,mode) || !eglSwapBuffers(display,surface)) goto cleanup;
      for (unsigned frame = 0; frame < WARMUP + FRAMES; ++frame) {
         if (!draw(objects,mode,.2f + frame * .075f,t)) goto cleanup;
         if (!check(eglSwapBuffers(display,surface), "present")) goto cleanup;
         int64_t end = now_ns();
         if (!check(end > t[2], "clock")) goto cleanup;
         if (frame >= WARMUP)
            printf("[ps5-cubes] mode=%u objects=%u frame=%u clear_ns=%lld draw_ns=%lld swap_ns=%lld total_ns=%lld\n",
               mode,objects,frame-WARMUP,(long long)(t[1]-t[0]),(long long)(t[2]-t[1]),
               (long long)(end-t[2]),(long long)(end-t[0]));
      }
      if (!draw(objects,mode,0,t) || !oracle(objects,mode) || !eglSwapBuffers(display,surface)) goto cleanup;
      ++completed;
   }
   passed = 1;
cleanup:
   if (current) {
      glUseProgram(0); glDeleteTextures(2,textures);
      if (instances) glDeleteBuffers(1,&instances);
      if (vbo) glDeleteBuffers(1,&vbo);
      if (vao) glDeleteVertexArrays(1,&vao);
      if (program) glDeleteProgram(program);
      if (vs) glDeleteShader(vs);
      if (fs) glDeleteShader(fs);
      clean &= glGetError() == GL_NO_ERROR;
   }
   if (display != EGL_NO_DISPLAY) {
      clean &= eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
      if (surface != EGL_NO_SURFACE) clean &= eglDestroySurface(display,surface);
      if (context != EGL_NO_CONTEXT) clean &= eglDestroyContext(display,context);
      clean &= eglTerminate(display);
   }
   clean &= eglGetError() == EGL_SUCCESS;
   printf("[ps5-cubes] completed=%u cleanup=%u result=%d\n",completed,clean,passed && clean ? 0 : 1);
   return passed && clean ? 0 : 1;
}
