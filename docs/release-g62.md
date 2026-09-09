# SDK 0.2.0 — optimized GL/SDL2 release

September 9, 2026. G62 consolidates the GPU blit/resolve, clear, color-format
and mip/layer optimizations with a depth-only framebuffer bounds fix. It is a
scoped experimental release, **not Khronos certification, a new full CTS
campaign or a universal stability guarantee**.

## Downloads and use

The [0.2.0 release](https://github.com/blackbearreloaded/ps5-opengl/releases/tag/sdk-0.2.0)
provides two separate fixed-profile archives and SHA-256 sidecars:

| Archive | Exact-binary qualification |
| --- | --- |
| `ps5-opengl-sdk-0.2.0-2160p120-sdl2.tar.gz` | 4K native sample, focused GPU and bounded application/lifecycle checks below |
| `ps5-opengl-sdk-0.2.0-1440p120-sdl2.tar.gz` | Host-checked only; no new 1440p console acceptance |

Each includes compiled GL/EGL and SDL2 libraries, headers, Make/pkg-config/CMake
metadata, complete project/dependency sources, examples, licenses, checksums
and reconstructed validation/provenance. Do not mix libraries between profiles.
An already configured native homebrew environment and the pinned external
toolchain/title boilerplate remain prerequisites; no console enablement,
proprietary runtime modules or native title assets are distributed here.

For example, verify the downloaded 4K archive and every extracted file:

```sh
sha256sum --check ps5-opengl-sdk-0.2.0-2160p120-sdl2.tar.gz.sha256
tar -xzf ps5-opengl-sdk-0.2.0-2160p120-sdl2.tar.gz
cd ps5-opengl-sdk-0.2.0-2160p120-sdl2
sha256sum --check SHA256SUMS
(cd sdk && sha256sum --check manifest.sha256)
(cd sdl2 && sha256sum --check manifest.sha256)
```

Follow [Building](building.md), the [SDL2 bridge](../integration/SDL2/README.md)
and the included examples for integration. The bridge supports one fixed
window and one unshared Core 3.3 context, not a complete SDL platform.
Existing G25/G47/G55 releases retain their own identities and acceptance.

## Final 4K qualification

All accepted runs use the final runtime below on one firmware-6.02 console,
with owner-configured 2160p output and a direct Hisense 55U78N HDMI4 connection.
There were **14 clean final native cycles**. Each records exact app/SDK
identity, title teardown, healthy services and exact-token lock release.

| Gate | Result |
| --- | --- |
| Compact CTS | **202/202 Pass**, configurations 51/51/50/50; zero failures, warnings, waivers or `NotSupported` |
| GPU transfer/clear batch | **22 blit/resolve + 46 clear + 6 format cases Pass**; pixel, state and driver-counter checks |
| GPU mip/layer batch | **8 cases Pass**, including neighboring-image preservation and state/counter checks |
| SDL2 | 180 frames and two exact pixel probes Pass; not an SDL FPS benchmark |
| ImGui window | **119.883392 FPS**, 30 seconds |
| ImGui offscreen, case 11 | **119.897011 completed FPS**, 30 seconds; frame p95 **9.136574 ms** |
| 128 cubes | Ordinary **58.09**, instanced **117.41 completed FPS**, 30 seconds per mode |
| Normal session | **120 seconds**; memory acceptance Pass, startup-inclusive cadence **partial-pass** |
| Paced lifecycle | Three launches, nine EGL sessions, 54 pixel checks; two five-second gaps per launch; Pass |

The normal-session windows were **113.6, 119.867, 119.833 and 119.867 FPS**.
Four steady samples showed zero tracked owned-heap/direct/mapped growth or
range. Tracked GPU allocations/mappings returned to zero; the owned heap ended
at 9,455 bytes/22 shared-lifetime blocks, not zero. This is bounded accounting,
not process-RSS, foreign-heap or exhaustive leak proof.

Window, offscreen, 3D, SDL and normal-session logs recorded native 2160p119.88
HDMI with same-resolution 59.94-Hz restoration. Each lifecycle run recorded
three expected HFR/restoration pairs. Numerical GPU/CTS tests do **not** count
as display qualification. HDMI negotiation is not an independent per-run TV
measurement or proof that every rendered frame reached the panel.

The CTS selection is fixed before running: all 51 smoke cases on two ordinary
targets, 50 each on two extreme-axis targets. Only
`KHR-GL33.framebuffer_blit.multisampled_to_singlesampled_blit_color_config_test`
on configurations 2 and 3 is deferred: earlier single-case times exceeded the
owner's 120-second bound. These are two explicit unexecuted cases, not discarded
failures. The historical 39,544-result campaign belongs to a different
[frozen SDK](validation.md); its acceptance is not inherited.

## Fixes and preserved failures

The first G62 CTS attempt found two packed-depth/stencil blit failures before
GPU submission. A depth-only framebuffer's write-disabled dummy color slot was
incorrectly sized to full scanout, too large for its MSAA placeholder. Describing
that unused slot as one texel preserves real depth/raster bounds, write disabling
and allocation checks. The host regression failed before the fix and passed
afterward. Two isolated regressions and the 202-execution sample then passed;
all final GPU/application gates were rerun on the corrected runtime.

Two immediate-recreation lifecycle attempts passed rendering/memory/close but
recorded HDMI reconnects and were not accepted for display qualification.
Five-second gaps between example sessions passed the unchanged display checks.
This supports a mode-switch timing explanation, not a proven minimum interval
or device-loss recovery. The delay is in the lifecycle example, not the runtime
or steady rendering. Rapid HFR mode churn remains unqualified.

An offline blit report used its last case's height as the display height.
The generator was corrected and rerun on the same raw receipt; original and
corrected reports are preserved. Only metadata changed, not pixel results,
executable identity or native acceptance criteria.

## Frozen identities

Runtime and SDL source companion:
`eb4b1b705fcdcc59d68c8275b1c4a02ec8781c91`.
Application/runner and packaging commits are separate; the archive's
`provenance.json` identifies the exact included source snapshot.

| SHA-256 | 4K120 | 1440p120, host-only |
| --- | --- | --- |
| GL manifest | `6778637f060904653a75d66b5e3eead872520d3d93e9980c9c383723999de5a5` | `52f867d4ded1894175e51a9a97ebf59c87b6cb11356bbc5bc3446fa5111d625f` |
| Runtime archive | `c3310041e2fdb71f9d5ed0c5a4c07c1d8d778af1492937c8f7cc4ee5ee55752b` | `a1c4f5c1d0185aeb2090765ff336416b824c3b712ede234633a2de6aead545ae` |
| SDL build receipt | `8a93c99f0860a9d53c9096b90c28bd4a3124706124763fc5ecdd4ccb69393c14` | `485404ff320923e0deb6ee8ec62cb38ffd5bb66b2e6e4a0a3dd87fd3908ebe96` |
| Local evidence index | `674fed05f367d353821e7b5f63d62a9dc4a504bddeed4d150dc70cf469161674` | `29fa4e602492f172b0a8b8552aa2910506e86dfb42e2aef496f547d303ef41d7` |

Both SDK/SDL pairs pass all 344 Core exports and host consumer links. 1440p is
host-only by owner decision: no duplicate console runs or borrowed 4K receipts.
G47 dependency lineage and the G51 Mesa fix remain separately checked; the
[linker-metadata exception](sdk-path-free-derivative.md) remains documented.

## Reproducing the release audit

The packager pins all six identities per profile, checks source equivalence,
recomputes raw receipt audits and rejects missing/changed inputs. It reuses the
[distribution contract](sdk-g55-release.md#build-provenance-and-distribution):

```sh
python3 tools/build-sdk-bundle.py --g62-profile "$PROFILE" \
  --sdk "$FINAL_GL" --sdl-build "$FINAL_SDL_NATIVE_BUILD" \
  --candidate "$G62_RELEASE_INDEX" --results "$LOCAL_EVIDENCE_ROOT" \
  --consumer-report "$FINAL_GL_CONSUMER_REPORT" \
  --source-commit "$(git rev-parse HEAD)" --third-party "$DEPENDENCY_SOURCE_CACHE" \
  --destination "$NEW_BUNDLE_DIRECTORY"
```

The private index and raw receipts remain local. Only reconstructed numerical
results/hashes enter `focused-validation.json`; build provenance and consumer
checks are separate. Prefixes and nested source archives are scanned for private
paths, hosts and native assets. Packaging does not strip or modify frozen libs.
Release acceptance also requires a fresh extraction: archive/root checksums,
both prefix manifests, three GL and two SDL consumer links using extracted libs.
CI runs host/staging checks; fresh CI SDKs do not inherit native results.

## Scope boundary

No ten-minute tests or duplicate 1440p hardware runs were performed for this
release. CPU fallbacks outside eligible fast paths are not automatically missing
API functionality. Multi-hour sessions, suspend/resume, device loss, exhaustive
memory pressure, full SDL/GLFW portability, new physical-input checks and other
firmware remain outside the claim. See [limitations](limitations.md) and
[performance](performance.md).
