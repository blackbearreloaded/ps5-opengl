// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

// Unmodified Sokol GL backend; EGL platform glue and deterministic public-API oracle.
#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
// The SDK carries Khronos headers through 4.6, but this consumer targets 3.3.
// Select Sokol's existing 3.3 fallback paths, as a 3.3-only GL loader would.
#undef GL_VERSION_4_2
#undef GL_VERSION_4_3
#define SOKOL_GFX_IMPL
#define SOKOL_GLCORE
#define SOKOL_EXTERNAL_GL_LOADER
#define SOKOL_DEBUG
#include "sokol_gfx.h"

static unsigned log_errors;
static void logger(const char *tag, uint32_t level, uint32_t item, const char *message,
                   uint32_t line, const char *file, void *user)
{
   (void)tag; (void)file; (void)user;
   log_errors += level <= 2;
   printf("[ps5-sokol] log level=%u item=%u line=%u %s\n", level, item, line, message ? message : "");
}

static int check(int ok, const char *stage)
{
   if (!ok) printf("[ps5-sokol] FAIL %s gl=0x%x egl=0x%x\n", stage, glGetError(), eglGetError());
   return ok;
}

static int render_frames(EGLDisplay display, EGLSurface surface)
{
   const float vertices[][4] = {{-.25f,-.5f,0,0},{.25f,-.5f,1,0},{.25f,.5f,1,1},{-.25f,.5f,0,1}};
   const uint16_t indices[] = {0,1,2,0,2,3};
   const unsigned char texels[] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
   unsigned char *pixels = malloc(640 * 480 * 4);
   GLuint framebuffer = 0, color = 0;
   int ok = check(pixels != NULL, "readback allocation"), all_frames = 1;
   glGenFramebuffers(1, &framebuffer);
   glGenRenderbuffers(1, &color);
   for (int frame = 0; frame < 3 && ok; ++frame) {
      const int scale = frame == 1 ? 2 : 1, width = 320 * scale, height = 240 * scale;
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
      glBindRenderbuffer(GL_RENDERBUFFER, color);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color);
      if (!check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "framebuffer")) { ok = 0; break; }
      sg_setup(&(sg_desc){.environment.defaults = {.color_format = SG_PIXELFORMAT_RGBA8,
         .depth_format = SG_PIXELFORMAT_NONE, .sample_count = 1}, .logger.func = logger});
      sg_buffer vertex = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(vertices)});
      sg_buffer index = sg_make_buffer(&(sg_buffer_desc){.usage.index_buffer = true, .data = SG_RANGE(indices)});
      sg_buffer instance = sg_make_buffer(&(sg_buffer_desc){.size = 4 * sizeof(float), .usage.dynamic_update = true});
      sg_image image = sg_make_image(&(sg_image_desc){.width = 2, .height = 2,
         .pixel_format = SG_PIXELFORMAT_RGBA8, .data.mip_levels[0] = SG_RANGE(texels)});
      sg_view view = sg_make_view(&(sg_view_desc){.texture.image = image});
      sg_sampler sampler = sg_make_sampler(&(sg_sampler_desc){.min_filter = SG_FILTER_NEAREST,
         .mag_filter = SG_FILTER_NEAREST, .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE});
      sg_shader shader = sg_make_shader(&(sg_shader_desc){
         .vertex_func.source = "#version 330 core\nlayout(location=0) in vec2 position;\n"
            "layout(location=1) in vec2 texcoord; layout(location=2) in vec2 offset;\n"
            "out vec2 uv; void main(){gl_Position=vec4(position+offset,0,1);uv=texcoord;}\n",
         .fragment_func.source = "#version 330 core\nin vec2 uv; uniform sampler2D tex; uniform vec4 tint;\n"
            "out vec4 color; void main(){color=texture(tex,uv)*tint;}\n",
         .attrs = {{.glsl_name="position"},{.glsl_name="texcoord"},{.glsl_name="offset"}},
         .uniform_blocks[0] = {.stage = SG_SHADERSTAGE_FRAGMENT, .size = 4 * sizeof(float),
            .glsl_uniforms[0] = {.type = SG_UNIFORMTYPE_FLOAT4, .glsl_name = "tint"}},
         .views[0].texture = {.stage = SG_SHADERSTAGE_FRAGMENT, .image_type = SG_IMAGETYPE_2D},
         .samplers[0].stage = SG_SHADERSTAGE_FRAGMENT,
         .texture_sampler_pairs[0] = {.stage = SG_SHADERSTAGE_FRAGMENT, .glsl_name = "tex"}
      });
      sg_pipeline pipeline = sg_make_pipeline(&(sg_pipeline_desc){.shader = shader,
         .layout = {.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE,
            .attrs = {{.format=SG_VERTEXFORMAT_FLOAT2}, {.format=SG_VERTEXFORMAT_FLOAT2},
                      {.buffer_index=1, .format=SG_VERTEXFORMAT_FLOAT2}}},
         .index_type = SG_INDEXTYPE_UINT16,
         .colors[0].blend = {.enabled=true, .src_factor_rgb=SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb=SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha=SG_BLENDFACTOR_ZERO, .dst_factor_alpha=SG_BLENDFACTOR_ONE}
      });
      ok = check(sg_isvalid() && sg_query_buffer_state(vertex) == SG_RESOURCESTATE_VALID &&
         sg_query_buffer_state(index) == SG_RESOURCESTATE_VALID && sg_query_buffer_state(instance) == SG_RESOURCESTATE_VALID &&
         sg_query_image_state(image) == SG_RESOURCESTATE_VALID && sg_query_view_state(view) == SG_RESOURCESTATE_VALID &&
         sg_query_sampler_state(sampler) == SG_RESOURCESTATE_VALID && sg_query_shader_state(shader) == SG_RESOURCESTATE_VALID &&
         sg_query_pipeline_state(pipeline) == SG_RESOURCESTATE_VALID && !log_errors, "renderer resources");
      if (ok) {
         const float offsets[] = {-.5f,0,.5f,0};
         const float tint[] = {frame == 1 ? .5f : 1,1,1,.5f};
         sg_update_buffer(instance, &SG_RANGE(offsets));
         sg_begin_pass(&(sg_pass){.action.colors[0] = {.load_action=SG_LOADACTION_CLEAR, .clear_value={0,0,1,1}},
            .swapchain = {.width=width, .height=height, .sample_count=1,
               .color_format=SG_PIXELFORMAT_RGBA8, .depth_format=SG_PIXELFORMAT_NONE, .gl.framebuffer=framebuffer}});
         sg_apply_pipeline(pipeline);
         sg_apply_scissor_rect(64 * scale, 0, 256 * scale, height, false);
         sg_apply_bindings(&(sg_bindings){.vertex_buffers={vertex,instance}, .index_buffer=index,
            .views[0]=view, .samplers[0]=sampler});
         sg_apply_uniforms(0, &SG_RANGE(tint));
         sg_draw(0, 6, 2);
         sg_end_pass();
         sg_commit();
         glFinish();
         glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
         glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
         unsigned mismatches = 0;
         for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            int expected[4] = {0,0,255,255};
            int left = x >= 64 * scale && x < 120 * scale;
            int right = x >= 200 * scale && x < 280 * scale;
            if ((left || right) && y >= 60 * scale && y < 180 * scale) {
               int tx = x >= (right ? 240 : 80) * scale, ty = y >= 120 * scale;
               const unsigned char *texel = texels + (ty * 2 + tx) * 4;
               expected[0] = texel[0] * tint[0] * .5f + .5f;
               expected[1] = texel[1] * .5f + .5f;
               expected[2] = texel[2] * .5f + 127.5f + .5f;
            }
            for (int c = 0; c < 4; ++c) if (abs(pixels[(y * width + x) * 4 + c] - expected[c]) > 2) {
               if (mismatches < 4) printf("[ps5-sokol] pixel=%d,%d c=%d got=%u expected=%d\n",
                  x,y,c,pixels[(y * width + x) * 4 + c],expected[c]);
               ++mismatches;
            }
         }
         glDisable(GL_SCISSOR_TEST);
         glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
         glBlitFramebuffer(0,0,width,height,0,0,1280,960,GL_COLOR_BUFFER_BIT,GL_NEAREST);
         ok = check(glGetError() == GL_NO_ERROR && eglSwapBuffers(display, surface), "render/readback/present");
         all_frames &= !mismatches && !log_errors;
         printf("[ps5-sokol] frame=%d scale=%d components=%d mismatches=%u logs=%u %s\n",
            frame,scale,width*height*4,mismatches,log_errors,ok && !mismatches && !log_errors ? "PASS":"FAIL");
         sg_reset_state_cache();
      }
      sg_shutdown();
      ok &= check(glGetError() == GL_NO_ERROR && !log_errors, "renderer cleanup");
   }
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   glDeleteFramebuffers(1, &framebuffer);
   glDeleteRenderbuffers(1, &color);
   free(pixels);
   return check(glGetError() == GL_NO_ERROR, "GL cleanup") && ok && all_frames;
}

int main(void)
{
#ifdef PS5_SOKOL_HOST_REFERENCE
   const EGLint surface_type = EGL_PBUFFER_BIT;
#else
   const EGLint surface_type = EGL_WINDOW_BIT;
#endif
   const EGLint attributes[] = {EGL_SURFACE_TYPE, surface_type, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
   const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR,3,EGL_CONTEXT_MINOR_VERSION_KHR,3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,EGL_NONE};
   EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL;
   EGLint count = 0;
   int result = 1;
   if (check(display != EGL_NO_DISPLAY && eglInitialize(display,NULL,NULL) && eglBindAPI(EGL_OPENGL_API) &&
      eglChooseConfig(display,attributes,&config,1,&count) && count == 1, "EGL initialization")) {
#ifdef PS5_SOKOL_HOST_REFERENCE
      const EGLint pbuffer[] = {EGL_WIDTH,1920,EGL_HEIGHT,1080,EGL_NONE};
      surface = eglCreatePbufferSurface(display,config,pbuffer);
#else
      surface = eglCreateWindowSurface(display,config,(EGLNativeWindowType)0,NULL);
#endif
      context = eglCreateContext(display,config,EGL_NO_CONTEXT,context_attributes);
      if (check(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT &&
         eglMakeCurrent(display,surface,surface,context), "EGL context")) {
         printf("[ps5-sokol] upstream=48c85905aeaa1350feb17515961aecb6c75447d8 GL=%s renderer=%s\n",
            glGetString(GL_VERSION),glGetString(GL_RENDERER));
         result = render_frames(display,surface) ? 0 : 1;
      }
   }
   EGLBoolean cleanup = EGL_TRUE;
   if (display != EGL_NO_DISPLAY) {
      cleanup &= eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
      if (context != EGL_NO_CONTEXT) cleanup &= eglDestroyContext(display,context);
      if (surface != EGL_NO_SURFACE) cleanup &= eglDestroySurface(display,surface);
      cleanup &= eglTerminate(display);
   }
   if (!check(cleanup && eglGetError() == EGL_SUCCESS, "EGL cleanup")) result = 1;
   printf("[ps5-sokol] finished status=%d\n", result);
   return result;
}
