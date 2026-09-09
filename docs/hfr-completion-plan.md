# High-resolution completion work

Starting point: G37–G40 qualified the unchanged G31/G32 artifacts at native
1440p119.88 and 2160p119.88 on one firmware-6.02 console. These are focused
application receipts, not new CTS campaigns or universal game-performance claims.

Latest local status: [G55 release preparation](#g55-release-preparation).
The milestones below retain earlier failures and then superseding results.

**Current policy, September 9:** use 4K as the hardware release gate. After it
passes, omit duplicate 1440p native runs; retain per-profile build, checksum and
consumer checks. Label 1440p host-checked only. Investigate it on hardware only
when a resolution-specific issue warrants it; historical receipts stay unchanged.

| Gate | Owner | Work and acceptance |
| --- | --- | --- |
| G41 | Packaging agent | Frozen GL/SDL2 bundles for both heights; complete public sources, licenses, checksums, provenance and focused receipts; reject mismatched evidence; relocated consumers pass. Preserve old releases. |
| G42 | SDL agent | Bounded physical-input/reconnect example and host contract checks. Actual button, axis, disconnect and reconnect observations required; timeout is incomplete. |
| G43 | Independent-build agent | Separate clean build trees, pinned inputs, explicit reused/rebuilt inventory, manifest and relocated consumer checks. Newly built bytes remain host-checked until hardware tested. |
| G44 | Main | Reuse cubes ordinary/instanced profiles and selected ImGui offscreen cases. 30 seconds per measured workload, before/after pixels and exact retirement/cleanup. Report measured bottlenecks without extrapolating to games. |
| G45 | Main | Finish the already-started ten-minute 1440p heap/GPU check; use two minutes at 4K per owner preference. Reuse three-session EGL lifecycle checks. No deliberate hardware faults or memory exhaustion. |
| G47 | Build/packaging agents + main | Path-free release derivatives: preserve original SDKs, strip debug data from dependency copies, prefix-map rebuild only the runtime, verify new manifests/links and focused native receipts. No inherited CTS acceptance. |
| G46 | Main | Review and integrate isolated agent changes, freeze source/artifacts, run affected checks and publish scoped release evidence. Do not transfer old conformance claims to changed bytes. |

Only the main agent touches the console, through the existing native-folder
runner and shared exact-token lock. Prepare and audit offline; release the lock
after every bounded cycle. Use logs and numerical probes, not routine screenshots.
Stop on uncertain foreground, lifecycle or service health. After G44's first
4K-render/1440p-HDMI mismatch, the owner selected 2160p. The identical frozen
cube app then qualified native 2160p119.88 and restoration to 2160p59.94.

Physical controller actions and an independently available second console/firmware
require owner participation. Suspend/resume, device-loss recovery and hardware
OOM behavior stay explicitly unqualified unless safely validated later. CPU
staging outside the measured fast paths is a performance limitation, not an
automatic functionality failure; optimize only a demonstrated bottleneck.

Record one short milestone per completed gate, linking immutable evidence. No
full 39,544-case rerun for example, documentation or packaging-only changes.

- 2026-09-08 | G44 1440p cubes | 0826b57 | pass: 57.01 ordinary / 112.91 instanced FPS; pixels, counts, HDMI, teardown/health/unlock | results/g44-cubes-1440-20260908/; next offscreen.
- 2026-09-08 | G44 1440p offscreen | 0826b57 | pass: 119.90 completed FPS, pixels/retirement/preview HDMI/cleanup/health/unlock | results/g44-offscreen-1440-20260908/; next soak.
- 2026-09-08 | G45 1440p | partial-pass: 71,678 frames/600s, 21 pixels, zero steady tracked growth, GPU balanced, clean/healthy/unlocked; first-window cadence miss | results/g45-soak-1440-20260908/.
- 2026-09-08 | G45 1440p lifecycle | pass: three EGL sessions/18 frame oracles, zero post-session heap growth, GPU baseline restored, three HDMI restore cycles, clean/healthy/unlocked | results/g45-lifecycle-1440-20260908/.
- 2026-09-08 | G42 | pass: owner-confirmed physical Cross/stick sequence before/after removal and new instance; 38.336s, HDMI restoration, clean/healthy/unlocked | docs/sdl-input-validation.md.
- 2026-09-08 | G44 4K cubes attempt 1 | render workload passed, HDMI mismatch: 2160p render / 1440p119.88 output; clean/healthy/unlocked; not native-4K acceptance | results/g44-cubes-2160-20260908/.
- 2026-09-08 | G44 native 4K cubes | pass: 39.96 ordinary / 59.94 instanced FPS; all pixels/counts, native 2160p HDMI, clean/healthy/unlocked; clear ~12ms | results/g44-cubes-2160-native-20260908/.
- 2026-09-08 | G43 | reviewed host pass: independent 1440/2160 GL builds, 38 files/344 exports each, six relocated GL links and two 1440 SDL links; no hardware inheritance | docs/independent-hfr-build.md.
- 2026-09-08 | G41/G46 | integrated 24 packaging checks + SDL integrity/profile checks; full host suite passed; frozen HFR archives blocked by personal build paths, G47 preparing separate derivatives | docs/sdk-bundle-hfr.md.
- 2026-09-08 | G47 build | both derivatives host-checked: exact manifests, six relocated consumers, privacy audit and explicit ADDRSIG exception; originals unchanged | docs/sdk-path-free-derivative.md.
- 2026-09-08 | G47 4K | five cycles closed: window 119.879 FPS, offscreen 119.901 completed FPS, three EGL sessions/18 pixels, SDL 180 frames/two pixels; 120s memory pass/cadence partial; HDMI/cleanup/health/unlock passed, no live TV observation | results/g47-{window,offscreen,soak,lifecycle,sdl2}-2160-20260908/.
- 2026-09-08 | G46/G47 | local 4K bundle verified: 242 checksummed files, extracted GL/SDL manifests and five consumer links pass; integrated host suite/31 packaging tests pass; unpublished | docs/sdk-bundle-hfr.md.
- 2026-09-08 | G47 1440p | no-run: owner requested Chiaki configuration, but Windows denied desktop access before launch; no settings changed, post-health passed and exact lock released | results/g47-chiaki-display-20260908/.
- 2026-09-09 | G47 1440p | pass: window 119.880 FPS; SDL 180 frames/two pixels; both native HDMI/restore/cleanup/health/unlock passed | results/g47-{window,sdl2}-1440-20260909/.
- 2026-09-09 | G46/G47 | both local HFR bundles verified: 242 checksummed files each, GL/SDL manifests and five extracted consumer links each; host suite passed; unpublished | docs/sdk-bundle-hfr.md.
- 2026-09-09 | G46/G47 | published: both frozen HFR SDKs and sidecars downloaded from GitHub and hash-verified; source CI passed; prior assets unchanged | docs/sdk-bundle-hfr.md.

The 1440p soak's first 30-second window was 112.17 FPS; the remaining nineteen
were 119.83–119.87 FPS. Its strict 120-Hz cadence target failed. Explicit
`--report-cadence-miss` preserves `target_met=false` while checking all pixel,
timing and retirement records; it does not turn that result into a full pass.
Tracked owned heap ended at 9,455 bytes/22 blocks; tracked GPU allocations and
mappings returned to zero. Module-internal memory, RSS and longer sessions are
outside this observation. No additional ten-minute runs are planned.

To avoid duplicate console cycles, the 4K offscreen, two-minute memory check
and three-session EGL check used G47's separately verified release derivative.
Prepared but unrun G31 versions stay preserved. New 30-second ImGui and
180-frame SDL receipts bind the 4K derivative to its actual binaries. The
equivalent two 1440p checks also passed after the owner selected 1440p,
with fresh 1440p119.88 negotiation and 1440p59.94 restoration in both logs.
No desktop control or new app build was needed. Both matching archives passed
local assembly, extraction, integrity and consumer-link checks. The focused
high-resolution release gate is complete and the exact SDK archives are published.
No console lock is held. The broader qualification limits above remain.

## Next performance gates (local development)

- G48: separate depth-fill/cache-flush costs; optimize the existing clear path
  with unchanged ownership, bounds, masks and synchronization. Reuse host
  correctness checks, a bounded clear/depth/lifecycle batch, then matched
  30-second ordinary/instanced cube runs. Preserve the released G47 bytes.
- G49: diagnose first-window cadence using existing receipts, then instrument
  only the missing timing boundaries. Retain startup costs and original failed
  receipts; a warm-up exclusion is not a fix. Use short bounded checks.

Only the main agent uses the console through the exact-token lock. Start at
the owner's recorded 1440p; native 4K comparisons require matching output.
No game changes, whole-CTS rerun, release replacement or automatic publication.

G48's first candidate replaces only the unscissored tiled depth fill with
Mesa's existing `util_memset32`; cache flushing, masks, layer bounds and queue
ordering stay unchanged. The local SDK changes only `ps5_screen.o` in G47's
runtime archive; the other four objects and all dependencies remain byte-identical.
Run the existing clear sweep, layered-depth gate, three-session lifecycle gate
and 30-second-per-mode cube profile. Require their pixel/status/retirement
oracles and clean teardown; compare stage times, not just refresh-capped FPS.
The host fill/flush microbenchmark is screening evidence, not PS5 performance.

G49 uses unchanged G47 runtime bytes and the existing startup-inclusive
30-second profiling mode (not a shortened soak). Add frame-0, first-30-frame
and first-window stage/clock receipts, preserving the existing probes and
acceptance. Historical cadence misses remain partial passes.

- 2026-09-09 | G48 precheck | clear sweep passed; layered draws missed identically on candidate and G47 control; both clean/healthy/unlocked | results/g48-layered{,-control}-1440-20260909/.

The layered test assigned `gl_Layer` only before its vertex loop, although
[GLSL 3.30 sections 7.1/8.10](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.3.30.pdf)
leave outputs undefined after `EmitVertex`. It also assumed untouched texture
contents were zero. Define every emitted layer and initialize the attachment;
keep the same per-layer pixel expectations. Compare this corrected test on the
unchanged release and the clear candidate before resuming performance tests.
The two original failed receipts remain unchanged; no driver fix is claimed.

The corrected oracle still failed on G47 (`g48-layered-fixed-control-1440-20260909`).
The next candidate adds the missing layer XOR to CPU depth/stencil addressing,
including transfers, mip staging and scissored clears. Pinned AMD 16-pipe
`64KB_Z_X` tables exactly match the existing X/Y/sample masks, but include
Z3..Z0 at address bits 8..11. Host checks cover both formats, 1x/4x samples,
32 layers and tile boundaries; device qualification is still pending.

- 2026-09-09 | G48 layer fix | pass: 3,072 clear and 3,072 draw pixels across three layers; full host suite and three installed consumer links pass; clean/healthy/unlocked | results/g48-layer-xor-1440-20260909/.

Next, reuse the scissored masked depth/stencil and 4x depth-texture gates,
then three-session lifecycle and paired 30-second-per-mode cubes. These check
the relevant layer-zero siblings, not every array/mip combination. The old
depth-array shader lacks scalar selection; depth-mip-target also requires an
invalid 3D depth texture. Do not use those unchanged as acceptance or relax their
oracles. Mip staging and nonzero-layer stencil/4x checks remain separate gaps.

- 2026-09-09 | G49 startup diagnostic | frame-0 clear 1,884.944ms versus steady 0.816ms; first window 112.22 FPS, pixels/cleanup/HDMI/health/unlock pass; not a cadence pass | results/g49-startup-1440-20260909/.

Reuse the native preparation timestamps on video acquisition in one additional
startup receipt. Separate setup, scanout flush, video acquisition/preparation,
and command construction/flush, preserving the existing warmup exclusion only
for steady averages. No new per-frame clocks, changed retries, initialization
order, or acceptance. A separate G47-derived diagnostic changes only the runtime
backend object; do not mix it into the clear-performance comparison.

- 2026-09-09 | G48 focused regression | scissor/4x-depth/lifecycle pass; 18 lifecycle pixels, zero post-session tracked heap growth, GPU baseline restored; all clean/healthy/unlocked | results/g48-layer-xor-{scissor,msaa-depth,lifecycle}-1440-20260909/.
- 2026-09-09 | G48 paired 1440p | clear 6.10→3.05ms; ordinary 57.24→59.94 FPS, instanced 114.81→119.88 FPS; all pixels/counts/HDMI/cleanup pass | results/g48-{control,layer-xor}-cubes-1440-20260909/; .local/g48-cubes-1440-comparison-v1.json.
- 2026-09-09 | G49 native startup | video acquisition/preparation 1,856.544ms of first-clear 1,862.156ms; setup 0.163ms, one open attempt, no retry sleep; diagnostic/pixels/HDMI/cleanup/health/unlock pass, not cadence acceptance | results/g49-startup-prepare-1440-20260909/.

Both 1440/2160 G48 derivatives pass their manifests and three installed consumer
links; full host tests and the actual AMD-layout/depth-fill checks pass. The
1440 candidate SDK manifest is `ced3acfcb0a7650197692acef1f102d802c2e44897af7849e9d2b8fa9abb94b4`;
the diagnostic SDK manifest is `b7ea5ac1c8713a6d1049b7b674c5d62c85777d63d80d81f6b82f7ad632419432`.
They change only `ps5_screen.o` and only `ps5_agc_runtime_backend.o`, respectively,
against frozen G47; all other runtime members and dependencies are byte-identical.
Source is local, not pushed; a combined release build remains unqualified.

Next: owner selects 2160p and returns home. Reuse exactly the frozen 4K cube
folders `.local/g48-control-apps/2160/cubes` (eboot
`282ae274947a9145af3f1025a77088ec3be90d9f6b50c06078c6bf0f46126abc`)
and `.local/g48-layer-xor-regression-apps/2160/cubes` (eboot
`1a042b20a56cfa444a83a0e797d926119197a36c57b03311da761fd4cd3c2626`).
Require native 2160p119.88/restoration evidence and the unchanged profile auditor.
No performance extrapolation from 1440p, new long soak or whole-CTS rerun.

## G50 offline completion (September 9)

- Depth CPU regressions now exercise real map/unmap, mip staging and clear
  callers with independent AMD-table addresses, allocation extents and clear
  values. Nonzero layers, 1x/4x, packed padding, exact flush ranges, rejected
  capacities and untouched neighbors pass ASan/UBSan. Five deliberately broken
  helpers are rejected; cache/queue calls remain mocked, not hardware proof.
- Corrected depth-array scalar sampling and depth-mip legality pass llvmpipe
  pixel/error checks, including deliberate failures. Five legal mip targets
  must render; 3D depth allocation must produce `GL_INVALID_OPERATION`.
- `make test`, staging/initial two depth-target/layered-mip/ImGui/cube reference tests,
  compiler/GLSL/multidraw checks pass. Historical CTS receipts are only audited,
  not rerun or inherited by new binaries. See [host checks](testing.md#host-only).
- Combined OpenGL candidates contain the clear/layer fixes and startup
  diagnostics: five freshly compiled runtime objects per profile, 38 manifest
  entries, all 344 exports and six Make/pkg-config/CMake consumer links pass.
  The sixteen G47 dependency archives, import libraries and headers are reused
  byte-for-byte, including the [documented ADDRSIG exception](sdk-path-free-derivative.md).
  This is a clean **runtime** build, not a fresh dependency build or release.

| Local candidate | SDK manifest SHA-256 |
| --- | --- |
| `build/g50-combined-1440-v1/gl` | `f9ee4c4221c30626e04fce0771e587c752e769e95775366b02fd29957e73c1ac` |
| `build/g50-combined-2160-v1/gl` | `7c46afad98e16b7099c2a23fdf9d5e8c4de55d0929ab161fa6f0380c1ba43932` |

Each sibling `candidate.json` records input hashes, flags and consumer evidence;
runtime source is `5c4a43b`, prior to the separate Mesa-query successor below.
Builds use prefix maps and pass the existing complete-file private-context scan.
Corrected depth-array, depth-mip and 30-second ImGui startup folders are frozen
under `.local/g50-combined-apps/{1440,2160}/`, each with hashes and native-link
verification. None has been uploaded or run; G47 and prior G48/G49 artifacts
remain unchanged. No console access, lock acquisition or publication in G50.

The submission review identifies whole-descriptor snapshots as the next bounded
optimization: 128 ordinary cube draws copy 10.5 MiB and initialize 14 MiB, derived
from source, **not measured savings**. Consider live-inline-constant copies first,
then bounded constant flushes, retaining ownership/fences. Native allocation
reuse needs separate lifetime qualification. Details: `.local/g50-submission-review.md`.
No production submission change or new PS5 speedup is claimed.

When hardware is available, first qualify the corrected depth oracles and
combined binary in short native cycles; preserve status/pixels/retirement checks.
Then use the already frozen paired 4K cubes above after 2160p is selected.
Native nonzero-layer stencil/4x coverage and combined release qualification are
still pending; host success cannot close those gates.

## G51 offline Mesa query fix

Correcting the older attachment test exposed another issue: Mesa returned zero
for a nonzero 1D-array attachment layer. [GL 3.3 core §6.1.13, printed p277](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf)
requires the selected layer. The existing Mesa patch now uses its shared array
target helper in this query. An extracted-code regression covers 32 layers,
plain/null/error cases and rejects the original predicate. The corrected GL
oracle retains layer two, checks all 256 1D-array texels for routing/preservation,
and replaces its illegal 3D-depth draw with a legal 2D-array draw.

Unpatched system llvmpipe still fails the strict query (`selected=2`, `query=0`),
although routing, pixels and the other attachments pass. Five deliberate GL
faults are rejected. The host runner preserves **exit 1**, explicitly not a pass;
the new PS5 Mesa object is separately CPU-tested and cross-compiled, not executed
by that system renderer. No system-driver patch or hardware proof is claimed.

Both `build/g51-layer-query-v1/{1440,2160}/gl` successors pass manifests, private
context scans, 344 exports and six installed consumer links. Only
`main_fbobject.c.o` in `libmesa.a` changes from G50; 219 other Mesa members,
runtime objects and every other SDK file remain exact. The new object's debug
strip is semantically checked with one explicitly recorded ADDRSIG-link
invalidation (12 bytes); no ICF link option was introduced. G50 originals remain
unchanged. Build/input/member/consumer hashes are in the sibling `candidate.json`.

| G51 profile | SDK manifest SHA-256 |
| --- | --- |
| 1440p120 | `5444439c3f5f6fd50633c856de519ece4c8f36345e13642e48d4dca2538a14b2` |
| 2160p120 | `1eb3f40dcd6334dcdcbb7133244fc8c91dd491567eb37a9482c3a7bb834fb777` |

Qualify G51 with the strict corrected attachment gate before release. This fixes
a CPU metadata-query bug, not draw speed or HDMI negotiation. The 4K paired
performance and native nonzero-layer stencil/4x gates above remain pending.

- 2026-09-09 | G51 frozen | both strict query apps native-link verified at
  `.local/g51-query-apps/{1440,2160}/depth-targets/`; final host suite/static
  capability audit pass, known system-query failure preserved. Exact SDK/app/log
  inventory: `.local/g51-offline-inventory-v1.json`. Eight folders total including
  the six preserved G50 controls; zero uploads/launches/publication.

- 2026-09-09 | G51 4K native | pass: layer-2 query/routing, four depth targets, clean/healthy/unlocked; 2160p119.88/restoration logged | results/g51-depth-targets-2160-20260909-v1/.

- 2026-09-09 | G50 depth-array | failed: user SIGSEGV during mip generation; app exited/services healthy/unlocked, owner confirmed home | results/g50-depth-array-2160-20260909-v1/.

The byte-identical local rebuild resolves the failed app's stack to
`st_generate_mipmap` / `glGenerateMipmap`. The CPU mip filter dispatched depth
formats through Mesa's absent RGBA converter. G52 uses the existing depth
unpack/pack helpers, preserving stencil bytes; the runnable sanitizer regression
covers both depth formats, color controls, odd/1D extents, selected layers and
base levels, and rejects the old dispatch. G52's native result is recorded below.
The console recorded an app-level segmentation fault, not a kernel panic; the
failed receipt is retained and was not retried unchanged.

- 2026-09-09 | G48 paired 4K | pass: clear 12.1→5.6ms; ordinary 39.96→57.69 / instanced 59.94→119.02 FPS; pixels/HDMI/cleanup/health/unlock | results/g48-{control,layer-xor}-cubes-2160-20260909-v1/.

The paired cube runs use the two frozen G48 executables listed above, 128
objects and 30 seconds per mode. `.local/g48-cubes-2160-comparison-v1.json`
binds the unchanged auditor and receipt hashes. Both negotiated 2160p119.88
and restored 2160p59.94. These are completed workload averages, not a promise
of steady 60/120 FPS or an arbitrary game's performance; cadence misses remain.

- 2026-09-09 | G52 depth-array | caa5d7c | pass: corrected mip generation/raw/shadow sampling, cleanup/health/unlock | results/g52-depth-array-2160-20260909-v1/.

- 2026-09-09 | G52 depth-mip | failed: five draws rejected with status -17; expected 3D-depth rejection passed, clean/healthy/unlocked | results/g52-depth-mip-2160-20260909-v1/.

G53 aligns depth-mip staging offsets to the existing native API's 2 MiB
requirement, rather than color staging's 64 KiB. The pointer validators remain
unchanged. Extracted allocation/validator regression passes under sanitizers,
covers rounding/overflow and packed depth/stencil, and rejects the old alignment.
Host suites pass; G53's native result is recorded below. This can add up to roughly
2 MiB of padding per affected resource; no memory-use improvement is claimed.

- 2026-09-09 | G53 depth-mip | a236962 | pass: all five targets depth=0.5, draw=0/5, cleanup/health/unlock | results/g53-depth-mip-2160-20260909-v1/.
- 2026-09-09 | G53 array/MSAA | failed: 1x array depth/stencil blit rejected, then 4x depth-array format selection asserted; user abort, not recorded kernel panic; healthy/unlocked, owner confirmed recovery | results/g53-depth-array-samples-2160-20260909-v1/.

G54 addresses those two admission/copy gaps and the depth-MS-array descriptor
type found in the offline review. Existing depth/stencil copy paths now select
one checked tiled layer and retain its slice XOR; no GPU submission/lifetime
change. Mip-chain blits remain outside this path. Host regressions cover masks,
bounds, copy/resolve/replicate and capability prerequisites. The array oracle
stops on its first failed variant; native results follow.

## G54 local qualification

- 2026-09-09 | G53 4K startup | a236962 | pass: 3,404 frames/30.007s including startup (113.44 FPS), video preparation 1,571.212ms, native HDMI/cleanup/health/unlock | results/g53-startup-2160-20260909-v1/.
- 2026-09-09 | G54 array/MSAA | 0f78ee9 | pass: 24,576 color/depth/stencil pixel tuples at 1x/4x, four draws, layers 2/3 and neighboring-layer preservation, cleanup/health/unlock | results/g54-depth-array-samples-2160-20260909-v1/.
- 2026-09-09 | G54 MS-array fetch | 0f78ee9 | pass: D32/D32S8, all four sample indices at layers 2/3, 4,096 resolved-depth + 2,048 stencil + 2,048 sampled-color pixels, cleanup/health/unlock | results/g54-depth-array-fetch-2160-20260909-v1/.

Both `build/g54-depth-array-{1440,2160}-v1/gl` SDKs pass 38-file manifests,
344 exports and three consumer links each. Only `ps5_screen.o` changes from
G53; the other runtime objects and dependencies remain exact. Local manifests:

| Profile | SDK manifest SHA-256 |
| --- | --- |
| 1440p120 | `af3bf641cab22a968198b59840604198500efb4e305e68abcb94dd222510da1e` |
| 2160p120 | `3d87c654b45bbb24a59cdbdb6c969466404ae14ace82ffbc49ae0bb0db856794` |

Scope: the newest 2160p runtime has the two focused G54 native receipts, not a
new full CTS campaign. Its 1440p counterpart is host-checked only. MSAA values
are uniform across samples: fetching every sample index does not prove sample
isolation. Mip-chain depth blits, suspend/resume, device loss and hardware OOM
remain unqualified. The G53 startup window is diagnostic, not soak acceptance
or a G54 performance measurement. No publication or release replacement made.

## G55 release preparation

- 2026-09-09 | G55 v1 | 89c2602 | failed native: 28 D32 mip-blit cases passed; packed-mip initialization clears rejected, clean/healthy/unlocked | results/g55-depth-mip-blit-2160-20260909-v1/.

The shared blit paths now address canonical linear mip subresources, including
masked copies, resolve and replication. The native batch exposed a separate
D32S8 clear admission/stride gap; its fix preserves stencil write masks and
unselected channels. Sanitizer and software-GL regressions pass. Final packaged
qualification remains pending; the earlier G47 downloads are unchanged.

- 2026-09-09 | G55 v2 4K | 9f3b6dd | pass: 112 mip-blit cases, array/MSAA, fetch, five depth targets, ImGui/SDL HDMI, six clean/healthy/unlocked cycles | results/g55-*-2160-20260909-v2/.

Exact final-binary evidence is frozen in `build/g55-release-index-v2/2160p120.json`.
Both SDK/SDL pairs and all 1440p apps are built; three GL consumer links per
profile pass. The initially planned 1440p native window was canceled by owner
decision after the six 4K passes; its SDK remains explicitly host-checked only.
The packager recomputes raw numerical/lifecycle/HDMI receipts before distribution;
43 packaging regressions and SDL integrity/profile checks pass. No publication.

- 2026-09-09 | G55 release preparation complete | 6de0298 packaging / 9f3b6dd runtime | two archives: 244 checksums, GL/SDL manifests and five extracted consumer links each; 4K native-qualified, 1440p host-only per owner policy | [archive identities](sdk-g55-release.md#prepared-archives--september-9). No GitHub push/upload.

- 2026-09-09 | G55 published | 9eb75fc publication / unchanged frozen archives | hosted host/staging checks passed; four uploaded assets match local hashes/sizes; older releases unchanged | [release](https://github.com/blackbearreloaded/ps5-opengl/releases/tag/sdk-0.1.0-perf20260909-g55-hfr-sdl2-focused). No new console run.
