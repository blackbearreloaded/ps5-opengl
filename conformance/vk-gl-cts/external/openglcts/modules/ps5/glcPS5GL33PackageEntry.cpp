#include "glcPS5GL33PackageEntry.hpp"

#include "gl3cTestPackages.hpp"
#include "tcuTestPackage.hpp"

namespace {

tcu::TestPackage *createGL33Package(tcu::TestContext &testCtx) {
  return new gl3cts::GL33TestPackage(testCtx, "KHR-GL33");
}

} // anonymous namespace

void glctsRegisterPS5GL33Package(void) {
  static bool registered = false;

  if (!registered) {
    tcu::TestPackageRegistry::getSingleton()->registerPackage(
        "KHR-GL33", createGL33Package);
    registered = true;
  }
}
