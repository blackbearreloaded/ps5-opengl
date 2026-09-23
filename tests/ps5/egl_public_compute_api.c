// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Private compute-API candidate only, not release or conformance coverage.
 * All shader execution and resource binding goes through normal GL entrypoints.
 * The internal context is used only to observe native submission results. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
extern int ps5_egl_current_compute_status(unsigned *);
#include "ps5_screen.h"

#define TAG "[ps5-egl-compute-api] "
#define GUARD UINT32_C(0xcdcdcdcd)

static int
check_gl(const char *step)
{
   GLenum error;
   int ok = 1;
   while ((error = glGetError()) != GL_NO_ERROR) {
      printf(TAG "%s GL error=%x\n", step, error);
      ok = 0;
   }
   printf(TAG "%s %s\n", step, ok ? "ok" : "FAILED");
   return ok;
}

static int
has_extension(const char *name)
{
   GLint count = 0;
   glGetIntegerv(GL_NUM_EXTENSIONS, &count);
   for (GLint i = 0; i < count; ++i) {
      const char *extension = (const char *)glGetStringi(GL_EXTENSIONS, i);
      if (extension && !strcmp(extension, name))
         return 1;
   }
   printf(TAG "missing extension %s\n", name);
   return 0;
}

static int
check_egl(const char *step, EGLBoolean result)
{
   EGLint error = eglGetError();
   printf(TAG "%s result=%u EGL error=%x\n", step, result, error);
   return result == EGL_TRUE && error == EGL_SUCCESS;
}

static int
compile_program(const char *name, const char *source, GLuint *shader, GLuint *program)
{
   GLint compiled = 0, linked = 0;
   char log[4096] = {0};
   *shader = glCreateShader(GL_COMPUTE_SHADER);
   if (!check_gl("create shader") || !*shader)
      return 0;
   glShaderSource(*shader, 1, &source, NULL);
   printf(TAG "%s compile begin\n", name);
   glCompileShader(*shader);
   glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);
   glGetShaderInfoLog(*shader, sizeof(log), NULL, log);
   printf(TAG "%s compile=%d log=%s\n", name, compiled, log);
   if (!check_gl("compile") || !compiled)
      return 0;
   *program = glCreateProgram();
   if (!check_gl("create program") || !*program)
      return 0;
   glAttachShader(*program, *shader);
   printf(TAG "%s link begin\n", name);
   glLinkProgram(*program);
   glGetProgramiv(*program, GL_LINK_STATUS, &linked);
   glGetProgramInfoLog(*program, sizeof(log), NULL, log);
   printf(TAG "%s link=%d log=%s\n", name, linked, log);
   return check_gl("link") && linked;
}

static int
reset_output(GLuint buffer)
{
   uint32_t words[80];
   for (unsigned i = 0; i < 80; ++i)
      words[i] = GUARD;
   glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
   glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(words), words, GL_DYNAMIC_DRAW);
   glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, buffer, 32, 16);
   return check_gl("reset guarded output");
}

static int
check_buffer(const char *name, GLenum target, GLuint buffer, uint32_t expected)
{
   uint32_t words[80] = {0};
   printf(TAG "%s readback begin\n", name);
   glBindBuffer(target, buffer);
   glGetBufferSubData(target, 0, sizeof(words), words);
   if (!check_gl("guarded readback"))
      return 0;
   for (unsigned i = 0; i < 80; ++i) {
      uint32_t wanted = i == 8 ? expected : GUARD;
      if (words[i] != wanted) {
         printf(TAG "%s word=%u actual=%08x expected=%08x\n",
                name, i, words[i], wanted);
         return 0;
      }
   }
   printf(TAG "%s value=%08x guards=79/79 PASS\n", name, expected);
   return 1;
}

static int
finish_dispatch(const char *name, unsigned expected_dispatches, GLbitfield barriers)
{
   unsigned dispatches = 0;
   if (!check_gl("dispatch"))
      return 0;
   glMemoryBarrier(barriers);
   if (!check_gl("barrier"))
      return 0;
   glFinish();
   int status = ps5_egl_current_compute_status(&dispatches);
   printf(TAG "%s native status=%d dispatches=%u expected=%u\n",
          name, status, dispatches, expected_dispatches);
   return check_gl("dispatch finish") && !status && dispatches == expected_dispatches;
}

static int
dispatch_checked(const char *name, unsigned expected_dispatches, GLbitfield barriers)
{
   printf(TAG "%s dispatch begin\n", name);
   glDispatchCompute(1, 1, 1);
   return finish_dispatch(name, expected_dispatches, barriers);
}

/* Only the bounded 192-ID, 64-shared and 3-command-word fixtures use these. */
static int
reset_results(GLuint buffer, unsigned count)
{
   uint32_t words[208];
   if (!count || count > 192)
      return 0;
   for (unsigned i = 0; i < count + 16; ++i)
      words[i] = GUARD;
   glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
   glBufferData(GL_SHADER_STORAGE_BUFFER, (count + 16) * sizeof(*words), words, GL_DYNAMIC_DRAW);
   glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, buffer, 32, count * sizeof(*words));
   return check_gl("reset array output");
}

static int
check_words(const char *name, GLuint buffer, const uint32_t *expected, unsigned count)
{
   uint32_t words[208] = {0};
   if (count > 208)
      return 0;
   printf(TAG "%s readback begin\n", name);
   glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
   glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, count * sizeof(*words), words);
   if (!check_gl("array readback"))
      return 0;
   for (unsigned i = 0; i < count; ++i) {
      if (words[i] != expected[i]) {
         printf(TAG "%s word=%u actual=%08x expected=%08x\n", name, i, words[i], expected[i]);
         return 0;
      }
   }
   printf(TAG "%s words=%u PASS\n", name, count);
   return 1;
}

static int
reject_indirect(const char *name, GLintptr offset,
                GLenum expected_error, unsigned expected_dispatches)
{
   unsigned dispatches = 0;
   if (!check_gl("indirect negative setup"))
      return 0;
   printf(TAG "%s indirect offset=%ld begin\n", name, (long)offset);
   glDispatchComputeIndirect(offset);
   GLenum error = glGetError();
   glFinish();
   int status = ps5_egl_current_compute_status(&dispatches);
   printf(TAG "%s error=%x expected=%x status=%d dispatches=%u expected=%u\n",
          name, error, expected_error, status, dispatches, expected_dispatches);
   return check_gl("indirect negative finish") && error == expected_error &&
          !status && dispatches == expected_dispatches;
}

int
main(void)
{
   static const char *source =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "layout(local_size_x=1, local_size_y=1, local_size_z=1) in;\n"
      "uniform uint addend;\n"
      "layout(std140) uniform Input { uint input_value; };\n"
      "layout(std430) buffer Output { uint result; };\n"
      "void main() { result = input_value + addend; }\n";
   static const char *image_source =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_image_load_store : require\n"
      "layout(local_size_x=1) in;\n"
      "layout(r32f) uniform writeonly image2D destination;\n"
      "uniform float value;\n"
      "void main() { imageStore(destination, ivec2(0), vec4(value, 0, 0, 1)); }\n";
   static const char *sample_source =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "layout(local_size_x=1) in;\n"
      "uniform sampler2D source_texture;\n"
      "layout(std430) buffer Output { uint result; };\n"
      "void main() { result = floatBitsToUint(texture(source_texture, vec2(0.5)).x); }\n";
   static const char *atomic_source =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "#extension GL_ARB_shader_atomic_counters : require\n"
      "layout(local_size_x=1) in;\n"
      "layout(binding=7, offset=12) uniform atomic_uint counter;\n"
      "layout(std430) buffer Output { uint result; };\n"
      "void main() { result = atomicCounterIncrement(counter); }\n";
   static const char *ids_source =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "layout(local_size_x=4, local_size_y=2, local_size_z=2) in;\n"
      "layout(std430) buffer Output { uint results[]; };\n"
      "void main() {\n"
      "  uvec3 p = gl_WorkGroupID * gl_WorkGroupSize + gl_LocalInvocationID;\n"
      "  uint index = p.x + 8u * p.y + 48u * p.z;\n"
      "  uint local_index = gl_LocalInvocationID.x + 4u * (gl_LocalInvocationID.y + 2u * gl_LocalInvocationID.z);\n"
      "  if (index < 192u) results[index] =\n"
      "    any(notEqual(p, gl_GlobalInvocationID)) || local_index != gl_LocalInvocationIndex\n"
      "    ? 0xbad00000u : 3u * index + 17u + 7u * gl_NumWorkGroups.x +\n"
      "      11u * gl_NumWorkGroups.y + 13u * gl_NumWorkGroups.z;\n"
      "}\n";
   static const char *shared_source =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "layout(local_size_x=64) in;\n"
      "shared uint values[64];\n"
      "layout(std430) buffer Output { uint results[]; };\n"
      "void main() {\n"
      "  uint id = gl_LocalInvocationIndex;\n"
      "  values[id] = 3u * id + 17u;\n"
      "  barrier();\n"
      "  results[id] = values[id ^ 32u];\n"
      "}\n";
   static const char *commands_source =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "layout(local_size_x=3) in;\n"
      "layout(std430) buffer Output { uint commands[]; };\n"
      "void main() { uint id = gl_LocalInvocationIndex; commands[id] = id == 1u ? 3u : 2u; }\n";
   static const char *normalized_store_template =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_image_load_store : require\n"
      "layout(local_size_x=1) in;\n"
      "layout(%s) uniform writeonly image2D destination;\n"
      "uniform vec4 value;\n"
      "void main() { imageStore(destination, ivec2(1, 1), value); }\n";
   static const char *normalized_load_template =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_image_load_store : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "layout(local_size_x=1) in;\n"
      "layout(%s) uniform readonly image2D source_image;\n"
      "layout(std430) buffer Output { uvec4 result; };\n"
      "void main() { result = floatBitsToUint(imageLoad(source_image, ivec2(1, 1))); }\n";
   static const struct {
      const char *name, *layout;
      GLenum internal_format, raw_type;
      unsigned endpoint, levels;
   } normalized_formats[] = {
      {"RGBA16", "rgba16", GL_RGBA16, GL_UNSIGNED_SHORT, 0xffffu, 1},
      {"RGBA8 mipmapped-linear", "rgba8", GL_RGBA8, GL_UNSIGNED_BYTE, 0xffu, 2},
      {"RGBA8 base-only tiled", "rgba8", GL_RGBA8, GL_UNSIGNED_BYTE, 0xffu, 1},
   };
   static const char *capacity_source =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "layout(local_size_x=1) in;\n"
      "layout(std430) buffer Capacity { uint words[]; } capacity;\n"
      "layout(std430) buffer Report { uint length; } report;\n"
      "void main() {\n"
      "  uint count = uint(capacity.words.length());\n"
      "  report.length = count;\n"
      "  if (count == 33554432u) {\n"
      "    capacity.words[0] = 0x13579bdfu;\n"
      "    capacity.words[33554431] = 0x2468ace0u;\n"
      "  }\n"
      "}\n";
   static const char *robust_source =
      "#version 330\n"
      "#extension GL_ARB_compute_shader : require\n"
      "#extension GL_ARB_shader_storage_buffer_object : require\n"
      "layout(local_size_x=1) in;\n"
      "uniform uint index;\n"
      "layout(std430) buffer Data { uint words[]; } data;\n"
      "layout(std430) buffer Report { uvec2 result; } report;\n"
      "void main() { uint value=data.words[index]; data.words[index]=0x2468ace0u;"
      " report.result=uvec2(value,uint(data.words.length())); }\n";
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   const EGLint surface_attributes[] = {
      EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE,
   };
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL;
   EGLint count = 0;
   GLuint shader = 0, program = 0, buffers[2] = {0, 0};
   GLuint extra_shaders[3] = {0}, extra_programs[3] = {0};
   GLuint grid_shaders[3] = {0}, grid_programs[3] = {0}, grid_buffers[2] = {0};
   GLuint normalized_shaders[3][2] = {{0}}, normalized_programs[3][2] = {{0}};
   GLuint normalized_textures[3] = {0};
   GLuint capacity_shader = 0, capacity_program = 0, capacity_buffer = 0;
   GLuint robust_shader = 0, robust_program = 0;
   GLuint texture = 0, atomic_buffer = 0;
   GLint major = 0, minor = 0, profile = 0;
   GLint addend = -1, block_size = 0, ubo_alignment = 0, ssbo_alignment = 0;
   unsigned baseline = 0;
   int initialized = 0, current = 0, passed = 0;

   setvbuf(stdout, NULL, _IONBF, 0);
   puts(TAG "compute capability gate; no swaps");
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !check_egl("initialize", eglInitialize(display, NULL, NULL)))
      goto cleanup;
   initialized = 1;
   if (!check_egl("bind API", eglBindAPI(EGL_OPENGL_API)) ||
       !check_egl("choose config", eglChooseConfig(display, config_attributes,
                                                  &config, 1, &count)) || count != 1)
      goto cleanup;
   surface = eglCreatePbufferSurface(display, config, surface_attributes);
   if (!check_egl("create pbuffer", surface != EGL_NO_SURFACE))
      goto cleanup;
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
   if (!check_egl("create context", context != EGL_NO_CONTEXT) ||
       !check_egl("make current", eglMakeCurrent(display, surface, surface, context)))
      goto cleanup;
   current = 1;
   glGetIntegerv(GL_MAJOR_VERSION, &major);
   glGetIntegerv(GL_MINOR_VERSION, &minor);
   glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
   printf(TAG "GL version=%d.%d profile=%x\n", major, minor, profile);
   if (major < 3 || (major == 3 && minor < 3) ||
       !(profile & GL_CONTEXT_CORE_PROFILE_BIT) ||
       !has_extension("GL_ARB_compute_shader") ||
       !has_extension("GL_ARB_shader_storage_buffer_object") ||
       !check_gl("context contract"))
      goto cleanup;

   if (!compile_program("defaults+UBO", source, &shader, &program))
      goto cleanup;
   addend = glGetUniformLocation(program, "addend");
   GLuint block = glGetUniformBlockIndex(program, "Input");
   if (!check_gl("uniform lookup") || addend < 0 || block == GL_INVALID_INDEX)
      goto cleanup;
   glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_DATA_SIZE, &block_size);
   glUniformBlockBinding(program, block, 0);
   /* Output uses the normal initial SSBO binding zero; no binding layout. */
   glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ubo_alignment);
   glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &ssbo_alignment);
   printf(TAG "UBO size=%d alignment=%d SSBO alignment=%d\n",
          block_size, ubo_alignment, ssbo_alignment);
   if (!check_gl("resource contract") || block_size != 16 ||
       ubo_alignment <= 0 || 16 % ubo_alignment ||
       ssbo_alignment <= 0 || 32 % ssbo_alignment)
      goto cleanup;
   glUseProgram(program);
   glGenBuffers(2, buffers);
   glFinish();
   if (!check_gl("setup") || !buffers[0] || !buffers[1])
      goto cleanup;
   (void)ps5_egl_current_compute_status(&baseline);

   for (unsigned pass = 0; pass < 2; ++pass) {
      uint32_t words[80], readback[80], input[8];
      unsigned dispatches = 0;
      const uint32_t expected = pass ? 40 : 20;
      for (unsigned i = 0; i < 80; ++i)
         words[i] = GUARD;
      for (unsigned i = 0; i < 8; ++i)
         input[i] = GUARD;
      input[4] = pass ? 31 : 13;
      printf(TAG "pass=%u upload begin\n", pass);
      glUniform1ui(addend, pass ? 9 : 7);
      glBindBuffer(GL_UNIFORM_BUFFER, buffers[0]);
      glBufferData(GL_UNIFORM_BUFFER, sizeof(input), input, GL_DYNAMIC_DRAW);
      glBindBufferRange(GL_UNIFORM_BUFFER, 0, buffers[0], 16, 16);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
      glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(words), words, GL_DYNAMIC_DRAW);
      glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, buffers[1], 32, 16);
      if (!check_gl("upload/bind"))
         goto cleanup;
      printf(TAG "pass=%u dispatch begin\n", pass);
      glDispatchCompute(1, 1, 1);
      if (!check_gl("dispatch"))
         goto cleanup;
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
      if (!check_gl("barrier"))
         goto cleanup;
      puts(TAG "readback begin");
      glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(readback), readback);
      glFinish();
      int status = ps5_egl_current_compute_status(&dispatches);
      printf(TAG "pass=%u native status=%d dispatches=%u baseline=%u\n",
             pass, status, dispatches, baseline);
      if (!check_gl("readback/finish") || status || dispatches != baseline + pass + 1)
         goto cleanup;
      for (unsigned i = 0; i < 80; ++i) {
         uint32_t wanted = i == 8 ? expected : GUARD;
         if (readback[i] != wanted) {
            printf(TAG "pass=%u word=%u actual=%08x expected=%08x\n",
                   pass, i, readback[i], wanted);
            goto cleanup;
         }
      }
      printf(TAG "pass=%u value=%u guards=79/79 PASS\n", pass, expected);
   }

   if (!has_extension("GL_ARB_shader_image_load_store") ||
       !has_extension("GL_ARB_shader_atomic_counters") ||
       !check_gl("image/atomic extensions"))
      goto cleanup;
   if (!compile_program("image writer", image_source, &extra_shaders[0], &extra_programs[0]) ||
       !compile_program("texture sampler", sample_source, &extra_shaders[1], &extra_programs[1]) ||
       !compile_program("atomic counter", atomic_source, &extra_shaders[2], &extra_programs[2]))
      goto cleanup;
   GLint destination = glGetUniformLocation(extra_programs[0], "destination");
   GLint value = glGetUniformLocation(extra_programs[0], "value");
   GLint sampler = glGetUniformLocation(extra_programs[1], "source_texture");
   if (!check_gl("image/sampler uniforms") || destination < 0 || value < 0 || sampler < 0)
      goto cleanup;
   glGenTextures(1, &texture);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
   const float initial_pixel = 0.0f;
   glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, 1, 1, 0, GL_RED, GL_FLOAT, &initial_pixel);
   glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
   glGenBuffers(1, &atomic_buffer);
   if (!check_gl("image/atomic resources") || !texture || !atomic_buffer)
      goto cleanup;

   for (unsigned pass = 0; pass < 2; ++pass) {
      printf(TAG "image/atomic pass=%u begin\n", pass);
      /* Order the next image write after earlier shader accesses, including
       * sampling in the preceding pass. No texture readback/reset is used. */
      glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
      glUseProgram(extra_programs[0]);
      glUniform1i(destination, 0);
      glUniform1f(value, pass ? 0.75f : 0.25f);
      if (!check_gl("image uniforms") ||
          !dispatch_checked("image writer", baseline + 3 + pass * 3,
                            GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT))
         goto cleanup;
      glUseProgram(extra_programs[1]);
      glUniform1i(sampler, 0);
      if (!reset_output(buffers[1]) ||
          !dispatch_checked("texture sampler", baseline + 4 + pass * 3,
                            GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT) ||
          !check_buffer("sampled image", GL_SHADER_STORAGE_BUFFER, buffers[1],
                        pass ? UINT32_C(0x3f400000) : UINT32_C(0x3e800000)))
         goto cleanup;

      uint32_t counters[80];
      const uint32_t initial_counter = pass ? 17 : 5;
      for (unsigned i = 0; i < 80; ++i)
         counters[i] = GUARD;
      counters[8] = initial_counter;
      glUseProgram(extra_programs[2]);
      glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, atomic_buffer);
      glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(counters), counters, GL_DYNAMIC_DRAW);
      /* GL byte 20 + declaration byte 12 = byte 32. Mesa must supply the
       * alignment correction when lowering binding 7 to a shader buffer. */
      glBindBufferRange(GL_ATOMIC_COUNTER_BUFFER, 7, atomic_buffer, 20, 16);
      if (!reset_output(buffers[1]) ||
          !dispatch_checked("atomic counter", baseline + 5 + pass * 3,
                            GL_ATOMIC_COUNTER_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT |
                            GL_BUFFER_UPDATE_BARRIER_BIT) ||
          !check_buffer("atomic returned", GL_SHADER_STORAGE_BUFFER, buffers[1], initial_counter) ||
          !check_buffer("atomic incremented", GL_ATOMIC_COUNTER_BUFFER, atomic_buffer,
                        initial_counter + 1))
         goto cleanup;
   }

   /* Keep atomic binding 7 attached: changing programs must clear stale
    * lowered slots through the normal Mesa state atoms, not a manual unbind. */
   puts(TAG "return to defaults+UBO program begin");
   glUseProgram(program);
   glUniform1ui(addend, 7);
   const uint32_t restored_input[4] = {13, GUARD, GUARD, GUARD};
   glBindBuffer(GL_UNIFORM_BUFFER, buffers[0]);
   glBufferSubData(GL_UNIFORM_BUFFER, 16, sizeof(restored_input), restored_input);
   glBindBufferRange(GL_UNIFORM_BUFFER, 0, buffers[0], 16, 16);
   if (!reset_output(buffers[1]) ||
       !dispatch_checked("restored defaults+UBO", baseline + 9,
                         GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT |
                         GL_BUFFER_UPDATE_BARRIER_BIT) ||
       !check_buffer("restored output", GL_SHADER_STORAGE_BUFFER, buffers[1], 20) ||
       !check_buffer("retired atomic unchanged", GL_ATOMIC_COUNTER_BUFFER, atomic_buffer, 18))
      goto cleanup;
   puts(TAG "dispatch delta=9 (original 2 + image/sampler/atomic 6 + restored 1)");

   if (!compile_program("3D IDs", ids_source, &grid_shaders[0], &grid_programs[0]) ||
       !compile_program("shared xor32", shared_source, &grid_shaders[1], &grid_programs[1]) ||
       !compile_program("indirect arguments", commands_source, &grid_shaders[2], &grid_programs[2]))
      goto cleanup;
   glGenBuffers(2, grid_buffers);
   if (!check_gl("grid buffers") || !grid_buffers[0] || !grid_buffers[1])
      goto cleanup;
   uint32_t ids_expected[208], shared_expected[80], commands_expected[19];
   for (unsigned i = 0; i < 208; ++i)
      ids_expected[i] = i >= 8 && i < 200 ? 3 * (i - 8) + 90 : GUARD;
   for (unsigned i = 0; i < 80; ++i)
      shared_expected[i] = i >= 8 && i < 72 ? 3 * ((i - 8) ^ 32u) + 17 : GUARD;
   for (unsigned i = 0; i < 19; ++i)
      commands_expected[i] = i >= 8 && i < 11 ? (i == 9 ? 3 : 2) : GUARD;

   glUseProgram(grid_programs[0]);
   if (!reset_results(grid_buffers[0], 192))
      goto cleanup;
   puts(TAG "3D IDs direct dispatch begin");
   glDispatchCompute(2, 3, 2);
   if (!finish_dispatch("3D IDs direct", baseline + 10,
                        GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT) ||
       !check_words("3D IDs direct", grid_buffers[0], ids_expected, 208))
      goto cleanup;

   glUseProgram(grid_programs[1]);
   if (!reset_results(grid_buffers[0], 64) ||
       !dispatch_checked("shared xor32", baseline + 11,
                         GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT) ||
       !check_words("shared xor32", grid_buffers[0], shared_expected, 80))
      goto cleanup;

   glUseProgram(grid_programs[2]);
   if (!reset_results(grid_buffers[1], 3) ||
       !dispatch_checked("indirect arguments", baseline + 12,
                         GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT |
                         GL_BUFFER_UPDATE_BARRIER_BIT) ||
       !check_words("indirect arguments", grid_buffers[1], commands_expected, 19))
      goto cleanup;
   /* Check the GPU-generated counts before allowing indirect submission. */
   glUseProgram(grid_programs[0]);
   glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
   if (!reject_indirect("missing indirect buffer", 32,
                        GL_INVALID_OPERATION, baseline + 12))
      goto cleanup;
   glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, grid_buffers[1]);
   if (!reject_indirect("misaligned indirect offset", 2,
                        GL_INVALID_VALUE, baseline + 12) ||
       /* 76-byte allocation: offset 68 leaves only 8 bytes, not the required 12. */
       !reject_indirect("short indirect command range", 68,
                        GL_INVALID_OPERATION, baseline + 12) ||
       !reset_results(grid_buffers[0], 192))
      goto cleanup;
   glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
   if (!check_gl("indirect command barrier"))
      goto cleanup;
   puts(TAG "3D IDs indirect offset=32 dispatch begin");
   glDispatchComputeIndirect(32);
   if (!finish_dispatch("3D IDs indirect", baseline + 13,
                        GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT) ||
       !check_words("3D IDs indirect", grid_buffers[0], ids_expected, 208) ||
       !check_words("indirect arguments unchanged", grid_buffers[1], commands_expected, 19))
      goto cleanup;

   glUseProgram(program);
   glUniform1ui(addend, 7);
   glBindBufferRange(GL_UNIFORM_BUFFER, 0, buffers[0], 16, 16);
   if (!reset_output(buffers[1]) ||
       !dispatch_checked("post-indirect defaults+UBO", baseline + 14,
                         GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT) ||
       !check_buffer("post-indirect output", GL_SHADER_STORAGE_BUFFER, buffers[1], 20))
      goto cleanup;
   puts(TAG "dispatch delta=14 (prior 9 + IDs/shared/arguments/indirect 4 + restored 1); negatives=3 PASS");

   for (unsigned format = 0; format < 3; ++format) {
      const char *name = normalized_formats[format].name;
      const GLenum internal_format = normalized_formats[format].internal_format;
      const GLenum raw_type = normalized_formats[format].raw_type;
      const unsigned levels = normalized_formats[format].levels;
      const int byte_format = raw_type == GL_UNSIGNED_BYTE;
      char store_source[512], load_source[512], store_name[32], load_name[32];
      int store_length = snprintf(store_source, sizeof(store_source), normalized_store_template,
                                  normalized_formats[format].layout);
      int load_length = snprintf(load_source, sizeof(load_source), normalized_load_template,
                                 normalized_formats[format].layout);
      if (store_length < 0 || (size_t)store_length >= sizeof(store_source) ||
          load_length < 0 || (size_t)load_length >= sizeof(load_source))
         goto cleanup;
      snprintf(store_name, sizeof(store_name), "%s store", name);
      snprintf(load_name, sizeof(load_name), "%s load", name);
      if (!compile_program(store_name, store_source, &normalized_shaders[format][0], &normalized_programs[format][0]) ||
          !compile_program(load_name, load_source, &normalized_shaders[format][1], &normalized_programs[format][1]))
         goto cleanup;
      GLint destination = glGetUniformLocation(normalized_programs[format][0], "destination");
      GLint value = glGetUniformLocation(normalized_programs[format][0], "value");
      GLint source_image = glGetUniformLocation(normalized_programs[format][1], "source_image");
      if (!check_gl("normalized image uniforms") || destination < 0 || value < 0 || source_image < 0)
         goto cleanup;
      GLushort initial16[36];
      GLubyte initial8[36];
      const GLubyte mip1_initial[4] = {0x16, 0x47, 0x98, 0xdb};
      for (unsigned i = 0; i < 36; ++i) {
         initial16[i] = (GLushort)(0x1234u + i * 0x101u);
         initial8[i] = (GLubyte)(0x23u + i * 5u);
      }
      glGenTextures(1, &normalized_textures[format]);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, normalized_textures[format]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
      /* Rows are 12/24 bytes, both aligned to the unchanged default pack/unpack 4. */
      if (levels > 1) {
         /* Immutable two-level RGBA8 selects canonical-linear mip storage.
          * The separate base-only entry exercises the checked tiled image SRD. */
         if (!has_extension("GL_ARB_texture_storage"))
            goto cleanup;
         PFNGLTEXSTORAGE2DPROC texture_storage =
            (PFNGLTEXSTORAGE2DPROC)eglGetProcAddress("glTexStorage2D");
         if (!texture_storage) {
            puts(TAG "missing glTexStorage2D entrypoint");
            goto cleanup;
         }
         puts(TAG "RGBA8 mipmapped-linear immutable levels=2");
         texture_storage(GL_TEXTURE_2D, levels, internal_format, 3, 3);
         if (!check_gl("RGBA8 immutable allocation"))
            goto cleanup;
         glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 3, 3, GL_RGBA, GL_UNSIGNED_BYTE, initial8);
         glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip1_initial);
         GLint immutable = 0, mip_width = 0, mip_height = 0;
         glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_IMMUTABLE_FORMAT, &immutable);
         glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &mip_width);
         glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_HEIGHT, &mip_height);
         if (!check_gl("RGBA8 two-level storage") || !immutable || mip_width != 1 || mip_height != 1)
            goto cleanup;
      } else {
         printf(TAG "%s mutable base-only allocation\n", name);
         glTexImage2D(GL_TEXTURE_2D, 0, internal_format, 3, 3, 0, GL_RGBA, raw_type,
                      byte_format ? (const void *)initial8 : (const void *)initial16);
      }
      if (!check_gl("normalized sentinel texture") || !normalized_textures[format])
         goto cleanup;
      for (unsigned pass = 0; pass < 2; ++pass) {
         GLushort raw16[36] = {0};
         GLubyte raw8[36] = {0};
         uint32_t expected[80];
         printf(TAG "%s pass=%u begin\n", name, pass);
         glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
         glUseProgram(normalized_programs[format][0]);
         glUniform1i(destination, 0);
         glUniform4f(value, pass ? 1.0f : 0.0f, pass ? 0.0f : 1.0f,
                     pass ? 1.0f : 0.0f, pass ? 0.0f : 1.0f);
         glBindImageTexture(0, normalized_textures[format], 0, GL_FALSE, 0, GL_WRITE_ONLY, internal_format);
         if (!check_gl("normalized store binding") ||
             !dispatch_checked(store_name, baseline + 15 + format * 4 + pass * 2,
                               GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT))
            goto cleanup;
         printf(TAG "%s raw texture readback begin\n", name);
         glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, raw_type,
                       byte_format ? (void *)raw8 : (void *)raw16);
         if (!check_gl("normalized raw texture readback"))
            goto cleanup;
         for (unsigned i = 0; i < 36; ++i) {
            /* Pixel (1,1) is texel 4, lanes 16..19; all eight neighbors stay intact. */
            unsigned wanted = i >= 16 && i < 20
               ? (((i - 16) & 1u) != pass ? normalized_formats[format].endpoint : 0u)
               : (byte_format ? initial8[i] : initial16[i]);
            unsigned actual = byte_format ? raw8[i] : raw16[i];
            if (actual != wanted) {
               printf(TAG "%s pass=%u raw lane=%u actual=%04x expected=%04x\n",
                      name, pass, i, actual, wanted);
               goto cleanup;
            }
         }
         printf(TAG "%s pass=%u raw lanes=36/36 neighbors=8/8 PASS\n", name, pass);
         if (levels > 1) {
            GLubyte mip1_raw[4] = {0};
            glGetTexImage(GL_TEXTURE_2D, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip1_raw);
            if (!check_gl("RGBA8 mip1 readback"))
               goto cleanup;
            for (unsigned i = 0; i < 4; ++i) {
               if (mip1_raw[i] != mip1_initial[i]) {
                  printf(TAG "RGBA8 pass=%u mip1 lane=%u actual=%02x expected=%02x\n",
                         pass, i, (unsigned)mip1_raw[i], (unsigned)mip1_initial[i]);
                  goto cleanup;
               }
            }
            printf(TAG "RGBA8 mipmapped-linear pass=%u mip1 sentinel=4/4 unchanged PASS\n", pass);
         }
         for (unsigned i = 0; i < 80; ++i)
            expected[i] = i >= 8 && i < 12
               ? (((i - 8) & 1u) != pass ? UINT32_C(0x3f800000) : 0) : GUARD;
         glUseProgram(normalized_programs[format][1]);
         glUniform1i(source_image, 0);
         glBindImageTexture(0, normalized_textures[format], 0, GL_FALSE, 0, GL_READ_ONLY, internal_format);
         if (!reset_output(buffers[1]) ||
             !dispatch_checked(load_name, baseline + 16 + format * 4 + pass * 2,
                               GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT |
                               GL_SHADER_IMAGE_ACCESS_BARRIER_BIT) ||
             !check_words(load_name, buffers[1], expected, 80))
            goto cleanup;
         printf(TAG "%s pass=%u channels=4/4 SSBO guards=76/76 PASS\n", name, pass);
      }
   }
   puts(TAG "dispatch delta=26 (prior 14 + RGBA16 4 + RGBA8 mipmapped-linear 4 + RGBA8 base-only tiled 4); negatives=3 PASS");

   const GLsizeiptr capacity_bytes = 134217728;
   const GLsizeiptr allocation_bytes = 134217792;
   GLint advertised_ssbo_size = 0;
   glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &advertised_ssbo_size);
   printf(TAG "capacity proof: advertised SSBO maximum=%d expected=%lld\n",
          advertised_ssbo_size, (long long)capacity_bytes);
   if (!check_gl("advertised SSBO cap") || advertised_ssbo_size != capacity_bytes ||
       !has_extension("GL_ARB_program_interface_query") ||
       !has_extension("GL_ARB_robust_buffer_access_behavior") ||
       !compile_program("robust SSBO bounds", robust_source,
                        &robust_shader, &robust_program) ||
       !compile_program("128MiB unsized capacity", capacity_source, &capacity_shader, &capacity_program))
      goto cleanup;
   GLuint robust_data_block = glGetProgramResourceIndex(
      robust_program, GL_SHADER_STORAGE_BLOCK, "Data");
   GLuint robust_report_block = glGetProgramResourceIndex(
      robust_program, GL_SHADER_STORAGE_BLOCK, "Report");
   GLuint capacity_block = glGetProgramResourceIndex(capacity_program, GL_SHADER_STORAGE_BLOCK, "Capacity");
   GLuint report_block = glGetProgramResourceIndex(capacity_program, GL_SHADER_STORAGE_BLOCK, "Report");
   if (!check_gl("storage block lookup") ||
       robust_data_block == GL_INVALID_INDEX ||
       robust_report_block == GL_INVALID_INDEX ||
       capacity_block == GL_INVALID_INDEX || report_block == GL_INVALID_INDEX)
      goto cleanup;
   PFNGLSHADERSTORAGEBLOCKBINDINGPROC bind_storage_block =
      (PFNGLSHADERSTORAGEBLOCKBINDINGPROC)eglGetProcAddress("glShaderStorageBlockBinding");
   if (!bind_storage_block) {
      puts(TAG "missing glShaderStorageBlockBinding entrypoint");
      goto cleanup;
   }
   bind_storage_block(capacity_program, capacity_block, 0);
   bind_storage_block(capacity_program, report_block, 1);
   bind_storage_block(robust_program, robust_data_block, 0);
   bind_storage_block(robust_program, robust_report_block, 1);
   if (!check_gl("capacity block bindings"))
      goto cleanup;
   {
      uint32_t data[80], report[4] = {GUARD, GUARD, GUARD, GUARD};
      for (unsigned i = 0; i < 80; ++i)
         data[i] = GUARD;
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
      glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(data), data, GL_DYNAMIC_DRAW);
      glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, buffers[1], 32, 16);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
      glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(report), report, GL_DYNAMIC_DRAW);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, buffers[0]);
      glUseProgram(robust_program);
      glUniform1ui(glGetUniformLocation(robust_program, "index"), 4);
      if (!check_gl("robust setup") ||
          !dispatch_checked("robust SSBO bounds", baseline + 27,
                            GL_SHADER_STORAGE_BARRIER_BIT |
                            GL_BUFFER_UPDATE_BARRIER_BIT))
         goto cleanup;
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
      glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(report), report);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[1]);
      glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(data), data);
      if (!check_gl("robust readback") || report[0] || report[1] != 4)
         goto cleanup;
      for (unsigned i = 0; i < 80; ++i)
         if (data[i] != GUARD)
            goto cleanup;
      puts(TAG "robust SSBO OOB read=0 write=discard PASS");
   }
   GLint64 actual_bytes = 0;
   uint32_t edge_guards[16];
   for (unsigned i = 0; i < 16; ++i)
      edge_guards[i] = GUARD;
   glGenBuffers(1, &capacity_buffer);
   if (!check_gl("capacity buffer name") || !capacity_buffer)
      goto cleanup;
   glBindBuffer(GL_SHADER_STORAGE_BUFFER, capacity_buffer);
   if (!check_gl("capacity allocation binding"))
      goto cleanup;
   puts(TAG "capacity allocation begin bytes=134217792; no retries on failure");
   glBufferData(GL_SHADER_STORAGE_BUFFER, allocation_bytes, NULL, GL_DYNAMIC_DRAW);
   if (!check_gl("capacity allocation")) /* Including OOM: stop before any access. */
      goto cleanup;
   glGetBufferParameteri64v(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &actual_bytes);
   printf(TAG "capacity allocation actual bytes=%lld\n", (long long)actual_bytes);
   if (!check_gl("capacity allocation size") || actual_bytes != allocation_bytes)
      goto cleanup;
   /* Initialize/read only the first and last 64 bytes. The interior is not checked. */
   glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(edge_guards), edge_guards);
   glBufferSubData(GL_SHADER_STORAGE_BUFFER, capacity_bytes, sizeof(edge_guards), edge_guards);
   if (!check_gl("capacity boundary guards") || !reset_output(buffers[1]))
      goto cleanup;
   glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, buffers[1], 32, 16);
   glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, capacity_buffer, 32, capacity_bytes);
   glUseProgram(capacity_program);
   if (!check_gl("capacity ranges") ||
       !dispatch_checked("128MiB unsized capacity", baseline + 28,
                         GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT) ||
       !check_buffer("capacity length", GL_SHADER_STORAGE_BUFFER, buffers[1], UINT32_C(33554432)))
      goto cleanup;
   glBindBuffer(GL_SHADER_STORAGE_BUFFER, capacity_buffer);
   for (unsigned edge = 0; edge < 2; ++edge) {
      uint32_t words[16] = {0};
      const unsigned changed = edge ? 7 : 8;
      const uint32_t marker = edge ? UINT32_C(0x2468ace0) : UINT32_C(0x13579bdf);
      printf(TAG "capacity %s 64-byte readback begin\n", edge ? "last" : "first");
      glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, edge ? capacity_bytes : 0, sizeof(words), words);
      if (!check_gl("capacity boundary readback"))
         goto cleanup;
      for (unsigned i = 0; i < 16; ++i) {
         const uint32_t wanted = i == changed ? marker : GUARD;
         if (words[i] != wanted) {
            printf(TAG "capacity edge=%u word=%u actual=%08x expected=%08x\n", edge, i, words[i], wanted);
            goto cleanup;
         }
      }
      printf(TAG "capacity edge=%u changed word=%u guards=15/15 PASS\n", edge, changed);
   }
   /* Delete the large GL object promptly; context teardown retires any cached refs. */
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
   glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
   glDeleteBuffers(1, &capacity_buffer);
   capacity_buffer = 0;
   if (!check_gl("capacity buffer delete"))
      goto cleanup;
   puts(TAG "dispatch delta=28 (prior 26 + robust 1 + capacity 1); negatives=3 PASS; capacity interior unchecked");
   passed = 1;

cleanup:
   puts(TAG "cleanup begin");
   if (current) {
      glFinish();
      passed &= check_gl("cleanup finish");
      glUseProgram(0);
      glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
      /* Do not issue unsupported SSBO calls if context validation failed. */
      if (buffers[1])
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
      if (capacity_program)
         glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
      if (capacity_buffer)
         glDeleteBuffers(1, &capacity_buffer);
      if (capacity_program)
         glDeleteProgram(capacity_program);
      if (capacity_shader)
         glDeleteShader(capacity_shader);
      if (robust_program)
         glDeleteProgram(robust_program);
      if (robust_shader)
         glDeleteShader(robust_shader);
      if (atomic_buffer) {
         glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 7, 0);
         glDeleteBuffers(1, &atomic_buffer);
      }
      if (texture || normalized_textures[0] || normalized_textures[1] || normalized_textures[2]) {
         glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
         glBindTexture(GL_TEXTURE_2D, 0);
         glDeleteTextures(1, &texture);
         glDeleteTextures(3, normalized_textures);
      }
      glDeleteBuffers(2, buffers);
      if (grid_buffers[1])
         glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
      glDeleteBuffers(2, grid_buffers);
      for (unsigned i = 0; i < 3; ++i) {
         for (unsigned operation = 0; operation < 2; ++operation) {
            if (normalized_programs[i][operation])
               glDeleteProgram(normalized_programs[i][operation]);
            if (normalized_shaders[i][operation])
               glDeleteShader(normalized_shaders[i][operation]);
         }
      }
      for (unsigned i = 0; i < 3; ++i) {
         if (grid_programs[i])
            glDeleteProgram(grid_programs[i]);
         if (grid_shaders[i])
            glDeleteShader(grid_shaders[i]);
         if (extra_programs[i])
            glDeleteProgram(extra_programs[i]);
         if (extra_shaders[i])
            glDeleteShader(extra_shaders[i]);
      }
      if (program)
         glDeleteProgram(program);
      if (shader)
         glDeleteShader(shader);
      passed &= check_gl("delete GL objects");
      passed &= check_egl("unbind", eglMakeCurrent(display, EGL_NO_SURFACE,
                                                   EGL_NO_SURFACE, EGL_NO_CONTEXT));
   }
   if (context != EGL_NO_CONTEXT)
      passed &= check_egl("destroy context", eglDestroyContext(display, context));
   if (surface != EGL_NO_SURFACE)
      passed &= check_egl("destroy pbuffer", eglDestroySurface(display, surface));
   if (initialized)
      passed &= check_egl("terminate", eglTerminate(display));
   printf(TAG "result=%d %s\n", passed ? 0 : 1, passed ? "PASS" : "FAIL");
   return passed ? 0 : 1;
}
