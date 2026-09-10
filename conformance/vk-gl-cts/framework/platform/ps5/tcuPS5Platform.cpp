// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tcuPS5Platform.hpp"

#include "gluContextFactory.hpp"
#include "gluRenderConfig.hpp"
#include "gluRenderContext.hpp"
#include "glwFunctionLoader.hpp"
#include "glwFunctions.hpp"
#include "glwEnums.hpp"
#include "tcuCommandLine.hpp"
#include "tcuRenderTarget.hpp"
#include "tcuTestCase.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstdio>
#include <vector>

namespace tcu {
namespace ps5 {
namespace {

class PublicFunctionLoader : public glw::FunctionLoader {
public:
  glw::GenericFuncType get(const char *name) const override {
    return reinterpret_cast<glw::GenericFuncType>(eglGetProcAddress(name));
  }
};

class RenderContext : public glu::RenderContext {
public:
  RenderContext(const glu::RenderConfig &config,
                const glu::RenderContext *sharedContext);
  ~RenderContext(void) override;

  glu::ContextType getType(void) const override { return m_type; }

  const glw::Functions &getFunctions(void) const override {
    return m_functions;
  }

  const tcu::RenderTarget &getRenderTarget(void) const override {
    return m_target;
  }

  glw::GenericFuncType getProcAddress(const char *name) const override {
    return PublicFunctionLoader().get(name);
  }

  void postIterate(void) override {
    m_functions.finish();
  }

  void makeCurrent(void) override;

  EGLContext eglContext(void) const { return m_context; }

private:
  void release(void);

  const glu::ContextType m_type;
  const int m_width;
  const int m_height;
  EGLDisplay m_display;
  EGLSurface m_surface;
  EGLContext m_context;
  glw::Functions m_functions;
  tcu::RenderTarget m_target;
};

class ContextFactory : public glu::ContextFactory {
public:
  ContextFactory(void)
      : glu::ContextFactory("ps5", "PS5 public EGL pbuffer context") {}

  glu::RenderContext *
  createContext(const glu::RenderConfig &config, const tcu::CommandLine &,
                const glu::RenderContext *sharedContext) const override {
    return new RenderContext(config, sharedContext);
  }
};

void requireEGL(EGLBoolean result, const char *operation) {
  if (result != EGL_TRUE)
    throw tcu::ResourceError(operation);
}

RenderContext::RenderContext(const glu::RenderConfig &config,
                             const glu::RenderContext *sharedContext)
    : m_type(config.type),
      m_width(glu::getValueOrDefault(config, &glu::RenderConfig::width, 256)),
      m_height(glu::getValueOrDefault(config, &glu::RenderConfig::height, 256)),
      m_display(EGL_NO_DISPLAY), m_surface(EGL_NO_SURFACE),
      m_context(EGL_NO_CONTEXT) {
  try {
    if (!glu::isContextTypeGLCore(m_type) || m_type.getMajorVersion() != 3 ||
        m_type.getMinorVersion() != 3)
      throw tcu::NotSupportedError(
          "PS5 CTS target supports OpenGL 3.3 core only");

    const glu::ContextFlags unsupported =
        glu::ContextFlags(glu::CONTEXT_ROBUST | glu::CONTEXT_NO_ERROR);
    if ((m_type.getFlags() & unsupported) != 0)
      throw tcu::NotSupportedError(
          "Requested OpenGL context flags are unsupported");
    if (config.surfaceType != glu::RenderConfig::SURFACETYPE_DONT_CARE &&
        config.surfaceType != glu::RenderConfig::SURFACETYPE_OFFSCREEN_GENERIC)
      throw tcu::NotSupportedError("PS5 CTS adapter currently supports pbuffers only");
    if (config.componentType == glu::RenderConfig::COMPONENT_TYPE_FLOAT)
      throw tcu::NotSupportedError("PS5 EGL has no floating-point pbuffer config");

    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_display == EGL_NO_DISPLAY)
      throw tcu::ResourceError("eglGetDisplay failed");

    requireEGL(eglInitialize(m_display, nullptr, nullptr),
               "eglInitialize failed");
    requireEGL(eglBindAPI(EGL_OPENGL_API), "eglBindAPI failed");

    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE,
        EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT,
        EGL_RED_SIZE,
        glu::getValueOrDefault(config, &glu::RenderConfig::redBits, 8),
        EGL_GREEN_SIZE,
        glu::getValueOrDefault(config, &glu::RenderConfig::greenBits, 8),
        EGL_BLUE_SIZE,
        glu::getValueOrDefault(config, &glu::RenderConfig::blueBits, 8),
        EGL_ALPHA_SIZE,
        glu::getValueOrDefault(config, &glu::RenderConfig::alphaBits, 8),
        EGL_DEPTH_SIZE,
        glu::getValueOrDefault(config, &glu::RenderConfig::depthBits, 32),
        EGL_STENCIL_SIZE,
        glu::getValueOrDefault(config, &glu::RenderConfig::stencilBits, 8),
        EGL_SAMPLES,
        glu::getValueOrDefault(config, &glu::RenderConfig::numSamples, 0),
        EGL_CONFIG_ID,
        glu::getValueOrDefault(config, &glu::RenderConfig::id, EGL_DONT_CARE),
        EGL_NONE,
    };
    EGLConfig eglConfig = nullptr;
    EGLint configCount = 0;
    requireEGL(eglChooseConfig(m_display, configAttributes, &eglConfig, 1,
                               &configCount),
               "eglChooseConfig failed");
    if (configCount != 1)
      throw tcu::NotSupportedError("No matching PS5 EGL pbuffer config");

    const EGLint surfaceAttributes[] = {
        EGL_WIDTH, m_width, EGL_HEIGHT, m_height, EGL_NONE,
    };
    m_surface =
        eglCreatePbufferSurface(m_display, eglConfig, surfaceAttributes);
    if (m_surface == EGL_NO_SURFACE)
      throw tcu::ResourceError("eglCreatePbufferSurface failed");

    EGLint contextFlags = 0;
    if ((m_type.getFlags() & glu::CONTEXT_DEBUG) != 0)
      contextFlags |= EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR;
    if ((m_type.getFlags() & glu::CONTEXT_FORWARD_COMPATIBLE) != 0)
      contextFlags |= EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR;
    const EGLint contextAttributes[] = {
        EGL_CONTEXT_MAJOR_VERSION_KHR,
        3,
        EGL_CONTEXT_MINOR_VERSION_KHR,
        3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
        EGL_CONTEXT_FLAGS_KHR,
        contextFlags,
        EGL_NONE,
    };

    const RenderContext *shared =
        dynamic_cast<const RenderContext *>(sharedContext);
    if (sharedContext != nullptr && shared == nullptr)
      throw tcu::NotSupportedError("Cannot share with a foreign context type");
    m_context = eglCreateContext(m_display, eglConfig,
                                 shared ? shared->eglContext() : EGL_NO_CONTEXT,
                                 contextAttributes);
    if (m_context == EGL_NO_CONTEXT)
      throw tcu::ResourceError("eglCreateContext failed");

    makeCurrent();
    const PublicFunctionLoader loader;
    glu::initFunctions(&m_functions, &loader, m_type.getAPI());

    EGLint width = 0, height = 0;
    requireEGL(eglQuerySurface(m_display, m_surface, EGL_WIDTH, &width),
               "eglQuerySurface width failed");
    requireEGL(eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &height),
               "eglQuerySurface height failed");
    const auto attribute = [&](EGLint name) {
      EGLint value = 0;
      requireEGL(eglGetConfigAttrib(m_display, eglConfig, name, &value),
                 "eglGetConfigAttrib failed");
      return value;
    };
    glw::GLint doubleBuffered = 0;
    m_functions.getIntegerv(GL_DOUBLEBUFFER, &doubleBuffered);
    const glw::GLenum colorAttachment = doubleBuffered ? GL_BACK_LEFT : GL_FRONT_LEFT;
    const auto bits = [&](glw::GLenum attachment, glw::GLenum name) {
      glw::GLint value = 0;
      m_functions.getFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, attachment, name, &value);
      if (m_functions.getError() != GL_NO_ERROR)
        throw tcu::ResourceError("Default framebuffer attachment query failed");
      return value;
    };
    const int red = bits(colorAttachment, GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE);
    const int green = bits(colorAttachment, GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE);
    const int blue = bits(colorAttachment, GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE);
    const int alpha = bits(colorAttachment, GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE);
    const int depth = bits(GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE);
    const int stencil = bits(GL_STENCIL, GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE);
    glw::GLint samples = 0;
    m_functions.getIntegerv(GL_SAMPLES, &samples);
    if (m_functions.getError() != GL_NO_ERROR ||
        red != attribute(EGL_RED_SIZE) || green != attribute(EGL_GREEN_SIZE) ||
        blue != attribute(EGL_BLUE_SIZE) || alpha != attribute(EGL_ALPHA_SIZE) ||
        depth != attribute(EGL_DEPTH_SIZE) || stencil != attribute(EGL_STENCIL_SIZE) ||
        samples != attribute(EGL_SAMPLES))
      throw tcu::ResourceError("EGL config and actual default framebuffer disagree");
    m_target = tcu::RenderTarget(width, height, tcu::PixelFormat(red, green, blue, alpha),
                                 depth, stencil, samples);
    std::printf("[ps5-opengl-cts] egl-config=%d size=%dx%d rgba=%d/%d/%d/%d "
                "depth=%d stencil=%d samples=%d conformant=0x%x caveat=0x%x\n",
                attribute(EGL_CONFIG_ID), width, height, red, green, blue, alpha,
                depth, stencil, samples, attribute(EGL_CONFORMANT), attribute(EGL_CONFIG_CAVEAT));
  } catch (...) {
    release();
    throw;
  }
}

RenderContext::~RenderContext(void) { release(); }

void RenderContext::release(void) {
  if (m_display == EGL_NO_DISPLAY)
    return;
  if (eglGetCurrentContext() == m_context)
    (void)eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT);
  if (m_context != EGL_NO_CONTEXT)
    (void)eglDestroyContext(m_display, m_context);
  if (m_surface != EGL_NO_SURFACE)
    (void)eglDestroySurface(m_display, m_surface);
  m_context = EGL_NO_CONTEXT;
  m_surface = EGL_NO_SURFACE;
  m_display = EGL_NO_DISPLAY;
}

void RenderContext::makeCurrent(void) {
  requireEGL(eglMakeCurrent(m_display, m_surface, m_surface, m_context),
             "eglMakeCurrent failed");
}

} // anonymous namespace

Platform::Platform(void) {
  m_contextFactoryRegistry.registerFactory(new ContextFactory());
}

Platform::~Platform(void) {
  const EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display != EGL_NO_DISPLAY)
    (void)eglTerminate(display);
}

void Platform::getMemoryLimits(tcu::PlatformMemoryLimits &limits) const {
  limits.totalSystemMemory = 64u * 1024u * 1024u;
  limits.totalDeviceLocalMemory = 0;
  limits.deviceMemoryAllocationGranularity = 16u * 1024u;
  limits.devicePageSize = 16u * 1024u;
  limits.devicePageTableEntrySize = 8;
  limits.devicePageTableHierarchyLevels = 4;
}

} // namespace ps5
} // namespace tcu

tcu::Platform *createPlatform(void) { return new tcu::ps5::Platform(); }
