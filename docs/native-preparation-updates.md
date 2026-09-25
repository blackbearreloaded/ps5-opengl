# Native preparation and rendering updates

The native backend can prepare eligible draws on two workers while collecting
results in submission order. Resource references and prepared shader state stay
owned until their readers finish; deleting a shader drains queued preparation.
GPU retirement is scoped to dependencies rather than every operation.

## Build configuration

For asynchronous preparation, build the SDK with:

```sh
PS5_ASYNC_NATIVE_PREP=1 PS5_NATIVE_PREP_WORKERS=2 \
PS5_DRAW_PROFILE=0 PS5_DRAW_GPU_TIMESTAMPS=0 make sdk-gl46
```

`PS5_NATIVE_PREP_WORKERS` supports one or two workers and defaults to two when
asynchronous preparation is enabled. Use one worker for profiling, GPU timing,
or runtime diagnostics; the compiler rejects these shared-counter diagnostics
with multiple workers. Profiling results from one worker are not a comparable
throughput baseline for the two-worker build. Rebuild the SDK and relink its
consumers after changing these settings.

The optional weak `ps5_opengl_register_gl_consumer(void)` hook lets a diagnostic
consumer identify its GL thread. It defaults to a no-op and is only referenced
in native draw-profile builds. Consumers with a private integration hook should
provide the generic symbol when updating to this source revision.

## Rendering and preparation changes

- Cache prepared shader pairs, complete-key register transformations, and
  immutable sampler encoding. Publish related descriptors together and copy
  only vertex/constant descriptor ranges needed by the draw.
- Resolve render-target register defaults once per draw and count texture
  bindings without repeated stage scans.
- Queue supported GPU texture blits, preserve CPU waits for dependent mip work,
  and coalesce compatible framebuffer release tails.
- Keep supported multisample clears and array resolves on the GPU while
  preserving query behavior.
- Render supported linear RG16F color targets directly instead of staging
  through the CPU.
- Copy full matching tiled depth/stencil slices in bulk only when dimensions,
  layer index, plane spans, and clipping permit it. Partial or incompatible
  copies retain the existing path.
- Increase bounded Mesa upload batches on PS5 and suppress repeated clear
  diagnostics in normal builds.

## Validation and limits

The publication candidate passed 21 affected host regression scripts. Focused host checks exercise the actual implementation for asynchronous queue
ordering, cancellation, shader lifetime, descriptors, register reuse, blits,
and depth/stencil copies. Some compile their fixtures with ASan/UBSan.
Native checks additionally cover multisample rendering, color formats, geometry
relinking with exact pixels and primitive queries, and concurrent heap integrity.

The large-heap oracle uses a 2 GiB direct-memory-backed region, verifies mixed
allocation/free patterns, then checks a 1.5 GiB coalesced allocation. This is a
qualification test, not a replacement allocator or a recommendation that every
consumer allocate that amount. Passing it does not establish that arbitrary
application heap usage is safe.

Full-slice depth/stencil copies passed native pixel checks. Their benefit depends
on hitting the supported path; no general frame-rate gain is claimed. Geometry
compilation timing distinguishes compilation cost from the rest of a frame.

These are focused regression checks, not a new full conformance campaign,
Khronos certification, or a newly published SDK binary. Existing release hashes
and qualification receipts describe their original artifacts. Unfinished blit
profiling and temporary fallback logging are not part of this update.