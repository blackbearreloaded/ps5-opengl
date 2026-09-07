# Performance

The September 7 release completes the [frozen Core 3.3 validation campaign](validation.md).
Correctness coverage does not imply desktop-driver performance or predictable game FPS.

## Measured results

The final SDK's 1080p ImGui demo rendered **5,997 frames in 300 seconds (~19.99 FPS)**.
All 11 periodic shape readbacks passed. Its receipts contain 5,997 successful
two-draw batches, successful presenter close and clean native-title teardown.
The 30 FPS setting is a pacing ceiling, not achieved throughput. This was a
numerical rendering/lifecycle check; no fresh TV or controller observation is claimed.

The following matched 1080p cube benchmark was measured earlier in development
at `0dbffb4`, not rerun on the final executable. Each control/candidate ran the
same shaders, scene, 668 depth/texture probes and 48 measured frames.

| Workload | Synchronous control | Batched candidate |
| --- | ---: | ---: |
| 1 ordinary draw | 21.89 FPS | 21.89 FPS |
| 8 ordinary draws | 6.00 FPS | 17.82 FPS |
| 32 ordinary draws | 1.76 FPS | 8.73 FPS |
| 1 / 8 / 32 instanced cubes | ~20.06 FPS | ~20.06 FPS |

These are small-scene CPU wall-clock measurements, including clear, draw and
presentation—not GPU timer results or expected full-game FPS. The batched
benchmark verified 108 retired groups containing 528 draws; timing alone was
not its correctness oracle. The final SDK separately passes the upstream Sokol
cube's 180 frames and 2,596 checks with a live 8.3 MB heap readback allocation.

## Release defaults

- Eligible ordinary and multi-draw work is grouped into bounded batches of up
  to eight draws. Private descriptors, retained resources and explicit drain
  boundaries preserve ordering. Ineligible state remains synchronous.
- Eligible full single-target RGBA8 clears use the GPU at **16,384 pixels or
  larger**. Smaller clears use the existing CPU path because submission overhead
  was slower in the measured small-target workload. This tunable floor is
  conservative, not a claim of the optimal crossover for every workload.
- Large transfer staging uses owned CPU mappings. Some formats, transfers,
  clears and other paths still use CPU fallbacks.

Batching is enabled by default for native runtime builds. To build a synchronous
diagnostic control, explicitly disable both options and rebuild the SDK:

```sh
PS5_DEFERRED_DRAW_BATCH=0 PS5_MULTIDRAW_BATCH=0 make sdk
```

Configuration changes invalidate the affected runtime objects automatically.
Do not rebuild a frozen SDK during its hardware campaign.

## Remaining performance work

Clear and presentation waits still dominate the small examples. Broader
submission coalescing, transfer/format acceleration and real-application profiling
remain useful follow-up work. Any new runtime must receive its own relevant
regressions and release validation; it does not inherit these results.

See [limitations](limitations.md), the [cube benchmark](../examples/core33-cubes/README.md),
and the preserved [development measurements and failure history](performance-history.md).
