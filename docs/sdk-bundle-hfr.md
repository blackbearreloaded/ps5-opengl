# Frozen high-resolution SDK bundles

The `--hfr-profile 1440p120` and `--hfr-profile 2160p120` modes of
`tools/build-sdk-bundle.py` assemble the original G31 GL SDK and matching G32
SDL2 payload. Version names are
`0.1.0-perf20260908-g41-<profile>-sdl2-focused`; output uses the existing
`.tar.gz` format and checksum sidecar. Existing sampled, targeted and CI
version modes remain separate.

**Current assembly blocker:** the original GL static libraries contain personal
absolute build paths. The HFR packager rejects these bytes before creating a
distribution directory. Removing paths would change frozen hashes; it is not
authorized by this packaging change. No distributable HFR archive has been
produced. The parent must resolve the conflict between unchanged libraries and
path-free distribution before publication. Local consumer checks can still use
the original libraries. Passing those checks does not waive this blocker.

## Exact scope

These bytes have two bounded native cycles per profile on one recorded
firmware-6.02 console, using HDMI4 on a Hisense 55U78N:

| Profile | ImGui, 3,597 measured frames | SDL2 | Active / restored HDMI |
| --- | --- | --- | --- |
| 1440p120 | 119.881660 FPS over 30.004589 seconds, two passing pixels | 180 frames, two exact pixels | 2560×1440 at 119.88 / 59.94 Hz |
| 2160p120 | 119.883639 FPS over 30.004094 seconds, two passing pixels | 180 frames, two exact pixels | 3840×2160 at 119.88 / 59.94 Hz |

All four cycles passed restoration, native teardown, service health and
exact-token release. These are captured console HDMI modes, not independent
per-run TV measurements. Native 1440p required the owner's output selection.
SDL's nominal profile is not measured SDL FPS. No sampled CTS, full CTS,
extended HFR soak, physical-controller acceptance or arbitrary-game performance
is inherited. The historical 39,544-result campaign belongs to another runtime.

## Identity and source ownership

The GL build source is `3cdc90bbc14def6bc3025b4460fba0892841114a`; the SDL
integration source companion is `ac2a52aa7faac5e7b9bcd6660cee36263f3e5324`.
The packaging/source snapshot must be the clean committed checkout executing
the packager. Its GL build inputs must remain equivalent to the frozen source.
The later packaging commit is not reported as the original binary build commit.

| SHA-256 | 1440p120 | 2160p120 |
| --- | --- | --- |
| GL manifest | `d2d6169960d379f3987b8b7c3b8f81cf05f97072cdfa67eaba393fc82e327567` | `2785038b1020824a882e533b874bb7658e6f41210e689a79ccd363ffc3af1b61` |
| GL runtime | `b7f3ca590d9befa516691b893617d8ae049ce187c47737aad041c5788b93fabe` | `83a8f4729bafcb0b61be6e4b96f1603c1d682b26243f47475cff2d2af7353374` |
| SDL native receipt | `1d970f2268ccef33712ab893464410b9f95e02e19c0d2b6cdf5c45d375285527` | `027bff821586f6baa51822a7a4304b914bd59f84d81f64bf765973313db61c84` |

The packager pins G39/G40's 1440p acceptance records and G37/G38's 2160p
records, verifies every referenced raw receipt hash, and reuses the existing
ImGui/display auditors. SDL checks retain the recorded auditor's drawable,
180-frame and two-pixel requirements. A render-size/HDMI mismatch, wrong binary,
changed record, missing receipt, failed restoration or incomplete lifecycle
rejects assembly. Only selected measurements, identities and receipt hashes
enter `focused-validation.json`; raw logs and lifecycle JSON stay local.

SDL's existing verifier checks its saved integration snapshot, pinned upstream
source tar, complete installed file set and GL identity. The old snapshot may
differ from current integration sources. It is neither executed nor rewritten.
The original `hardware_run: false` build receipt remains unchanged; the later
hardware acceptance is a separate document.

## Local assembly and consumer checks

Use WSL and the public payload SDK from native-app boilerplate commit
`4e1d1277dd0531a9a9df8c780e446b9cc26534dd`. Set `CANONICAL`, `SDL_BUILDS` and
`PS5_PAYLOAD_SDK` to existing local inputs. Create a separate relocated GL copy
and consumer report per profile before assembly:

```sh
PROFILE=1440p120 # repeat with 2160p120 in distinct output directories
SDK="$CANONICAL/build/sdk/ps5-opengl-core33-g31-$PROFILE"
cp -a "$SDK" "build/relocated-$PROFILE"
python3 tools/check-sdk-consumers.py \
  --sdk "build/relocated-$PROFILE" --payload-sdk "$PS5_PAYLOAD_SDK" \
  --example-dir examples/core33-triangle \
  --registry "$CANONICAL/third_party/mesa-26.2.0/src/mesa/glapi/glapi/registry/gl.xml" \
  --output "build/consumers-$PROFILE" --forbid-root "$CANONICAL"
python3 tools/build-sdk-bundle.py --hfr-profile "$PROFILE" \
  --sdk "$SDK" --sdl-build "$SDL_BUILDS/native-$PROFILE-20260908" \
  --source-commit "$(git rev-parse HEAD)" --results "$CANONICAL/results" \
  --consumer-report "build/consumers-$PROFILE/summary.json" \
  --third-party "$CANONICAL/third_party" --destination "build/bundle-$PROFILE"
```

The final command currently fails on the privacy blocker above. There is no
override. `--third-party` only reads pinned existing source archives/repositories;
it performs no fetch or rebuild. Every destination must be new and outside inputs.

The existing archive layout retains separate `sdk/` and `sdl2/` prefixes,
headers/libraries, Make/pkg-config/CMake metadata, licenses, examples, complete
committed project/dependency sources, exact SDL integration inputs, manifests,
checksums and provenance. The source snapshot is not silently redacted: source
archives also pass the HFR content gate. A privacy failure requires review.
No toolchain binaries, proprietary modules, native title assets or raw console
logs belong in a distribution. The generated graphics import stubs are public
project artifacts, not firmware modules.

For an approved archive, verify its checksum sidecar before extracting into a
new directory, then run `sha256sum --check SHA256SUMS` at its root and
`sha256sum --check manifest.sha256` inside both prefixes. Re-run the included
GL consumer checker using the extracted example and `verification/gl.xml`.
SDL consumers use `sdl2` pkg-config metadata with both prefixes in
`PKG_CONFIG_LIBDIR`, or `SDL2::SDL2` with both in `CMAKE_PREFIX_PATH`.
Use the C++ link driver, `SDL_MAIN_HANDLED`, `SDL_SetMainReady` and the native
entry point; do not add SDL2main. These links do not execute on hardware.

## Fresh CI builds

Manual release-workflow runs offer `1080p60` (default), `1440p120` and `2160p120`.
Tag-triggered builds retain 1080p60. HFR manual artifacts append the profile to
their version. `--ci-profile` checks the SDK header against the recorded runtime
defines; malformed, duplicate or mismatched profile definitions fail packaging.
This lane builds fresh GL SDKs and remains explicitly **host-only**. It does not
reuse the frozen SDL payload, receive G37–G40 acceptance, or publish a manual run.
No workflow was dispatched or release changed by this local packaging work.

Packaging regressions: `python3 -m unittest discover -s tools -p test_sdk_bundle.py`.
The archive fixture checks reproducible gzip/tar output, extraction, checksums,
relocation and tamper detection; it is not a graphics SDK or hardware result.

- 2026-09-08 | G41 | partial-pass: frozen G37–G40 audits, host suite, 22 packaging tests and ten relocated links pass; archives blocked by embedded personal paths | parent decision.
