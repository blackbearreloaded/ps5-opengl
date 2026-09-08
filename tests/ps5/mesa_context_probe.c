// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/nir/nir_shader_compiler_options.h"
#include "frontend/api.h"
#include "util/glheader.h"
#include "pipe/p_state.h"
#include "pipe/p_screen.h"
#include "ps5_screen.h"
#include "state_tracker/st_context.h"
#include "util/u_inlines.h"

GLuint GLAPIENTRY _mesa_CreateShader(GLenum type);
void GLAPIENTRY _mesa_ShaderSource(GLuint shader, GLsizei count,
                                   const GLchar *const *strings,
                                   const GLint *length);
void GLAPIENTRY _mesa_CompileShader(GLuint shader);
void GLAPIENTRY _mesa_GetShaderiv(GLuint shader, GLenum pname, GLint *params);
void GLAPIENTRY _mesa_GetShaderInfoLog(GLuint shader, GLsizei size,
                                       GLsizei *length, GLchar *log);
GLuint GLAPIENTRY _mesa_CreateProgram(void);
void GLAPIENTRY _mesa_AttachShader(GLuint program, GLuint shader);
void GLAPIENTRY _mesa_BindAttribLocation(GLuint program, GLuint index,
                                         const GLchar *name);
void GLAPIENTRY _mesa_LinkProgram(GLuint program);
void GLAPIENTRY _mesa_GetProgramiv(GLuint program, GLenum pname, GLint *params);
void GLAPIENTRY _mesa_GetProgramInfoLog(GLuint program, GLsizei size,
                                        GLsizei *length, GLchar *log);
void GLAPIENTRY _mesa_DeleteProgram(GLuint program);
void GLAPIENTRY _mesa_DeleteShader(GLuint shader);
void GLAPIENTRY _mesa_UseProgram(GLuint program);
GLint GLAPIENTRY _mesa_GetUniformLocation(GLuint program,
                                          const GLchar *name);
void GLAPIENTRY _mesa_Uniform4f(GLint location, GLfloat v0, GLfloat v1,
                               GLfloat v2, GLfloat v3);
void GLAPIENTRY _mesa_Uniform1i(GLint location, GLint value);
void GLAPIENTRY _mesa_Uniform1f(GLint location, GLfloat value);
void GLAPIENTRY _mesa_GenTextures(GLsizei count, GLuint *textures);
void GLAPIENTRY _mesa_BindTexture(GLenum target, GLuint texture);
void GLAPIENTRY _mesa_TexParameteri(GLenum target, GLenum pname, GLint param);
void GLAPIENTRY _mesa_TexImage2D(GLenum target, GLint level,
                                 GLint internal_format, GLsizei width,
                                 GLsizei height, GLint border, GLenum format,
                                 GLenum type, const GLvoid *pixels);
void GLAPIENTRY _mesa_TexSubImage2D(GLenum target, GLint level,
                                    GLint xoffset, GLint yoffset,
                                    GLsizei width, GLsizei height,
                                    GLenum format, GLenum type,
                                    const GLvoid *pixels);
void GLAPIENTRY _mesa_DeleteTextures(GLsizei count, const GLuint *textures);
void GLAPIENTRY _mesa_GenFramebuffers(GLsizei count, GLuint *framebuffers);
void GLAPIENTRY _mesa_BindFramebuffer(GLenum target, GLuint framebuffer);
GLenum GLAPIENTRY _mesa_CheckFramebufferStatus(GLenum target);
void GLAPIENTRY _mesa_DeleteFramebuffers(GLsizei count,
                                         const GLuint *framebuffers);
void GLAPIENTRY _mesa_GenRenderbuffers(GLsizei count, GLuint *renderbuffers);
void GLAPIENTRY _mesa_BindRenderbuffer(GLenum target, GLuint renderbuffer);
void GLAPIENTRY _mesa_RenderbufferStorage(GLenum target,
                                          GLenum internal_format,
                                          GLsizei width, GLsizei height);
void GLAPIENTRY _mesa_GetRenderbufferParameteriv(GLenum target, GLenum pname,
                                                  GLint *params);
void GLAPIENTRY _mesa_FramebufferRenderbuffer(GLenum target,
                                              GLenum attachment,
                                              GLenum renderbuffer_target,
                                              GLuint renderbuffer);
void GLAPIENTRY _mesa_DeleteRenderbuffers(GLsizei count,
                                          const GLuint *renderbuffers);
void GLAPIENTRY _mesa_Viewport(GLint x, GLint y, GLsizei width,
                               GLsizei height);
void GLAPIENTRY _mesa_ReadPixels(GLint x, GLint y, GLsizei width,
                                 GLsizei height, GLenum format, GLenum type,
                                 GLvoid *pixels);
void GLAPIENTRY _mesa_Enable(GLenum cap);
void GLAPIENTRY _mesa_Disable(GLenum cap);
void GLAPIENTRY _mesa_EnableClientState(GLenum cap);
void GLAPIENTRY _mesa_DisableClientState(GLenum cap);
void GLAPIENTRY _mesa_DepthFunc(GLenum func);
void GLAPIENTRY _mesa_DepthMask(GLboolean mask);
void GLAPIENTRY _mesa_StencilFunc(GLenum func, GLint ref, GLuint mask);
void GLAPIENTRY _mesa_StencilMask(GLuint mask);
void GLAPIENTRY _mesa_StencilOp(GLenum fail, GLenum zfail, GLenum zpass);
void GLAPIENTRY _mesa_ClearDepth(GLclampd depth);
void GLAPIENTRY _mesa_Clear(GLbitfield mask);
void GLAPIENTRY _mesa_GenBuffers(GLsizei count, GLuint *buffers);
void GLAPIENTRY _mesa_BindBuffer(GLenum target, GLuint buffer);
void GLAPIENTRY _mesa_BufferData(GLenum target, GLsizeiptr size,
                                 const GLvoid *data, GLenum usage);
void GLAPIENTRY _mesa_DeleteBuffers(GLsizei count, const GLuint *buffers);
void GLAPIENTRY _mesa_VertexAttribPointer(GLuint index, GLint size,
                                          GLenum type, GLboolean normalized,
                                          GLsizei stride,
                                          const GLvoid *pointer);
void GLAPIENTRY _mesa_EnableVertexAttribArray(GLuint index);
void GLAPIENTRY _mesa_GenVertexArrays(GLsizei count, GLuint *arrays);
void GLAPIENTRY _mesa_BindVertexArray(GLuint array);
void GLAPIENTRY _mesa_DeleteVertexArrays(GLsizei count, const GLuint *arrays);
void GLAPIENTRY _mesa_DrawArrays(GLenum mode, GLint first, GLsizei count);
void GLAPIENTRY _mesa_DrawElements(GLenum mode, GLsizei count, GLenum type,
                                   const GLvoid *indices);
void GLAPIENTRY _mesa_PrimitiveRestartIndex(GLuint index);
void GLAPIENTRY _mesa_Finish(void);
GLenum GLAPIENTRY _mesa_GetError(void);

struct probe_drawable {
   struct pipe_frontend_drawable base;
   struct st_visual visual;
   struct pipe_resource *target;
};

static bool
probe_flush_front(struct st_context *st,
                  struct pipe_frontend_drawable *base,
                  enum st_attachment_type attachment)
{
   (void)st;
   (void)base;
   return attachment == ST_ATTACHMENT_FRONT_LEFT;
}

static bool
probe_validate(struct st_context *st, struct pipe_frontend_drawable *base,
               const enum st_attachment_type *attachments, unsigned count,
               struct pipe_resource **out,
               struct pipe_resource **resolve)
{
   struct probe_drawable *drawable = (struct probe_drawable *)base;
   unsigned i;

   (void)st;
   if (resolve)
      *resolve = NULL;
   for (i = 0; i < count; ++i) {
      if (attachments[i] != ST_ATTACHMENT_FRONT_LEFT)
         return false;
      pipe_resource_reference(&out[i], drawable->target);
   }
   return true;
}

static int
compile_shader(GLenum type, const char *source, GLuint *shader_out)
{
   GLchar log[1024];
   GLsizei log_length = 0;
   GLint compiled = GL_FALSE;
   GLuint shader = _mesa_CreateShader(type);

   if (!shader)
      return -1;
   _mesa_ShaderSource(shader, 1, &source, NULL);
   _mesa_CompileShader(shader);
   _mesa_GetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   _mesa_GetShaderInfoLog(shader, sizeof(log), &log_length, log);
   printf("[ps5-mesa] shader type=0x%x object=%u compiled=%d log=%.*s\n",
          type, shader, compiled, log_length, log);
   if (!compiled) {
      _mesa_DeleteShader(shader);
      return -1;
   }
   *shader_out = shader;
   return 0;
}

static int
link_program(GLuint vs, GLuint fs, GLuint *program_out)
{
   GLchar log[1024];
   GLsizei log_length = 0;
   GLint linked = GL_FALSE;
   GLuint program = _mesa_CreateProgram();

   if (!program)
      return -1;
   _mesa_AttachShader(program, vs);
   _mesa_AttachShader(program, fs);
   _mesa_BindAttribLocation(program, 0, "a_position");
   _mesa_BindAttribLocation(program, 1, "a_uv");
   printf("[ps5-mesa] program object=%u link-begin\n", program);
   _mesa_LinkProgram(program);
   printf("[ps5-mesa] program object=%u link-return\n", program);
   _mesa_GetProgramiv(program, GL_LINK_STATUS, &linked);
   _mesa_GetProgramInfoLog(program, sizeof(log), &log_length, log);
   printf("[ps5-mesa] program object=%u linked=%d log=%.*s\n",
          program, linked, log_length, log);
   if (!linked) {
      _mesa_DeleteProgram(program);
      return -1;
   }
   *program_out = program;
   return 0;
}

static int
frontend_get_param(struct pipe_frontend_screen *screen,
                   enum st_manager_param param)
{
   (void)screen;
   (void)param;
   return 0;
}

static uint32_t
probe_hash32(const void *data, size_t size)
{
   const uint8_t *bytes = data;
   uint32_t hash = UINT32_C(2166136261);

   while (size--)
      hash = (hash ^ *bytes++) * UINT32_C(16777619);
   return hash;
}

int
main(void)
{
   struct pipe_frontend_screen frontend;
   struct st_context_attribs attribs;
   struct st_config_options options;
   struct st_context *st;
   struct pipe_screen *screen;
   struct pipe_resource resource_template;
   struct probe_drawable drawable;
   enum st_context_error error = ST_CONTEXT_SUCCESS;
   int core = 0;
   int compat = 0;
   int es1 = 0;
   int es2 = 0;
   GLuint vs = 0;
   GLuint fs = 0;
   GLuint program = 0;
   GLint sampler_location = -1;
   GLint depth_location = -1;
   GLuint texture = 0;
   GLuint framebuffer = 0;
   GLuint color_renderbuffer = 0;
   GLuint depth_renderbuffer = 0;
#ifdef PS5_PUBLIC_STENCIL_TEST
   GLuint stencil_renderbuffer = 0;
#endif
   GLenum framebuffer_status = 0;
   GLenum depth_framebuffer_status = 0;
   GLenum post_depth_framebuffer_status = 0;
#ifdef PS5_PUBLIC_STENCIL_TEST
   GLenum stencil_framebuffer_status = 0;
   GLenum post_stencil_framebuffer_status = 0;
#endif
   GLenum depth_setup_error = GL_NO_ERROR;
   GLint depth_internal_format = 0;
   GLint depth_size = 0;
   bool depth_float_extension = false;
#ifdef PS5_PUBLIC_STENCIL_TEST
   GLenum depth_detach_sync_error = GL_NO_ERROR;
   GLenum stencil_setup_error = GL_NO_ERROR;
   GLint stencil_internal_format = 0;
   GLint stencil_size = 0;
#endif
   GLuint vertex_buffers[2] = {0};
   GLuint vertex_arrays[2] = {0};
   GLuint index_buffer = 0;
   GLenum gl_error = GL_NO_ERROR;
   unsigned draw_calls = 0;
   int draw_status = -1;
   void *target_address = NULL;
   size_t target_allocation_size = 0;
   size_t target_nonzero = 0;
   size_t fbo_readback_nonzero = 0;
   uint32_t fbo_readback_hash = 0;
   size_t direct_loop_nonzero = 0;
   size_t indexed_loop_nonzero = 0;
   uint32_t direct_loop_hash = 0;
   uint32_t indexed_loop_hash = 0;
   GLenum direct_loop_error = GL_NO_ERROR;
   GLenum indexed_loop_error = GL_NO_ERROR;
   unsigned direct_loop_draw_calls = 0;
   unsigned indexed_loop_draw_calls = 0;
   int direct_loop_draw_status = -1;
   int indexed_loop_draw_status = -1;
   size_t restart_baseline_nonzero = 0;
   size_t restart_draw_nonzero = 0;
   uint32_t restart_baseline_hash = 0;
   uint32_t restart_draw_hash = 0;
   GLenum restart_baseline_error = GL_NO_ERROR;
   GLenum restart_draw_error = GL_NO_ERROR;
   unsigned restart_baseline_draw_calls = 0;
   unsigned restart_draw_calls = 0;
   int restart_baseline_draw_status = -1;
   int restart_draw_status = -1;
   size_t index_u16_nonzero = 0;
   size_t index_u8_nonzero = 0;
   size_t index_u32_nonzero = 0;
   uint32_t index_u16_hash = 0;
   uint32_t index_u8_hash = 0;
   uint32_t index_u32_hash = 0;
   GLenum index_u16_error = GL_NO_ERROR;
   GLenum index_u8_error = GL_NO_ERROR;
   GLenum index_u32_error = GL_NO_ERROR;
   unsigned index_u16_draw_calls = 0;
   unsigned index_u8_draw_calls = 0;
   unsigned index_u32_draw_calls = 0;
   int index_u16_draw_status = -1;
   int index_u8_draw_status = -1;
   int index_u32_draw_status = -1;
   size_t depth_write_nonzero = 0;
   size_t depth_mask_nonzero = 0;
   uint32_t depth_write_hash = 0;
   uint32_t depth_mask_hash = 0;
   GLenum depth_write_error = GL_NO_ERROR;
   GLenum depth_mask_error = GL_NO_ERROR;
   unsigned depth_near_draw_calls = 0;
   unsigned depth_far_draw_calls = 0;
   unsigned depth_mask_near_draw_calls = 0;
   unsigned depth_mask_far_draw_calls = 0;
   int depth_near_draw_status = -1;
   int depth_far_draw_status = -1;
   int depth_mask_near_draw_status = -1;
   int depth_mask_far_draw_status = -1;
#ifdef PS5_PUBLIC_STENCIL_TEST
   size_t stencil_readback_nonzero = 0;
   uint32_t stencil_readback_hash = 0;
   GLenum stencil_readback_error = GL_NO_ERROR;
   unsigned stencil_write_draw_calls = 0;
   unsigned stencil_pass_draw_calls = 0;
   unsigned stencil_reject_draw_calls = 0;
   int stencil_write_draw_status = -1;
   int stencil_pass_draw_status = -1;
   int stencil_reject_draw_status = -1;
#endif
   bool passed;
   bool drawable_registered = false;
   static const char vertex_source[] =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "attribute vec2 a_uv;\n"
      "varying vec2 v_uv;\n"
      "uniform float u_depth;\n"
      "void main() {\n"
      "  gl_Position = vec4(a_position, u_depth, 1.0);\n"
      "  v_uv = a_uv;\n"
      "}\n";
   static const char fragment_source[] =
      "#version 120\n"
      "varying vec2 v_uv;\n"
      "uniform sampler2D u_texture;\n"
      "void main() { gl_FragColor = texture2D(u_texture, v_uv); }\n";
   static const float vertices[12] = {
      -0.5f, -0.5f, 0.0f, 1.0f,
       0.5f, -0.5f, 1.0f, 1.0f,
      -0.5f,  0.5f, 0.0f, 0.0f,
   };
   static const float wide_vertices[18] = {
      -0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f,
       0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 1.0f,
      -0.5f,  0.5f, 0.0f, 1.0f, 0.0f, 0.0f,
   };
   static const uint16_t indices[3] = {0, 1, 2};
   static const float restart_baseline_vertices[24] = {
      -0.900f, -0.5f, 0.0f, 1.0f,
      -0.650f, -0.5f, 1.0f, 1.0f,
      -0.775f,  0.5f, 0.5f, 0.0f,
      -0.350f, -0.5f, 0.0f, 1.0f,
      -0.100f, -0.5f, 1.0f, 1.0f,
      -0.225f,  0.5f, 0.5f, 0.0f,
   };
   static const float restart_vertices[24] = {
       0.100f, -0.5f, 0.0f, 1.0f,
       0.350f, -0.5f, 1.0f, 1.0f,
       0.225f,  0.5f, 0.5f, 0.0f,
       0.650f, -0.5f, 0.0f, 1.0f,
       0.900f, -0.5f, 1.0f, 1.0f,
       0.775f,  0.5f, 0.5f, 0.0f,
   };
   static const uint16_t restart_baseline_indices[6] = {0, 1, 2, 3, 4, 5};
   static const uint16_t restart_indices[7] = {
      0, 1, 2, UINT16_MAX, 3, 4, 5,
   };
   static const float index_u16_vertices[12] = {
      -0.875f, 0.625f, 0.0f, 1.0f,
      -0.625f, 0.625f, 1.0f, 1.0f,
      -0.750f, 0.875f, 0.5f, 0.0f,
   };
   static const float index_u8_vertices[12] = {
      -0.250f, 0.625f, 0.0f, 1.0f,
       0.000f, 0.625f, 1.0f, 1.0f,
      -0.125f, 0.875f, 0.5f, 0.0f,
   };
   static const float index_u32_vertices[12] = {
       0.375f, 0.625f, 0.0f, 1.0f,
       0.625f, 0.625f, 1.0f, 1.0f,
       0.500f, 0.875f, 0.5f, 0.0f,
   };
   static const uint16_t index_u16_indices[4] = {UINT16_MAX, 0, 1, 2};
   static const uint8_t index_u8_indices[4] = {UINT8_MAX, 0, 1, 2};
   static const uint32_t index_u32_indices[4] = {UINT32_MAX, 0, 1, 2};
   static const float depth_near_vertices[12] = {
      -0.875f, -0.875f, 0.25f, 0.25f,
      -0.625f, -0.875f, 0.25f, 0.25f,
      -0.750f, -0.625f, 0.25f, 0.25f,
   };
   static const float depth_far_vertices[12] = {
      -0.875f, -0.875f, 0.75f, 0.25f,
      -0.625f, -0.875f, 0.75f, 0.25f,
      -0.750f, -0.625f, 0.75f, 0.25f,
   };
   static uint32_t texels[64 * 64];
   static uint32_t fbo_readback[64 * 64];
   static uint32_t loop_readback[980 * 560];
   static uint32_t restart_readback[780 * 560];
   static uint32_t index_width_readback[264 * 150];
   static uint32_t depth_readback[264 * 150];


   memset(&frontend, 0, sizeof(frontend));
   memset(&options, 0, sizeof(options));
   memset(&drawable, 0, sizeof(drawable));
   screen = ps5_screen_create();
   if (!screen) {
      printf("[ps5-mesa] result=1 stage=screen\n");
      return 1;
   }
   frontend.screen = screen;
   frontend.get_param = frontend_get_param;

   memset(&resource_template, 0, sizeof(resource_template));
   resource_template.target = PIPE_TEXTURE_2D;
   resource_template.format = PIPE_FORMAT_R8G8B8A8_UNORM;
   resource_template.width0 = 1920;
   resource_template.height0 = 1080;
   resource_template.depth0 = 1;
   resource_template.array_size = 1;
   resource_template.nr_samples = 1;
   resource_template.nr_storage_samples = 1;
   resource_template.bind =
      PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET;
   drawable.target = screen->resource_create(screen, &resource_template);
   if (!drawable.target) {
      printf("[ps5-mesa] result=2 stage=drawable-resource\n");
      screen->destroy(screen);
      return 2;
   }
   drawable.visual.buffer_mask = ST_ATTACHMENT_FRONT_LEFT_MASK;
   drawable.visual.color_format = PIPE_FORMAT_R8G8B8A8_UNORM;
   drawable.visual.depth_stencil_format = PIPE_FORMAT_NONE;
   drawable.visual.accum_format = PIPE_FORMAT_NONE;
   drawable.visual.samples = 1;
   drawable.base.stamp = 1;
   drawable.base.ID = 1;
   drawable.base.fscreen = &frontend;
   drawable.base.visual = &drawable.visual;
   drawable.base.flush_front = probe_flush_front;
   drawable.base.validate = probe_validate;
   printf("[ps5-mesa] stage=query graphics=%u accelerated=%u\n",
          screen->caps.graphics, screen->caps.accelerated);
   printf("[ps5-mesa] nir-io vs=0x%x fs=0x%x intrinsic-bit=0x%x\n",
          screen->nir_options[MESA_SHADER_VERTEX]->io_options,
          screen->nir_options[MESA_SHADER_FRAGMENT]->io_options,
          nir_io_has_intrinsics);
   st_api_query_versions(&frontend, &options, &core, &compat, &es1, &es2);
   printf("[ps5-mesa] versions core=%d compat=%d es1=%d es2=%d\n",
          core, compat, es1, es2);

   memset(&attribs, 0, sizeof(attribs));
   attribs.profile = API_OPENGL_COMPAT;
   attribs.major = 2;
   attribs.minor = 0;
   attribs.visual = drawable.visual;
   attribs.options = options;
   st = st_api_create_context(&frontend, &attribs, &error, NULL);
   if (!st) {
      printf("[ps5-mesa] result=2 stage=context error=%d\n", error);
      st_screen_destroy(&frontend);
      pipe_resource_reference(&drawable.target, NULL);
      screen->destroy(screen);
      return 2;
   }
   printf("[ps5-mesa] context profile=compat version=%u glsl=%u\n",
          st->ctx->Version, st->ctx->Const.GLSLVersion);
   if (!st_api_make_current(st, &drawable.base, &drawable.base)) {
      printf("[ps5-mesa] result=3 stage=make-current\n");
      st_destroy_context(st);
      st_screen_destroy(&frontend);
      pipe_resource_reference(&drawable.target, NULL);
      screen->destroy(screen);
      return 3;
   }
   drawable_registered = true;
   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fs) ||
       link_program(vs, fs, &program)) {
      printf("[ps5-mesa] result=4 stage=glsl\n");
      if (program)
         _mesa_DeleteProgram(program);
      if (fs)
         _mesa_DeleteShader(fs);
      if (vs)
         _mesa_DeleteShader(vs);
      st_api_destroy_drawable(&drawable.base);
      st_api_make_current(NULL, NULL, NULL);
      st_destroy_context(st);
      st_screen_destroy(&frontend);
      pipe_resource_reference(&drawable.target, NULL);
      screen->destroy(screen);
      return 4;
   }
   printf("[ps5-mesa] stage=use-program\n");
   _mesa_UseProgram(program);
   sampler_location = _mesa_GetUniformLocation(program, "u_texture");
   depth_location = _mesa_GetUniformLocation(program, "u_depth");
   printf("[ps5-mesa] uniforms u_texture=%d u_depth=%d\n",
          sampler_location, depth_location);
   _mesa_Uniform1i(sampler_location, 0);
   _mesa_Uniform1f(depth_location, 0.0f);
   for (unsigned y = 0; y < 64; ++y) {
      for (unsigned x = 0; x < 64; ++x) {
         static const uint32_t colors[4] = {
            UINT32_C(0xff0000ff), UINT32_C(0xff00ff00),
            UINT32_C(0xffff0000), UINT32_C(0xffffffff),
         };
         texels[y * 64 + x] = colors[(y >= 32 ? 2 : 0) + (x >= 32)];
      }
   }
   _mesa_GenTextures(1, &texture);
   _mesa_BindTexture(GL_TEXTURE_2D, texture);
   _mesa_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   _mesa_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   _mesa_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   _mesa_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   _mesa_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   _mesa_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   printf("[ps5-mesa] stage=texture-upload-begin\n");
   _mesa_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA,
                    GL_UNSIGNED_BYTE, texels);
   printf("[ps5-mesa] stage=texture-upload-end error=0x%x\n",
          _mesa_GetError());
   _mesa_GenVertexArrays(2, vertex_arrays);
   _mesa_GenBuffers(2, vertex_buffers);
   _mesa_BindVertexArray(vertex_arrays[0]);
   _mesa_BindBuffer(GL_ARRAY_BUFFER, vertex_buffers[0]);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                    GL_STATIC_DRAW);
   _mesa_VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                             4 * sizeof(float), NULL);
   _mesa_EnableVertexAttribArray(0);
   _mesa_VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                             4 * sizeof(float),
                             (const GLvoid *)(uintptr_t)(2 * sizeof(float)));
   _mesa_EnableVertexAttribArray(1);
   _mesa_GenBuffers(1, &index_buffer);
   _mesa_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
   _mesa_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                    GL_STATIC_DRAW);
   printf("[ps5-mesa] stage=draw-elements-vao0\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_BindVertexArray(vertex_arrays[1]);
   _mesa_BindBuffer(GL_ARRAY_BUFFER, vertex_buffers[1]);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(wide_vertices), wide_vertices,
                    GL_STATIC_DRAW);
   _mesa_VertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE,
                             6 * sizeof(float), NULL);
   _mesa_EnableVertexAttribArray(0);
   _mesa_VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                             6 * sizeof(float),
                             (const GLvoid *)(uintptr_t)(4 * sizeof(float)));
   _mesa_EnableVertexAttribArray(1);
   _mesa_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
   printf("[ps5-mesa] stage=draw-elements-vao1 texture-nearest\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_BindVertexArray(vertex_arrays[0]);
   printf("[ps5-mesa] stage=draw-elements-vao0-reuse texture-nearest\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);

   _mesa_GenFramebuffers(1, &framebuffer);
   _mesa_GenRenderbuffers(1, &color_renderbuffer);
   _mesa_BindRenderbuffer(GL_RENDERBUFFER, color_renderbuffer);
   _mesa_RenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1920, 1080);
   _mesa_BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   _mesa_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_RENDERBUFFER, color_renderbuffer);
   framebuffer_status = _mesa_CheckFramebufferStatus(GL_FRAMEBUFFER);
   printf("[ps5-mesa] stage=fbo status=0x%x framebuffer=%u color=%u\n",
          framebuffer_status, framebuffer, color_renderbuffer);
   _mesa_Viewport(0, 0, 1920, 1080);
   _mesa_BindVertexArray(vertex_arrays[0]);
   _mesa_Uniform1f(depth_location, 0.0f);

   _mesa_BindBuffer(GL_ARRAY_BUFFER, vertex_buffers[0]);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(index_u16_vertices),
                    index_u16_vertices, GL_STATIC_DRAW);
   _mesa_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
   _mesa_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_u16_indices),
                    index_u16_indices, GL_STATIC_DRAW);
   printf("[ps5-mesa] stage=index-width-u16 fbo=0x%x crop=112,870,264,150 offset=2\n",
          framebuffer_status);
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT,
                      (const GLvoid *)(uintptr_t)sizeof(uint16_t));
   _mesa_Finish();
   index_u16_draw_status = ps5_context_last_draw_status(
      st->pipe, &index_u16_draw_calls);
   memset(index_width_readback, 0, sizeof(index_width_readback));
   _mesa_ReadPixels(112, 870, 264, 150, GL_RGBA, GL_UNSIGNED_BYTE,
                    index_width_readback);
   index_u16_error = _mesa_GetError();
   for (size_t i = 0;
        i < sizeof(index_width_readback) / sizeof(index_width_readback[0]); ++i)
      index_u16_nonzero += index_width_readback[i] != 0;
   index_u16_hash = probe_hash32(index_width_readback,
                                 sizeof(index_width_readback));
   printf("[ps5-mesa] index-width-u16 status=%d calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          index_u16_draw_status, index_u16_draw_calls, index_u16_nonzero,
          index_u16_hash, index_u16_error);

   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(index_u8_vertices),
                    index_u8_vertices, GL_STATIC_DRAW);
   _mesa_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_u8_indices),
                    index_u8_indices, GL_STATIC_DRAW);
   printf("[ps5-mesa] stage=index-width-u8 fbo=0x%x crop=712,870,264,150 offset=1\n",
          framebuffer_status);
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_BYTE,
                      (const GLvoid *)(uintptr_t)sizeof(uint8_t));
   _mesa_Finish();
   index_u8_draw_status = ps5_context_last_draw_status(
      st->pipe, &index_u8_draw_calls);
   memset(index_width_readback, 0, sizeof(index_width_readback));
   _mesa_ReadPixels(712, 870, 264, 150, GL_RGBA, GL_UNSIGNED_BYTE,
                    index_width_readback);
   index_u8_error = _mesa_GetError();
   for (size_t i = 0;
        i < sizeof(index_width_readback) / sizeof(index_width_readback[0]); ++i)
      index_u8_nonzero += index_width_readback[i] != 0;
   index_u8_hash = probe_hash32(index_width_readback,
                                sizeof(index_width_readback));
   printf("[ps5-mesa] index-width-u8 status=%d calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          index_u8_draw_status, index_u8_draw_calls, index_u8_nonzero,
          index_u8_hash, index_u8_error);

   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(index_u32_vertices),
                    index_u32_vertices, GL_STATIC_DRAW);
   _mesa_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_u32_indices),
                    index_u32_indices, GL_STATIC_DRAW);
   printf("[ps5-mesa] stage=index-width-u32 fbo=0x%x crop=1312,870,264,150 offset=4\n",
          framebuffer_status);
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT,
                      (const GLvoid *)(uintptr_t)sizeof(uint32_t));
   _mesa_Finish();
   index_u32_draw_status = ps5_context_last_draw_status(
      st->pipe, &index_u32_draw_calls);
   memset(index_width_readback, 0, sizeof(index_width_readback));
   _mesa_ReadPixels(1312, 870, 264, 150, GL_RGBA, GL_UNSIGNED_BYTE,
                    index_width_readback);
   index_u32_error = _mesa_GetError();
   for (size_t i = 0;
        i < sizeof(index_width_readback) / sizeof(index_width_readback[0]); ++i)
      index_u32_nonzero += index_width_readback[i] != 0;
   index_u32_hash = probe_hash32(index_width_readback,
                                 sizeof(index_width_readback));
   printf("[ps5-mesa] index-width-u32 status=%d calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          index_u32_draw_status, index_u32_draw_calls, index_u32_nonzero,
          index_u32_hash, index_u32_error);

   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(restart_baseline_vertices),
                    restart_baseline_vertices, GL_STATIC_DRAW);
   _mesa_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
   _mesa_BufferData(GL_ELEMENT_ARRAY_BUFFER,
                    sizeof(restart_baseline_indices),
                    restart_baseline_indices, GL_STATIC_DRAW);
   printf("[ps5-mesa] stage=primitive-restart-baseline fbo=0x%x crop=90,260,780,560\n",
          framebuffer_status);
   _mesa_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, NULL);
   _mesa_Finish();
   restart_baseline_draw_status = ps5_context_last_draw_status(
      st->pipe, &restart_baseline_draw_calls);
   memset(restart_readback, 0, sizeof(restart_readback));
   _mesa_ReadPixels(90, 260, 780, 560, GL_RGBA, GL_UNSIGNED_BYTE,
                    restart_readback);
   restart_baseline_error = _mesa_GetError();
   for (size_t i = 0;
        i < sizeof(restart_readback) / sizeof(restart_readback[0]); ++i)
      restart_baseline_nonzero += restart_readback[i] != 0;
   restart_baseline_hash = probe_hash32(restart_readback,
                                        sizeof(restart_readback));
   printf("[ps5-mesa] primitive-restart-baseline status=%d calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          restart_baseline_draw_status, restart_baseline_draw_calls,
          restart_baseline_nonzero, restart_baseline_hash,
          restart_baseline_error);

   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(restart_vertices),
                    restart_vertices, GL_STATIC_DRAW);
   _mesa_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(restart_indices),
                    restart_indices, GL_STATIC_DRAW);
   _mesa_PrimitiveRestartIndex(UINT16_MAX);
   _mesa_EnableClientState(GL_PRIMITIVE_RESTART_NV);
   printf("[ps5-mesa] stage=primitive-restart-elements fbo=0x%x crop=1050,260,780,560\n",
          framebuffer_status);
   _mesa_DrawElements(GL_TRIANGLE_STRIP, 7, GL_UNSIGNED_SHORT, NULL);
   _mesa_DisableClientState(GL_PRIMITIVE_RESTART_NV);
   _mesa_Finish();
   restart_draw_status = ps5_context_last_draw_status(
      st->pipe, &restart_draw_calls);
   memset(restart_readback, 0, sizeof(restart_readback));
   _mesa_ReadPixels(1050, 260, 780, 560, GL_RGBA, GL_UNSIGNED_BYTE,
                    restart_readback);
   restart_draw_error = _mesa_GetError();
   for (size_t i = 0;
        i < sizeof(restart_readback) / sizeof(restart_readback[0]); ++i)
      restart_draw_nonzero += restart_readback[i] != 0;
   restart_draw_hash = probe_hash32(restart_readback,
                                    sizeof(restart_readback));
   printf("[ps5-mesa] primitive-restart-elements status=%d calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          restart_draw_status, restart_draw_calls, restart_draw_nonzero,
          restart_draw_hash, restart_draw_error);

   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                    GL_STATIC_DRAW);
   _mesa_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                    GL_STATIC_DRAW);
   printf("[ps5-mesa] stage=line-loop-arrays\n");
   _mesa_DrawArrays(GL_LINE_LOOP, 0, 3);
   _mesa_Finish();
   direct_loop_draw_status = ps5_context_last_draw_status(
      st->pipe, &direct_loop_draw_calls);
   memset(loop_readback, 0, sizeof(loop_readback));
   _mesa_ReadPixels(470, 260, 980, 560, GL_RGBA, GL_UNSIGNED_BYTE,
                    loop_readback);
   direct_loop_error = _mesa_GetError();
   for (size_t i = 0; i < sizeof(loop_readback) / sizeof(loop_readback[0]); ++i)
      direct_loop_nonzero += loop_readback[i] != 0;
   direct_loop_hash = probe_hash32(loop_readback, sizeof(loop_readback));
   printf("[ps5-mesa] line-loop-arrays status=%d calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          direct_loop_draw_status, direct_loop_draw_calls,
          direct_loop_nonzero, direct_loop_hash, direct_loop_error);
   printf("[ps5-mesa] stage=line-loop-elements\n");
   _mesa_DrawElements(GL_LINE_LOOP, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_Finish();
   indexed_loop_draw_status = ps5_context_last_draw_status(
      st->pipe, &indexed_loop_draw_calls);
   memset(loop_readback, 0, sizeof(loop_readback));
   _mesa_ReadPixels(470, 260, 980, 560, GL_RGBA, GL_UNSIGNED_BYTE,
                    loop_readback);
   indexed_loop_error = _mesa_GetError();
   for (size_t i = 0; i < sizeof(loop_readback) / sizeof(loop_readback[0]); ++i)
      indexed_loop_nonzero += loop_readback[i] != 0;
   indexed_loop_hash = probe_hash32(loop_readback, sizeof(loop_readback));
   printf("[ps5-mesa] line-loop-elements status=%d calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          indexed_loop_draw_status, indexed_loop_draw_calls,
          indexed_loop_nonzero, indexed_loop_hash, indexed_loop_error);

   _mesa_GenRenderbuffers(1, &depth_renderbuffer);
   _mesa_BindRenderbuffer(GL_RENDERBUFFER, depth_renderbuffer);
   depth_float_extension = st->ctx->Extensions.ARB_depth_buffer_float;
   _mesa_RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT,
                             1920, 1080);
   _mesa_GetRenderbufferParameteriv(GL_RENDERBUFFER,
                                    GL_RENDERBUFFER_INTERNAL_FORMAT,
                                    &depth_internal_format);
   _mesa_GetRenderbufferParameteriv(GL_RENDERBUFFER,
                                    GL_RENDERBUFFER_DEPTH_SIZE,
                                    &depth_size);
   _mesa_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_RENDERBUFFER, depth_renderbuffer);
   depth_framebuffer_status = _mesa_CheckFramebufferStatus(GL_FRAMEBUFFER);
   depth_setup_error = _mesa_GetError();
   printf("[ps5-mesa] stage=depth-fbo requested=0x%x internal=0x%x bits=%d arb-depth-float=%u status=0x%x framebuffer=%u depth=%u error=0x%x\n",
          GL_DEPTH_COMPONENT, depth_internal_format, depth_size,
          depth_float_extension, depth_framebuffer_status, framebuffer,
          depth_renderbuffer, depth_setup_error);

   _mesa_DepthMask(GL_TRUE);
   _mesa_ClearDepth(1.0);
   _mesa_Clear(GL_DEPTH_BUFFER_BIT);
   _mesa_Enable(GL_DEPTH_TEST);
   _mesa_DepthFunc(GL_LESS);
   _mesa_BindVertexArray(vertex_arrays[0]);
   _mesa_BindBuffer(GL_ARRAY_BUFFER, vertex_buffers[0]);
   _mesa_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
   _mesa_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                    GL_STATIC_DRAW);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(depth_near_vertices),
                    depth_near_vertices, GL_STATIC_DRAW);
   _mesa_Uniform1f(depth_location, 0.25f);
   printf("[ps5-mesa] stage=depth-write-near crop=112,60,264,150 depth=0.25 color=red\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_Finish();
   depth_near_draw_status = ps5_context_last_draw_status(
      st->pipe, &depth_near_draw_calls);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(depth_far_vertices),
                    depth_far_vertices, GL_STATIC_DRAW);
   _mesa_Uniform1f(depth_location, 0.75f);
   printf("[ps5-mesa] stage=depth-write-far crop=112,60,264,150 depth=0.75 color=green\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_Finish();
   depth_far_draw_status = ps5_context_last_draw_status(
      st->pipe, &depth_far_draw_calls);
   memset(depth_readback, 0, sizeof(depth_readback));
   _mesa_ReadPixels(112, 60, 264, 150, GL_RGBA, GL_UNSIGNED_BYTE,
                    depth_readback);
   depth_write_error = _mesa_GetError();
   for (size_t i = 0;
        i < sizeof(depth_readback) / sizeof(depth_readback[0]); ++i)
      depth_write_nonzero += depth_readback[i] != 0;
   depth_write_hash = probe_hash32(depth_readback, sizeof(depth_readback));
   printf("[ps5-mesa] depth-write near-status=%d near-calls=%u far-status=%d far-calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          depth_near_draw_status, depth_near_draw_calls,
          depth_far_draw_status, depth_far_draw_calls,
          depth_write_nonzero, depth_write_hash, depth_write_error);

   _mesa_DepthMask(GL_TRUE);
   _mesa_ClearDepth(1.0);
   _mesa_Clear(GL_DEPTH_BUFFER_BIT);
   _mesa_DepthMask(GL_FALSE);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(depth_near_vertices),
                    depth_near_vertices, GL_STATIC_DRAW);
   _mesa_Uniform1f(depth_location, 0.25f);
   printf("[ps5-mesa] stage=depth-mask-near crop=112,60,264,150 depth=0.25 color=red\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_Finish();
   depth_mask_near_draw_status = ps5_context_last_draw_status(
      st->pipe, &depth_mask_near_draw_calls);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(depth_far_vertices),
                    depth_far_vertices, GL_STATIC_DRAW);
   _mesa_Uniform1f(depth_location, 0.75f);
   printf("[ps5-mesa] stage=depth-mask-far crop=112,60,264,150 depth=0.75 color=green\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_Finish();
   depth_mask_far_draw_status = ps5_context_last_draw_status(
      st->pipe, &depth_mask_far_draw_calls);
   memset(depth_readback, 0, sizeof(depth_readback));
   _mesa_ReadPixels(112, 60, 264, 150, GL_RGBA, GL_UNSIGNED_BYTE,
                    depth_readback);
   depth_mask_error = _mesa_GetError();
   for (size_t i = 0;
        i < sizeof(depth_readback) / sizeof(depth_readback[0]); ++i)
      depth_mask_nonzero += depth_readback[i] != 0;
   depth_mask_hash = probe_hash32(depth_readback, sizeof(depth_readback));
   printf("[ps5-mesa] depth-mask near-status=%d near-calls=%u far-status=%d far-calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          depth_mask_near_draw_status, depth_mask_near_draw_calls,
          depth_mask_far_draw_status, depth_mask_far_draw_calls,
          depth_mask_nonzero, depth_mask_hash, depth_mask_error);

   _mesa_Disable(GL_DEPTH_TEST);
   _mesa_DepthMask(GL_TRUE);
   _mesa_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_RENDERBUFFER, 0);
   post_depth_framebuffer_status =
      _mesa_CheckFramebufferStatus(GL_FRAMEBUFFER);
#ifdef PS5_PUBLIC_STENCIL_TEST
   _mesa_Clear(0);
   depth_detach_sync_error = _mesa_GetError();
   printf("[ps5-mesa] stage=depth-detach-sync-before-stencil error=0x%x\n",
          depth_detach_sync_error);
   _mesa_DeleteRenderbuffers(1, &depth_renderbuffer);
   depth_renderbuffer = 0;
   printf("[ps5-mesa] stage=depth-release-before-stencil\n");
   _mesa_GenRenderbuffers(1, &stencil_renderbuffer);
   _mesa_BindRenderbuffer(GL_RENDERBUFFER, stencil_renderbuffer);
   _mesa_RenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                             1920, 1080);
   _mesa_GetRenderbufferParameteriv(GL_RENDERBUFFER,
                                    GL_RENDERBUFFER_INTERNAL_FORMAT,
                                    &stencil_internal_format);
   _mesa_GetRenderbufferParameteriv(GL_RENDERBUFFER,
                                    GL_RENDERBUFFER_STENCIL_SIZE,
                                    &stencil_size);
   _mesa_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                 GL_RENDERBUFFER, stencil_renderbuffer);
   stencil_framebuffer_status = _mesa_CheckFramebufferStatus(GL_FRAMEBUFFER);
   stencil_setup_error = _mesa_GetError();
   printf("[ps5-mesa] stage=stencil-fbo requested=0x%x internal=0x%x bits=%d status=0x%x framebuffer=%u stencil=%u error=0x%x\n",
          GL_STENCIL_INDEX8, stencil_internal_format, stencil_size,
          stencil_framebuffer_status, framebuffer, stencil_renderbuffer,
          stencil_setup_error);

   _mesa_Enable(GL_STENCIL_TEST);
   _mesa_StencilMask(UINT32_C(0xff));
   _mesa_StencilFunc(GL_ALWAYS, 0x5a, UINT32_C(0xff));
   _mesa_StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(depth_near_vertices),
                    depth_near_vertices, GL_STATIC_DRAW);
   _mesa_Uniform1f(depth_location, 0.0f);
   printf("[ps5-mesa] stage=stencil-write crop=112,60,264,150 func=ALWAYS ref=5a ops=KEEP,KEEP,REPLACE color=red\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_Finish();
   stencil_write_draw_status = ps5_context_last_draw_status(
      st->pipe, &stencil_write_draw_calls);

   _mesa_StencilFunc(GL_EQUAL, 0x5a, UINT32_C(0xff));
   _mesa_StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(depth_far_vertices),
                    depth_far_vertices, GL_STATIC_DRAW);
   printf("[ps5-mesa] stage=stencil-pass crop=112,60,264,150 func=EQUAL ref=5a ops=KEEP,KEEP,KEEP color=green\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_Finish();
   stencil_pass_draw_status = ps5_context_last_draw_status(
      st->pipe, &stencil_pass_draw_calls);

   _mesa_StencilFunc(GL_EQUAL, 0x33, UINT32_C(0xff));
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(depth_near_vertices),
                    depth_near_vertices, GL_STATIC_DRAW);
   printf("[ps5-mesa] stage=stencil-reject crop=112,60,264,150 func=EQUAL ref=33 ops=KEEP,KEEP,KEEP color=red\n");
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   _mesa_Finish();
   stencil_reject_draw_status = ps5_context_last_draw_status(
      st->pipe, &stencil_reject_draw_calls);
   memset(depth_readback, 0, sizeof(depth_readback));
   _mesa_ReadPixels(112, 60, 264, 150, GL_RGBA, GL_UNSIGNED_BYTE,
                    depth_readback);
   stencil_readback_error = _mesa_GetError();
   for (size_t i = 0;
        i < sizeof(depth_readback) / sizeof(depth_readback[0]); ++i)
      stencil_readback_nonzero += depth_readback[i] != 0;
   stencil_readback_hash = probe_hash32(depth_readback,
                                        sizeof(depth_readback));
   printf("[ps5-mesa] stencil write-status=%d write-calls=%u pass-status=%d pass-calls=%u reject-status=%d reject-calls=%u nonzero=%zu hash=%08x error=0x%x\n",
          stencil_write_draw_status, stencil_write_draw_calls,
          stencil_pass_draw_status, stencil_pass_draw_calls,
          stencil_reject_draw_status, stencil_reject_draw_calls,
          stencil_readback_nonzero, stencil_readback_hash,
          stencil_readback_error);

   _mesa_Disable(GL_STENCIL_TEST);
   _mesa_FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                 GL_RENDERBUFFER, 0);
   post_stencil_framebuffer_status =
      _mesa_CheckFramebufferStatus(GL_FRAMEBUFFER);
#endif
   _mesa_BindVertexArray(vertex_arrays[0]);
   _mesa_BindBuffer(GL_ARRAY_BUFFER, vertex_buffers[0]);
   _mesa_BufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices,
                    GL_STATIC_DRAW);
   _mesa_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
   _mesa_BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                    GL_STATIC_DRAW);
   _mesa_Uniform1f(depth_location, 0.0f);
   _mesa_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, NULL);
   printf("[ps5-mesa] stage=finish\n");
   _mesa_Finish();
   memset(fbo_readback, 0, sizeof(fbo_readback));
   _mesa_ReadPixels(928, 508, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE,
                    fbo_readback);
   for (size_t i = 0; i < sizeof(fbo_readback) / sizeof(fbo_readback[0]); ++i)
      fbo_readback_nonzero += fbo_readback[i] != 0;
   fbo_readback_hash = probe_hash32(fbo_readback, sizeof(fbo_readback));
   printf("[ps5-mesa] fbo-readback nonzero=%zu hash=%08x first=%08x center=%08x\n",
          fbo_readback_nonzero, fbo_readback_hash, fbo_readback[0],
          fbo_readback[32 * 64 + 32]);
   _mesa_BindFramebuffer(GL_FRAMEBUFFER, 0);
   printf("[ps5-mesa] stage=draw-return\n");
   gl_error = _mesa_GetError();
   draw_status = ps5_context_last_draw_status(st->pipe, &draw_calls);
   if (ps5_resource_info(drawable.target, &target_address, NULL,
                         &target_allocation_size) == 0) {
      const uint32_t *words = target_address;
      size_t i;

      for (i = 0; i < target_allocation_size / sizeof(*words); ++i)
         target_nonzero += words[i] != 0;
   }
   printf("[ps5-mesa] draw status=%d calls=%u gl-error=0x%x target-nonzero=%zu\n",
          draw_status, draw_calls, gl_error, target_nonzero);
   passed = sampler_location >= 0 && depth_location >= 0 &&
#ifdef PS5_PUBLIC_STENCIL_TEST
      draw_status == 0 && draw_calls == 18 &&
#else
      draw_status == 0 && draw_calls == 15 &&
#endif
      framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
      depth_framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
      post_depth_framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
      depth_setup_error == GL_NO_ERROR &&
      depth_internal_format == GL_DEPTH_COMPONENT && depth_size == 32 &&
      !depth_float_extension &&
      index_u16_nonzero &&
      index_u16_nonzero == index_u8_nonzero &&
      index_u16_nonzero == index_u32_nonzero &&
      index_u16_hash == index_u8_hash &&
      index_u16_hash == index_u32_hash &&
      index_u16_draw_status == 0 && index_u16_draw_calls == 4 &&
      index_u8_draw_status == 0 && index_u8_draw_calls == 5 &&
      index_u32_draw_status == 0 && index_u32_draw_calls == 6 &&
      index_u16_error == GL_NO_ERROR &&
      index_u8_error == GL_NO_ERROR &&
      index_u32_error == GL_NO_ERROR &&
      direct_loop_nonzero &&
      direct_loop_nonzero == indexed_loop_nonzero &&
      direct_loop_hash == indexed_loop_hash &&
      direct_loop_draw_status == 0 && direct_loop_draw_calls == 9 &&
      indexed_loop_draw_status == 0 && indexed_loop_draw_calls == 10 &&
      direct_loop_error == GL_NO_ERROR &&
      indexed_loop_error == GL_NO_ERROR &&
      restart_baseline_draw_status == 0 &&
      restart_baseline_draw_calls == 7 &&
      restart_draw_status == 0 && restart_draw_calls == 8 &&
      restart_baseline_nonzero &&
      restart_baseline_nonzero == restart_draw_nonzero &&
      restart_baseline_hash == restart_draw_hash &&
      restart_baseline_error == GL_NO_ERROR &&
      restart_draw_error == GL_NO_ERROR &&
      depth_near_draw_status == 0 && depth_near_draw_calls == 11 &&
      depth_far_draw_status == 0 && depth_far_draw_calls == 12 &&
      depth_mask_near_draw_status == 0 &&
      depth_mask_near_draw_calls == 13 &&
      depth_mask_far_draw_status == 0 &&
      depth_mask_far_draw_calls == 14 &&
      depth_write_nonzero == 16320 && depth_mask_nonzero == 16320 &&
      depth_write_hash == UINT32_C(0x21c32545) &&
      depth_mask_hash == UINT32_C(0x29ca9ec5) &&
      depth_write_error == GL_NO_ERROR &&
      depth_mask_error == GL_NO_ERROR &&
#ifdef PS5_PUBLIC_STENCIL_TEST
      depth_detach_sync_error == GL_NO_ERROR &&
      stencil_framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
      post_stencil_framebuffer_status == GL_FRAMEBUFFER_COMPLETE &&
      stencil_setup_error == GL_NO_ERROR &&
      stencil_internal_format == GL_STENCIL_INDEX8 && stencil_size == 8 &&
      stencil_write_draw_status == 0 && stencil_write_draw_calls == 15 &&
      stencil_pass_draw_status == 0 && stencil_pass_draw_calls == 16 &&
      stencil_reject_draw_status == 0 && stencil_reject_draw_calls == 17 &&
      stencil_readback_nonzero == 16320 &&
      stencil_readback_hash == UINT32_C(0x29ca9ec5) &&
      stencil_readback_error == GL_NO_ERROR &&
#endif
      fbo_readback_nonzero && gl_error == GL_NO_ERROR && target_nonzero;
   _mesa_BindVertexArray(0);
   _mesa_DeleteVertexArrays(2, vertex_arrays);
   _mesa_DeleteBuffers(2, vertex_buffers);
   _mesa_DeleteBuffers(1, &index_buffer);
   _mesa_DeleteFramebuffers(1, &framebuffer);
   if (depth_renderbuffer)
      _mesa_DeleteRenderbuffers(1, &depth_renderbuffer);
#ifdef PS5_PUBLIC_STENCIL_TEST
   _mesa_DeleteRenderbuffers(1, &stencil_renderbuffer);
#endif
   _mesa_DeleteRenderbuffers(1, &color_renderbuffer);
   _mesa_DeleteTextures(1, &texture);
   _mesa_DeleteProgram(program);
   _mesa_DeleteShader(fs);
   _mesa_DeleteShader(vs);
   if (drawable_registered)
      st_api_destroy_drawable(&drawable.base);
   st_api_make_current(NULL, NULL, NULL);
   st_destroy_context(st);
   st_screen_destroy(&frontend);
   pipe_resource_reference(&drawable.target, NULL);
   screen->destroy(screen);
   printf("[ps5-mesa] result=%d\n", passed ? 0 : 5);
   return passed ? 0 : 5;
}
