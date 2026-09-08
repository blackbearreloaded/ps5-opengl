# Local SDK candidate 0.1.0-perf20260908-targeted

This **local-only 1080p60 SDK** contains the G18 nonzero-mip copy fix and
G19 color-blit channel-mapping fix. It is not published or Khronos certified.
Its acceptance is **24 mip cycles plus 18 format checks**, not a new full CTS
campaign. The older prerelease and G13 archives remain unchanged.

## Evidence and identity

- Runtime source: `44435a7ccf7e3d30e33169867a5421a5fd86fae2`.
- SDK manifest: `344673952a789cae7e4a6d4c6670e3cf8c6bbf595fb07ebf27a8ffe3680014be`.
- Runtime archive: `627857a44a8101b0ab0df319293a554143e96405be8a1ec55fc48bbd1830caa5`.
- Native batch: 24 ordered copy/upload/draw/sample cycles across 2D, array and
  volume mips, with exact mip/layer/base guards; all passed. The adjacent format
  gate passed all 18 checks, including R16F-to-RGBA32F conversion.
- Two EGL sessions in one bounded native run on one recorded firmware-6.02
  console. Tracked direct/mapped GPU bytes and counts return to zero each time;
  peak 92,798,976 bytes. Owned heap ends at 9,375 bytes/21 blocks each time,
  with no post-session growth. Module-owned memory, CPU mmap and RSS are excluded.
- Clean title teardown, healthy services and exact-token lock release recorded.
- Host sanitizer regressions, SDK manifest, 344 Core exports and installed-SDK
  Make/pkg-config/CMake consumers pass. These host checks do not execute the GPU.

`targeted-validation.json` records the two groups separately, memory observations
and hashes of the private receipts; `full_matrix_complete` remains `false`.
`consumer-validation.json` contains sanitized host results. `provenance.json`
identifies both the frozen runtime and its newer source/documentation companion.
The packager rejects changed receipt hashes and mismatched SDK identities.

The 204-case sample, ten-minute soak and 59.95 FPS offscreen measurement belong
to **G13**, not these newer bytes. The historical 39,544 accounted results also
belong to another binary. G19 has no fresh performance, HDMI-mode, physical-input,
long-soak, suspend/resume, device-loss or cross-firmware qualification. CPU
fallbacks and application-specific platform adaptation remain; see
[enhancement scope](enhancement-plan.md) and [limitations](limitations.md).

## Contents and use

The tar.gz includes compiled SDK libraries/headers, source examples, dependency
sources, licenses, provenance and checksums. It excludes raw console logs,
proprietary runtime modules, console-enablement code, toolchain binaries,
ready-to-launch apps, and the separate local PPSA77800 app/artwork.
Scanout flags are fixed at 1080p60; GPU-memory diagnostics were application-side,
not part of the SDK. Neither packaging nor verification rebuilds the SDK.

From the extracted root, follow [verification and consumer commands](sdk-bundle.md#verify-and-link-a-consumer).
Use the extracted `tools/check-sdk-consumers.py`, `sdk/`, `examples/` and
`verification/gl.xml` with a separately installed public payload SDK. Do not
run `make sdk` merely to verify the frozen binaries: a rebuild creates a new
candidate and does not inherit this acceptance.

## Prepare without publishing

From the checkout with the private receipts and frozen SDK:

```sh
python3 tools/build-sdk-bundle.py \
  --targeted-version 0.1.0-perf20260908-targeted \
  --sdk build/sdk/ps5-opengl-core33-g19-blit-swizzle \
  --source-commit FULL_SOURCE_SNAPSHOT_COMMIT \
  --candidate .local/g19-candidate.json \
  --results results/g19-transfer-regressions-20260908 \
  --consumer-report .local/g19-consumers/summary.json \
  --destination build/bundles/new-g19-output
```

The output directory must be new and outside the SDK. Source equivalence is
checked against the accepted runtime and native batch. CI keeps its separate
host-only mode; it cannot inherit these hardware results. Publication requires
a separate request.
