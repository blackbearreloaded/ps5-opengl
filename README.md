# PS5 OpenGL

**OpenGL 3.3 Core and GLSL 3.30 for PlayStation 5 homebrew.**

An experimental native graphics stack built on Mesa/Gallium, with runtime shader
compilation, fullscreen EGL presentation, a relocatable static SDK, and practical
examples. Application rendering code uses standard OpenGL—not private GPU types.

The project completed its defined Core 3.3 validation campaign on a PS5. It is
**not a Khronos-certified implementation**, a stock-console installation method,
or a guarantee that every desktop application will run unchanged.

**Performance-branch status:** the validation totals below describe the frozen
publication baseline, not the newer runtime in this branch. GPU-clear changes
have focused hardware validation; experimental multi-draw batching is opt-in.
The updated runtime still needs release-candidate validation before promotion.
See [Performance](docs/performance.md) for measurements and remaining work.

## What is included

- **OpenGL 3.3 Core / GLSL 3.30:** Mesa state tracking, GLSL/NIR compilation,
  the PS5 Gallium driver, and the native GPU/presentation backend.
- **Native EGL:** fullscreen surfaces and the tested context/resource lifecycle.
- **Developer SDK:** headers, static archives, import stubs, and Make,
  pkg-config and CMake integration. Build artifacts are generated locally.
- **Examples:** a triangle, Dear ImGui, NanoVG, Sokol and a 3D cube benchmark using public APIs.
- **Validation:** 39,544 individual results, exclusion reviews, provenance
  hashes, and an offline evidence verifier.
- **Pinned sources:** upstream revisions and complete local compiler/platform
  patches; no dependency on unpublished research commits.

## Examples

| Example | Demonstrates | Validation |
| --- | --- | --- |
| [Triangle](examples/core33-triangle/README.md) | Minimal public EGL/OpenGL consumer | Installed SDK compile/link checks |
| [Dear ImGui](examples/core33-imgui/README.md) | Widgets, textures, fonts and blending | Six-frame hardware oracle |
| [ImGui TV demo](examples/core33-imgui/README.md#g10-visible-tv-demo) | Readable 1080p UI, animated shapes, gamepad navigation | TV-confirmed; 2,851 frames over five minutes |
| [NanoVG](examples/core33-nanovg/README.md) | Its upstream GL3 vector renderer | 45 pixel probes; zero dirty stencil pixels |
| [Sokol](examples/core33-sokol/README.md) | Its GL backend through public OpenGL | 1,843,200 component comparisons; zero mismatches |
| [3D cubes benchmark](examples/core33-cubes/README.md) | Lit textured cubes, depth testing, ordinary versus instanced draws | Ordinary baseline passed; instanced texture mismatch under investigation |

The accepted baseline TV demo measured approximately **9.5 FPS**; the GPU-clear
candidate measured approximately **15 FPS** in a separate short profile, not the
five-minute baseline run. Its 30 FPS setting is a pacing ceiling, not achieved
performance. Controller connection was recorded;
hardware widget changes were not captured. Host navigation tests pass.

## Validation status

The frozen campaign covers **9,886 cases across four configurations**:

| Result | Per configuration | Total |
| --- | ---: | ---: |
| Pass | 9,351 | **37,404** |
| Individually reviewed `NotSupported` | 535 | **2,140** |
| Accounted | **9,886** | **39,544** |

No gaps, duplicates, required-case failures, warnings, waivers, or incomplete
results remain in the accepted set. Optional/inapplicable exclusions are reviewed
individually: **39,544 accounted results does not mean 39,544 passes**. Full upstream
swizzle and LOD-bias workloads ran in every configuration. The campaign includes
20 uneventful CTS lifecycle cycles and three final external-renderer hardware checks.

See the [validation report](docs/validation.md) for adaptations, exceptions and
limits, and the [machine-readable evidence](validation/2026-09-06/README.md).

```sh
make test
```

This runs host checks and verifies the published export. **It does not rerun the
PS5 campaign.** CI has no console access.

## Build your first native app

Use Linux/WSL and the pinned native-app boilerplate. Follow
[Building](docs/building.md) for prerequisites and the initial checkout.

```sh
make source-fetch
make sdk
make imgui-demo
```

Upload the generated **folder** `build/native-app/PPSA99005/dist/PPSA99005` to
`/data/homebrew/PPSA99005` in an already configured native homebrew environment,
then launch its registered title. Do not send the application executable to an
ELF loader. Each TV-demo launch runs for five minutes.

Existing projects still need PS5 entry-point, build, window/input and lifecycle
integration. The SDK does not replace GLX, WGL, SDL or GLFW platform code.

## Documentation

| Document | Purpose |
| --- | --- |
| [Building](docs/building.md) | Dependencies, source setup, SDK and native apps |
| [Using the SDK](docs/consumer-build.md) | Make, pkg-config and CMake integration |
| [Architecture](docs/architecture.md) | Frontend, shader compiler, driver and platform boundaries |
| [Testing](docs/testing.md) | Host checks, bounded hardware cases and acceptance |
| [Validation report](docs/validation.md) | Exact results, identity and exceptions |
| [Limitations](docs/limitations.md) | Compatibility, performance and hardware scope |
| [Performance](docs/performance.md) | Measured bottlenecks, GPU acceleration candidates and validation boundaries |
| [Contributing](CONTRIBUTING.md) | Changes, regression selection and reporting |
| [Third-party notices](THIRD_PARTY_NOTICES.md) | Upstream projects, licenses and source pins |

## Project references

| Project | Role |
| --- | --- |
| [Mesa](https://www.mesa3d.org/) | OpenGL, Gallium, GLSL/NIR, ACO/RADV and AMD layout infrastructure |
| [OpenGNM PSBC](https://github.com/PS4-OpenGNM/opengnm-psbc) / [OpenGNM](https://github.com/PS4-OpenGNM/opengnm) | Shader compiler foundation and reference declarations |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) | Public homebrew build toolchain and import declarations |
| [Native app boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) | Native app assembly, runtime and folder packaging |
| [Khronos VK-GL-CTS](https://github.com/KhronosGroup/VK-GL-CTS) | Pinned OpenGL inventory and runner |
| [Dear ImGui](https://github.com/ocornut/imgui), [NanoVG](https://github.com/memononen/nanovg), [Sokol](https://github.com/floooh/sokol) | Existing renderer integrations |
| [PS5 GPU research](https://github.com/blackbearreloaded/ps5-gpu-research) | Companion research notes; access may be required |
| [Hardware video decoding research](https://github.com/blackbearreloaded/ps5-hardware-video-decoding-research) | Related presentation/lifecycle work; not OpenGL validation |

## License

Project-owned code is provided under [GPL-3.0](LICENSE). Upstream components and
derived patches retain their copyright and licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [LICENSES](LICENSES).

No vendor SDK, firmware modules, device keys, proprietary shader packages,
console-enablement payloads, or raw device logs are distributed here. This
independent project is not affiliated with Sony or The Khronos Group.
