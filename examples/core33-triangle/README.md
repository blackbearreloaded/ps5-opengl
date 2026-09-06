# Minimal Core 3.3 triangle

The smallest public EGL/OpenGL consumer: create a Core 3.3 context, compile/link
GLSL, upload vertex data, draw and clean up. No private driver headers are used.

Run `make sdk` at the repository root first. The installed Make example uses
`Makefile.installed`; the in-tree `Makefile` uses the source-tree link fragment.
`tools/verify-installed-sdk.sh` compiles and links this example through Make,
pkg-config and CMake. These linked targets are not standalone native folder apps;
use the boilerplate packaging path for console execution.

See [Using the SDK](../../docs/consumer-build.md), or build the ready-to-package
[ImGui TV demo](../core33-imgui/README.md) with `make imgui-demo`.
