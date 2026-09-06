# Third-party projects and notices

Project-owned code uses the repository's [GPL-3.0 license](LICENSE). This does not
relicense upstream components. Preserve their notices and applicable per-file
licenses in derived sources and redistributed builds.

Sources are fetched at immutable revisions from [dependencies.json](dependencies.json).
No complete upstream source checkout, public payload SDK, vendor SDK or firmware
module is copied into this repository. Patches and the test inventory are included.

| Project | Use / modifications | License reference |
| --- | --- | --- |
| [Mesa 26.2.0](https://www.mesa3d.org/) | GLSL, NIR, Gallium, GL dispatch/state tracker, ACO/RADV, AMD AddressLib and utilities; PS5 patch included | Primarily MIT; component/file-specific licenses in [LICENSES/Mesa](LICENSES/Mesa) and fetched source |
| [OpenGNM PSBC](https://github.com/PS4-OpenGNM/opengnm-psbc) | Shader compiler; complete PS5 delta over public `a92a1228` included | [MIT](LICENSES/OpenGNM-PSBC.txt); retained Mesa file notices also apply |
| [OpenGNM](https://github.com/PS4-OpenGNM/opengnm) | Shader/container reference declarations | [MIT](LICENSES/OpenGNM.txt) |
| [SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers) | Compiler grammar/header inputs | Fetched `LICENSE` and per-file notices |
| [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | Declarations required by the compiler's RADV subset; not a Vulkan port | Fetched `LICENSE.md`; Apache-2.0/MIT by file |
| [VK-GL-CTS](https://github.com/KhronosGroup/VK-GL-CTS) | Inventory, runner and six disclosed platform/test adaptations | [Apache-2.0](LICENSES/VK-GL-CTS.txt) and component notices |
| [Dear ImGui 1.91.9b](https://github.com/ocornut/imgui) | Unmodified renderer/core in the examples | [MIT](LICENSES/Dear-ImGui.txt) |
| [NanoVG](https://github.com/memononen/nanovg) | Unmodified GL3 renderer | [zlib](LICENSES/NanoVG.txt); preserve embedded dependency notices |
| [Sokol](https://github.com/floooh/sokol) | Unmodified GL backend; selects existing 3.3 fallback | [zlib/libpng](LICENSES/Sokol.txt) |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | Public homebrew compilers, import libraries and C/C++ support; build prerequisite | SDK's component/per-file licenses; not a vendor SDK |
| [Native app boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | Native runtime, linker/container conversion, assets and folder assembly | Its GPL-3.0-or-later and retained component notices |
| [LLVM](https://llvm.org/) | Clang/LLD, compiler builtins, libc++, libc++abi and libunwind | Apache-2.0 with LLVM exceptions or component-specific notices |
| [zlib](https://zlib.net/) | Native boilerplate build-time compression | zlib license in fetched source |

The build also uses Python, GNU Make/binutils, Meson, Ninja, Mako, PyYAML,
packaging, glslang and SPIR-V Tools. These are host tools, not bundled runtime
implementations; their own projects retain their licenses.

The minimal TV-demo pad declarations were adapted from the independently authored
`ps5-input-investigation` research header; no vendor header is included. Related
research references are in the main README. Historical test descriptions and
upstream names do not imply endorsement or formal conformance.

Before distributing binaries, retain the applicable runtime and dependency notices
alongside them and review their source-distribution requirements. This initial
publication contains source and derived validation data, not a binary release.
