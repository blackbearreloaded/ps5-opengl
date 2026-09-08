# Local enhancement gates

Control: published source `efc7df3`; preserve existing SDKs and validation exports.
Work stays local. Follow `.local/ENVIRONMENT.md` and the native-title protocol;
no console interaction without the shared lock, and no graphics ELF injection.

| Gate | Change | Acceptance |
| --- | --- | --- |
| G11 | Reserve the resource arena for buffers | Actual allocator/eligibility host tests, cleanup/fallback regressions, native texture/buffer creation and rendering. |
| G12 | Reuse linear fragment-texture flushes within retained batches | Changed backing/size, updates, batch/context boundaries and uncached stages remain correct; matched textured workload improves. |
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
- 2026-09-08 | G12 | host and native deferred-draw regression passed, including texture updates; clean teardown/health and exact-token release | `.local/g12-*.log`, `results/g12-texture-flush-20260908/`; larger-texture performance comparison still pending.
