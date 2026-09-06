#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>

#if defined(PS5_MULTI_DRAW_TEST) || defined(PS5_GEOMETRY_ADJACENCY_TEST)
int ps5_egl_current_draw_status(unsigned *draw_calls);
#endif

#define WIDTH 1920
#define HEIGHT 1080
#define CROP_WIDTH 64
#define CROP_HEIGHT 64
#ifdef PS5_PACKED_VERTEX_TEST
#define EXPECTED_PIXEL UINT32_C(0xff0000ff)
#elif defined(PS5_OCCLUSION_QUERY_TEST) || defined(PS5_DEPTH_CLAMP_TEST)
#define EXPECTED_PIXEL UINT32_C(0xff00ff00)
#else
#define EXPECTED_PIXEL UINT32_C(0xffff00ff)
#define EXPECTED_HASH UINT32_C(0x64e31dc5)
#endif

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
      printf("[ps5-egl-probe] shader type=0x%x compile=0 log=%.*s\n",
             type, length, log);
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

#ifdef PS5_CORE_33_TEST
static int
has_core_extension(const char *name)
{
   GLint count = 0;

   glGetIntegerv(GL_NUM_EXTENSIONS, &count);
   for (GLint index = 0; index < count; ++index) {
      const char *extension = (const char *)glGetStringi(
         GL_EXTENSIONS, (GLuint)index);
      if (extension && strcmp(extension, name) == 0)
         return 1;
   }
   return 0;
}
#endif

int
main(void)
{
#ifdef PS5_PACKED_VERTEX_TEST
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec4 a_position;\n"
      "attribute vec4 a_color;\n"
      "varying vec4 v_color;\n"
      "void main() {\n"
      "   gl_Position = vec4(a_position.xy, 0.0, 1.0);\n"
      "   v_color = a_color;\n"
      "}\n";
#elif defined(PS5_DEPTH_CLAMP_TEST) && defined(PS5_CORE_33_TEST)
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec3 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 1.0); }\n";
#elif defined(PS5_GEOMETRY_VARYING_TEST) || \
      defined(PS5_GEOMETRY_VARYING_LIMIT_TEST) || \
      defined(PS5_DEFAULT_UNIFORM_LIMIT_TEST)
   static const char *vertex_source =
      "#version 330 core\n"
      "layout(location = 0) in vec2 a_position;\n"
#ifdef PS5_DEFAULT_UNIFORM_LIMIT_TEST
      "uniform vec4 vertex_uniforms[256];\n"
      "void main() { gl_Position = vec4(a_position +\n"
      "   vertex_uniforms[gl_VertexID].xy, 0.0, 1.0); }\n";
#else
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
#endif
#elif defined(PS5_GLSL_330_TEST)
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
#elif defined(PS5_DUAL_SOURCE_BLEND_TEST) && defined(PS5_CORE_33_TEST)
   static const char *vertex_source =
      "#version 330\n"
      "layout(location = 0) in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
#elif defined(PS5_DUAL_SOURCE_BLEND_TEST)
   static const char *vertex_source =
      "#version 130\n"
      "in vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
#elif defined(PS5_DEPTH_CLAMP_TEST)
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec3 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 1.0); }\n";
#else
   static const char *vertex_source =
      "#version 120\n"
      "attribute vec2 a_position;\n"
      "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
#endif
#ifdef PS5_PACKED_VERTEX_TEST
   static const char *fragment_source =
      "#version 120\n"
      "varying vec4 v_color;\n"
      "void main() { gl_FragColor = v_color; }\n";
#elif defined(PS5_GEOMETRY_VARYING_LIMIT_TEST)
   static const char *fragment_source =
      "#version 330 core\n"
      "in vec4 g_varyings[32];\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() {\n"
      "   color = vec4(0.0);\n"
      "   for (int slot = 0; slot < 32; ++slot)\n"
      "      color += g_varyings[slot];\n"
      "}\n";
#elif defined(PS5_DEFAULT_UNIFORM_LIMIT_TEST)
   static const char *fragment_source =
      "#version 330 core\n"
      "uniform vec4 fragment_uniforms[256];\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { color = vec4(1.0, 0.0, 1.0, 1.0) +\n"
      "   fragment_uniforms[int(gl_FragCoord.x) & 255]; }\n";
#elif defined(PS5_GEOMETRY_VARYING_TEST)
   static const char *fragment_source =
      "#version 330 core\n"
      "in vec4 g_color;\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() { color = g_color; }\n";
#elif defined(PS5_GLSL_330_TEST)
   static const char *fragment_source =
      "#version 330\n"
      "layout(location = 0) out vec4 color;\n"
      "void main() {\n"
      "   uint one = floatBitsToUint(1.0);\n"
      "   color = vec4(uintBitsToFloat(one), 0.0, 1.0, 1.0);\n"
      "}\n";
#elif defined(PS5_DUAL_SOURCE_BLEND_TEST) && defined(PS5_CORE_33_TEST)
   static const char *fragment_source =
      "#version 330\n"
      "out vec4 primary;\n"
      "out vec4 secondary;\n"
      "void main() {\n"
      "   primary = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "   secondary = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
#elif defined(PS5_DUAL_SOURCE_BLEND_TEST)
   static const char *fragment_source =
      "#version 130\n"
      "out vec4 primary;\n"
      "out vec4 secondary;\n"
      "void main() {\n"
      "   primary = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "   secondary = vec4(1.0, 0.0, 0.0, 1.0);\n"
      "}\n";
#else
   static const char *fragment_source =
      "#version 120\n"
      "void main() { gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); }\n";
#endif
#ifdef PS5_GEOMETRY_ADJACENCY_TEST
   static const char *geometry_sources[4] = {
      "#version 330 core\n"
      "layout(lines_adjacency) in;\n"
      "layout(triangle_strip, max_vertices = 3) out;\n"
      "void main(){for(int i=0;i<3;++i){gl_Position=gl_in[i].gl_Position;"
      "EmitVertex();}EndPrimitive();}\n",
      "#version 330 core\n"
      "layout(lines_adjacency) in;\n"
      "layout(triangle_strip, max_vertices = 3) out;\n"
      "void main(){for(int i=0;i<3;++i){gl_Position=gl_in[i].gl_Position;"
      "EmitVertex();}EndPrimitive();}\n",
      "#version 330 core\n"
      "layout(triangles_adjacency) in;\n"
      "layout(triangle_strip, max_vertices = 3) out;\n"
      "void main(){const int v[3]=int[3](0,4,2);for(int i=0;i<3;++i){"
      "gl_Position=gl_in[v[i]].gl_Position;EmitVertex();}EndPrimitive();}\n",
      "#version 330 core\n"
      "layout(triangles_adjacency) in;\n"
      "layout(triangle_strip, max_vertices = 3) out;\n"
      "void main(){const int v[3]=int[3](0,4,2);for(int i=0;i<3;++i){"
      "gl_Position=gl_in[v[i]].gl_Position;EmitVertex();}EndPrimitive();}\n",
   };
#elif defined(PS5_GEOMETRY_MINIMAL_TEST) || \
      defined(PS5_GEOMETRY_VARYING_TEST) || \
      defined(PS5_GEOMETRY_VARYING_LIMIT_TEST) || \
      defined(PS5_DEFAULT_UNIFORM_LIMIT_TEST)
   static const char *geometry_source =
      "#version 330 core\n"
      "layout(triangles) in;\n"
      "layout(triangle_strip, max_vertices = 3) out;\n"
#ifdef PS5_GEOMETRY_VARYING_TEST
      "out vec4 g_color;\n"
#elif defined(PS5_GEOMETRY_VARYING_LIMIT_TEST)
      "out vec4 g_varyings[32];\n"
#elif defined(PS5_DEFAULT_UNIFORM_LIMIT_TEST)
      "uniform vec4 geometry_uniforms[256];\n"
#endif
      "void main() {\n"
      "   for (int i = 0; i < 3; ++i) {\n"
      "      gl_Position = gl_in[i].gl_Position;\n"
#ifdef PS5_GEOMETRY_VARYING_TEST
      "      g_color = vec4(1.0, 0.0, 1.0, 1.0) *\n"
      "                gl_in[i].gl_Position.w;\n"
#elif defined(PS5_GEOMETRY_VARYING_LIMIT_TEST)
      "      for (int slot = 0; slot < 32; ++slot)\n"
      "         g_varyings[slot] = vec4(0.03125, 0.0, 0.03125, 0.03125) *\n"
      "                            gl_in[i].gl_Position.w;\n"
#elif defined(PS5_DEFAULT_UNIFORM_LIMIT_TEST)
      "      gl_Position += vec4(geometry_uniforms[i].xy, 0.0, 0.0);\n"
#endif
      "      EmitVertex();\n"
      "   }\n"
      "   EndPrimitive();\n"
      "}\n";
#endif
#if defined(PS5_OCCLUSION_QUERY_TEST) || defined(PS5_DEPTH_CLAMP_TEST)
   static const char *green_fragment_source =
#ifdef PS5_CORE_33_TEST
      "#version 330\n"
      "out vec4 color;\n"
      "void main() { color = vec4(0.0, 1.0, 0.0, 1.0); }\n";
   static const char *red_fragment_source =
      "#version 330\n"
      "out vec4 color;\n"
      "void main() { color = vec4(1.0, 0.0, 0.0, 1.0); }\n";
#else
      "#version 120\n"
      "void main() { gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }\n";
   static const char *red_fragment_source =
      "#version 120\n"
      "void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";
#endif
#endif
#ifdef PS5_DUAL_SOURCE_BLEND_TEST
#ifdef PS5_CORE_33_TEST
   static const char *blue_fragment_source =
      "#version 330\n"
      "out vec4 color;\n"
      "void main() { color = vec4(0.0, 0.0, 1.0, 1.0); }\n";
#else
   static const char *blue_fragment_source =
      "#version 130\n"
      "out vec4 color;\n"
      "void main() { color = vec4(0.0, 0.0, 1.0, 1.0); }\n";
#endif
#endif
#ifdef PS5_PACKED_VERTEX_TEST
#define PACK_SNORM10(x, y) \
   (((uint32_t)(x) & UINT32_C(0x3ff)) | \
    (((uint32_t)(y) & UINT32_C(0x3ff)) << 10) | (UINT32_C(1) << 30))
   static const uint32_t vertices[6] = {
      PACK_SNORM10(-256, -256), UINT32_C(0xffff0000),
      PACK_SNORM10( 256, -256), UINT32_C(0xffff0000),
      PACK_SNORM10(   0,  256), UINT32_C(0xffff0000),
   };
#undef PACK_SNORM10
#elif defined(PS5_DEPTH_CLAMP_TEST)
   static const float vertices[9] = {
      -0.5f, -0.5f, 2.0f,
       0.5f, -0.5f, 2.0f,
      0.0f,  0.5f, 2.0f,
   };
#elif defined(PS5_GEOMETRY_ADJACENCY_TEST)
   static const float vertices[12] = {
      -0.5f, -0.5f,  0.5f, -0.5f,
       0.0f,  0.5f, -0.8f,  0.0f,
       0.5f, -0.5f,  0.8f,  0.0f,
   };
#else
   static const float vertices[6] = {
      -0.5f, -0.5f,
       0.5f, -0.5f,
       0.0f,  0.5f,
   };
#endif
   static uint32_t pixels[CROP_WIDTH * CROP_HEIGHT];
   const EGLint config_attributes[] = {
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_NONE,
   };
#ifdef PS5_CORE_33_TEST
   const EGLint context_attributes[] = {
      EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
      EGL_CONTEXT_MINOR_VERSION_KHR, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
      EGL_NONE,
   };
#endif
   EGLDisplay display = EGL_NO_DISPLAY;
   EGLConfig config = NULL;
   EGLSurface surface = EGL_NO_SURFACE;
   EGLContext context = EGL_NO_CONTEXT;
   EGLint egl_major = 0, egl_minor = 0, count = 0;
   EGLint width = 0, height = 0, context_version = 0;
   GLuint vertex_shader = 0, fragment_shader = 0, program = 0, buffer = 0;
#ifdef PS5_GEOMETRY_ADJACENCY_TEST
   GLuint geometry_shaders[4] = {0}, adjacency_programs[4] = {0};
   unsigned adjacency_draw_calls = 0;
   int adjacency_draw_status = -1;
#elif defined(PS5_GEOMETRY_MINIMAL_TEST) || \
      defined(PS5_GEOMETRY_VARYING_TEST) || \
      defined(PS5_GEOMETRY_VARYING_LIMIT_TEST) || \
      defined(PS5_DEFAULT_UNIFORM_LIMIT_TEST)
   GLuint geometry_shader = 0;
#endif
#ifdef PS5_DEFAULT_UNIFORM_LIMIT_TEST
   static const GLfloat zero_uniforms[256 * 4];
   GLint uniform_locations[3] = {-1, -1, -1};
   int uniform_limit_ok = 0;
#endif
#ifdef PS5_MULTI_DRAW_TEST
   GLuint index_buffer = 0;
   unsigned draw_calls = 0;
   int draw_status = -1;
#endif
#ifdef PS5_SYNC_TEST
   GLsync sync = 0;
   GLenum sync_wait = GL_WAIT_FAILED;
   GLint sync_type = 0, sync_condition = 0, sync_status = 0, sync_flags = -1;
   GLsizei sync_length = 0;
   int sync_live = 0, sync_deleted = 0, sync_queries = 1;
#endif
#ifdef PS5_CORE_33_TEST
   GLuint vertex_array = 0;
   EGLint profile_mask = 0;
   int create_context_extension = 0;
#endif
#if defined(PS5_OCCLUSION_QUERY_TEST) || defined(PS5_DEPTH_CLAMP_TEST)
   GLuint green_shader = 0, red_shader = 0;
   GLuint green_program = 0, red_program = 0;
#endif
#ifdef PS5_DUAL_SOURCE_BLEND_TEST
   GLuint blue_shader = 0, blue_program = 0;
#endif
#ifdef PS5_OCCLUSION_QUERY_TEST
   GLuint queries[2] = {0, 0};
   GLuint samples = 0, any_samples = 0;
#endif
#ifdef PS5_DEPTH_CLAMP_TEST
   int depth_clamp_extension = 0;
#endif
#ifdef PS5_GLSL_330_TEST
   int glsl330_extensions = 0;
#endif
#ifdef PS5_PACKED_VERTEX_TEST
   int packed_vertex_extensions = 0;
#endif
#ifdef PS5_DUAL_SOURCE_BLEND_TEST
   int dual_source_extension = 0;
   GLint max_dual_source_targets = 0;
#endif
   GLint linked = GL_FALSE;
   const GLubyte *gl_version = NULL, *glsl_version = NULL;
   const GLubyte *gl_vendor = NULL, *gl_renderer = NULL;
   unsigned matching = 0, unexpected = 0;
   uint32_t pixel_hash = 0;
   GLenum gl_error = GL_NO_ERROR;
   GLenum cleanup_gl_error = GL_NO_ERROR;
   EGLint cleanup_egl_error = EGL_SUCCESS;
   EGLBoolean cleanup_ok = EGL_TRUE;
   int made_current = 0;
   int passed = 0;

   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (display == EGL_NO_DISPLAY ||
       !eglInitialize(display, &egl_major, &egl_minor) ||
       !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(display, config_attributes, &config, 1, &count) ||
       count != 1)
      goto cleanup;

   surface = eglCreateWindowSurface(display, config,
                                    (EGLNativeWindowType)0, NULL);
#ifdef PS5_CORE_33_TEST
   create_context_extension = strstr(
      eglQueryString(display, EGL_EXTENSIONS),
      "EGL_KHR_create_context") != NULL;
   context = eglCreateContext(display, config, EGL_NO_CONTEXT,
                              context_attributes);
#else
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
#endif
   if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
       !eglMakeCurrent(display, surface, surface, context) ||
       !eglSwapInterval(display, 0))
      goto cleanup;
   made_current = 1;

   if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
       !eglQuerySurface(display, surface, EGL_HEIGHT, &height) ||
       !eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION,
                        &context_version))
      goto cleanup;
#ifdef PS5_CORE_33_TEST
   if (!eglQueryContext(display, context,
                        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, &profile_mask))
      goto cleanup;
#endif
   printf("[ps5-egl-probe] egl=%d.%d vendor=%s version=%s apis=%s surface=%dx%d context=%d\n",
          egl_major, egl_minor, eglQueryString(display, EGL_VENDOR),
          eglQueryString(display, EGL_VERSION),
          eglQueryString(display, EGL_CLIENT_APIS), width, height,
          context_version);
   gl_version = glGetString(GL_VERSION);
   glsl_version = glGetString(GL_SHADING_LANGUAGE_VERSION);
   gl_vendor = glGetString(GL_VENDOR);
   gl_renderer = glGetString(GL_RENDERER);
   if (!gl_version || !glsl_version || !gl_vendor || !gl_renderer)
      goto cleanup;
   printf("[ps5-egl-probe] gl version=%s glsl=%s vendor=%s renderer=%s\n",
          gl_version, glsl_version, gl_vendor, gl_renderer);
#ifdef PS5_GLSL_330_TEST
#ifdef PS5_CORE_33_TEST
   glsl330_extensions =
      has_core_extension("GL_ARB_explicit_attrib_location") &&
      has_core_extension("GL_ARB_shader_bit_encoding");
#else
   glsl330_extensions =
      strstr((const char *)glGetString(GL_EXTENSIONS),
             "GL_ARB_explicit_attrib_location") != NULL &&
      strstr((const char *)glGetString(GL_EXTENSIONS),
             "GL_ARB_shader_bit_encoding") != NULL;
#endif
#endif
#ifdef PS5_PACKED_VERTEX_TEST
   packed_vertex_extensions =
      strstr((const char *)glGetString(GL_EXTENSIONS),
             "GL_ARB_vertex_type_2_10_10_10_rev") != NULL &&
      strstr((const char *)glGetString(GL_EXTENSIONS),
             "GL_EXT_vertex_array_bgra") != NULL;
#endif
#ifdef PS5_DUAL_SOURCE_BLEND_TEST
#ifdef PS5_CORE_33_TEST
   dual_source_extension = has_core_extension(
      "GL_ARB_blend_func_extended");
#else
   dual_source_extension =
      strstr((const char *)glGetString(GL_EXTENSIONS),
             "GL_ARB_blend_func_extended") != NULL;
#endif
   glGetIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS,
                 &max_dual_source_targets);
#endif

   if (compile_shader(GL_VERTEX_SHADER, vertex_source, &vertex_shader) ||
       compile_shader(GL_FRAGMENT_SHADER, fragment_source, &fragment_shader))
      goto cleanup;
#if defined(PS5_GEOMETRY_MINIMAL_TEST) || \
    defined(PS5_GEOMETRY_VARYING_TEST) || \
    defined(PS5_GEOMETRY_VARYING_LIMIT_TEST) || \
    defined(PS5_DEFAULT_UNIFORM_LIMIT_TEST)
   if (compile_shader(GL_GEOMETRY_SHADER, geometry_source, &geometry_shader))
      goto cleanup;
#endif
#ifdef PS5_GEOMETRY_ADJACENCY_TEST
   for (unsigned i = 0; i < 4; ++i) {
      if (compile_shader(GL_GEOMETRY_SHADER, geometry_sources[i],
                         &geometry_shaders[i]))
         goto cleanup;
      adjacency_programs[i] = glCreateProgram();
      if (!adjacency_programs[i])
         goto cleanup;
      glAttachShader(adjacency_programs[i], vertex_shader);
      glAttachShader(adjacency_programs[i], geometry_shaders[i]);
      glAttachShader(adjacency_programs[i], fragment_shader);
      glBindAttribLocation(adjacency_programs[i], 0, "a_position");
      glLinkProgram(adjacency_programs[i]);
      glGetProgramiv(adjacency_programs[i], GL_LINK_STATUS, &linked);
      if (!linked)
         goto cleanup;
   }
   glUseProgram(adjacency_programs[0]);
#else
   program = glCreateProgram();
   glAttachShader(program, vertex_shader);
#if defined(PS5_GEOMETRY_MINIMAL_TEST) || \
    defined(PS5_GEOMETRY_VARYING_TEST) || \
    defined(PS5_GEOMETRY_VARYING_LIMIT_TEST) || \
    defined(PS5_DEFAULT_UNIFORM_LIMIT_TEST)
   glAttachShader(program, geometry_shader);
#endif
   glAttachShader(program, fragment_shader);
   glBindAttribLocation(program, 0, "a_position");
#ifdef PS5_DUAL_SOURCE_BLEND_TEST
   glBindFragDataLocationIndexed(program, 0, 0, "primary");
   glBindFragDataLocationIndexed(program, 0, 1, "secondary");
#endif
#ifdef PS5_PACKED_VERTEX_TEST
   glBindAttribLocation(program, 1, "a_color");
#endif
   glLinkProgram(program);
   glGetProgramiv(program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(program);
#endif
#ifdef PS5_DEFAULT_UNIFORM_LIMIT_TEST
   uniform_locations[0] = glGetUniformLocation(program,
                                                "vertex_uniforms[0]");
   uniform_locations[1] = glGetUniformLocation(program,
                                                "geometry_uniforms[0]");
   uniform_locations[2] = glGetUniformLocation(program,
                                                "fragment_uniforms[0]");
   uniform_limit_ok = uniform_locations[0] >= 0 &&
                      uniform_locations[1] >= 0 &&
                      uniform_locations[2] >= 0;
   for (unsigned i = 0; i < 3 && uniform_limit_ok; ++i)
      glUniform4fv(uniform_locations[i], 256, zero_uniforms);
#endif
#ifdef PS5_CORE_33_TEST
   glGenVertexArrays(1, &vertex_array);
   glBindVertexArray(vertex_array);
#endif
   glGenBuffers(1, &buffer);
   glBindBuffer(GL_ARRAY_BUFFER, buffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
#ifdef PS5_PACKED_VERTEX_TEST
   glVertexAttribPointer(0, 4, GL_INT_2_10_10_10_REV, GL_TRUE,
                         2 * sizeof(uint32_t), NULL);
   glVertexAttribPointer(1, GL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE,
                         2 * sizeof(uint32_t),
                         (const void *)(uintptr_t)sizeof(uint32_t));
#elif defined(PS5_DEPTH_CLAMP_TEST)
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), NULL);
#else
   glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
#endif
   glEnableVertexAttribArray(0);
#ifdef PS5_PACKED_VERTEX_TEST
   glEnableVertexAttribArray(1);
#endif
   glViewport(0, 0, WIDTH, HEIGHT);
#if defined(PS5_OCCLUSION_QUERY_TEST) || defined(PS5_DEPTH_CLAMP_TEST)
   if (compile_shader(GL_FRAGMENT_SHADER, green_fragment_source,
                      &green_shader) ||
       compile_shader(GL_FRAGMENT_SHADER, red_fragment_source, &red_shader))
      goto cleanup;
   green_program = glCreateProgram();
   red_program = glCreateProgram();
   if (!green_program || !red_program)
      goto cleanup;
   glAttachShader(green_program, vertex_shader);
   glAttachShader(green_program, green_shader);
   glBindAttribLocation(green_program, 0, "a_position");
   glLinkProgram(green_program);
   glGetProgramiv(green_program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glAttachShader(red_program, vertex_shader);
   glAttachShader(red_program, red_shader);
   glBindAttribLocation(red_program, 0, "a_position");
   glLinkProgram(red_program);
   glGetProgramiv(red_program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;

#ifdef PS5_OCCLUSION_QUERY_TEST
   glGenQueries(2, queries);
   glBeginQuery(GL_SAMPLES_PASSED, queries[0]);
   glUseProgram(program);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glEndQuery(GL_SAMPLES_PASSED);
   glGetQueryObjectuiv(queries[0], GL_QUERY_RESULT, &samples);

   glBeginQuery(GL_ANY_SAMPLES_PASSED, queries[1]);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glEndQuery(GL_ANY_SAMPLES_PASSED);
   glGetQueryObjectuiv(queries[1], GL_QUERY_RESULT, &any_samples);

   glUseProgram(green_program);
   glBeginConditionalRender(queries[1], GL_QUERY_WAIT);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glEndConditionalRender();
   glUseProgram(red_program);
   glBeginConditionalRender(queries[0], GL_QUERY_WAIT_INVERTED);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glEndConditionalRender();
#else
#ifdef PS5_CORE_33_TEST
   depth_clamp_extension = has_core_extension("GL_ARB_depth_clamp");
#else
   depth_clamp_extension = strstr(
      (const char *)glGetString(GL_EXTENSIONS), "GL_ARB_depth_clamp") != NULL;
#endif
   glUseProgram(green_program);
   glEnable(GL_DEPTH_CLAMP);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glUseProgram(red_program);
   glEnable(GL_RASTERIZER_DISCARD);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glDisable(GL_RASTERIZER_DISCARD);
   glDisable(GL_DEPTH_CLAMP);
   glDrawArrays(GL_TRIANGLES, 0, 3);
#endif
#else
#ifdef PS5_DUAL_SOURCE_BLEND_TEST
   if (compile_shader(GL_FRAGMENT_SHADER, blue_fragment_source,
                      &blue_shader))
      goto cleanup;
   blue_program = glCreateProgram();
   if (!blue_program)
      goto cleanup;
   glAttachShader(blue_program, vertex_shader);
   glAttachShader(blue_program, blue_shader);
   glBindAttribLocation(blue_program, 0, "a_position");
   glBindFragDataLocationIndexed(blue_program, 0, 0, "color");
   glLinkProgram(blue_program);
   glGetProgramiv(blue_program, GL_LINK_STATUS, &linked);
   if (!linked)
      goto cleanup;
   glUseProgram(blue_program);
   glDrawArrays(GL_TRIANGLES, 0, 3);
   glUseProgram(program);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);
#endif
#ifdef PS5_MULTI_DRAW_TEST
   {
      static const GLint first[2] = {0, 0};
      static const GLsizei counts[2] = {3, 3};
      static const uint16_t indices[3] = {0, 1, 2};
      static const void *offsets[2] = {NULL, NULL};

      glMultiDrawArrays(GL_TRIANGLES, first, counts, 2);
      glGenBuffers(1, &index_buffer);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                   GL_STATIC_DRAW);
      glMultiDrawElements(GL_TRIANGLES, counts, GL_UNSIGNED_SHORT,
                          offsets, 2);
      draw_status = ps5_egl_current_draw_status(&draw_calls);
   }
#else
#ifdef PS5_GEOMETRY_ADJACENCY_TEST
   {
      static const GLenum modes[4] = {
         GL_LINES_ADJACENCY, GL_LINE_STRIP_ADJACENCY,
         GL_TRIANGLES_ADJACENCY, GL_TRIANGLE_STRIP_ADJACENCY,
      };
      static const GLsizei counts[4] = {4, 4, 6, 6};

      for (unsigned i = 0; i < 4; ++i) {
         glUseProgram(adjacency_programs[i]);
         glDrawArrays(modes[i], 0, counts[i]);
      }
      adjacency_draw_status =
         ps5_egl_current_draw_status(&adjacency_draw_calls);
   }
#else
   glDrawArrays(GL_TRIANGLES, 0, 3);
#endif
#endif
#endif
#ifdef PS5_SYNC_TEST
   sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
   if (sync) {
      sync_live = glIsSync(sync);
      sync_wait = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1);
      glGetSynciv(sync, GL_OBJECT_TYPE, 1, &sync_length, &sync_type);
      sync_queries &= sync_length == 1;
      glGetSynciv(sync, GL_SYNC_CONDITION, 1, &sync_length,
                  &sync_condition);
      sync_queries &= sync_length == 1;
      glGetSynciv(sync, GL_SYNC_STATUS, 1, &sync_length, &sync_status);
      sync_queries &= sync_length == 1;
      glGetSynciv(sync, GL_SYNC_FLAGS, 1, &sync_length, &sync_flags);
      sync_queries &= sync_length == 1;
      glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
      glDeleteSync(sync);
      sync_deleted = !glIsSync(sync);
      sync = 0;
   }
#endif
   glFinish();
   glReadPixels((WIDTH - CROP_WIDTH) / 2, (HEIGHT - CROP_HEIGHT) / 2,
                CROP_WIDTH, CROP_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   gl_error = glGetError();
   for (unsigned i = 0; i < CROP_WIDTH * CROP_HEIGHT; ++i) {
      matching += pixels[i] == EXPECTED_PIXEL;
      unexpected += pixels[i] != EXPECTED_PIXEL;
   }
   pixel_hash = hash32(pixels, sizeof(pixels));
   printf("[ps5-egl-probe] public-gl matching=%u unexpected=%u hash=%08x error=0x%x\n",
          matching, unexpected, pixel_hash, gl_error);
#ifdef PS5_OCCLUSION_QUERY_TEST
   printf("[ps5-egl-probe] occlusion samples=%u any=%u "
          "normal=draw inverted=skip\n", samples, any_samples);
#endif
#ifdef PS5_DEPTH_CLAMP_TEST
   printf("[ps5-egl-probe] depth-clamp extension=%u clamp=draw "
          "discard=skip clip=skip\n", depth_clamp_extension);
#endif
#ifdef PS5_GLSL_330_TEST
   printf("[ps5-egl-probe] glsl330 explicit-locations=1 bit-encoding=1 "
          "extensions=%u\n", glsl330_extensions);
#endif
#ifdef PS5_PACKED_VERTEX_TEST
   printf("[ps5-egl-probe] packed-vertex snorm2101010=position "
          "bgra8=color extensions=%u\n", packed_vertex_extensions);
#endif
#ifdef PS5_DUAL_SOURCE_BLEND_TEST
   printf("[ps5-egl-probe] dual-source-blend extension=%u max=%d "
          "src1=red destination=blue expected=magenta\n",
          dual_source_extension, max_dual_source_targets);
#endif
#ifdef PS5_MULTI_DRAW_TEST
   printf("[ps5-egl-probe] multi-draw status=%d submissions=%u "
          "arrays=2 elements=2\n", draw_status, draw_calls);
#endif
#ifdef PS5_SYNC_TEST
   printf("[ps5-egl-probe] sync live=%u wait=0x%x type=0x%x "
          "condition=0x%x status=0x%x flags=0x%x deleted=%u\n",
          sync_live, sync_wait, sync_type, sync_condition, sync_status,
          sync_flags, sync_deleted);
#endif
#ifdef PS5_GEOMETRY_MINIMAL_TEST
   printf("[ps5-egl-probe] geometry-minimal resource-free=1 output=triangle-strip\n");
#endif
#ifdef PS5_GEOMETRY_VARYING_TEST
   printf("[ps5-egl-probe] geometry-varying exact=rgba1011 dependency=position-w\n");
#endif
#ifdef PS5_GEOMETRY_VARYING_LIMIT_TEST
   printf("[ps5-egl-probe] geometry-varying-limit slots=32 components=128\n");
#endif
#ifdef PS5_DEFAULT_UNIFORM_LIMIT_TEST
   printf("[ps5-egl-probe] default-uniform-limit stages=3 components=1024 "
          "locations=%d/%d/%d\n", uniform_locations[0],
          uniform_locations[1], uniform_locations[2]);
#endif
#ifdef PS5_GEOMETRY_ADJACENCY_TEST
   printf("[ps5-egl-probe] geometry-adjacency native=10/11/12/13 draw=%d/%u\n",
          adjacency_draw_status, adjacency_draw_calls);
#endif
   if (!eglSwapBuffers(display, surface))
      goto cleanup;
   passed = egl_major == 1 && egl_minor == 4 && width == WIDTH &&
            height == HEIGHT &&
#ifdef PS5_CORE_33_TEST
            context_version == 3 && create_context_extension &&
            profile_mask == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
            strncmp((const char *)gl_version, "3.3 ", 4) == 0 &&
            strncmp((const char *)glsl_version, "3.30", 4) == 0 &&
#else
            context_version == 2 &&
            strncmp((const char *)gl_version, "2.1 ", 4) == 0 &&
#ifdef PS5_GLSL_330_TEST
            strncmp((const char *)glsl_version, "3.30", 4) == 0 &&
            glsl330_extensions &&
#else
            strncmp((const char *)glsl_version, "1.20", 4) == 0 &&
#endif
#endif
            matching == CROP_WIDTH * CROP_HEIGHT && !unexpected &&
#ifdef PS5_OCCLUSION_QUERY_TEST
             samples > 0 && any_samples == GL_TRUE &&
#endif
#ifdef PS5_DEPTH_CLAMP_TEST
            depth_clamp_extension &&
#endif
#ifdef PS5_PACKED_VERTEX_TEST
            packed_vertex_extensions &&
#endif
#ifdef PS5_DUAL_SOURCE_BLEND_TEST
            dual_source_extension && max_dual_source_targets == 1 &&
#endif
#ifdef PS5_MULTI_DRAW_TEST
            draw_status == 0 && draw_calls == 4 &&
#endif
#ifdef PS5_SYNC_TEST
            sync_live && sync_wait == GL_ALREADY_SIGNALED &&
            sync_queries && sync_type == GL_SYNC_FENCE &&
            sync_condition == GL_SYNC_GPU_COMMANDS_COMPLETE &&
            sync_status == GL_SIGNALED && sync_flags == 0 && sync_deleted &&
#endif
#ifdef PS5_GEOMETRY_ADJACENCY_TEST
            adjacency_draw_status == 0 && adjacency_draw_calls == 4 &&
#endif
#ifdef PS5_DEFAULT_UNIFORM_LIMIT_TEST
            uniform_limit_ok &&
#endif
#if !defined(PS5_OCCLUSION_QUERY_TEST) && !defined(PS5_DEPTH_CLAMP_TEST) && \
    !defined(PS5_PACKED_VERTEX_TEST)
            pixel_hash == EXPECTED_HASH &&
#endif
            gl_error == GL_NO_ERROR;

cleanup:
#ifdef PS5_SYNC_TEST
   if (sync)
      glDeleteSync(sync);
#endif
#ifdef PS5_MULTI_DRAW_TEST
   if (index_buffer)
      glDeleteBuffers(1, &index_buffer);
#endif
#ifdef PS5_CORE_33_TEST
   if (vertex_array)
      glDeleteVertexArrays(1, &vertex_array);
#endif
#ifdef PS5_DUAL_SOURCE_BLEND_TEST
   if (blue_program)
      glDeleteProgram(blue_program);
   if (blue_shader)
      glDeleteShader(blue_shader);
#endif
#ifdef PS5_OCCLUSION_QUERY_TEST
   if (queries[0] || queries[1])
      glDeleteQueries(2, queries);
#endif
#if defined(PS5_OCCLUSION_QUERY_TEST) || defined(PS5_DEPTH_CLAMP_TEST)
   if (red_program)
      glDeleteProgram(red_program);
   if (green_program)
      glDeleteProgram(green_program);
   if (red_shader)
      glDeleteShader(red_shader);
   if (green_shader)
      glDeleteShader(green_shader);
#endif
   if (buffer)
      glDeleteBuffers(1, &buffer);
   if (program)
      glDeleteProgram(program);
   if (fragment_shader)
      glDeleteShader(fragment_shader);
#ifdef PS5_GEOMETRY_ADJACENCY_TEST
   for (unsigned i = 0; i < 4; ++i) {
      if (adjacency_programs[i])
         glDeleteProgram(adjacency_programs[i]);
      if (geometry_shaders[i])
         glDeleteShader(geometry_shaders[i]);
   }
#elif defined(PS5_GEOMETRY_MINIMAL_TEST) || \
      defined(PS5_GEOMETRY_VARYING_TEST) || \
      defined(PS5_GEOMETRY_VARYING_LIMIT_TEST) || \
      defined(PS5_DEFAULT_UNIFORM_LIMIT_TEST)
   if (geometry_shader)
      glDeleteShader(geometry_shader);
#endif
   if (vertex_shader)
      glDeleteShader(vertex_shader);
   if (made_current)
      cleanup_gl_error = glGetError();
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
      cleanup_ok &= eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
      made_current = 0;
   }
   if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
      cleanup_ok &= eglDestroyContext(display, context);
   if (display != EGL_NO_DISPLAY && surface != EGL_NO_SURFACE)
      cleanup_ok &= eglDestroySurface(display, surface);
   if (display != EGL_NO_DISPLAY)
      cleanup_ok &= eglTerminate(display);
   cleanup_egl_error = eglGetError();
   passed = passed && cleanup_ok && cleanup_gl_error == GL_NO_ERROR &&
            cleanup_egl_error == EGL_SUCCESS;
   printf("[ps5-egl-probe] cleanup gl=0x%x egl=0x%x ok=%u result=%d\n",
          cleanup_gl_error, cleanup_egl_error, cleanup_ok, passed ? 0 : 1);
   return passed ? 0 : 1;
}
