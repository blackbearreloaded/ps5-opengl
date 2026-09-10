// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef _TCUPS5PLATFORM_HPP
#define _TCUPS5PLATFORM_HPP

#include "egluPlatform.hpp"
#include "gluPlatform.hpp"
#include "tcuPlatform.hpp"

namespace tcu {
namespace ps5 {

class Platform : public tcu::Platform, private glu::Platform, private eglu::Platform {
public:
  Platform(void);
  ~Platform(void) override;

  const glu::Platform &getGLPlatform(void) const override {
    return static_cast<const glu::Platform &>(*this);
  }

  const eglu::Platform &getEGLPlatform(void) const override {
    return static_cast<const eglu::Platform &>(*this);
  }

  void getMemoryLimits(tcu::PlatformMemoryLimits &limits) const override;
};

} // namespace ps5
} // namespace tcu

#endif // _TCUPS5PLATFORM_HPP
