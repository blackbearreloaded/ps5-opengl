# Eden streaming synchronization

Mesa u_upload_mgr and bufferobj explicitly request PIPE_MAP_UNSYNCHRONIZED for
streaming writes. The PS5 map, flush-region, unmap and subdata callbacks ignored
that contract and drained every batch retaining the same allocation, including
nonoverlapping upload ranges. Respect the flag for buffer writes; reads, texture
staging and ordinary synchronized writes retain their existing hazard waits.
Invalid map/subdata bounds are rejected before draining.

Storage-buffer/image binding changes also no longer drain. Storage-consuming
draws and dispatches are synchronous; deferred eligibility excludes VS/FS
storage, geometry and patches. Deferred non-storage aliases retain their own
resource references. Binding validation and ownership remain unchanged.

The existing transfer regression extracts the production functions and checks
no-wait writes, read+write/read synchronization even with the flag, texture
synchronization, subdata content and invalid bounds. Binding regressions retain
validation/reference checks and assert rebinding does not drain. The complete
host suite passed before the added transfer cases; those new cases also pass.

Hardware comparison target is Eden headless-fw602-20260920-233503 (6.506510s for
frames20..30, HUD1.6FPS). CPU/JIT, UI and captures remain unchanged. No performance
improvement is claimed before hardware measurement.

PS5 SDK and Eden package checks pass. Hardware result: Eden05c5d22, case
headless-fw602-20260920-235615. Native/menu/HUD and five query checks pass.
Frames20..30:6.439778s vs6.506510s (1.03% shorter), not a demonstrated material
speedup. HUD1.6FPS, peak2/8 batches, first frame81.510s. Existing806 errors,
zero critical. Normal return0 and cleanup/service health pass. These removed
waits were not dominant; isolate the remaining blocking paths before another run.
