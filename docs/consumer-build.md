# Using the SDK

After `make sdk`, consume the relocatable package under
`build/sdk/ps5-opengl-core33`. Applications use EGL, GL and KHR headers—not AGC
packages, Gallium types or private descriptors. Move the whole package together.
See [Building](building.md) for dependencies.

## Make

```make
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
include $(PS5_OPENGL_PREFIX)/share/ps5-opengl-core33/ps5-opengl-core33.mk
CPPFLAGS += $(PS5_OPENGL_PUBLIC_CFLAGS)

app.elf: app.o
	$(CXX) -o $@ $^ $(PS5_OPENGL_LDLIBS) $(PS5_OPENGL_LDFLAGS)
```

## pkg-config

```sh
PKG_CONFIG_PATH="$PS5_OPENGL_PREFIX/lib/pkgconfig" \
  pkg-config --cflags --libs ps5-opengl-core33
```

Compile C sources with the C compiler; use the C++ linker for the complete static
dependency graph. The verifier tests actual compilation/linking with these flags.

## CMake

```cmake
find_package(PS5OpenGLCore33 CONFIG REQUIRED)
target_link_libraries(app PRIVATE PS5OpenGLCore33::OpenGL)
set_property(TARGET app PROPERTY LINKER_LANGUAGE CXX)
```

Set `PS5OpenGLCore33_DIR` to the package's `lib/cmake/PS5OpenGLCore33` directory
and configure PS5 cross-compilers. The boilerplate separately assembles a linked
target into a runnable native folder application.

## Native application heap

The native example and CTS builders share [app_heap.c](../native-app/app_heap.c):
an app-owned, process-lifetime 128 MiB heap. The standalone graphics SDK does not
silently replace the application's allocator. Custom folder-app integrations
must include the helper and its complete linker wrap set together, as the native
builders do:

```text
--wrap=malloc --wrap=calloc --wrap=realloc --wrap=free --wrap=posix_memalign --wrap=malloc_usable_size
```

Foreign pointers retain their original allocator; partial wrapping is unsafe.
The performance-branch cube passes shader setup and 180 rendered frames with an
additional 8.3 MB malloc live. Larger budgets, exhaustive OOM recovery and general
cross-module ownership contracts are not established by this check.

## Verify the interface

```sh
bash tools/verify-installed-sdk.sh
```

This regenerates the SDK, verifies its manifest/metadata, links the triangle
through Make, pkg-config and CMake, and checks 344 Core exports. It does not
execute console tests. Do not regenerate a frozen package during a campaign.
