# SDK archives

For new integrations, use [SDK 0.2.0](release-g62.md). Earlier
[releases](https://github.com/blackbearreloaded/ps5-opengl/releases) retain their
original libraries and evidence. CI artifacts are [fresh host-checked builds](ci-releases.md),
not automatically console-qualified.

## Contents

An SDK archive contains static GL/EGL libraries, public headers, Make/pkg-config/
CMake metadata, source snapshots, examples, licenses, checksums and provenance.
Some profiles also include a matching SDL2 prefix. The package is not a
ready-to-launch console app or an installation/enablement tool.

Keep the complete profile together. The archive's validation JSON and source
provenance describe its exact scope; do not infer acceptance from a similar
version, another display profile or a newer README.

## Verify and link a consumer

Check the download's SHA-256 sidecar, extract into a new directory, and verify:

```sh
sha256sum --check SHA256SUMS
(cd sdk && sha256sum --check manifest.sha256)
# Only when the archive contains SDL2:
(cd sdl2 && sha256sum --check manifest.sha256)
```

Use [Make, pkg-config or CMake](consumer-build.md) with the extracted prefix.
For an existing frozen GL SDK, verify and link consumers without rebuilding it:

```sh
python3 tools/check-sdk-consumers.py --help
```

Supply the SDK, public payload toolchain and a fresh output directory as
documented by that command. SDL consumer checks are in
`tools/test_sdl_sdk.py` and the [SDL integration guide](../integration/SDL2/README.md).

## Frozen baseline package

The [original sampled 1080p60 package](https://github.com/blackbearreloaded/ps5-opengl/releases/tag/v0.1.0-perf20260907-sampled)
retains its narrower release-specific acceptance. It is separate from the
[full-campaign runtime](validation.md) and from the newer GPU-optimized SDK.
Consult its bundled guide for its immutable identities and original contents.
