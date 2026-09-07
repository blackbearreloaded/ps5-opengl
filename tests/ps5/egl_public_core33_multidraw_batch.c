/* Real multi-draw ordering, descriptor isolation, inline uniforms and reuse. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>

enum { BANDS = 9, WIDTH = BANDS * 8, HEIGHT = 32, DRAWS = BANDS + 2 };
struct vertex { float position[2], color[4]; };
static struct vertex vertices[(BANDS + 1) * 6];
static uint8_t pixels[WIDTH * HEIGHT * 4];
static const uint8_t colors[7][4] = {
   {255,0,0,255}, {0,255,0,255}, {0,0,255,255}, {255,255,0,255},
   {255,0,255,255}, {0,255,255,255}, {255,255,255,255}
};
#ifdef PS5_MULTIDRAW_TEXTURE_TEST
static uint8_t texture_pixels[2][4][4] = {
   {{255,255,255,255}, {0,255,255,255}, {255,0,255,255}, {255,255,0,255}},
   {{255,255,255,255}, {255,0,255,255}, {0,255,255,255}, {255,255,0,255}}
};
#endif
#ifdef PS5_NATIVE_MULTIDRAW_TEST
extern int ps5_egl_current_draw_status(unsigned *);
#endif

static int64_t now_ns(void)
{
   struct timespec t;
   return clock_gettime(CLOCK_MONOTONIC, &t) == 0 ? (int64_t)t.tv_sec * 1000000000 + t.tv_nsec : 0;
}

static GLuint shader(GLenum type, const char *text)
{
   GLuint s = glCreateShader(type);
   GLint ok = 0;
   glShaderSource(s, 1, &text, NULL);
   glCompileShader(s);
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok) { glDeleteShader(s); return 0; }
   return s;
}

static int check_pixels(unsigned green, int band0_black)
{
#ifdef PS5_NATIVE_MULTIDRAW_TEST
   if (ps5_egl_current_draw_status(NULL) != 0) return 0;
#endif
   glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   if (glGetError() != GL_NO_ERROR) return 0;
   for (unsigned y = 0; y < HEIGHT; ++y) {
      for (unsigned x = 0; x < WIDTH; ++x) {
         uint8_t expected[4];
         memcpy(expected, colors[(x / 8) % 7], 4);
         if (x < 8) memcpy(expected, colors[2], 4); /* Last subdraw overlaps band zero. */
         expected[1] *= green;
#ifdef PS5_MULTIDRAW_TEXTURE_TEST
         unsigned texel0 = (x % 8 >= 4) + 2 * (y % 16 >= 8);
         unsigned texel1 = (x % 16 >= 8) + 2 * (y % 8 >= 4);
         for (unsigned c = 0; c < 4; ++c)
            expected[c] = expected[c] * (texture_pixels[0][texel0][c] / 255) *
                                       (texture_pixels[1][texel1][c] / 255);
#endif
         if (band0_black && x < 8) memset(expected, 0, 3);
         if (memcmp(pixels + 4 * (y * WIDTH + x), expected, 4)) {
            printf("[ps5-multidraw] pixel mismatch x=%u y=%u\n", x, y);
            return 0;
         }
      }
   }
   return 1;
}

int main(void)
{
   const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
   const EGLint ctx[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE};
   const EGLint sa[] = {EGL_WIDTH, WIDTH, EGL_HEIGHT, HEIGHT, EGL_NONE};
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLContext context = EGL_NO_CONTEXT;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLConfig config;
   EGLint count = 0;
   GLuint vs = 0, fs = 0, program = 0, vao = 0, vbo = 0, ebo = 0;
   GLuint query = 0;
#ifdef PS5_MULTIDRAW_TEXTURE_TEST
   GLuint textures[2] = {0};
#endif
   GLsync fence = NULL;
   int current = 0, passed = 0, clean = 1;
   unsigned completed = 0;
   GLint first[DRAWS], base[DRAWS];
   GLsizei counts[DRAWS];
   const void *offsets[DRAWS];
   uint16_t indices16[(BANDS + 1) * 6];
   uint32_t indices32[(BANDS + 1) * 6];
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) || !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, ca, &config, 1, &count) || count != 1) goto cleanup;
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx);
   surface = eglCreatePbufferSurface(display, config, sa);
   if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
       !eglMakeCurrent(display, surface, surface, context)) goto cleanup;
   current = 1;
#ifdef PS5_MULTIDRAW_HOST_REFERENCE
   glDrawBuffer(GL_FRONT); glReadBuffer(GL_FRONT);
#endif
   vs = shader(GL_VERTEX_SHADER, "#version 330 core\nlayout(location=0) in vec2 p;"
      "layout(location=1) in vec4 c; out vec4 color; void main(){gl_Position=vec4(p,0,1); color=c;}");
   fs = shader(GL_FRAGMENT_SHADER, "#version 330 core\nin vec4 color; uniform vec4 tint;"
#ifdef PS5_MULTIDRAW_TEXTURE_TEST
      "uniform sampler2D image0; uniform sampler2D image1; out vec4 result;"
      "void main(){result=color*tint*texture(image0,gl_FragCoord.xy/vec2(8,16))*"
      "texture(image1,gl_FragCoord.xy/vec2(16,8));}");
#else
      "out vec4 result; void main(){result=color*tint;}");
#endif
   if (!vs || !fs) goto cleanup;
   program = glCreateProgram(); glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
   GLint linked = 0;
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked) goto cleanup;
   glUseProgram(program);
   GLint tint = glGetUniformLocation(program, "tint");
   if (tint < 0) goto cleanup;
#ifdef PS5_MULTIDRAW_TEXTURE_TEST
   GLint image0 = glGetUniformLocation(program, "image0"), image1 = glGetUniformLocation(program, "image1");
   if (image0 < 0 || image1 < 0) goto cleanup;
   glUniform1i(image0, 0); glUniform1i(image1, 7);
   glGenTextures(2, textures);
   for (unsigned unit = 0; unit < 2; ++unit) {
      glActiveTexture(unit ? GL_TEXTURE7 : GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, textures[unit]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture_pixels[unit]);
   }
#endif
   for (unsigned band = 0; band <= BANDS; ++band) {
      const unsigned position = band == BANDS ? 0 : band;
      const float x0 = -1.0f + 2.0f * position / BANDS, x1 = -1.0f + 2.0f * (position + 1) / BANDS;
      const float p[6][2] = {{x0,-1},{x1,-1},{x0,1},{x0,1},{x1,-1},{x1,1}};
      for (unsigned v = 0; v < 6; ++v) {
         memcpy(vertices[band * 6 + v].position, p[v], sizeof(p[v]));
         for (unsigned c = 0; c < 4; ++c)
            vertices[band * 6 + v].color[c] = colors[band == BANDS ? 2 : band % 7][c] / 255.0f;
         indices16[band * 6 + v] = indices32[band * 6 + v] = band * 6 + v;
      }
   }
   for (unsigned i = 0, band = BANDS; i < DRAWS; ++i) {
      counts[i] = i == 3 ? 0 : 6;
      first[i] = base[i] = i == DRAWS - 1 ? BANDS * 6 : i == 3 ? 0 : (GLint)(--band * 6);
   }
   glGenVertexArrays(1, &vao); glBindVertexArray(vao);
   glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void *)offsetof(struct vertex, position));
   glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(struct vertex), (void *)offsetof(struct vertex, color));
   glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
   glGenBuffers(1, &ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
   glViewport(0, 0, WIDTH, HEIGHT);
   for (unsigned mode = 0; mode < 4; ++mode) {
#ifdef PS5_MULTIDRAW_TEXTURE_TEST
      if (mode == 2) {
         memcpy(texture_pixels[1][0], colors[4], 4);
         glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, textures[1]);
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, texture_pixels[1][0]);
         printf("[ps5-multidraw-texture] upload-after-batch=1 units=0,7\n");
      }
#endif
      const unsigned green = mode < 2;
      const GLenum type = mode == 2 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
      const unsigned index_size = mode == 2 ? 4 : 2;
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, mode == 2 ? sizeof(indices32) : sizeof(indices16),
                   mode == 2 ? (const void *)indices32 : (const void *)indices16, GL_STATIC_DRAW);
      for (unsigned i = 0; i < DRAWS; ++i)
         offsets[i] = (const void *)(uintptr_t)(mode == 3 ? 0 : first[i] * index_size);
      glUniform4f(tint, 1, green, 1, 1);
      glDrawArrays(GL_TRIANGLES, 0, 6); /* Warm the program before timing. */
      int64_t elapsed[2];
      for (unsigned batched = 0; batched < 2; ++batched) {
         glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
         int64_t start = now_ns();
         if (batched) {
            if (!mode) glMultiDrawArrays(GL_TRIANGLES, first, counts, DRAWS);
            else if (mode == 3) glMultiDrawElementsBaseVertex(GL_TRIANGLES, counts, type, offsets, DRAWS, base);
            else glMultiDrawElements(GL_TRIANGLES, counts, type, offsets, DRAWS);
         } else {
            for (unsigned i = 0; i < DRAWS; ++i) {
               if (!mode) glDrawArrays(GL_TRIANGLES, first[i], counts[i]);
               else if (mode == 3) glDrawElementsBaseVertex(GL_TRIANGLES, counts[i], type, offsets[i], base[i]);
               else glDrawElements(GL_TRIANGLES, counts[i], type, offsets[i]);
            }
         }
         elapsed[batched] = now_ns() - start;
         if (start <= 0 || elapsed[batched] <= 0 || !check_pixels(green, 0)) goto cleanup;
      }
      glUniform4f(tint, 0, 1, 1, 1); glDrawArrays(GL_TRIANGLES, 0, 6);
      if (!check_pixels(green, 1)) goto cleanup; /* Original descriptor/uniform storage restored. */
      printf("[ps5-multidraw] mode=%u serial_ns=%lld batch_ns=%lld pixels=%u PASS\n",
             mode, (long long)elapsed[0], (long long)elapsed[1], 3 * WIDTH * HEIGHT);
      ++completed;
   }
   /* Active queries must keep the synchronous fallback, even after batching. */
   glUniform4f(tint, 1, 1, 1, 1);
   glClear(GL_COLOR_BUFFER_BIT);
   glGenQueries(1, &query);
   glBeginQuery(GL_SAMPLES_PASSED, query);
   glMultiDrawArrays(GL_TRIANGLES, first, counts, DRAWS);
   glEndQuery(GL_SAMPLES_PASSED);
   GLuint samples = 0;
   glGetQueryObjectuiv(query, GL_QUERY_RESULT, &samples);
   if (samples != (WIDTH + 8) * HEIGHT || !check_pixels(1, 0)) goto cleanup;
   fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   if (!fence) goto cleanup;
   GLenum waited = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000);
   if (waited != GL_ALREADY_SIGNALED && waited != GL_CONDITION_SATISFIED) goto cleanup;
   /* Orphan the original VBO immediately; no queued draw may still need it. */
   for (unsigned i = 0; i < 6; ++i) memset(vertices[i].color, 0, 3 * sizeof(float));
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);
   glDrawArrays(GL_TRIANGLES, 0, 6);
   if (!check_pixels(1, 1)) goto cleanup;
   printf("[ps5-multidraw] query_samples=%u fence=1 orphan=1 pixels=%u PASS\n", samples, 2 * WIDTH * HEIGHT);
#ifdef PS5_MULTIDRAW_TEXTURE_TEST
   printf("[ps5-multidraw-texture] sampled=2 uploads=1 pixels=%u PASS\n", 14 * WIDTH * HEIGHT);
#endif
   passed = 1;
cleanup:
   if (current) {
      if (fence) glDeleteSync(fence);
      if (query) glDeleteQueries(1, &query);
#ifdef PS5_MULTIDRAW_TEXTURE_TEST
      glDeleteTextures(2, textures);
#endif
      glUseProgram(0);
      if (ebo) glDeleteBuffers(1, &ebo);
      if (vbo) glDeleteBuffers(1, &vbo);
      if (vao) glDeleteVertexArrays(1, &vao);
      if (program) glDeleteProgram(program);
      if (vs) glDeleteShader(vs);
      if (fs) glDeleteShader(fs);
      clean &= glGetError() == GL_NO_ERROR;
   }
   if (display != EGL_NO_DISPLAY) {
      clean &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (surface != EGL_NO_SURFACE) clean &= eglDestroySurface(display, surface);
      if (context != EGL_NO_CONTEXT) clean &= eglDestroyContext(display, context);
      clean &= eglTerminate(display);
   }
   clean &= eglGetError() == EGL_SUCCESS;
   printf("[ps5-multidraw] completed=%u cleanup=%u result=%d\n", completed, clean, passed && clean ? 0 : 1);
   return passed && clean ? 0 : 1;
}
