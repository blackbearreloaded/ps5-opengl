# Frozen high-resolution SDK bundles

The `--hfr-profile 1440p120` and `--hfr-profile 2160p120` modes of
`tools/build-sdk-bundle.py` assemble the original G31 GL SDK and matching G32
SDL2 payload. Version names are
`0.1.0-perf20260908-g41-<profile>-sdl2-focused`; output uses the existing
`.tar.gz` format and checksum sidecar. Existing sampled, targeted and CI
version modes remain separate.

**Original frozen-mode blocker:** the original GL static libraries contain personal
absolute build paths. The HFR packager rejects these bytes before creating a
distribution directory. Removing paths changes frozen hashes. No distributable
original-runtime HFR archive has been produced. G47 is a separate, hash-pinned
derivative input branch, not an override for this frozen mode. It requires new
runtime identity and maintainer focused hardware qualification. Local consumer checks can still
use the original libraries. Passing those checks does not waive this blocker.

The read-only ELF audit found nine actual private paths per runtime in
allocatable `.rodata.str1.1` (`SHF_ALLOC|SHF_MERGE|SHF_STRINGS`, flags `0x32`).
These offsets are identical at both resolutions and have code relocations:

| Archive member | String offsets within `.rodata.str1.1` |
| --- | --- |
| `ps5_egl.o` | `0xee`, `0x1a4` |
| `ps5_screen.o` | `0x9a`, `0x151`, `0x7f0`, `0x8ac`, `0xd51` |
| `u_framebuffer.o` | `0x19`, `0xcd` |

The referenced filenames are Mesa's `util/simple_mtx.h`,
`gallium/auxiliary/util/u_inlines.h` and `util/format/u_format.h`, plus PSBC's
`util/simple_mtx.h`, `compiler/nir/nir.h`, `compiler/nir/nir_intrinsics_indices.h`
and `util/format/u_format.h`, under their recorded `third_party` roots. These
assertion filenames are compiled into the runtime, including its
`u_framebuffer.o` helper; rebuilding the Mesa or PSBC libraries is unnecessary.

The other 16 GL archives are shared byte-for-byte across profiles and have
3,701 private-path occurrences each profile, all debug-only. Each runtime also
has 30 debug occurrences. SDL2, the two import stubs and the umbrella linker
script have none. Whole-file occurrence accounting found no paths outside the
classified sections; GNU `ar t` independently confirmed member inventories.
A debug-strip derivative alone cannot sanitize the runtime assertion filenames.

The smallest proposed derivative keeps the original inputs, strips debug data
only in new copies of the 16 shared archives, and rebuilds the small runtime
archive per profile with `-ffile-prefix-map=<actual-root>=ps5-opengl` (including
all source/build roots). Keep assertions and original runtime options. Compare
all retained section bytes/attributes, normalized relocations, symbol identities
and archive membership for the stripped copies; validate new runtime changes
and relocated links separately. The existing runtime Make target invokes Ninja
for Mesa, so a reviewed runtime-only build must prevent that dependency rebuild.
No strip, derivative or rebuild was performed during this read-only audit.
New manifests must identify the derivative and original inputs separately;
retain original SDL/GL receipts and do not transfer frozen hardware acceptance.

## Separate G47 derivative input

`--g47-profile` reuses the existing SDL verifier/copy, source snapshots, privacy
gate, `.tar.gz` writer and checksum layout. It does not build or strip anything.
The accepted independent build provenance SHA-256 is
`fbb0cbc4b3fb872ceb6c9e265c489e92e57a659288b404bff9d4460a4a74d9ed`.
Its source is `23a594c3a5fda4135599d0dea8d2bf4dcef47a39`; original G31 identities
remain recorded separately. The independent build audit's documentation signoff is
`71dd08a558264934a25c94b344df95bf8f3c8ed9`, not the runtime build source.

The independent build stripped debug data from **copies** of 16 dependency archives using
`llvm-strip-21 --strip-debug -o <new-copy> <staged-G31-copy>`. Only the five
runtime objects were rebuilt with the frozen configuration and file/debug/macro
prefix maps. No Mesa/PSBC dependency rebuild occurred. The public provenance
contains the exact mapped Make commands, compiler/input identities, 38-file
original-to-derived maps per profile and four pinned host audit records.

There is one reviewed linker-metadata exception: 1,263 nonallocated LLVM
address-significance sections per profile retain their bytes/attributes but
lose the symbol-table link after debug symbols are removed (585 are nonempty).
LLVM 21 deliberately invalidates these tables when symbol indices change;
LLD ignores the zero-link table, conservatively limiting safe ICF. All six
resolved consumer links requested no ICF. This is **not strict semantic or
optimization equivalence**; other retained sections, normalized symbols,
relocations, groups and archive inventories passed the exhaustive independent checks.
No manual ELF repair is used. See the pinned `dependency-audit.json`,
`addrsig-guard.json` and `consumer-audit.json`; their public bytes are copied
unchanged under `verification/g47-*.json` together with the privacy audit and
derivative provenance. The privacy claim is no actual private build paths,
not absence of public URLs or generic temporary-path constants.

| G47 SHA-256 | 1440p120 | 2160p120 |
| --- | --- | --- |
| GL manifest | `5c9da7020167a604400f9a378d20689c78cd8d9d8ec48d889b74aa698e63965c` | `1585e458b2ce884bc68c3e0b9439955e0e47e1d895e0ce76023ccd5739a7e577` |
| GL runtime | `38072f5d0ed30c43b273fac36ebceabbaf943bfe72a956f38e4d0ad157f2ac8a` | `fcca06d1701edae0881105c3cdb24eb92a152397e024184c2eabe9254d79a675` |
| New SDL receipt | `7d430fdeb4ff8262aaf362e74de2e4304d0c12ac5630dc4355e49282dd384f9e` | `0646a0792c5ab1278374e4cb0fadc35d732dd373936b1ac36058d6b8a57f1779` |

SDL's source companion is `ad738dca822e0559f8a17b655784dfc72a4fd30a`.
Its complete recorded integration snapshot is verified, never executed or
rewritten; it may differ from the packaging checkout. These new offline SDL
receipts bind the G47 SDKs. Original G32/G42 hardware results do not transfer.

The new focused qualification requires one 30-second ImGui window and one
180-frame SDL cycle **for each exact new SDK/runtime/eboot**. The window build
source is pinned independently of the runner checkout: a later documentation
commit may run the unchanged app. Every acceptance/raw/candidate identity is
checked. Top-level native HDMI negotiation must reproduce the exact profile
and same-resolution 60 Hz restoration; render size and the nested ImGui
`output_mode_verified: false` are not HDMI acceptance. Missing/standby/mismatched
HDMI evidence fails packaging. Live per-run TV visual confirmation is not
recorded. No old acceptance, sampled/full CTS, extra workload, controller or
soak qualification enters this branch.

At this input-branch milestone, 2160's new window and SDL receipts are pinned:
3,597 measured window frames over 30.005143 seconds (119.879451 FPS), and 180
SDL frames; two exact pixels per workload. Both native HDMI captures show
3840×2160 at 119.88 Hz, restored to 59.94 Hz, with clean teardown and health.
Window acceptance SHA-256 is
`241720ec3328603b819f8c7a6508bda4f28c5672f9a445d75ddd22cd9bedc912`;
SDL acceptance is `90417420114e0b4fe941c50ddf8bb5c4b8c9783c278a971a0a6f38b7a661facd`.
On September 9, the frozen 1440p pair passed its new focused qualification:
3,597 window frames over 30.004993 seconds (**119.880050 FPS**) and 180 SDL
frames, with two exact pixel probes each. Both captured native 2560×1440 at
119.88 Hz, restoration to 59.94 Hz, clean teardown and healthy services.
The runner checkout was `92dc5ac3dce314f70b188024bd10db809dcd52fe`; app build
and library identities remain unchanged. Window acceptance SHA-256:
`3b04666af2cac957287ef98e140b710482b94c9fbf26a07c51e020704717298a`;
SDL acceptance: `a1b85e8aca51264927becc7e5c5ab5b5e9a934a03d6e0a69d0706726a26a30af`.
Both profile receipt pairs are now pinned; missing or modified evidence still
fails packaging. The owner reported the TV on, but no per-run visual or
physical-input acceptance is claimed for these two tests.

The local 2160p archive was subsequently assembled from source snapshot
`e41dea9d6799c8ad0c4ebe053fcb83174c5ecc55`. Archive SHA-256:
`38ee215ba2429b0f7ff6e96d3375c674ee2380e629111a15c1fa01cc59d6069e`
(174,370,158 bytes). Its checksum sidecar, 242 checksummed files, both installed
manifests, and five GL/SDL consumer links passed after extraction into a new
directory. The integrated host suite, including 31 packaging checks, passed.
The archive remains local and unpublished; earlier downloads are unchanged.
Local output: `build/bundle-g47-2160p120-v1/`; extraction checks:
`build/g47-extracted-2160-v1/`. This later documentation does not change that
archive's source snapshot or frozen library identities.

The matching 1440p archive was assembled on September 9 from source snapshot
`2309e5ca9868fae7b111ad5596bee103e60a4c8e`. Archive SHA-256:
`8c0a91b6951eb96e075464e751ca727c764826e857301518d916244f3ca25538`
(174,370,464 bytes). The sidecar, all 242 checksummed files, GL/SDL manifests
and five extracted consumer links passed; the full host suite also passed.
Local output: `build/bundle-g47-1440p120-v1/`; extracted verification:
`build/g47-extracted-1440-v1/`. Both archives remain local and unpublished.
The original 4K archive checksum was reverified unchanged; no library rebuild
or new CTS campaign was needed for this qualification and packaging milestone.

For either qualified profile, set `DERIVATIVE` and `SDL_BUILDS` to the preserved
`build/g47-path-free-v1` and `build/g47-sdl-v1` roots and run from the clean
packaging checkout (destination must not exist):

```sh
PROFILE=2160p120 # or 1440p120, with HEIGHT=1440
HEIGHT=2160
python3 tools/build-sdk-bundle.py --g47-profile "$PROFILE" \
  --sdk "$DERIVATIVE/relocated/$PROFILE/gl" \
  --derivative-provenance "$DERIVATIVE/provenance.json" \
  --sdl-build "$SDL_BUILDS/native-$PROFILE" \
  --candidate "$CANONICAL/.local/g47-apps/$HEIGHT/window/candidate.json" \
  --source-commit "$(git rev-parse HEAD)" --results "$CANONICAL/results" \
  --third-party "$CANONICAL/third_party" --destination "build/bundle-g47-$PROFILE-v1"
```

Versions are `0.1.0-perf20260908-g47-<profile>-sdl2-focused`. The pinned independent relocated
consumer audit supplies the host evidence; no separate policy format or runtime
configuration override is accepted. Repeat extraction/integrity and GL/SDL
relocated consumer links on each completed distribution as described below.

## Original G31/G32 scope (not G47 acceptance)

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
The gate matches actual input/user roots and verified receipt hosts, not generic
`/home/user` paths, localhost documentation or upstream `.bin` fixtures. Its own
source and legitimate public source fixtures are regression-tested. The actual
host/service detail at `docs/release-candidate-20260908.md:34` was removed by an
explicit authorized source edit. The post-edit tracked-source scan has no
actual-private-context findings; complete snapshots are not silently redacted.
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

- 2026-09-08 | G41 | partial-pass: frozen G37–G40 audits, host suite, 22 initial packaging tests and ten relocated links pass; archives blocked by embedded personal paths | maintainer decision.
- 2026-09-08 | G41 review | 24 packaging tests and host suite pass; source privacy cleanup and runtime-only dry runs pass; nine allocated paths per runtime require a prefix-map derivative | originals and receipts preserved; no derivative built.
