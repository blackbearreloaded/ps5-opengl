// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "glcPS5GL33PackageEntry.hpp"

#include "gl3cTestPackages.hpp"
#include "glcConfigPackage.hpp"
#include "tcuTestPackage.hpp"

namespace {

tcu::TestPackage *createGL33Package(tcu::TestContext &testCtx) {
  return new gl3cts::GL33TestPackage(testCtx, "KHR-GL33");
}

tcu::TestPackage *createConfigPackage(tcu::TestContext &testCtx) {
  return new glcts::ConfigPackage(testCtx, "CTS-Configs");
}

} // anonymous namespace

void glctsRegisterPS5GL33Package(void) {
  static bool registered = false;

  if (!registered) {
    tcu::TestPackageRegistry::getSingleton()->registerPackage(
        "KHR-GL33", createGL33Package);
    tcu::TestPackageRegistry::getSingleton()->registerPackage(
        "CTS-Configs", createConfigPackage);
    registered = true;
  }
}
