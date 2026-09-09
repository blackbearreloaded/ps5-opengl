# G55 frozen GL/SDL2 release packaging

G55 is a separate fixed-profile successor for 1440p120 and 2160p120. The final
**4K SDK pair has passed all six native checks** below, including matching
2160p119.88 HDMI and same-resolution 59.94 Hz restoration in ImGui and SDL.
The 1440p pair is built and host-checked, not separately console-validated.
**By owner decision, 4K is the hardware release gate and duplicate 1440p native
runs are no longer required after it passes.** No output-setting change is
needed. Neither pair has been published.

The successor fixes depth/stencil mip-subresource blits and packed mip clears,
including masks and preservation of neighboring images. Its 112-case native
batch checks 1,373,440 depth and 1,018,240 stencil pixels. The first candidate's
packed-clear failure is retained separately and excluded from acceptance.
The short ImGui startup-inclusive window averages 113.63 FPS at 4K; this
includes preparation cost and is not a sustained-cadence result. SDL checks
180 frames and two exact pixels, not measured FPS.

Each profile retains six frozen identity pins, with an explicit acceptance
scope. The 4K index binds six native runs; the 1440p index is host-only and must
contain no native runs. Missing pins reject packaging before creating output.
A host pass, another resolution's pass, or an older receipt is never labeled as
native acceptance for a new binary. An incomplete mip-blit run cannot qualify
the complete 112-case 4K gate.

The packager builds nothing, launches nothing, and publishes nothing. G47
profiles, archives and acceptance remain unchanged. G55 has no sampled/full CTS,
extended-soak, physical-controller or independently observed TV acceptance.

## Prepared archives — September 9

Both archives passed their sidecar checksums, all **244** root file checksums,
GL/SDL manifests, and **three GL plus two SDL consumer builds after extraction**.
The packaging/source snapshot is `6de02989bd292168d2d1625282dc683c83d4ba9d`;
the runtime and SDL build source remains `9f3b6dda933727dad61175b69438005e6f8abf7b`.
No runtime rebuild or additional console cycle was needed for the policy change.

| Archive | Bytes | SHA-256 |
| --- | --- | --- |
| `ps5-opengl-sdk-0.1.0-perf20260909-g55-2160p120-sdl2-focused.tar.gz` | 174453207 | `5cd937cd2e42e9bb736e4e098a885a50a8e669f2e7c47686fa124eb93b91be2a` |
| `ps5-opengl-sdk-0.1.0-perf20260909-g55-1440p120-sdl2-host-checked.tar.gz` | 174455260 | `607010cf28b391a6db816eeb289832b2a57f72b38adc38d83ba20695c749b474` |

The 4K entry supersedes the unpublished packaging draft ending in checksum
`0c0113`; that draft is preserved locally. Historical G47/G25 downloads are
unchanged. Local `make test`, `make test-staging`, 43 packaging regressions and
SDL integrity/profile checks passed. The staging gates are required by both
GitHub workflows; the updated workflows have not yet run on GitHub. Publication
of source and these exact archives is still a separate step.

## Maintainer integration API

The final identities are pinned independently in
`tools/build-sdk-bundle.py:G55["1440p120"]` and `2160p120`:

| Pin | Required value |
| --- | --- |
| `runtime` | Full commit used to build the final runtime |
| `sdk` | Final GL `manifest.sha256` SHA-256 |
| `archive` | Final `lib/libps5_opengl_core33.a` SHA-256 |
| `sdl_receipt` | New SDL native build's outer `receipt.json` SHA-256 |
| `sdl_source` | Full source companion used for the SDL build/folder |
| `evidence` | SHA-256 of the reviewed per-profile release index below |

There is no command-line identity or acceptance-scope override. The fixed versions are
`0.1.0-perf20260909-g55-2160p120-sdl2-focused` and
`0.1.0-perf20260909-g55-1440p120-sdl2-host-checked`. Record the runtime build commit, application
build commits, runner commits, and packaging/source-snapshot commit separately.
Do not replace the runtime commit with a later test-only or documentation commit.

`--candidate` names a JSON release index; `--results` is the local evidence root.
Every reference in the index is a normalized relative POSIX path inside that
root. Absolute paths, traversal, symlinks and missing inputs are rejected. The
workspace root is convenient when the preserved SDKs are in sibling clones.
The index and raw records are inputs only; they are not copied into the bundle.

```json
{
  "format": "ps5-opengl-g55-release-v1",
  "display_profile": {"width": 3840, "height": 2160, "fps": 120},
  "runtime_source_commit": "FULL_FINAL_RUNTIME_COMMIT",
  "sdk_manifest_sha256": "FINAL_GL_MANIFEST_SHA256",
  "runtime_archive_sha256": "FINAL_RUNTIME_SHA256",
  "sdl_source_companion": "FULL_SDL_SOURCE_COMMIT",
  "sdl_build_receipt_sha256": "FINAL_SDL_RECEIPT_SHA256",
  "inherited_acceptance": false,
  "g47_provenance": "dev/ps5-opengl-g43-independent-20260908/build/g47-path-free-v1/provenance.json",
  "g47_sdk": "dev/ps5-opengl-g43-independent-20260908/build/g47-path-free-v1/relocated/2160p120/gl",
  "g51_candidate": "publish/ps5-opengl/build/g51-layer-query-v1/candidate.json",
  "build_provenance": "publish/ps5-opengl/build/FINAL_G55_BUILD/candidate.json",
  "build_provenance_sha256": "FINAL_BUILD_RECORD_SHA256",
  "consumer_report_sha256": "FINAL_GL_CONSUMER_SUMMARY_SHA256",
  "runs": {
    "mip-blit": {
      "audit": "publish/ps5-opengl/results/RUN/ACCEPTANCE.json",
      "audit_sha256": "ACCEPTANCE_SHA256",
      "app": "publish/ps5-opengl/results/RUN/PPSA99005-YYYYMMDD-HHMMSS-opengl.log",
      "candidate": "publish/ps5-opengl/.local/FINAL_APPS/mip-blit/candidate.json",
      "source_companion": "FULL_APP_BUILD_COMMIT"
    }
  }
}
```

The example intentionally cannot pass. For native 4K acceptance, supply exactly
these six `runs` keys:

| Key | Required raw checks |
| --- | --- |
| `mip-blit` | Native `egl_public_core33_depth_mip_blit.o`; all 112 ordered cases, four initializations/cleanups, 1,373,440 depth pixels and 1,018,240 stencil pixels, zero errors |
| `depth-array-samples` | Native `egl_public_core33_depth_array_samples.o`; 24,576 pixel tuples, 1x/4x, all three phases/four layers, four explicit draws and cleanup |
| `depth-array-fetch` | Native `egl_public_core33_msaa4_depth_array_texture.o`; D32/D32S8 resolve/stencil/sample counts, native driver counter and cleanup |
| `depth-mip` | Native `egl_public_core33_depth_mip_target.o`; five legal targets at depth 0.5, five successful draws, expected `GL_INVALID_OPERATION` for illegal 3D depth and cleanup |
| `imgui` | Native `egl_public_core33_imgui_tv.o`; explicit `mode: "startup"` or `mode: "window"`, described below |
| `sdl` | Newly linked `egl_public_core33_sdl2.o`; 180 frames and the two exact pixel probes |

The 1440p index instead uses its own profile and exact SDK/SDL/build/consumer
hashes, `"qualification": "host-only"` and `"runs": {}`. Packaging still verifies
the same dependency lineage, source equivalence, SDL integrity and consumer
contracts. Its generated summary reports zero native cycles and
`hardware_validation: false`; no 4K receipt is copied or counted as a 1440p run.
The raw native-audit contract below applies only to the 4K profile.

Each entry has the five fields shown for `mip-blit`; `imgui` also requires
`mode`. The four depth gates require empty candidate `build_flags`. Startup
requires `PS5_IMGUI_PROFILE="1", PS5_GPU_MEMORY_PROFILE="1"`; window requires
`PS5_IMGUI_PROFILE="1", PS5_IMGUI_WINDOW_BENCHMARK="1",
PS5_IMGUI_WINDOW_TARGET="120"`.

## Raw audit contract

The acceptance JSON retains the existing `.local/g44-audit.py` structure:
`classification: "pass"`, `workload`, `display`, `candidate`,
`runner_companion`, and `raw_sha256`. The latter must contain exactly `app`,
`klog`, `cycle`, `runner`, and `candidate`, with SHA-256 of their exact bytes.
The cycle, runner and klog paths are derived from the selected `-opengl.log`.
The external candidate must equal the embedded candidate.

`g55_release_evidence.summarize_workload(kind, text, display, mode=None)` is a
pure function for the maintainer's offline auditor. It returns the canonical
`workload` object. The mip-blit parser checks every emitted case identity,
sample direction, mask, pixel count and error field against the reviewed
14-stage oracle; totals alone are insufficient. The existing local auditor's
`depth-mip-blit` kind maps to the index key `mip-blit`.

Use `summarize-display.hdmi_report(klog, "PPSA99005", width, height, 119.88)`
for `display`; add `acceptance_scope: "not-a-display-test"` for the four depth
gates. These numerical gates do not establish display qualification. ImGui and
SDL must independently have native matching HDMI negotiation and restoration
to 60 Hz at the same resolution. HDMI logs do not establish per-frame TV output.

Startup reuses the existing ImGui profile/retirement/preparation auditor and
checks the complete startup clock, frame and stage accounting. Its elapsed
first-window FPS remains diagnostic, including startup cost; a low value is
not relabeled as a 120-FPS pass. Window mode instead requires the existing
30-second native window benchmark and its timing/output checks. Neither mode
is a soak. Uniform MSAA values remain explicitly outside sample-isolation proof.

Every run must bind the final GL manifest/runtime and executable to the native
cycle; require the recorded libc, gate, runner/protocol commit, clean teardown,
post-health and lock release. The packager recomputes the workload and display
and compares them to the acceptance JSON after verifying every raw hash. It
never imports or executes a verifier from the evidence directory.

For SDL, create a canonical acceptance wrapper with the same fields rather
than feeding an old G47 SDL acceptance. Embed the new `folder.py` candidate,
include its hash as `raw_sha256.candidate`, and set `runner_companion` from the
runner. `source_companion` in the index identifies the SDL app build. The folder
candidate's SDL receipt, GL identity, selected-test hash, example selector,
template libc and executable must all match. The SDL build receipts retain
`hardware_run: false`; hardware validation is separate.

## Build provenance and distribution

The existing final SDK `candidate.json` is expected to contain
`source_companion`, `hardware_run: false`, `new_manifest: {sha256, files}` and
`runtime_sha256`. The index pins its exact bytes; those fields must match the
final SDK. Private commands in that record remain local.

G47's existing verifier checks its original SDK and five pinned provenance/audit
records. G55 then checks that all installed files except the runtime and Mesa
archives retain those reviewed bytes. The separately pinned G51 record must
match the final `libmesa.a` and the distributed `toolchain/mesa-ps5.patch`.
Its source/object hashes and ADDRSIG exception enter
`verification/g55-build-validation.json`; the private original does not.
The G47 records remain historical dependency evidence, not G55 consumer or
hardware acceptance. A different Mesa rebuild requires a separately reviewed
update to this explicit contract, not an identity override.

Run from the clean, committed packaging checkout. Runtime source equivalence
and application/integration source equivalence must hold against the archived
source companion. The GL consumer report is separately hash-pinned and must
pass for the exact SDK with all 344 Core exports and three consumer links.

```sh
python3 tools/build-sdk-bundle.py --g55-profile "$PROFILE" \
  --sdk "$FINAL_GL" --sdl-build "$FINAL_SDL_NATIVE_BUILD" \
  --candidate "$G55_RELEASE_INDEX" --results "$LOCAL_EVIDENCE_ROOT" \
  --consumer-report "$FINAL_GL_CONSUMER_REPORT" \
  --source-commit "$(git rev-parse HEAD)" --third-party "$DEPENDENCY_SOURCE_CACHE" \
  --destination "$NEW_BUNDLE_DIRECTORY"
```

The unchanged SDL verifier/copy path preserves both SDL receipts, manifest,
licenses, upstream tar and exact integration snapshot. The unchanged assembly
includes project source/patches/notices, Mesa source, and all eight existing
dependency/example source snapshots. Payload toolchains and native title assets
remain separate prerequisites. Nothing is stripped or redacted while packaging.

The privacy scan checks both prefixes before staging and the entire staged
distribution, including nested source archives, before archiving. Hosts from all
six native receipts are forbidden markers. Only reconstructed numerical summaries and
hashes are published in `focused-validation.json`; raw logs, build commands,
candidate files, hostnames and private paths are not copied.

After assembly, verify the archive sidecar, extract into a new directory,
verify root `SHA256SUMS` and both prefix manifests, then run three GL and two
SDL consumer links from the extracted sources/metadata. Inspect dependency/link
traces for use of the extracted prefixes; do not use `verify-installed-sdk.sh`,
which invokes the installer. The old count of 242 files is not a G55 invariant.
Keep final archive hashes for subsequent download verification.

Host packaging tests: `python3 -m unittest discover -s tools -p test_sdk_bundle.py`.
SDL integrity/profile checks: `python3 tools/test_sdl_sdk.py`.
