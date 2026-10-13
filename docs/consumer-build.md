# Using the SDK

After `make sdk`, consume the relocatable package under
`build/sdk/ps5-opengl-gl46`. Applications use EGL, GL and KHR headers—not AGC
packages, Gallium types or private descriptors. Move the whole package together.
See [Building](building.md) for dependencies.

## Make

```make
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
include $(PS5_OPENGL_PREFIX)/share/ps5-opengl/ps5-opengl.mk
CPPFLAGS += $(PS5_OPENGL_PUBLIC_CFLAGS)

app.elf: app.o
	$(CXX) -o $@ $^ $(PS5_OPENGL_LDLIBS) $(PS5_OPENGL_LDFLAGS)
```

## pkg-config

```sh
PKG_CONFIG_PATH="$PS5_OPENGL_PREFIX/lib/pkgconfig" \
  pkg-config --cflags --libs ps5-opengl
```

Compile C sources with the C compiler; use the C++ linker for the complete static
dependency graph. The verifier tests actual compilation/linking with these flags.

## CMake

```cmake
find_package(PS5OpenGL CONFIG REQUIRED)
target_link_libraries(app PRIVATE PS5OpenGL::OpenGL)
set_property(TARGET app PROPERTY LINKER_LANGUAGE CXX)
```

Set `PS5OpenGL_DIR` to the package's `lib/cmake/PS5OpenGL` directory
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
The validated cube passes shader setup and 180 rendered frames with an
additional 8.3 MB malloc live. Larger budgets, exhaustive OOM recovery and general
cross-module ownership contracts are not established by this check.

## Unrecoverable GPU submission errors

The native backend uses `_Exit(EXIT_FAILURE)` after a native submission
or suspend error, or when completion cannot be confirmed within the bounded
poll loop: 30 seconds of 1 ms sleeps (SDKs up to 1.0.1 gave up after 2). A
console confirms within milliseconds; the longer bound is for hosts that
compile shaders inside the first submissions. An SDK built with another
`-DPS5_RETIREMENT_WAIT_MS=<milliseconds>` in its runtime defines uses that
bound. It does not return to callers, release potentially in-flight
buffers, or run application exit handlers. This is application fail-stop, not
device-loss recovery or a console reset. Save important application state during
normal operation; do not depend on exit handlers after a GPU failure. Fault-path
tests run on the host; normal console regressions do not prove hardware recovery.
The same fail-stop rule applies to failed resource unmap/backing release. Backing
is never released after a failed unmap, including partial-allocation cleanup;
the driver does not claim recovery from these ownership errors.

## Driver statistics in the log

The SDK is built with its draw statistics compiled in. They are counted while
the application runs and printed to standard output once, when the OpenGL
context and the display are torn down. The `[ps5-batch-summary]` line on
standard error, which names the build's batching configuration, comes with them.

The periodic reports are opt-in. SDKs up to 1.0.1 printed about thirty lines
every 10,000 draws on the thread that draws, and one timing line for every
clear done on the CPU. An application that writes its log to console storage
line by line lost a frame of about 180 ms to each report. For a measurement,
turn them back on before the first draw:

```c
setenv("PS5_DRAW_PROFILE_LOG", "1", 1);
```

Only the value `1` turns them on, and the variable is read once. Lines that
report a failure or a fallback are not statistics and are always printed.

## Verify the interface

```sh
bash tools/verify-installed-sdk.sh
```

This regenerates the SDK, verifies its manifest/metadata, links the triangle
through Make, pkg-config and CMake, and checks all 657 OpenGL 4.6 Core exports
plus the 344-command OpenGL 3.3 compatibility surface. It does not execute
console tests. Do not regenerate a frozen package during a campaign.

The generic Make fragment, `ps5-opengl` pkg-config package and
`PS5OpenGL::OpenGL` CMake target are canonical. The installed Core33-named
aliases remain for source compatibility with existing OpenGL 3.3 ports.
