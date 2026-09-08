# G25 OpenGL + SDL2 SDK

Version **`0.1.0-perf20260908-g25-sdl2-sampled`** is an experimental, fixed
**1080p60** SDK. It combines the frozen G25 graphics runtime with a separately
installed SDL2 adapter. It is sample-validated, **not a new full CTS campaign or
Khronos certification**. Existing releases and their binaries remain unchanged.

The archive contains compiled libraries and headers, Make/pkg-config/CMake
metadata, source examples, project/dependency sources, licenses, checksums and
provenance. SDL2 is optional for consumers; it does not replace the EGL API.
No ready-to-launch app, firmware module, toolchain binary, raw console log or
local PPSA77800 app/artwork is included. The public payload SDK and native-app
boilerplate remain separate prerequisites.

## Exact-binary acceptance

G27 and G28 ran on one recorded firmware-6.02 console on September 8, 2026.
All eight G27 cycles and the G28 SDL2 cycle ended with native-title teardown,
healthy services and exact-token lock release. Numerical readbacks and logs
were used; no new physical-controller or independent HDMI check is implied.

| Check | Result for these bytes |
| --- | --- |
| Selected CTS cases | 51 cases × four configurations: **204 Pass**, no exclusions, warnings or incomplete results |
| Matched 1080p offscreen workload, 30 seconds | 1,799 frames, **59.9468 FPS**, frame p95 17.2141 ms; pixel checks pass |
| 128 textured/depth-tested cubes, 30 seconds per mode | Ordinary **59.9405 FPS**, instanced **59.9388 FPS**; 1,799 frames each, pixel checks pass |
| 600-second diagnostic soak | 21 pixel probes; 20 steady samples with zero tracked heap/direct/mapped growth; tracked GPU bytes/counts return to zero |
| EGL lifecycle | Three sessions in one native launch/exit cycle; no post-session heap growth, balanced tracked GPU memory |
| SDL2 native example | **180 frames, two exact pixel checks** at 1920×1080; context/window/SDL cleanup passes |
| Host checks | `make test`, real-SDL ASan/UBSan contract, 52 invalid-input checks, relocated GL and SDL consumer links pass |

The cube workload uses 1,536 triangles and two 2×2 textures; it is a draw-overhead
test, not a game or texture-bandwidth benchmark. Average FPS does not guarantee
every frame meets its budget. Soak accounting excludes module-owned allocations,
CPU mmap and RSS; this is bounded stability evidence, not exhaustive leak proof.

The earlier G25 format/sRGB/seven-case staging batch used this same graphics
archive and remains applicable. Its sRGB composite workload stayed around
19.99 cycles/s: the storage change did **not** establish a performance gain.
Other formats, mip levels and layers still use CPU staging where required.
See [performance scope](performance.md#broader-format-and-subresource-coverage-g25-local).

The historical 39,544 accounted results belong to another frozen runtime:
37,404 Pass and 2,140 reviewed NotSupported. This release does not inherit that
full-campaign acceptance or the old G19 game/controller results.

## Identity and provenance

| Artifact | SHA-256 / source commit |
| --- | --- |
| Graphics runtime source | `61a0919bff9e6944caef2b8ad6c6ef9fd9a30279` |
| GL SDK manifest | `749057e84def5ead284614db5d81a0a7e9c037f474afd95cd88ebceea5d65cda` |
| GL runtime archive | `5b6129538328ab8b950dfbdb2f1395774b4a91e7d4030d1acafbabe043829d9e` |
| CTS candidate manifest | `ff6759b193b5bb9138a72e53214e112c2ddc68aca6338ca3b476c8172ee9bd15` |
| CTS native executable | `c0fecd3703d8bc2479f4db95ce7cdaddb8e6eaa5d33ee49f129a970ef6e2cdc4` |
| SDL2 installed manifest | `a5678bf7bf6c239dc785fae6403c6e27ee730003a0308c7e8593ffdad7c05f7e` |
| SDL2 static archive | `a6ee98675d58615ded0292da6eb491e45e818b91d86756e7b3cbecbe3aa2fa04` |
| SDL2 native executable | `e8664fa9600e7ce77689de1f5cebc2acabf9a863170e3d887d7702d4dd25a41f` |
| SDL2 build receipt | `d5f06b106f9d2de1613113773e1aaa01d30ac38769a4158c8916d72e175a8af7` |

G27's application source companion is `b71766c797a7614030f1b3c7f962edc7633747b0`;
G28's is `8b4b870844d9bfe2cf8080d1b97d96480aa92bcb`. The later source snapshot in
`provenance.json` adds packaging/documentation without rebuilding those binaries.
The packager checks graphics-source equivalence and exact SDK/receipt hashes.
`sample-validation.json` includes individual sampled cases and private raw-receipt
hashes. Local acceptance records for the other checks are identified below;
they contain the app identities, measurements and raw-receipt hashes.

| Acceptance record | SHA-256 |
| --- | --- |
| G27 offscreen | `1bdee08f1a3c014fe46a1da1fd10c3fc15a59cf944cf8c57f4c7c889b4638ab6` |
| G27 cubes | `5cdfec0750f238a51e465f8e7de97bb467ba45850f1b60bd53d216878a482f96` |
| G27 soak | `bca5ef2ec64ae5d9b2813498d017a1501bf1f1104ec20d6afee5b9306e2dea3d` |
| G27 lifecycle | `6a936df79e930cf54696abc80c50e959b0ae2687a453c169998cd1c76fd2a55d` |
| G28 SDL2 | `1b3c04d0e6962337277b9c80d09d44b6537bf21d3c25a0de975b743b68e3b35b` |

SDL build receipts deliberately retain `hardware_run: false`: they describe
compilation, not the later native run. G28's separate acceptance above qualifies
only the listed example and binary pair. Neither receipt rewriting nor acceptance
inheritance is used. Provenance's local-candidate status describes assembly;
GitHub's release records publication state.

## Verify and consume

In Linux/WSL, verify the downloaded `.tar.gz.sha256`, extract into a new directory,
then run inside the extracted root:

```sh
sha256sum --check SHA256SUMS
(cd sdk && sha256sum --check manifest.sha256)
(cd sdl2 && sha256sum --check manifest.sha256)
export PS5_OPENGL_PREFIX="$PWD/sdk"
export PKG_CONFIG_LIBDIR="$PWD/sdl2/lib/pkgconfig:$PWD/sdk/lib/pkgconfig"
unset PKG_CONFIG_PATH
pkg-config --cflags --libs --static sdl2
```

For GL-only Make/pkg-config/CMake verification, use the included
`tools/check-sdk-consumers.py`, `examples/core33-triangle`, `verification/gl.xml`
and a separately installed public payload SDK; see
[consumer commands](sdk-bundle.md#verify-and-link-a-consumer).

SDL consumers use `find_package(SDL2 CONFIG REQUIRED)` and `SDL2::SDL2` (or
`SDL2::SDL2-static`), with both `sdl2/` and `sdk/` in `CMAKE_PREFIX_PATH`.
The supplied metadata sets `SDL_MAIN_HANDLED`; call `SDL_SetMainReady` and use
the normal PS5 native entry point. Use the C++ link driver for static C++ runtime
dependencies. The platform import libraries come from the public payload SDK.
Do not add SDL2main, OSMesa or a second presentation owner.

To rebuild a **native folder**, first extract `sources/ps5-opengl.tar` into a new
directory and follow its [SDL2 integration guide](../integration/SDL2/README.md).
The full source checkout contains the required native CRT, heap wrapper and
assembly helpers; the loose consumer examples alone are not an app packager.
The unmodified pinned SDL source is in `sources/SDL2.tar`, and the exact integration
inputs used for this binary are in `sources/SDL2-integration.tar`. Build receipts
record their hashes. The newer integration README is a separate companion.

## Boundaries and maintenance

SDL2 supports one fixed 1080p Core 3.3 window/context. Physical controller buttons,
disconnect/reconnect and multi-user behavior remain unverified on this candidate.
Full SDL platform support, GLFW, audio, mouse, IME, resize, sharing and multiple
windows are not included. Multi-hour sessions, exhaustive OOM, suspend/resume,
device-loss recovery and other firmware also need separate qualification.
This fixed 1080p60 release makes no 4K120 HDMI claim.

Maintain the separate prefixes and complete manifests. A new build creates new
bytes and needs its own scoped acceptance; do not rebuild merely to consume this
archive. The automatic `v*` CI lane builds fresh, host-only SDKs. A separately
reviewed frozen hardware-qualified archive uses an `sdk-*` tag so that lane does
not rebuild or replace its assets. Neither lane overwrites older releases.
