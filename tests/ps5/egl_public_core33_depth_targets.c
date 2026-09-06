#include <math.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#define SIZE 64
#define LAYERS 4

int ps5_egl_current_draw_status(unsigned *draw_calls);

static GLuint
compile_shader(GLenum type, const char *source)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512] = {0};
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-core33-depth-targets] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   return shader;
}

static int
test_target(GLenum target, unsigned layer, float *depth)
{
   GLuint texture = 0, framebuffer = 0;
   GLenum face = GL_TEXTURE_CUBE_MAP_NEGATIVE_Y;
   GLint object_type = 0, object_name = 0, attached_layer = -1;
   int height = target == GL_TEXTURE_1D ? 1 : SIZE;
   int result = 0;

   glGenTextures(1, &texture);
   glBindTexture(target, texture);
   glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   switch (target) {
   case GL_TEXTURE_1D:
      glTexImage1D(target, 0, GL_DEPTH_COMPONENT32F, SIZE, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
      break;
   case GL_TEXTURE_1D_ARRAY:
      glTexImage2D(target, 0, GL_DEPTH_COMPONENT32F, SIZE, LAYERS, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
      break;
   case GL_TEXTURE_CUBE_MAP:
      for (unsigned index = 0; index < 6; ++index)
         glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + index, 0,
                      GL_DEPTH_COMPONENT32F, SIZE, SIZE, 0,
                      GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
      break;
   case GL_TEXTURE_3D:
      glTexImage3D(target, 0, GL_DEPTH_COMPONENT32F, SIZE, SIZE, LAYERS, 0,
                   GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
      break;
   default:
      goto cleanup;
   }

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glDrawBuffer(GL_NONE);
   glReadBuffer(GL_NONE);
   if (target == GL_TEXTURE_1D)
      glFramebufferTexture1D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             target, texture, 0);
   else if (target == GL_TEXTURE_CUBE_MAP)
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                             face, texture, 0);
   else
      glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                texture, 0, layer);
   glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &object_type);
   glGetFramebufferAttachmentParameteriv(
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
      GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &object_name);
   if (target == GL_TEXTURE_1D_ARRAY || target == GL_TEXTURE_3D)
      glGetFramebufferAttachmentParameteriv(
         GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
         GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER, &attached_layer);

   glViewport(0, 0, SIZE, height);
   glClearDepth(0.75);
   glClear(GL_DEPTH_BUFFER_BIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glFinish();
   glReadPixels(SIZE / 2, height / 2, 1, 1,
                GL_DEPTH_COMPONENT, GL_FLOAT, depth);
   result = glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
               GL_FRAMEBUFFER_COMPLETE &&
            object_type == GL_TEXTURE && object_name == (GLint)texture &&
            (attached_layer == -1 || attached_layer == (GLint)layer) &&
            fabsf(*depth - 0.5f) < 0.00001f &&
            glGetError() == GL_NO_ERROR;

cleanup:
   glBindFramebuffer(GL_FRAMEBUFFER, 0);
   if (framebuffer)
      glDeleteFramebuffers(1, &framebuffer);
   if (texture)
      glDeleteTextures(1, &texture);
   return result;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "const vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));\n"
      "void main(){gl_Position=vec4(p[gl_VertexID],0,1);}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "void main(){}\n";
   static const GLenum targets[4] = {
      GL_TEXTURE_1D, GL_TEXTURE_1D_ARRAY,
      GL_TEXTURE_CUBE_MAP, GL_TEXTURE_3D,
   };
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
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLConfig config = NULL;
   EGLint count = 0;
   GLuint shaders[2] = {0}, program = 0, vao = 0;
   GLint linked = GL_FALSE;
   float depths[4] = {0};
   unsigned matching = 0, draw_calls = 0;
   int draw_status = -1, current = 0, passed = 0;
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

   shaders[0] = compile_shader(GL_VERTEX_SHADER, vertex_source);
   shaders[1] = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
   if (!shaders[0] || !shaders[1])
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);
   glUseProgram(program);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LESS);
   glDepthMask(GL_TRUE);
   for (unsigned index = 0; index < 4; ++index)
      matching += test_target(targets[index], 2, &depths[index]);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   passed = matching == 4 && draw_status == 0 && draw_calls == 4 &&
            glGetError() == GL_NO_ERROR;
   printf("[ps5-egl-core33-depth-targets] matching=%u depths="
          "%.6f/%.6f/%.6f/%.6f draw=%d/%u result=%d\n",
          matching, depths[0], depths[1], depths[2], depths[3],
          draw_status, draw_calls, passed ? 0 : 1);

cleanup:
   if (current) {
      if (vao)
         glDeleteVertexArrays(1, &vao);
      if (program)
         glDeleteProgram(program);
      for (unsigned index = 0; index < 2; ++index)
         if (shaders[index])
            glDeleteShader(shaders[index]);
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
   printf("[ps5-egl-core33-depth-targets] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
