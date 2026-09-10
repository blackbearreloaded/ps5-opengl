# SDK preview: native 1440p120 and 4K120

[Download the frozen high-refresh preview](https://github.com/blackbearreloaded/ps5-opengl/releases/tag/sdk-0.1.0-perf20260909-g47-hfr-sdl2-focused).

Separate GL/SDL2 archives target 2560×1440 and 3840×2160 at nominal 120 Hz.
Both frozen profiles have focused numerical rendering, SDL and HDMI
qualification. Their 30-second ImGui window tests measured about 119.88 FPS,
with matching native HDMI negotiation and normal-output restoration.

These results belong to this preview, not subsequent SDKs. They are not a new
CTS campaign, independent panel-frame measurement or general game-FPS guarantee.
The [linker-metadata exception](sdk-path-free-derivative.md) remains part of
the build provenance.

Use each archive's included checksums, source snapshot and validation JSON
for exact identities. For new integrations, prefer [SDK 0.2.0](release-g62.md);
its 1440p package has a different, host-only qualification scope.
See [SDK consumption](consumer-build.md) and [SDL2](../integration/SDL2/README.md).
