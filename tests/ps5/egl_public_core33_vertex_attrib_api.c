#include <stdint.h>
#include <stdio.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

int
main(void)
{
   static const GLdouble doubles[4] = {1.0, 2.0, 3.0, 4.0};
   static const GLfloat floats[4] = {1.0f, 2.0f, 3.0f, 4.0f};
   static const GLshort shorts[4] = {1, 2, 3, 4};
   static const GLbyte bytes[4] = {1, 2, 3, 4};
   static const GLubyte ubytes[4] = {1, 2, 3, 4};
   static const GLint ints[4] = {1, 2, 3, 4};
   static const GLuint uints[4] = {1, 2, 3, 4};
   static const GLushort ushorts[4] = {1, 2, 3, 4};
   static const GLuint packed[1] = {UINT32_C(0xc0300801)};
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint major = 0, minor = 0, count = 0;
   GLdouble current_double[4] = {0.0, 0.0, 0.0, 0.0};
   GLint current_integer[4] = {0, 0, 0, 0};
   GLenum error = GL_NO_ERROR;
   int made_current = 0, passed = 0;
   EGLBoolean cleanup_ok = EGL_TRUE;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
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

   glVertexAttrib1d(5, 1.0);
   glVertexAttrib1dv(5, doubles);
   glVertexAttrib1f(5, 1.0f);
   glVertexAttrib1fv(5, floats);
   glVertexAttrib1s(5, 1);
   glVertexAttrib1sv(5, shorts);
   glVertexAttrib2d(5, 1.0, 2.0);
   glVertexAttrib2dv(5, doubles);
   glVertexAttrib2f(5, 1.0f, 2.0f);
   glVertexAttrib2fv(5, floats);
   glVertexAttrib2s(5, 1, 2);
   glVertexAttrib2sv(5, shorts);
   glVertexAttrib3d(5, 1.0, 2.0, 3.0);
   glVertexAttrib3dv(5, doubles);
   glVertexAttrib3f(5, 1.0f, 2.0f, 3.0f);
   glVertexAttrib3fv(5, floats);
   glVertexAttrib3s(5, 1, 2, 3);
   glVertexAttrib3sv(5, shorts);
   glVertexAttrib4bv(5, bytes);
   glVertexAttrib4d(5, 1.0, 2.0, 3.0, 4.0);
   glVertexAttrib4dv(5, doubles);
   glVertexAttrib4fv(5, floats);
   glVertexAttrib4iv(5, ints);
   glVertexAttrib4Nbv(5, bytes);
   glVertexAttrib4Niv(5, ints);
   glVertexAttrib4Nsv(5, shorts);
   glVertexAttrib4Nub(5, 1, 2, 3, 4);
   glVertexAttrib4Nubv(5, ubytes);
   glVertexAttrib4Nuiv(5, uints);
   glVertexAttrib4Nusv(5, ushorts);
   glVertexAttrib4s(5, 1, 2, 3, 4);
   glVertexAttrib4sv(5, shorts);
   glVertexAttrib4ubv(5, ubytes);
   glVertexAttrib4uiv(5, uints);
   glVertexAttrib4usv(5, ushorts);
   glVertexAttribP1ui(5, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed[0]);
   glVertexAttribP1uiv(5, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed);
   glVertexAttribP2ui(5, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed[0]);
   glVertexAttribP2uiv(5, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed);
   glVertexAttribP3ui(5, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed[0]);
   glVertexAttribP3uiv(5, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed);
   glVertexAttribP4ui(5, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed[0]);
   glVertexAttribP4uiv(5, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed);
   glGetVertexAttribdv(5, GL_CURRENT_VERTEX_ATTRIB, current_double);

   glVertexAttribI1i(6, 1);
   glVertexAttribI1iv(6, ints);
   glVertexAttribI1ui(6, 1);
   glVertexAttribI1uiv(6, uints);
   glVertexAttribI2i(6, 1, 2);
   glVertexAttribI2iv(6, ints);
   glVertexAttribI2ui(6, 1, 2);
   glVertexAttribI2uiv(6, uints);
   glVertexAttribI3i(6, 1, 2, 3);
   glVertexAttribI3iv(6, ints);
   glVertexAttribI3ui(6, 1, 2, 3);
   glVertexAttribI3uiv(6, uints);
   glVertexAttribI4bv(6, bytes);
   glVertexAttribI4i(6, 1, 2, 3, 4);
   glVertexAttribI4iv(6, ints);
   glVertexAttribI4sv(6, shorts);
   glVertexAttribI4ubv(6, ubytes);
   glVertexAttribI4uiv(6, uints);
   glVertexAttribI4usv(6, ushorts);
   glGetVertexAttribIiv(6, GL_CURRENT_VERTEX_ATTRIB, current_integer);

   error = glGetError();
   passed = major == 1 && minor == 4 &&
            current_double[0] >= 0.0 && current_double[0] <= 1.0 &&
            current_double[1] >= 0.0 && current_double[1] <= 1.0 &&
            current_double[2] >= 0.0 && current_double[2] <= 1.0 &&
            current_double[3] >= 0.0 && current_double[3] <= 1.0 &&
            current_integer[0] == 1 && current_integer[1] == 2 &&
            current_integer[2] == 3 && current_integer[3] == 4 &&
            error == GL_NO_ERROR;
   printf("[ps5-egl-vertex-attrib-api] float=%.6f/%.6f/%.6f/%.6f "
          "integer=%d/%d/%d/%d error=0x%x result=%d\n",
          current_double[0], current_double[1], current_double[2],
          current_double[3], current_integer[0], current_integer[1],
          current_integer[2], current_integer[3], error, passed ? 0 : 1);

cleanup:
   if (made_current) {
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
   printf("[ps5-egl-vertex-attrib-api] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
