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

The opt-in `PS5_DRAW_PROFILE=1` native build now separates presentation queue-idle,
flip submission and post-flip vblank CPU wall time. Combine it with
`PS5_IMGUI_PROFILE=1` for the existing 30-second demo, and audit its receipt with:

```sh
python3 tools/summarize-imgui-profile.py RECEIPT --submit-profile --present-profile --deferred-batches
```

`swap_other_ms` is the remaining EGL swap time (including deferred draw retirement),
not a GPU timer. Profiling preserves the existing waits and is not enabled in the
accepted SDK. Build/install profiling candidates into a separate SDK directory.

The first instrumented native run (`d453cb2`, September 7) passed 602 frames,
both pixel probes and 602 two-draw batch retirements, with clean title teardown,
healthy services and exact-token release. Its 572 post-warm-up frames averaged
**50.049 ms (~19.98 FPS)**: clear 17.769 ms, draw 4.020 ms, swap 28.225 ms.
The synchronous clear's completion poll accounted for 16.424 ms. Native
presentation took 15.834 ms (15.822 ms in post-flip vblank; queue-idle only
0.004 ms), leaving 12.390 ms of other swap work. Busy unregister remained;
close succeeded. This is a diagnostic result, not an optimization or new CTS campaign.

This identified the next target: coalesce eligible clear/draw work without losing
resource ownership or CPU/GPU ordering. The accepted SDK excludes internal blitter
draws from the queue; its clear also uses triangle-fan topology, outside ordinary batch
eligibility. In the accepted SDK, CPU buffer uploads/maps/unmaps unconditionally drain queued work.
Simply allowing the clear would move its wait to the next upload. Any successor
needs alias-aware resource-hazard checks, retained clear vertices/uniforms,
CPU-fallback/readback ordering and mixed clear/draw regression evidence first.
No presentation wait was removed on the basis of these timings.

The first successor changes buffer synchronization only: unrelated buffer
subdata/map/flush/unmap access can leave the batch queued; overlapping allocations,
textures and global lifecycle boundaries still retire it. Shared display-pool
ownership is distinguished from accesses to its separately retained arena slices.
The updated ordinary-draw oracle interleaves unrelated uploads while still requiring
8+2 draw groups, pixels and related-buffer/texture hazards to pass. Audit new receipts
with `tests/ps5/test_multidraw_lifetime.py RECEIPT --deferred-uploads`.
Clear batching and the accepted SDK are unchanged at this stage.

- 2026-09-07 | buffer hazards | aa27dcf | pass: four modes retained 8+2 groups across unrelated uploads; pixels/hazards/teardown healthy | results/buffer-hazards-20260907

The second successor (`fe337eb`) admits only the existing eligible **color-only** blitter
clear into the deferred queue (including its four-vertex nonindexed fan). General
blits, queries, mixed depth/stencil clears and presentation waits are unchanged.
Its 128x128 clear oracle passed the 256-value RGBA sweep (4,194,304 pixels), state
restoration, active-query exclusion and mixed color/depth/stencil checks. The
unchanged 30-second ImGui workload then passed 899 frames, both pixel probes and
899 three-draw retirements: one clear plus two ordinary draws per frame. Audit with
`summarize-imgui-profile.py RECEIPT --present-profile --clear-batches` so ordinary
two-draw batching alone cannot be mistaken for combined clear/draw execution.

| Same 1080p ImGui workload | Instrumented baseline | Clear-batching candidate |
| --- | ---: | ---: |
| Measured frames after 30-frame warm-up | 572 | 869 |
| Mean frame CPU wall time | 50.049 ms | 33.366 ms |
| Observed throughput | 19.98 FPS | 29.97 FPS |
| Mean clear CPU wall time | 17.769 ms | 1.856 ms |
| Mean draw CPU wall time | 4.020 ms | 4.071 ms |
| Mean swap CPU wall time | 28.225 ms | 27.404 ms |

This bounded sample improves throughput by **~50%** and reduces frame time by
**~33%**. The profiling build disables the normal demo's 30 FPS sleep: this
measured ceiling is not that limiter, nor a GPU-throughput or game-FPS estimate.
Candidate native presentation still took
15.889 ms, including 15.878 ms of post-flip vblank waiting. No presentation guard
was removed. Both successor gates closed cleanly, passed service checks and
released their exact lock tokens. The ImGui run still reported busy unregister
`80290009` followed by successful close; no fresh TV/controller check is claimed.

These are source candidates with separately built SDKs. The accepted SDK is
unchanged, and neither candidate inherits its full CTS campaign. Next: affected
3D renderer, depth/texture and longer-session regressions, then frozen-candidate
release validation before promotion. Remaining presentation time is a separate
profiling target, not justification to remove waits without lifecycle evidence.

### Achieved: sustained ~60 FPS in the same 1080p demo

- G1: split batched submit/suspend/completion-poll/cleanup timing, with unchanged
  synchronization and the frozen 33.366 ms run as control.
- G2: optimize the measured bottleneck only; preserve every completion marker,
  buffer ownership rule and error/timeout guard. Run focused host checks before
  one frozen, locked PPSA99005 cycle per candidate.
- G3: require ~16.67 ms/frame at the same 1080p scene, correct pixels and batching,
  then sustained cadence and clean teardown before declaring 60 FPS achieved.
  Other release work waits; accepted SDK remains unchanged.

The opt-in `PS5_GPU_PRESENT_BATCH=1` candidate uses Mesa's existing before-flush
callback to append a flip to the final deferred batch. A completion marker after
the flip preserves command ownership; the exact flip marker and empty queue are
required before returning from swap. Readback-drained/empty batches retain the
CPU-flip path. Its first 30-second hardware run passed 1,795 frames, two pixel
checks and 1,793 confirmed GPU flips: **16.683 ms/frame (~59.94 FPS)** after warm-up.
Presentation took 0.008 ms; the GPU completion poll still took 10.334 ms, but the
separate CPU flip no longer added another refresh interval. Clean teardown and
service checks passed; busy unregister remains. This is not an accepted release
default.

For the five-minute check, keep the same frozen SDK and add
`PS5_IMGUI_PROFILE_SOAK=1` to the profiled demo build. Audit with
`summarize-imgui-profile.py RECEIPT --present-profile --clear-batches --soak`:
all eleven pixel probes, ten 30-second windows at 59–60.5 FPS, matching GPU/CPU
flip coverage, frame accounting and clean lifecycle must pass.

The five-minute successor (`13b2db9`, unchanged frozen runtime from `18c4d65`)
passed **17,970 total frames**, all 11 pixel probes and 17,970 clear+two-draw
groups. Its 17,940 post-warm-up frames averaged **16.691 ms (~59.91 FPS)**;
whole-run throughput was **59.90 FPS**. All ten 30-second windows stayed between
59.87 and 59.93 FPS. The receipt confirmed 17,959 GPU flips; the eleven
readback-drained frames used the existing CPU-flip path. Title teardown and
post-run service checks passed, and the exact lock token was released.
Busy unregister `80290009` remains with successful close. This establishes the
60 FPS target for this 1080p ImGui workload, not arbitrary applications, a fresh
TV/controller check or promotion of the opt-in runtime into the accepted SDK.

- 2026-09-07 | profile | d453cb2 | pass: 572 warm frames, phase audit, pixels and teardown | results/present-profile-20260907/PPSA99005-20260907-093808-opengl.log
- 2026-09-07 | clear batching | fe337eb | pass: RGBA sweep, state/query/mixed-clear checks and teardown | results/deferred-clear-20260907
- 2026-09-07 | ImGui coalescing | fe337eb | pass: 899 clear+two-draw groups, pixels, ~29.97 FPS, healthy teardown | results/deferred-clear-imgui-20260907
- 2026-09-07 | batch profile | f5e2a03 | pass: 869 warm frames, poll 11.247 ms/~10 sleeps, submit+suspend 0.017 ms; healthy teardown | results/batch-profile-20260907
- 2026-09-07 | submit mode | 34e4fc1 | pass: pixels/lifecycle; no speedup (~29.97 FPS), opt-in probe removed | results/submit-mode-20260907
- 2026-09-07 | GPU presentation | 18c4d65 | pass: ~59.94 FPS, 1,793 GPU flips, pixels, healthy teardown | results/gpu-present-20260907
- 2026-09-07 | sustained GPU presentation | 13b2db9 | pass: 300 s/~59.90 FPS, 11 pixel probes, ten cadence windows, healthy teardown | results/gpu-present-soak-20260907

### Follow-up benchmark matrix

Measure **1920x1080, 2560x1440 and 3840x2160**, each targeting **30, 60, 90 and
120 FPS**: twelve combinations. Use a short excluded warm-up followed by
**30 seconds of measurement per combination**, not five minutes. That is six
minutes of measured time per workload, plus setup, warm-up and teardown.
The completed five-minute run is the presentation change's stability check,
not a requirement for every matrix entry.

Reuse the native test app and receipt auditing; batch compatible cases within
bounded, protocol-compliant console windows. Freeze the build and scene for
comparisons. Start with the lightweight UI, then repeat separately for textured
3D and heavier workloads. Report achieved FPS, frame-time percentiles, missed
frame budgets, correctness and lifecycle results. Keep render resolution and
render throughput separate from actual output resolution, display refresh and
presentation cadence. Higher resolutions and 90/120 FPS are unverified targets;
record unsupported display modes explicitly instead of counting repeated or
dropped frames as successful presentation.

G4 first measures completed **offscreen** ImGui rendering using the unchanged
frozen GPU-presentation SDK. The EGL window is still fixed at 1080p, so the
matrix uses RGBA8 FBOs, a fixed logical UI scaled to each resolution, 30 warm-up
frames and 30 measured seconds per case. Every measured frame calls `glFinish`;
CPU pacing is included in achieved FPS but excluded from render-stage timing.
Pixels are checked before/after each case, outside timing. All twelve cases
share one bounded title cycle; only an unmeasured preview is presented per case.
This workload is distinct from the interactive TV demo. No runtime, metadata,
display mode or accepted SDK changes are needed. Report mean throughput and
nearest-rank p50/p95/p99 frame/render times; frame-budget misses allow 0.25 ms
pacing jitter, render-budget misses use the exact target budget. A missed FPS
target is a benchmark result, not a rendering failure. Stop the batch on any
correctness, completion, allocation or lifecycle failure. Actual higher-resolution
and high-refresh scanout remains a separate, unvalidated integration step.

## OpenGL-only follow-up order

1. Profile the accepted workload; change one measured bottleneck at a time with
   matched pixel, retirement and lifecycle checks. Do not weaken completion guards.
2. Investigate busy unregister independently; validate repeated creation/close,
   allocation-failure handling and bounded endurance before claiming stability.
   Suspend/resume and device-loss recovery remain separate, unvalidated features.
3. Use application findings from the separate Yamagi port as library bug reports;
   no game-specific changes belong in this repository's driver.
4. Define the actual SDL/GLFW platform boundary from consumer requirements before
   implementing an adapter. Neither framework is currently supported.
5. Repeat independent builds and fresh TV/controller checks. Keep one-console,
   one-firmware results explicitly scoped; broader compatibility needs new evidence.
6. Produce a versioned SDK/example archive with licenses, dependency pins, hashes
   and validation provenance after freezing a release candidate. Source delivery
   and private repository visibility stay unchanged until publication is requested.

See [limitations](limitations.md), the [cube benchmark](../examples/core33-cubes/README.md),
and the preserved [development measurements and failure history](performance-history.md).
