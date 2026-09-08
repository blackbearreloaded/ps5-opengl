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

#define WIDTH 1920
#define HEIGHT 1080
#define SIZE 64
#define RANGE_OFFSET 256
#define GEOMETRY_RANGE_OFFSET 512
#define GREEN_PIXEL UINT32_C(0xff00ff00)
#define BLUE_PIXEL UINT32_C(0xffff0000)
#define GREEN_HASH UINT32_C(0xc38d1dc5)
#define BLUE_HASH UINT32_C(0xbdf93dc5)

#ifdef PS5_GEOMETRY_TEST
int ps5_egl_current_draw_status(unsigned *draw_calls);
#endif

#ifndef PS5_GEOMETRY_TEST
static int
has_extension(const char *extensions, const char *name)
{
   const size_t length = strlen(name);
   const char *match = extensions;

   while ((match = strstr(match, name))) {
      if ((match == extensions || match[-1] == ' ') &&
          (match[length] == '\0' || match[length] == ' '))
         return 1;
      match += length;
   }
   return 0;
}
#endif

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint ok = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return -1;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
   return 0;
}

static uint32_t
hash32(const void *data, size_t size)
{
   const uint8_t *bytes = data;
   uint32_t hash = UINT32_C(2166136261);

   while (size--)
      hash = (hash ^ *bytes++) * UINT32_C(16777619);
   return hash;
}

static int
draw_oracle(uint32_t expected, uint32_t expected_hash, uint32_t *pixels,
            unsigned expected_draw_calls)
{
   unsigned matching = 0;
#ifdef PS5_GEOMETRY_TEST
   unsigned draw_calls = 0;
   int draw_status;
#else
   (void)expected_draw_calls;
#endif

   glClear(GL_COLOR_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
#ifdef PS5_GEOMETRY_TEST
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   printf("[ps5-egl-ubo] draw-status=%d calls=%u expected=%u\n",
          draw_status, draw_calls, expected_draw_calls);
#endif
   glFinish();
   glReadPixels((WIDTH - SIZE) / 2, (HEIGHT - SIZE) / 2,
                SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   uint32_t hash = hash32(pixels, SIZE * SIZE * sizeof(*pixels));

   for (unsigned i = 0; i < SIZE * SIZE; ++i)
      matching += pixels[i] == expected;
   printf("[ps5-egl-ubo] pixel=%08x matching=%u hash=%08x error=0x%x\n",
          expected, matching, hash, error);
   int passed = matching == SIZE * SIZE && hash == expected_hash &&
                error == GL_NO_ERROR;
#ifdef PS5_GEOMETRY_TEST
   passed &= draw_status == 0 && draw_calls == expected_draw_calls;
#endif
   return passed;
}

int
main(void)
{
#ifdef PS5_GEOMETRY_TEST
   static const char *vertex_source =
      "#version 330 core\n"
      "in vec2 a_position;\n"
      "uniform float u_scale;\n"
      "uniform sampler2D u_vertex_texture;\n"
      "layout(std140) uniform VertexBlock { vec4 u_offset; };\n"
      "void main() {\n"
      "  float sampled = textureLod(u_vertex_texture, vec2(0.5), 0.0).r;\n"
      "  gl_Position = vec4(a_position * u_scale + u_offset.xy +\n"
      "                     vec2(sampled - 1.0, 0.0), 0.0, 1.0);\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform float u_gain;\n"
      "uniform sampler2D u_texture;\n"
      "layout(std140) uniform FragmentBlock { vec4 u_color; };\n"
      "out vec4 out_color;\n"
      "void main() {\n"
      "  out_color = texture(u_texture, vec2(0.5)) * u_color * u_gain;\n"
      "}\n";
   static const char *geometry_source =
      "#version 330 core\n"
      "layout(triangles) in;\n"
      "layout(triangle_strip, max_vertices = 3) out;\n"
      "layout(std140) uniform GeometryBlock { vec4 u_geometry_offset; };\n"
      "void main() {\n"
      "  for (int i = 0; i < 3; ++i) {\n"
      "    gl_Position = gl_in[i].gl_Position + u_geometry_offset;\n"
      "    EmitVertex();\n"
      "  }\n"
      "  EndPrimitive();\n"
      "}\n";
#else
   static const char *vertex_source =
      "#version 120\n"
      "#extension GL_ARB_uniform_buffer_object : require\n"
      "attribute vec2 a_position;\n"
      "uniform float u_scale;\n"
      "uniform sampler2D u_vertex_texture;\n"
      "layout(std140) uniform VertexBlock { vec4 u_offset; };\n"
      "void main() {\n"
      "  float sampled = texture2DLod(u_vertex_texture, vec2(0.5), 0.0).r;\n"
      "  gl_Position = vec4(a_position * u_scale + u_offset.xy +\n"
      "                     vec2(sampled - 1.0, 0.0), 0.0, 1.0);\n"
      "}\n";
   static const char *fragment_source =
      "#version 120\n"
      "#extension GL_ARB_uniform_buffer_object : require\n"
      "uniform float u_gain;\n"
      "uniform sampler2D u_texture;\n"
      "layout(std140) uniform FragmentBlock { vec4 u_color; };\n"
      "void main() {\n"
      "  gl_FragColor = texture2D(u_texture, vec2(0.5)) * u_color * u_gain;\n"
      "}\n";
#endif
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static const float vertex_block[4] = {0.0f, 0.0f, 0.0f, 0.0f};
#ifdef PS5_GEOMETRY_TEST
   static const float geometry_block[4] = {0.0f, 0.0f, 0.0f, 0.0f};
#endif
   static const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
   static const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
   static const uint32_t white = UINT32_C(0xffffffff);
   static uint32_t pixels[SIZE * SIZE];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
#ifdef PS5_GEOMETRY_TEST
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
#else
   const EGLint *context_attributes = NULL;
#endif
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0, width = 0, height = 0;
   GLuint vs = 0, gs = 0, fs = 0, program = 0, vao = 0, vbo = 0;
   GLuint ubo = 0, texture = 0;
   GLuint elapsed_query = 0, timestamp_query = 0;
   GLuint64 elapsed = 0, timestamp = 0;
   PFNGLQUERYCOUNTERPROC query_counter = NULL;
   PFNGLGETQUERYOBJECTUI64VPROC get_query_object_ui64v = NULL;
   GLuint vertex_index = GL_INVALID_INDEX, fragment_index = GL_INVALID_INDEX;
#ifdef PS5_GEOMETRY_TEST
   GLuint geometry_index = GL_INVALID_INDEX;
#endif
   GLint linked = GL_FALSE, scale = -1, gain = -1, sampler = -1;
   GLint vertex_sampler = -1;
   GLint max_vertex_blocks = 0, max_fragment_blocks = 0;
#ifdef PS5_GEOMETRY_TEST
   GLint max_geometry_blocks = 0;
#endif
   GLint max_block_size = 0, alignment = 0;
#ifndef PS5_SKIP_MSAA_POLICY_TEST
   GLint max_samples = 0;
#endif
   const GLubyte *version = NULL, *glsl = NULL;
#ifndef PS5_GEOMETRY_TEST
   const GLubyte *extensions = NULL;
#endif
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0, first_ok = 0, update_ok = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
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
   query_counter = (PFNGLQUERYCOUNTERPROC)
      eglGetProcAddress("glQueryCounter");
   get_query_object_ui64v = (PFNGLGETQUERYOBJECTUI64VPROC)
      eglGetProcAddress("glGetQueryObjectui64v");
   if (!query_counter || !get_query_object_ui64v)
      goto cleanup;
   eglQuerySurface(display, surface, EGL_WIDTH, &width);
   eglQuerySurface(display, surface, EGL_HEIGHT, &height);
   version = glGetString(GL_VERSION);
   glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
#ifndef PS5_GEOMETRY_TEST
   extensions = glGetString(GL_EXTENSIONS);
#endif
   glGetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &max_vertex_blocks);
   glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &max_fragment_blocks);
#ifdef PS5_GEOMETRY_TEST
   glGetIntegerv(GL_MAX_GEOMETRY_UNIFORM_BLOCKS, &max_geometry_blocks);
#endif
   glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &max_block_size);
   glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
#ifndef PS5_SKIP_MSAA_POLICY_TEST
   glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
#endif
   if (!version || !glsl
#ifndef PS5_GEOMETRY_TEST
       || !extensions ||
       !has_extension((const char *)extensions,
                      "GL_ARB_uniform_buffer_object") ||
       !has_extension((const char *)extensions, "GL_ARB_timer_query")
#ifndef PS5_SKIP_MSAA_POLICY_TEST
       ||
       !has_extension((const char *)extensions, "GL_ARB_texture_multisample") ||
       !has_extension((const char *)extensions,
                      "GL_EXT_framebuffer_multisample"))
#else
      )
#endif
#else
      )
#endif
      goto cleanup;

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fs)
#ifdef PS5_GEOMETRY_TEST
       || compile_shader(GL_GEOMETRY_SHADER, geometry_source, &gs)
#endif
      )
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vs);
#ifdef PS5_GEOMETRY_TEST
   glAttachShader(program, gs);
#endif
   glAttachShader(program, fs);
   glBindAttribLocation(program, 0, "a_position");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);
   scale = glGetUniformLocation(program, "u_scale");
   gain = glGetUniformLocation(program, "u_gain");
   sampler = glGetUniformLocation(program, "u_texture");
   vertex_sampler = glGetUniformLocation(program, "u_vertex_texture");
   vertex_index = glGetUniformBlockIndex(program, "VertexBlock");
   fragment_index = glGetUniformBlockIndex(program, "FragmentBlock");
#ifdef PS5_GEOMETRY_TEST
   geometry_index = glGetUniformBlockIndex(program, "GeometryBlock");
#endif
   if (scale < 0 || gain < 0 || sampler < 0 || vertex_sampler < 0 ||
       vertex_index == GL_INVALID_INDEX ||
       fragment_index == GL_INVALID_INDEX
#ifdef PS5_GEOMETRY_TEST
       || geometry_index == GL_INVALID_INDEX
#endif
      )
      goto cleanup;
   glUniform1f(scale, 1.0f);
   glUniform1f(gain, 1.0f);
   glUniform1i(sampler, 0);
   glUniform1i(vertex_sampler, 0);
   glUniformBlockBinding(program, vertex_index, 3);
   glUniformBlockBinding(program, fragment_index, 5);
#ifdef PS5_GEOMETRY_TEST
   glUniformBlockBinding(program, geometry_index, 7);
#endif

#ifdef PS5_GEOMETRY_TEST
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
#endif
   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
   glEnableVertexAttribArray(0);

   glGenBuffers(1, &ubo);
   glBindBuffer(GL_UNIFORM_BUFFER, ubo);
   glBufferData(GL_UNIFORM_BUFFER,
#ifdef PS5_GEOMETRY_TEST
                GEOMETRY_RANGE_OFFSET + sizeof(geometry_block),
#else
                RANGE_OFFSET + sizeof(green),
#endif
                NULL,
                GL_DYNAMIC_DRAW);
   glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(vertex_block), vertex_block);
   glBufferSubData(GL_UNIFORM_BUFFER, RANGE_OFFSET, sizeof(green), green);
#ifdef PS5_GEOMETRY_TEST
   glBufferSubData(GL_UNIFORM_BUFFER, GEOMETRY_RANGE_OFFSET,
                   sizeof(geometry_block), geometry_block);
#endif
   glBindBufferRange(GL_UNIFORM_BUFFER, 3, ubo, 0, sizeof(vertex_block));
   glBindBufferRange(GL_UNIFORM_BUFFER, 5, ubo, RANGE_OFFSET, sizeof(green));
#ifdef PS5_GEOMETRY_TEST
   glBindBufferRange(GL_UNIFORM_BUFFER, 7, ubo, GEOMETRY_RANGE_OFFSET,
                     sizeof(geometry_block));
#endif

   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, &white);

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glGenQueries(1, &elapsed_query);
   glGenQueries(1, &timestamp_query);
   glBeginQuery(GL_TIME_ELAPSED, elapsed_query);
   first_ok = draw_oracle(GREEN_PIXEL, GREEN_HASH, pixels, 1);
   glBufferSubData(GL_UNIFORM_BUFFER, RANGE_OFFSET, sizeof(blue), blue);
   update_ok = draw_oracle(BLUE_PIXEL, BLUE_HASH, pixels, 2);
   glEndQuery(GL_TIME_ELAPSED);
   query_counter(timestamp_query, GL_TIMESTAMP);
   get_query_object_ui64v(elapsed_query, GL_QUERY_RESULT, &elapsed);
   get_query_object_ui64v(timestamp_query, GL_QUERY_RESULT, &timestamp);
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT && max_vertex_blocks >= 12 &&
            max_fragment_blocks >= 12 && max_block_size >= 16384 &&
#ifdef PS5_GEOMETRY_TEST
            max_geometry_blocks >= 12 &&
#endif
#ifndef PS5_SKIP_MSAA_POLICY_TEST
            max_samples == 1 &&
#endif
            alignment > 0 && alignment <= RANGE_OFFSET &&
            RANGE_OFFSET % alignment == 0 && first_ok && update_ok &&
            elapsed > 0 && timestamp > 0 &&
            strncmp((const char *)version,
#ifdef PS5_GEOMETRY_TEST
                    "3.3 ", 4
#else
                    "2.1 ", 4
#endif
                   ) == 0 &&
            strncmp((const char *)glsl,
#ifdef PS5_GEOMETRY_TEST
                    "3.30", 4
#else
                    "1.20", 4
#endif
                   ) == 0;
#ifdef PS5_GEOMETRY_TEST
   printf("[ps5-egl-ubo] geometry-blocks=%d offset=%d\n",
          max_geometry_blocks, GEOMETRY_RANGE_OFFSET);
#endif

cleanup:
   if (elapsed_query)
      glDeleteQueries(1, &elapsed_query);
   if (timestamp_query)
      glDeleteQueries(1, &timestamp_query);
   if (texture)
      glDeleteTextures(1, &texture);
   if (ubo)
      glDeleteBuffers(1, &ubo);
   if (vbo)
      glDeleteBuffers(1, &vbo);
   if (vao)
      glDeleteVertexArrays(1, &vao);
   if (program)
      glDeleteProgram(program);
   if (fs)
      glDeleteShader(fs);
   if (gs)
      glDeleteShader(gs);
   if (vs)
      glDeleteShader(vs);
   if (made_current)
      cleanup_gl_error = glGetError();
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_egl_error = eglGetError();
   passed = passed && cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
            cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-ubo] limits=%d/%d/%d align=%d draws=%d/%d "
          "cleanup=%x/%x result=%d\n",
          max_vertex_blocks, max_fragment_blocks, max_block_size, alignment,
          first_ok, update_ok, cleanup_gl_error, cleanup_egl_error,
          passed ? 0 : 1);
   return passed ? 0 : 1;
}
