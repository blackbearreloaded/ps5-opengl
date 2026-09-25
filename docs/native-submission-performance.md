# Native submission and CPU preparation

Draw-heavy OpenGL applications can spend substantial CPU time waiting for
completion, allocating transient storage and repeatedly publishing unchanged
resources. The native driver now batches eligible command streams, retains
their resources until confirmed completion and scopes CPU waits to hazards.

See [Native preparation and rendering updates](native-preparation-updates.md)
for subsequent worker, descriptor, multisample, and lifetime improvements.
The artifact hashes below identify the earlier qualification only.

## Changes

- Direct packed-float and sRGB rendering for validated layouts, with guarded
  fallbacks and query-isolated GPU clears.
- Grouped native submission and up to eight in-flight batches. Fences,
  descriptors, resources and query storage retain ownership until confirmed
  completion; capacity pressure retires the oldest batch.
- Correct completion-marker visibility and CPU-upload cache acquisition.
  Reuse retired command/descriptor storage and unchanged texture publications.
  Buffer writes publish their ranges; persistent mappings bypass cached draw
  publication because later writes have no driver callback.
- Bounded CPU scratch reuse, compatible tiled-image copies, vectorized index
  bounds and aligned streaming clears.
- Offscreen rendering overlaps pending presentation, with completion waits
  before scanout reuse or shutdown. Native hot paths use `-O3` without fast-math.

Full native-work initialization and publication remain enabled. Optional
per-draw profiling and GPU timestamps can be disabled for production workloads.

## Validation and limits

- `make test`: production-code queue, lifetime, fence, cache-publication,
  transfer and presentation checks, including failure and boundary cases.
- `make test-staging`: CPU layout and software-OpenGL reference regressions.
- Native 1080p60 SDK cross-build and firmware 6.02 integration checks: startup
  draw/readback oracles, frame captures, orderly teardown and service health.

The console-tested runtime source was
`a3372535eec15d58286c5a61730b9b32609c88fa`, built with `PS5_DRAW_PROFILE=0` and
`PS5_DRAW_GPU_TIMESTAMPS=0`. Runtime archive SHA-256:
`1a513c43bdd6c0510964d0b6e29b5e4c61ec3f6b43118aabebfd300e6ac56bb6`.

Host checks do not establish hardware cache coherence or certification. The
integration run exercises a subset of API states and does not qualify every
application, resolution or queue depth. Fresh SDK builds do not inherit the
exact binary's hardware qualification. No application FPS or isolated GPU
execution-time claim is made here, and this change does not publish a new SDK
release or claim Khronos certification.
