# Local enhancement gates

Control: published source `efc7df3`; preserve existing SDKs and validation exports.
Work stays local. Follow `.local/ENVIRONMENT.md` and the native-title protocol;
no console interaction without the shared lock, and no graphics ELF injection.

| Gate | Change | Acceptance |
| --- | --- | --- |
| G11 | Reserve the resource arena for buffers | Actual allocator/eligibility host tests, cleanup/fallback regressions, native texture/buffer creation and rendering. |
| G12 | Reuse fragment-texture flushes within retained batches | Changed backing/size, updates, batch/context boundaries and uncached stages remain correct; compare matched workloads and substantiate any speedup claim. |
| G13 | Reuse native tiled storage for single-mip 2D RGBA8 render/sample images | Existing transfer-map ownership instead of dual copies; other formats/mips/layers unchanged; upload/readback/copy/batching regressions and matched FBO timing/pixels. |
| G14 | Reliable high-refresh startup and app failure handling | Supported/unsupported/error paths have bounded outcomes; repeated launches render or exit cleanly. Distinguish render cadence, API status and the negotiated HDMI signal. |
| G15 | Account for GPU allocations and mappings | Partial allocation/map failures and release ordering tested on host; native lifecycle returns tracked live bytes/counts to baseline. |
| G16 | Focused acceptance | Frozen candidate, targeted regressions and matched workloads, sustained session and launch/exit checks; no automatic full CTS rerun. |
| G18 | Mip transfer/object-churn regression | 24 ordered native copy/upload/draw/sample cycles, exact mip/layer/base guards, balanced tracked GPU memory and clean teardown. |
| G19 | Color-blit channel mappings | Same mip checks plus 18 format/conversion checks in one native batch; two balanced GPU-memory sessions and clean teardown. |
| G20 | Corrected SDK distribution | Preserve G19 binaries and targeted evidence; verify archive integrity and all three consumers after fresh extraction. No new console run or inherited CTS acceptance. |
| G21 | Frozen G19 acceptance | Reuse the 51-case smoke on four configurations, 30-second matched offscreen/3D profiles, a 600-second tracked-memory soak and three bounded launch/exit cycles. Freeze each native app; stop on failure, preserve all receipts. |
| G22 | Latest-SDK application | Isolated Yamagi build using unchanged G19; bounded existing demo/game scenario and clean teardown. Keep game changes separate; no implied physical-input acceptance. |
| G23 | Independent G19 build | Separate compilation and relocated consumers using independently built dependencies; identify reused inputs and binary differences. Do not substitute canonical compiled archives. |

Keep G11/G12 separate from game-specific changes and preserve G9's copy optimization.
Use existing tests and batching infrastructure. Hardware fault injection, console
settings changes and speculative recovery after unconfirmed GPU retirement are
excluded. Suspend/resume, device-loss and cross-firmware claims require their own
safe test conditions and evidence; untested behavior is not complete.

## Milestones

- 2026-09-08 | G11 | host and native deferred-draw regression passed; clean teardown/health, lock released | `.local/g11-*.log`, `results/g11-arena-20260908/`.
- 2026-09-08 | G12 | host and native deferred-draw regression passed, including texture updates; clean teardown/health and exact-token release | `.local/g12-*.log`, `results/g12-texture-flush-20260908/`. The later matched old-layout FBO control measures 19.981613 FPS before caching versus 19.981309 after: no measurable FPS gain in this workload; do not attribute G13's gain to caching alone (`results/g12-flush-control-20260908/`). Large-texture-specific performance is not established.
- 2026-09-08 | G13 | native hazards pass; identical 1080p offscreen workload improves 19.981309 to 59.947540 FPS (600 vs 1,799 frames/30 s), pixels/teardown/health pass | `results/g13-{tiled-rgba8,fbo-control,fbo-native}-20260908/`. Mean target met, not every frame: p95 17.20 ms; affected sample completed below.
- 2026-09-08 | G14 local app | `1fdf0f7`: 300 s, 35,693 frames, 11 probes, 119.60–119.77 steady FPS; native exit and restored output/teardown/health pass | sibling `workspace/dev/ps5-imgui-4k120/results/g14-exit-20260908/`; manual cold launch and earlier zero-support cause remain open.
- 2026-09-08 | G15 `d1d3eef` | 3 EGL sessions / 18 frames pass; tracked GPU direct/mapped peak 90,783,744 bytes, both return to zero bytes/counts every session; heap cleanup 9,455 bytes/22 blocks, no inter-session growth; zero failures/invalid flags, clean teardown/health/unlock | `results/g15-gpu-lifecycle-20260908/`.
- 2026-09-08 | G16 soak | frozen G13 SDK, 600 s / 35,942 total frames (35,912 after warm-up) / 21 probes pass; 20 steady samples show zero heap/GPU growth; direct and mapped peak 82,558,976 bytes, both return to zero bytes/counts; clean teardown/health/unlock | `results/g16-gpu-soak600-20260908/`. Relocated SDK Make/pkg-config/CMake consumers and full host acceptance pass.
- 2026-09-08 | G16 acceptance | same frozen SDK: **204/204 Pass**, 51 cases in each of four configurations, zero skips/warnings/failures; exact render targets, ordered inputs, binaries and clean cycles audited. Native 1D/2D/3D texture copies and cube/volume mip-FBO draw/readback also pass | `results/g16-{cts-smoke,texture-copy,layered-mip}-20260908/`, `.local/g16-{candidate,cts-audit}.json`. One 240-second incomplete attempt is retained but excluded; the complete replacement used a timing-derived 450-second ceiling and stopped on completion.

G15 diagnostic builds set `PS5_GPU_MEMORY_PROFILE=1`; the existing heap snapshot
also records linked-title direct allocations and mappings. Audit receipts with
`tools/summarize-app-heap.py --gpu` plus the existing session/steady-sample counts.
Require balanced post-session bytes/counts, observed allocation activity, and zero
failure/inconclusive flags. The bounded ledger serializes diagnostic memory calls;
do not use this build for matched performance claims. The SDK contains no ledger;
default app builds do not wrap GPU allocations. This excludes module-internal
allocations, CPU mmap and process RSS.

G16 uses the frozen G13 SDK for the soak and CTS (`PS5_OPENGL_PREFIX` now works
for both native builders). Run the existing 51-case smoke selection in each of
four configurations on one binary, preserving exact ordered receipts. Keep the
historical full-campaign and older SDK evidence separate.

Frozen local SDK: `build/sdk/ps5-opengl-core33-g13-tiled-rgba8`, manifest
`5dcdd41a1e26d714de88e8628e449098055f72dce842bd70106ada73140d9827`, runtime archive
`dd636df0119009173ace9b196b4903fd5da5e515e7e9ae3f1bea5273a871251a`.
The sample audit deliberately reports full-matrix `complete=false`; it does not
requalify all 39,544 historical executions. The remaining interactive acceptance
is a manual cold launch of PPSA77800 with TV/controller confirmation. No blanket
100% compatibility, multi-hour, suspend/resume, device-loss or cross-firmware claim.

## Completion boundary

| Area | Current state | Remaining acceptance |
| --- | --- | --- |
| Core implementation and focused regressions | G19 additionally fixes mip copies and color mappings; its 24 mip cycles and 18 format checks pass | No new full-matrix claim; retain each older candidate's evidence separately. |
| Distribution | Corrected G19 bundle verified: 120 file checksums and all three relocated consumers pass; G13 archive is historical | Local only; publication requires a separate request. |
| Manual startup and controls | G14 automated supported-mode/exit runs pass; owner reports TV-only launches always work | Fresh physical-control acceptance remains separate. Earlier zero support occurred with a capture card; do not force an unsupported display mode. |
| Broader platform coverage | Explicitly unqualified | Separate safe conditions for suspend/resume, device loss, hardware OOM, multi-hour sessions and another firmware. |
| Application integration | Fullscreen EGL and public GL examples supplied | SDL/GLFW are separate platform ports, not missing Core 3.3 commands. |

2026-09-08 manual attempt: **no-run**; Chiaki connected to the Store-selected
home screen with a sign-in prompt. No PPSA77800 launch event occurred; services
remained healthy, Remote Play disconnected without sleep, and exact lock released.
Evidence: local app `results/manual-acceptance-20260908/`. Owner approval covers
home navigation and title control, but the protocol's Store/sign-in stop remains.
The owner subsequently requested desktop-independent testing: use the existing
headless runner for numerical rendering/startup/lifecycle acceptance. Home-menu
navigation and physical controller/HDMI observations are separate, not prerequisites
for that lane and not implied by its results.

- 2026-09-08 | G14 headless | 35,740 frames/300 s, 11 probes, 119.83–119.87 FPS rendering 4K, API 120 Hz and restored 60 Hz; clean native exit/health/unlock | local app `results/g14-headless-relaunch-20260908/`. No widget input; HDMI qualification below.
- 2026-09-08 | Distribution | source `96d5cc5`, frozen G13 SDK, 204/204 sample Pass, 118/118 file checks, 344 exports and Make/pkg-config/CMake consumers pass after fresh extraction | `.local/g17-package-final.log`, `.local/g17-final-consumers/`; no push.

Prepared local archive: `build/bundles/g13-sampled-20260908-final/ps5-opengl-sdk-0.1.0-perf20260908-sampled.tar.gz`,
239,298,358 bytes, SHA-256 `47011df02b8fd282199b02333b7d2762e6699b7e225edaf325ca1189d34858f3`.
The earlier preparation directory without `-final` has superseded documentation;
use only the archive identified above. Neither archive changes the SDK binaries.

- 2026-09-08 | G14 display path | Owner identifies capture-card use during the zero-support failure; TV-only launches always worked. No speculative runtime retry. Saved local-app successful cycles report HDMI `1080P_11988` while rendering 3840x2160 at ~120 FPS, then restore `3840_2160P_5994`. These are not 4K120 HDMI receipts; local checker corrected, runtime/SDK unchanged.
- 2026-09-08 | G18 | G13 failed mip-copy pixels; `a00ce20` passes 24/24 cycles, GPU bytes/counts zero after cleanup; healthy/unlocked | `results/g18-mip-blit-20260908/`, `.local/g18-fixed-transfer-candidate.json`.

G18 removes a blanket nonzero-color-mip blit rejection; existing mapped mip/layer
bounds handle linear storage, while native tiled destinations still require mip 0.
The actual color branch and transfer mapper pass host sanitizer checks for all
level-0/1 pairs, 2D/array/volume layers, flip/scissor and invalid-map cleanup.
The original native failure is retained in `results/g18-transfer-20260908/`.
The corrected run peaks at 92,798,976 tracked direct/mapped GPU bytes, returns
both to zero with zero failures/invalid flags, and ends at 9,375 owned-heap bytes
in 21 blocks. One session does not measure long-term growth or module-owned memory.
The new SDK is `build/sdk/ps5-opengl-core33-g18-mip-blit`; host tests, its manifest,
344 exports and Make/pkg-config/CMake consumers pass. Frozen G13 bytes are unchanged;
their 204-case sample and soak must not be relabeled as G18 hardware acceptance.

- 2026-09-08 | G19 | `44435a7`: 24 mip cycles + 18 format checks pass; two clean sessions, GPU zero and no post-session heap growth; healthy/unlocked | `results/g19-transfer-regressions-20260908/`.

G18's adjacent format batch passed 17/18 checks but rejected R16F-to-RGBA32F
conversion (`results/g18-format-blit-20260908/`). Mesa requests inserted zero/one
channels; G19 reuses the existing format-swizzle helper after filtering/conversion,
preserving the same-format, unswizzled memcpy path. Host sanitizer regressions
cover mapped/tiled targets, nearest/linear filtering, permutations/constants and
rejection of integer linear filtering. The native batch is
`egl_public_core33_transfer_regressions`: it reuses both original gates unchanged.
The G19 SDK is `build/sdk/ps5-opengl-core33-g19-blit-swizzle`; its manifest,
344 exports and all three SDK consumers pass. Exact app/SDK hashes and the
30-second observation ceiling are in `.local/g19-candidate.json`; retained app
bytes are in `.local/g19-tested-app/`. No new full CTS or long-soak acceptance.

- 2026-09-08 | SDK independence | `94ce68f`: independent compiler/Mesa/runtime build, relocated consumers and 119 bundle checks pass; audit fix integrated | SDK agent `.local/agent-result.md`.

The independent package is an older source baseline, not the G18/G19 runtime,
and has no hardware acceptance. All four agent lanes are integrated and closed.
The [G19 bundle procedure](sdk-bundle-g19.md) uses the corrected runtime and its
own evidence; do not publish or relabel the preserved G13 or independent archives.

- 2026-09-08 | G20 distribution | source companion `fdf3696`, unchanged G19 SDK; 24 mip cycles + 18 format checks audited, full host tests pass, 120/120 extracted file checks and Make/pkg-config/CMake consumers pass with original checkout forbidden | `.local/g20-{package,host,extracted-*}.log`; no console access or publication.

Preserved targeted archive: `build/bundles/g19-targeted-20260908/ps5-opengl-sdk-0.1.0-perf20260908-targeted.tar.gz`,
239,313,840 bytes, SHA-256 `382d88ad2097a1b7ba2eaf0a74dc6d313e3d5a7016941fb81f5a68fadd553f2a`.
The source companion contains both fixes and the exact accepted batch sources;
its preparation-time documentation precedes this verification note. Hardware
scope is G19's targeted batch, not the historical full campaign, G13 sample/soak
or a new performance measurement. The archive includes no raw logs or PPSA77800 app.

- 2026-09-08 | G21 | unchanged G19: 204/204 sampled executions; matched FBO 59.95 FPS/p95 17.21 ms; both 128-cube modes 59.94 FPS; 600 s/35,941 frames/21 probes with zero growth across 20 steady heap/GPU samples; three launches/nine EGL sessions/54 frame oracles pass. Tracked GPU returns to zero, lifecycle heap stays 9,455 bytes/22 blocks; all ten cycles clean/healthy/unlocked | `results/g21-{cts-smoke,fbo,cubes,soak,lifecycle}-20260908/`, source companion `58f1ac6`, `.local/g21-cts-candidate.json`. No full-matrix, physical-input or exhaustive-stability claim.
- 2026-09-08 | G23 | independent G19 runtime compilation, 344 exports, nine installed/relocated/extracted consumer links and 120 bundle checks pass | SDK agent `78cb70d`, `.local/final-audit.json`. Reuses that agent's independently built Mesa/PSBC; not a full clean-room rebuild or byte-identical binary. New binary is host-only, not console-validated. Canonical frozen SDK unchanged; provenance wording fix integrated.
- 2026-09-08 | G22 | isolated Yamagi PPSA99005 replay reaches EOF: 632 timedemo frames/11.3 s/55.9 FPS, 649 successful presents; native shutdown/teardown/health/unlock pass | sibling `ps5-yamagi-g19-ppsa99005-20260908/results/g19-ppsa99005-parent-native/g22-acceptance.json`, native source `ad1e6f5`, collector `e49cf3e`, unchanged G19 SDK. One cold replay with single-level textures, not a universal 60-FPS or visual/input/audio claim; working game and saves untouched.

G21–G23 automated acceptance is closed. The remaining qualifications are the
explicit physical-control, HDMI, multi-hour/recovery and cross-firmware items
above, not unexecuted gates in this bounded campaign. Publication remains separate.

- 2026-09-08 | Final local distribution | source companion `9492484`, unchanged G19 SDK and fresh 204/204 sample; full host checks, 119/119 extracted checksums, 37 SDK entries, 344 exports and all three relocated consumers pass with original checkout forbidden | `.local/g21-{final-host,package,extracted-*}.log`; no rebuild or publication.

Latest local archive: `build/bundles/g19-sampled-20260908/ps5-opengl-sdk-0.1.0-perf20260908-g19-sampled.tar.gz`,
239,335,442 bytes, SHA-256 `22c5430489887c6478ff55a46db8f2e2352bb3a52342dfd0187caca871338440`.
Its source companion contains the completed G21–G23 results; this final archive
verification note is necessarily recorded after packaging. Older archives stay intact.
