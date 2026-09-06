#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>

#define WIDTH 128
#define HEIGHT 96

int ps5_egl_current_draw_status(unsigned *draw_calls);

static int
has_extension(const char *extensions, const char *name)
{
   size_t length = strlen(name);
   const char *match = extensions;

   while ((match = strstr(match, name))) {
      if ((match == extensions || match[-1] == ' ') &&
          (match[length] == '\0' || match[length] == ' '))
         return 1;
      match += length;
   }
   return 0;
}

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return -1;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[512];
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-mrt] shader=0x%x log=%.*s\n", type, length, log);
      glDeleteShader(shader);
      return -1;
   }
   *result = shader;
   return 0;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
   static const char *fragment_source =
      "#version 120\n"
      "#extension GL_ARB_draw_buffers : require\n"
      "void main() {\n"
      "  gl_FragData[0] = vec4(1.0);\n"
      "  gl_FragData[1] = vec4(1.0);\n"
      "  gl_FragData[2] = vec4(1.0);\n"
      "  gl_FragData[3] = vec4(1.0, 1.0, 1.0, 1.0);\n"
      "}\n";
   static const float vertices[6] = {
      -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f,
   };
   static const GLenum attachments[4] = {
      GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
      GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3,
   };
   static const uint32_t expected[4] = {
      UINT32_C(0xff0000ff), UINT32_C(0xff00ff00),
      UINT32_C(0xffff0000), UINT32_C(0x00000000),
   };
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0;
   GLuint vs = 0, fs = 0, program = 0, vbo = 0, framebuffer = 0;
   GLuint renderbuffers[4] = {0, 0, 0, 0};
   GLint linked = GL_FALSE, max_draw_buffers = 0, max_attachments = 0;
   GLenum status = 0, draw_error = GL_NO_ERROR;
   int draw_status = -100;
   unsigned draw_calls = 0;
   uint32_t pixels[4] = {0, 0, 0, 0};
   const GLubyte *version = NULL, *glsl = NULL, *extensions = NULL;
   EGLBoolean cleanup_ok = EGL_TRUE;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   int made_current = 0, passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;
   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;

   version = glGetString(GL_VERSION);
   glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
   extensions = glGetString(GL_EXTENSIONS);
   glGetIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);
   glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &max_attachments);
   if (!version || !glsl || !extensions || max_draw_buffers < 4 ||
       max_attachments < 4 ||
       !has_extension((const char *)extensions, "GL_ARB_draw_buffers") ||
       !has_extension((const char *)extensions, "GL_EXT_draw_buffers2"))
      goto cleanup;

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vs) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fs))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, vs);
   glAttachShader(program, fs);
   glBindAttribLocation(program, 0, "a_position");
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);

   glGenBuffers(1, &vbo);
   glBindBuffer(GL_ARRAY_BUFFER, vbo);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);

   glGenFramebuffers(1, &framebuffer);
   glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
   glGenRenderbuffers(4, renderbuffers);
   for (unsigned i = 0; i < 4; ++i) {
      glBindRenderbuffer(GL_RENDERBUFFER, renderbuffers[i]);
      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, WIDTH, HEIGHT);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachments[i],
                                GL_RENDERBUFFER, renderbuffers[i]);
   }
   glDrawBuffers(4, attachments);
   status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR)
      goto cleanup;

   glViewport(0, 0, WIDTH, HEIGHT);
   glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
   glClear(GL_COLOR_BUFFER_BIT);
   glColorMaski(0, GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
   glColorMaski(1, GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
   glColorMaski(2, GL_FALSE, GL_FALSE, GL_TRUE, GL_TRUE);
   glColorMaski(3, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   glBlendFunc(GL_ZERO, GL_ZERO);
   glEnablei(GL_BLEND, 3);
   if (glIsEnabledi(GL_BLEND, 0) || !glIsEnabledi(GL_BLEND, 3) ||
       glGetError() != GL_NO_ERROR)
      goto cleanup;
   glDrawArrays(GL_TRIANGLES, 0, 3);
   draw_status = ps5_egl_current_draw_status(&draw_calls);
   glFinish();
   for (unsigned i = 0; i < 4; ++i) {
      glReadBuffer(attachments[i]);
      glReadPixels(WIDTH / 2, HEIGHT / 2, 1, 1,
                   GL_RGBA, GL_UNSIGNED_BYTE, &pixels[i]);
   }
   draw_error = glGetError();
   passed = draw_error == GL_NO_ERROR && draw_status == 0 && draw_calls == 1;
   for (unsigned i = 0; i < 4; ++i)
      passed &= pixels[i] == expected[i];

cleanup:
   if (made_current) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      if (framebuffer)
         glDeleteFramebuffers(1, &framebuffer);
      glDeleteRenderbuffers(4, renderbuffers);
      if (vbo)
         glDeleteBuffers(1, &vbo);
      if (program)
         glDeleteProgram(program);
      if (vs)
         glDeleteShader(vs);
      if (fs)
         glDeleteShader(fs);
      cleanup_gl_error = glGetError();
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE,
                                   EGL_NO_SURFACE, EGL_NO_CONTEXT);
   }
   if (context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY) {
      cleanup_ok &= eglTerminate(display);
      cleanup_egl_error = eglGetError();
   }
   passed &= cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
             cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-mrt] egl=%d.%d api_strings=%u/%u limits=%d/%d status=0x%x"
          " pixels=%08x,%08x,%08x,%08x draw_status=%d submissions=%u"
          " draw_error=0x%x cleanup=%u/0x%x/0x%x result=%d\n",
          egl_major, egl_minor, version != NULL, glsl != NULL,
          max_draw_buffers, max_attachments, status, pixels[0], pixels[1],
          pixels[2], pixels[3], draw_status, draw_calls, draw_error, cleanup_ok,
          cleanup_gl_error, cleanup_egl_error, passed ? 0 : 1);
   return passed ? 0 : 1;
}
