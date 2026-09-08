# Local enhancement gates

Control: published source `efc7df3`; preserve existing SDKs and validation exports.
Work stays local. Follow `.local/ENVIRONMENT.md` and the native-title protocol;
no console interaction without the shared lock, and no graphics ELF injection.

| Gate | Change | Acceptance |
| --- | --- | --- |
| G11 | Reserve the resource arena for buffers | Actual allocator/eligibility host tests, cleanup/fallback regressions, native texture/buffer creation and rendering. |
| G12 | Reuse fragment-texture flushes within retained batches | Changed backing/size, updates, batch/context boundaries and uncached stages remain correct; compare matched workloads and substantiate any speedup claim. |
| G13 | Reuse native tiled storage for single-mip 2D RGBA8 render/sample images | Existing transfer-map ownership instead of dual copies; other formats/mips/layers unchanged; upload/readback/copy/batching regressions and matched FBO timing/pixels. |
| G14 | Reliable high-refresh startup and app failure handling | Supported/unsupported/error paths have bounded outcomes; repeated manual launches render or exit cleanly, never silently claim 120 Hz. |
| G15 | Account for GPU allocations and mappings | Partial allocation/map failures and release ordering tested on host; native lifecycle returns tracked live bytes/counts to baseline. |
| G16 | Focused acceptance | Frozen candidate, targeted regressions and matched workloads, sustained session and launch/exit checks; no automatic full CTS rerun. |

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
- 2026-09-08 | G16 soak | frozen G13 SDK, 600 s / 35,912 frames / 21 probes pass; 20 steady samples show zero heap/GPU growth; direct and mapped peak 82,558,976 bytes, both return to zero bytes/counts; clean teardown/health/unlock | `results/g16-gpu-soak600-20260908/`. Relocated SDK Make/pkg-config/CMake consumers and full host acceptance pass.
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
