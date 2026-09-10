# PS5 OpenGL

**OpenGL 3.3 Core and GLSL 3.30 for PlayStation 5 homebrew.**

A native graphics stack built on Mesa/Gallium, with runtime shader compilation,
fullscreen EGL presentation, a relocatable static SDK and an SDL2 bridge.
Applications use standard OpenGL; platform integration remains application-owned.

[![PS5 OpenGL ImGui demo with animated shapes and controller controls](docs/images/ps5-opengl-imgui.png)](https://i.imgur.com/jwyvPhT.mp4)

*Click the image to watch the demo.*

This project is experimental and **not Khronos-certified**. It does not provide
console enablement or guarantee that desktop applications run unchanged.

## Get started

[Download SDK 0.2.0](https://github.com/blackbearreloaded/ps5-opengl/releases/tag/sdk-0.2.0)
or [build from source](docs/building.md).

The downloadable SDK includes GL/EGL and SDL2 static libraries, headers,
Make/pkg-config/CMake integration, complete sources, examples, licenses and
checksums. Choose one complete profile; do not mix its libraries with another SDK.

| Profile | Qualification of SDK 0.2.0 downloads |
| --- | --- |
| **4K120** — 3840×2160 | 202 sampled CTS passes, 82 focused GPU cases and bounded application/lifecycle checks |
| **1440p120** — 2560×1440 | Build, export and consumer checks; not separately hardware-qualified |

See the [release guide](docs/release-g62.md) for checksums and scope.
[Older releases](https://github.com/blackbearreloaded/ps5-opengl/releases) retain
their own evidence. Fresh [CI-built archives](docs/ci-releases.md) are
host-checked, not automatically console-qualified.

**Source update:** the [EGL lifecycle safeguard](docs/lifecycle-reopen.md)
now enforces a five-second interval before reopening a closed high-refresh
presenter. Applications no longer need their own delay. Six EGL sessions passed
without logged HDMI reconnects; the 4K window benchmark retained 119.88 FPS.
This fix is in source, **not in the existing 0.2.0 downloads**.

## Features

- OpenGL 3.3 Core / GLSL 3.30 with Mesa state tracking and runtime shader compilation.
- Native fullscreen EGL and fixed 1080p60, 1440p120 and 4K120 build profiles.
- GPU-backed draws, batching and eligible transfer, clear and mip/layer paths.
- An SDL2 bridge for one fixed window, one unshared Core context and input/events.
- Public-API examples, pinned dependencies and reproducible host checks.

Some operations use CPU fallbacks. See [supported boundaries](docs/limitations.md)
before choosing the SDK for an application.

## Performance

| SDK 0.2.0 workload at 4K | Completed frames/s |
| --- | ---: |
| Dear ImGui window | 119.88 |
| Dear ImGui offscreen | 119.90 |
| 128 textured cubes, ordinary draws | 58.09 |
| 128 textured cubes, instanced draws | 117.41 |

These are 30-second workload measurements, not full-game FPS or perfect frame
pacing. Console logs confirmed native 2160p119.88 output for the window test;
offscreen throughput is not displayed FPS. See [performance and methodology](docs/performance.md).

## Examples

| Example | Demonstrates |
| --- | --- |
| [Triangle](examples/core33-triangle/README.md) | Minimal EGL/OpenGL application |
| [Dear ImGui](examples/core33-imgui/README.md) | Widgets, fonts, animated shapes and controller navigation |
| [NanoVG](examples/core33-nanovg/README.md) | Upstream GL3 vector renderer |
| [Sokol](examples/core33-sokol/README.md) | Existing OpenGL renderer integration |
| [Sokol cube](examples/core33-sokol-cube/README.md) | Rotation, depth testing and culling |
| [Textured cubes benchmark](examples/core33-cubes/README.md) | Ordinary versus instanced drawing |

After setting up the [build prerequisites](docs/building.md):

```sh
make source-fetch
make sdk
make imgui-demo
```

Deploy the generated folder `build/native-app/PPSA99005/dist/PPSA99005` to
`/data/homebrew/PPSA99005` in an already configured native homebrew environment.
Launch it as a registered title; do not send the graphics executable to an ELF
loader. See [SDK integration](docs/consumer-build.md) for your own application.

## Validation

The frozen full campaign accounts for **39,544 results** across four configurations:

| Classification | Results |
| --- | ---: |
| Pass | **37,404** |
| Individually reviewed `NotSupported` | **2,140** |
| Total accounted | **39,544** |

No gaps, duplicates or required-case failures remain in that accepted set.
**Accounted results are not all passes.** Later runtime changes have focused
regressions; they do not inherit the full campaign's acceptance.

The [validation report](docs/validation.md) identifies the tested binaries,
adaptations and exclusions. [Machine-readable evidence](validation/2026-09-07/README.md)
is included and checked by `make test`; that command does not rerun the PS5 campaign.

## Project Foundation

[PS5 GPU Research](https://github.com/blackbearreloaded/ps5-gpu-research)
documents the shader toolchain, GPU-visible memory, command submission,
synchronization and presentation findings that informed this implementation.
Related video-decoding research is separate from OpenGL validation.

## Documentation

- [Documentation index](docs/README.md)
- [Building](docs/building.md) and [using the SDK](docs/consumer-build.md)
- [SDL2 integration](integration/SDL2/README.md)
- [Architecture](docs/architecture.md), [lifecycle](docs/lifecycle-reopen.md) and [limitations](docs/limitations.md)
- [Performance](docs/performance.md), [testing](docs/testing.md) and [validation](docs/validation.md)
- [Contributing](CONTRIBUTING.md) and [third-party notices](THIRD_PARTY_NOTICES.md)

## Project references

| Project | Role |
| --- | --- |
| [Mesa](https://www.mesa3d.org/) | OpenGL, Gallium, GLSL/NIR, ACO/RADV and AMD layout infrastructure |
| [OpenGNM PSBC](https://github.com/PS4-OpenGNM/opengnm-psbc) / [OpenGNM](https://github.com/PS4-OpenGNM/opengnm) | Shader compiler foundation and reference declarations |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | Public homebrew toolchain and imports |
| [Native app boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | Native app assembly and folder packaging |
| [Khronos VK-GL-CTS](https://github.com/KhronosGroup/VK-GL-CTS) | Pinned OpenGL test inventory and runner |
| [SDL2](https://github.com/libsdl-org/SDL) | Window, context and input integration |
| [Dear ImGui](https://github.com/ocornut/imgui), [NanoVG](https://github.com/memononen/nanovg), [Sokol](https://github.com/floooh/sokol) | Existing renderer examples |
| [Hardware video decoding research](https://github.com/blackbearreloaded/ps5-hardware-video-decoding-research) | Related presentation/lifecycle research, not OpenGL validation |

## Maintainer and license

Maintained by [BlackBearReloaded](https://github.com/blackbearreloaded).
Project-specific implementation, integration and validation contributions are
credited in source headers. Upstream projects retain their authorship and licenses.

Project-owned code is [GPL-3.0-or-later](LICENSE). See
[third-party notices](THIRD_PARTY_NOTICES.md) and [LICENSES](LICENSES) for dependencies.

No vendor SDK, firmware modules, device keys, proprietary shader packages,
console-enablement payloads or raw device logs are distributed here.
This independent project is not affiliated with Sony or The Khronos Group.
