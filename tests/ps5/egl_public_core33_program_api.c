#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

static int
compile_shader(GLenum type, const char *source, GLuint *result)
{
   GLint compiled = GL_FALSE;
   GLuint shader = glCreateShader(type);

   if (!shader)
      return 0;
   glShaderSource(shader, 1, &source, NULL);
   glCompileShader(shader);
   glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
   if (!compiled) {
      char log[1024] = {0};
      GLsizei length = 0;

      glGetShaderInfoLog(shader, sizeof(log), &length, log);
      printf("[ps5-egl-program-api] shader=0x%x log=%.*s\n",
             type, length, log);
      glDeleteShader(shader);
      return 0;
   }
   *result = shader;
   return 1;
}

static GLint
uniform_location(GLuint program, const char *name, int *ok)
{
   GLint location = glGetUniformLocation(program, name);

   *ok &= location >= 0;
   return location;
}

int
main(void)
{
   static const char *vertex_source =
      "#version 330 core\n"
      "in vec2 position;\n"
      "uniform float f1; uniform vec2 f2;\n"
      "uniform int i1; uniform ivec2 i2; uniform ivec3 i3; uniform ivec4 i4;\n"
      "uniform uint u1; uniform uvec2 u2; uniform uvec3 u3; uniform uvec4 u4;\n"
      "uniform mat2 m2; uniform mat2x3 m23; uniform mat2x4 m24;\n"
      "uniform mat3x2 m32; uniform mat3 m3; uniform mat3x4 m34;\n"
      "uniform mat4x2 m42; uniform mat4x3 m43; uniform mat4 m4;\n"
      "layout(std140) uniform TestBlock { vec4 block_value; };\n"
      "out float sink_value;\n"
      "void main() {\n"
      "  float s=f1+f2.x+float(i1+i2.x+i3.x+i4.x)\n"
      "    +float(u1+u2.x+u3.x+u4.x)+m2[0][0]+m23[0][0]+m24[0][0]\n"
      "    +m32[0][0]+m3[0][0]+m34[0][0]+m42[0][0]+m43[0][0]\n"
      "    +m4[0][0]+block_value.x;\n"
      "  sink_value=s; gl_Position=vec4(position+s*1e-30,0,1);\n"
      "}\n";
   static const char *fragment_source =
      "#version 330 core\n"
      "in float sink_value;\n"
      "out vec4 output_color;\n"
      "void main() { output_color=vec4(fract(sink_value)); }\n";
   static const float fv[4] = {1.0f, 2.0f, 3.0f, 4.0f};
   static const GLint iv[4] = {1, 2, 3, 4};
   static const GLuint uv[4] = {1, 2, 3, 4};
   static const float matrix[16] = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
   };
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
   GLuint shaders[2] = {0, 0}, program = 0, uniform_buffer = 0;
   GLuint attached[2] = {0, 0}, uniform_index = GL_INVALID_INDEX;
   GLuint block_index = GL_INVALID_INDEX;
   GLint linked = GL_FALSE, validated = GL_FALSE, attached_count = 0;
   GLint attribute_location = -1, fragment_location = -1;
   GLint fragment_index = -1, active_attributes = 0, active_uniforms = 0;
   GLint uniform_type = 0, block_size = 0, block_binding = -1;
   GLint get_i = 0;
   GLuint get_u = 0;
   GLfloat get_f = 0.0f;
   char shader_source[4096] = {0}, active_name[128] = {0};
   char uniform_name[128] = {0}, block_name[128] = {0};
   char xfb_name[128] = {0};
   GLsizei source_length = 0, active_length = 0, uniform_length = 0;
   GLsizei block_length = 0, xfb_length = 0;
   GLenum active_type = 0, xfb_type = 0, error = GL_NO_ERROR;
   GLint active_size = 0, xfb_size = 0;
   int locations_ok = 1, reflection_ok = 0, identity_ok = 0;
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

   if (!compile_shader(GL_VERTEX_SHADER, vertex_source, &shaders[0]) ||
       !compile_shader(GL_FRAGMENT_SHADER, fragment_source, &shaders[1]))
      goto cleanup;
   program = glCreateProgram();
   glAttachShader(program, shaders[0]);
   glAttachShader(program, shaders[1]);
   glBindAttribLocation(program, 3, "position");
   glBindFragDataLocation(program, 1, "output_color");
   {
      const GLchar *varyings[1] = {"sink_value"};
      glTransformFeedbackVaryings(program, 1, varyings,
                                  GL_INTERLEAVED_ATTRIBS);
   }
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);

   glUniform1fv(uniform_location(program, "f1", &locations_ok), 1, fv);
   glUniform2fv(uniform_location(program, "f2", &locations_ok), 1, fv);
   glUniform1iv(uniform_location(program, "i1", &locations_ok), 1, iv);
   glUniform2i(uniform_location(program, "i2", &locations_ok), 1, 2);
   glUniform2iv(uniform_location(program, "i2", &locations_ok), 1, iv);
   glUniform3i(uniform_location(program, "i3", &locations_ok), 1, 2, 3);
   glUniform3iv(uniform_location(program, "i3", &locations_ok), 1, iv);
   glUniform4i(uniform_location(program, "i4", &locations_ok), 1, 2, 3, 4);
   glUniform1ui(uniform_location(program, "u1", &locations_ok), 1);
   glUniform1uiv(uniform_location(program, "u1", &locations_ok), 1, uv);
   glUniform2ui(uniform_location(program, "u2", &locations_ok), 1, 2);
   glUniform2uiv(uniform_location(program, "u2", &locations_ok), 1, uv);
   glUniform3ui(uniform_location(program, "u3", &locations_ok), 1, 2, 3);
   glUniform3uiv(uniform_location(program, "u3", &locations_ok), 1, uv);
   glUniform4ui(uniform_location(program, "u4", &locations_ok), 1, 2, 3, 4);
   glUniform4uiv(uniform_location(program, "u4", &locations_ok), 1, uv);
   glUniformMatrix2fv(uniform_location(program, "m2", &locations_ok),
                      1, GL_FALSE, matrix);
   glUniformMatrix2x3fv(uniform_location(program, "m23", &locations_ok),
                        1, GL_FALSE, matrix);
   glUniformMatrix2x4fv(uniform_location(program, "m24", &locations_ok),
                        1, GL_FALSE, matrix);
   glUniformMatrix3x2fv(uniform_location(program, "m32", &locations_ok),
                        1, GL_FALSE, matrix);
   glUniformMatrix3fv(uniform_location(program, "m3", &locations_ok),
                      1, GL_FALSE, matrix);
   glUniformMatrix3x4fv(uniform_location(program, "m34", &locations_ok),
                        1, GL_FALSE, matrix);
   glUniformMatrix4x2fv(uniform_location(program, "m42", &locations_ok),
                        1, GL_FALSE, matrix);
   glUniformMatrix4x3fv(uniform_location(program, "m43", &locations_ok),
                        1, GL_FALSE, matrix);
   glUniformMatrix4fv(uniform_location(program, "m4", &locations_ok),
                      1, GL_FALSE, matrix);

   glGenBuffers(1, &uniform_buffer);
   glBindBuffer(GL_UNIFORM_BUFFER, uniform_buffer);
   glBufferData(GL_UNIFORM_BUFFER, sizeof(fv), fv, GL_STATIC_DRAW);
   glBindBufferBase(GL_UNIFORM_BUFFER, 0, uniform_buffer);
   block_index = glGetUniformBlockIndex(program, "TestBlock");
   if (block_index != GL_INVALID_INDEX)
      glUniformBlockBinding(program, block_index, 0);

   attribute_location = glGetAttribLocation(program, "position");
   fragment_location = glGetFragDataLocation(program, "output_color");
   fragment_index = glGetFragDataIndex(program, "output_color");
   glGetProgramiv(program, GL_ACTIVE_ATTRIBUTES, &active_attributes);
   glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &active_uniforms);
   glGetActiveAttrib(program, 0, sizeof(active_name), &active_length,
                     &active_size, &active_type, active_name);
   glGetActiveUniform(program, 0, sizeof(active_name), &active_length,
                      &active_size, &active_type, active_name);
   {
      const GLchar *names[1] = {"f1"};
      glGetUniformIndices(program, 1, names, &uniform_index);
   }
   if (uniform_index != GL_INVALID_INDEX) {
      glGetActiveUniformsiv(program, 1, &uniform_index,
                            GL_UNIFORM_TYPE, &uniform_type);
      glGetActiveUniformName(program, uniform_index, sizeof(uniform_name),
                             &uniform_length, uniform_name);
   }
   if (block_index != GL_INVALID_INDEX) {
      glGetActiveUniformBlockiv(program, block_index,
                                GL_UNIFORM_BLOCK_DATA_SIZE, &block_size);
      glGetActiveUniformBlockiv(program, block_index,
                                GL_UNIFORM_BLOCK_BINDING, &block_binding);
      glGetActiveUniformBlockName(program, block_index, sizeof(block_name),
                                  &block_length, block_name);
   }
   glGetUniformfv(program, glGetUniformLocation(program, "f1"), &get_f);
   glGetUniformiv(program, glGetUniformLocation(program, "i1"), &get_i);
   glGetUniformuiv(program, glGetUniformLocation(program, "u1"), &get_u);
   glGetAttachedShaders(program, 2, &attached_count, attached);
   glGetTransformFeedbackVarying(program, 0, sizeof(xfb_name), &xfb_length,
                                 &xfb_size, &xfb_type, xfb_name);
   glGetShaderSource(shaders[0], sizeof(shader_source), &source_length,
                     shader_source);
   glValidateProgram(program);
   glGetProgramiv(program, GL_VALIDATE_STATUS, &validated);
   glFlush();

   identity_ok = glIsProgram(program) && glIsShader(shaders[0]) &&
                 glIsShader(shaders[1]) && glIsBuffer(uniform_buffer);
   reflection_ok = attribute_location == 3 && fragment_location == 1 &&
                   fragment_index == 0 && active_attributes == 1 &&
                   active_uniforms >= 20 && block_index != GL_INVALID_INDEX &&
                   uniform_index != GL_INVALID_INDEX &&
                   uniform_type == GL_FLOAT && strcmp(uniform_name, "f1") == 0 &&
                   block_size >= 16 && block_binding == 0 &&
                   strcmp(block_name, "TestBlock") == 0 &&
                   attached_count == 2 && source_length > 0 &&
                   strstr(shader_source, "#version 330 core") != NULL &&
                   xfb_length > 0 && xfb_size == 1 && xfb_type == GL_FLOAT &&
                   strcmp(xfb_name, "sink_value") == 0 &&
                   get_f == 1.0f && get_i == 1 && get_u == 1;

   glDetachShader(program, shaders[0]);
   glDetachShader(program, shaders[1]);
   glGetAttachedShaders(program, 2, &attached_count, attached);
   error = glGetError();
   passed = major == 1 && minor == 4 && locations_ok && identity_ok &&
            reflection_ok && validated && attached_count == 0 &&
            error == GL_NO_ERROR;
   printf("[ps5-egl-program-api] locations=%d identity=%d active=%d/%d "
          "attr=%d frag=%d/%d uniform=%u/%x block=%u/%d/%d "
          "validate=%d attached=%d error=0x%x result=%d\n",
          locations_ok, identity_ok, active_attributes, active_uniforms,
          attribute_location, fragment_location, fragment_index,
          uniform_index, uniform_type, block_index, block_size, block_binding,
          validated, attached_count, error, passed ? 0 : 1);

cleanup:
   if (made_current) {
      if (uniform_buffer)
         glDeleteBuffers(1, &uniform_buffer);
      if (program)
         glDeleteProgram(program);
      for (unsigned i = 0; i < 2; ++i)
         if (shaders[i])
            glDeleteShader(shaders[i]);
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
   printf("[ps5-egl-program-api] cleanup=%u result=%d\n",
          cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
