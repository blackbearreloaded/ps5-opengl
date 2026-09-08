// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

struct limit_check {
   GLenum name;
   GLint minimum;
   const char *label;
};

struct limit64_check {
   GLenum name;
   GLint64 minimum;
   const char *label;
};

int
main(void)
{
   static const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_NONE,
   };
   static const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
   static const struct limit_check limits[] = {
      {GL_MAX_TEXTURE_SIZE, 8192, "texture-2d"},
      {GL_MAX_CUBE_MAP_TEXTURE_SIZE, 2048, "texture-cube"},
      {GL_MAX_3D_TEXTURE_SIZE, 256, "texture-3d"},
      {GL_MAX_ARRAY_TEXTURE_LAYERS, 256, "texture-array"},
      {GL_MAX_RECTANGLE_TEXTURE_SIZE, 1024, "texture-rectangle"},
      {GL_MAX_RENDERBUFFER_SIZE, 1024, "renderbuffer"},
      {GL_SUBPIXEL_BITS, 4, "subpixel-bits"},
      {GL_MAX_TEXTURE_IMAGE_UNITS, 16, "fragment-textures"},
      {GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, 16, "vertex-textures"},
      {GL_MAX_GEOMETRY_TEXTURE_IMAGE_UNITS, 16, "geometry-textures"},
      {GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, 48, "combined-textures"},
      {GL_MAX_VERTEX_ATTRIBS, 16, "vertex-attribs"},
      {GL_MAX_VERTEX_UNIFORM_COMPONENTS, 1024, "vertex-uniform-components"},
      {GL_MAX_FRAGMENT_UNIFORM_COMPONENTS, 1024,
       "fragment-uniform-components"},
      {GL_MAX_GEOMETRY_UNIFORM_COMPONENTS, 1024,
       "geometry-uniform-components"},
      {GL_MAX_VARYING_COMPONENTS, 60, "varyings"},
      {GL_MAX_VERTEX_OUTPUT_COMPONENTS, 64, "vertex-output-components"},
      {GL_MAX_FRAGMENT_INPUT_COMPONENTS, 128, "fragment-input-components"},
      {GL_MAX_GEOMETRY_INPUT_COMPONENTS, 64, "geometry-input-components"},
      {GL_MAX_GEOMETRY_OUTPUT_COMPONENTS, 128,
       "geometry-output-components"},
      {GL_MAX_DRAW_BUFFERS, 8, "draw-buffers"},
      {GL_MAX_COLOR_ATTACHMENTS, 8, "color-attachments"},
      {GL_MAX_VERTEX_UNIFORM_BLOCKS, 12, "vertex-ubo"},
      {GL_MAX_FRAGMENT_UNIFORM_BLOCKS, 12, "fragment-ubo"},
      {GL_MAX_GEOMETRY_UNIFORM_BLOCKS, 12, "geometry-ubo"},
      {GL_MAX_COMBINED_UNIFORM_BLOCKS, 36, "combined-ubo"},
      {GL_MAX_UNIFORM_BLOCK_SIZE, 16384, "ubo-size"},
      {GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, 4, "xfb-buffers"},
      {GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS, 4, "xfb-separate"},
      {GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS, 64,
       "xfb-interleaved"},
      {GL_MAX_GEOMETRY_OUTPUT_VERTICES, 256, "geometry-vertices"},
      {GL_MAX_GEOMETRY_TOTAL_OUTPUT_COMPONENTS, 1024,
       "geometry-components"},
      {GL_MAX_CLIP_DISTANCES, 8, "clip-distances"},
      {GL_MAX_SAMPLES, 4, "samples"},
      {GL_MAX_SAMPLE_MASK_WORDS, 1, "sample-mask-words"},
      {GL_MAX_COLOR_TEXTURE_SAMPLES, 4, "color-texture-samples"},
      {GL_MAX_DEPTH_TEXTURE_SAMPLES, 4, "depth-texture-samples"},
      {GL_MAX_INTEGER_SAMPLES, 4, "integer-samples"},
      {GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, 1, "dual-source"},
      {GL_MAX_TEXTURE_BUFFER_SIZE, 65536, "texture-buffer"},
      {GL_MAX_UNIFORM_BUFFER_BINDINGS, 36, "ubo-bindings"},
      {GL_NUM_COMPRESSED_TEXTURE_FORMATS, 4, "compressed-formats"},
   };
   static const struct limit64_check limits64[] = {
      {GL_MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS, 50176,
       "combined-vertex-uniform-components"},
      {GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS, 50176,
       "combined-fragment-uniform-components"},
      {GL_MAX_COMBINED_GEOMETRY_UNIFORM_COMPONENTS, 50176,
       "combined-geometry-uniform-components"},
      {GL_MAX_SERVER_WAIT_TIMEOUT, 0, "server-wait-timeout"},
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL;
   EGLint count = 0;
   GLint major = 0, minor = 0, profile = 0, flags = -1;
   GLint extension_count = 0, alignment = 0;
   GLint min_texel_offset = 0, max_texel_offset = 0;
   GLint viewport[2] = {0, 0};
   GLfloat lod_bias = 0.0f;
   GLfloat point_range[2] = {0.0f, 0.0f};
   GLfloat aliased_line_range[2] = {0.0f, 0.0f};
   GLfloat smooth_line_range[2] = {0.0f, 0.0f};
   const GLubyte *vendor = NULL, *renderer = NULL, *version = NULL;
   unsigned matching = 0, matching64 = 0, valid_extensions = 0;
   unsigned query_errors = 0;
   GLenum extension_error = GL_NO_ERROR, invalid_index_error = GL_NO_ERROR;
   int current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, NULL, NULL) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context))
      goto cleanup;
   current = 1;

   vendor = glGetString(GL_VENDOR);
   renderer = glGetString(GL_RENDERER);
   version = glGetString(GL_VERSION);
   glGetIntegerv(GL_MAJOR_VERSION, &major);
   glGetIntegerv(GL_MINOR_VERSION, &minor);
   glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
   glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
   glGetIntegerv(GL_NUM_EXTENSIONS, &extension_count);
   glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
   for (unsigned i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i) {
      GLint value = 0;

      glGetIntegerv(limits[i].name, &value);
      query_errors += glGetError() != GL_NO_ERROR;
      matching += value >= limits[i].minimum;
      if (value < limits[i].minimum)
         printf("[ps5-egl-core33-limits] limit=%s value=%d minimum=%d\n",
                limits[i].label, value, limits[i].minimum);
   }
   for (unsigned i = 0; i < sizeof(limits64) / sizeof(limits64[0]); ++i) {
      GLint64 value = -1;

      glGetInteger64v(limits64[i].name, &value);
      query_errors += glGetError() != GL_NO_ERROR;
      matching64 += value >= limits64[i].minimum;
      if (value < limits64[i].minimum)
         printf("[ps5-egl-core33-limits] limit=%s value=%lld minimum=%lld\n",
                limits64[i].label, (long long)value,
                (long long)limits64[i].minimum);
   }
   glGetIntegerv(GL_MIN_PROGRAM_TEXEL_OFFSET, &min_texel_offset);
   glGetIntegerv(GL_MAX_PROGRAM_TEXEL_OFFSET, &max_texel_offset);
   glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport);
   glGetFloatv(GL_MAX_TEXTURE_LOD_BIAS, &lod_bias);
   glGetFloatv(GL_POINT_SIZE_RANGE, point_range);
   glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, aliased_line_range);
   glGetFloatv(GL_SMOOTH_LINE_WIDTH_RANGE, smooth_line_range);
   query_errors += glGetError() != GL_NO_ERROR;
   for (GLint i = 0; i < extension_count; ++i)
      valid_extensions += glGetStringi(GL_EXTENSIONS, i) != NULL;

   while (glGetError() != GL_NO_ERROR)
      ;
   passed = glGetString(GL_EXTENSIONS) == NULL;
   extension_error = glGetError();
   passed &= glGetStringi(GL_EXTENSIONS, extension_count) == NULL;
   invalid_index_error = glGetError();
   passed &= vendor && renderer && version &&
             strcmp((const char *)vendor, "PS5 homebrew") == 0 &&
             strcmp((const char *)renderer, "PS5 AGC") == 0 &&
             strncmp((const char *)version, "3.3 ", 4) == 0 &&
             major == 3 && minor == 3 &&
             (profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0 &&
             (profile & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) == 0 &&
             flags == 0 && extension_count > 0 &&
             valid_extensions == (unsigned)extension_count &&
             extension_error == GL_INVALID_ENUM &&
             invalid_index_error == GL_INVALID_VALUE &&
             matching == sizeof(limits) / sizeof(limits[0]) &&
             matching64 == sizeof(limits64) / sizeof(limits64[0]) &&
             query_errors == 0 &&
             min_texel_offset <= -8 && max_texel_offset >= 7 &&
             viewport[0] >= 1920 && viewport[1] >= 1080 &&
             lod_bias >= 2.0f &&
             point_range[0] <= 1.0f && point_range[1] >= 1.0f &&
             aliased_line_range[0] <= 1.0f &&
             aliased_line_range[1] >= 1.0f &&
             smooth_line_range[0] <= 1.0f &&
             smooth_line_range[1] >= 1.0f &&
             alignment >= 16 && (alignment & (alignment - 1)) == 0 &&
             glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-core33-limits] version=%d.%d profile=0x%x flags=0x%x "
          "extensions=%u/%d limits=%u/%zu limits64=%u/%zu alignment=%d "
          "texel=%d..%d viewport=%dx%d errors=%u result=%d\n",
          major, minor, profile, flags, valid_extensions, extension_count,
          matching, sizeof(limits) / sizeof(limits[0]), matching64,
          sizeof(limits64) / sizeof(limits64[0]), alignment,
          min_texel_offset, max_texel_offset, viewport[0], viewport[1],
          query_errors,
          passed ? 0 : 1);

cleanup:
   if (current) {
      cleanup_ok &= glGetError() == GL_NO_ERROR;
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_ok &= eglGetError() == EGL_SUCCESS;
   passed &= cleanup_ok;
   printf("[ps5-egl-core33-limits] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
