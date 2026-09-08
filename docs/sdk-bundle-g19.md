# Local G19 SDK candidates

This **local-only 1080p60 SDK** contains the G18 nonzero-mip copy fix and
G19 color-blit channel-mapping fix. It is not published or Khronos certified.
Its targeted acceptance is **24 mip cycles plus 18 format checks**. The same
frozen binaries now pass a fresh **204/204 execution sample** and the bounded
performance/stability checks below, not a new full CTS campaign.
The older prerelease, G13 archive and G19 targeted archive remain unchanged.

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

The preserved `0.1.0-perf20260908-targeted` archive's
`targeted-validation.json` records the two groups separately, memory observations
and hashes of the private receipts; `full_matrix_complete` remains `false`.
`consumer-validation.json` contains sanitized host results. `provenance.json`
identifies both the frozen runtime and its newer source/documentation companion.
The packager rejects changed receipt hashes and mismatched SDK identities.

## Fresh G21 acceptance of unchanged G19

`0.1.0-perf20260908-g19-sampled` uses these same SDK hashes and includes its own
`sample-validation.json`: 51 ordered cases in each of four configurations,
**204 Pass**, no skips/warnings/failures, four clean cycles. The exact native CTS
app is `f2967f1d57e3cef74e44e7c17375bfa15f0c5ff9b011a0de602d27aea82d646c`;
candidate manifest is `31d8e1d13bbacad9d87a7ad656612abe04a234a165bd0c82009ed398af70244a`.
`full_matrix_complete` remains `false`. Its source companion includes the fixes
and newer documentation; it does not rebuild the runtime.

Separate frozen applications linked to that SDK also pass:

| Check | Result |
| --- | --- |
| Matched 1080p offscreen, 30 seconds | 1,799 frames; 59.95 FPS; frame p95 17.21 ms; pixel checks pass |
| 128 textured/depth-tested cubes, 30 seconds per mode | 1,799 frames each; ordinary 59.940 FPS, instanced 59.942 FPS; oracles pass |
| 600-second tracked-memory soak | 35,941 total frames, 21 pixel probes; 20 steady samples with zero heap/direct/mapped growth; GPU peak 82,558,976 bytes and zero bytes/counts after cleanup |
| Three native launch/exit cycles | Nine EGL sessions, 54 frame oracles; each cleanup leaves zero tracked GPU bytes/counts and the same 9,455 heap bytes/22 blocks |

Each cycle records title teardown, healthy services and exact-token release.
These receipts are retained under `results/g21-*`; source companion `58f1ac6`.
The memory wrapper is app-only and excludes module allocations, CPU mmap and RSS.
The diagnostic soak is not a matched performance benchmark. The p95 also means
the offscreen average is not a guarantee of every frame meeting 16.67 ms.

G13's older sample/soak and the historical 39,544 accounted results belong to
different binaries. G19 has no fresh HDMI-mode, physical-input, multi-hour,
suspend/resume, device-loss or cross-firmware qualification. CPU
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

For the latest sampled archive, from the checkout with the private receipts and
frozen SDK:

```sh
python3 tools/build-sdk-bundle.py \
  --sample-version 0.1.0-perf20260908-g19-sampled \
  --sdk build/sdk/ps5-opengl-core33-g19-blit-swizzle \
  --source-commit FULL_SOURCE_SNAPSHOT_COMMIT \
  --candidate .local/g21-cts-candidate.json \
  --results results/g21-cts-smoke-20260908 \
  --destination build/bundles/new-g19-sampled-output
```

The earlier targeted package can still be reproduced with its own manifest:

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
