# Performance

The September 7 baseline completes the [frozen Core 3.3 validation campaign](validation.md).
Correctness coverage does not imply desktop-driver performance or predictable game FPS.

The later opt-in candidate `610e6a3` averages **119.88 FPS at 1080p, 1440p and 4K**
in the original windowed ImGui scene, over 30 measured seconds per size.
See [G5d results and limits](#g5d-verified-high-resolution-120-fps-candidate) and
[build instructions](../examples/core33-imgui/README.md#high-refresh-window-benchmark-opt-in).
These source changes have focused regressions, not a new full CTS campaign or
versioned binary SDK release. The measurements below retain their original candidates.

## Frozen validation-baseline measurements

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

An exact-byte 30-second control rerun after the FBO matrix reproduced
**16.682328 ms/frame (~59.94 FPS)** across 1,765 measured frames, versus the
original 16.682566 ms. The original `18c4d65` executable, libc and metadata were
hash-verified locally and remotely; no rebuild or workload change occurred.
Both pixel probes, all 1,795 clear+two-draw retirements, 1,793 GPU flips and
native/EGL cleanup passed. Title layers were released, services were healthy,
and the exact lock token was released. Busy unregister remains. This is the
reproducible **windowed control** for further comparisons; the sampleable-FBO
matrix below measures a different scene and storage/synchronization path.

- 2026-09-07 | profile | d453cb2 | pass: 572 warm frames, phase audit, pixels and teardown | results/present-profile-20260907/PPSA99005-20260907-093808-opengl.log
- 2026-09-07 | clear batching | fe337eb | pass: RGBA sweep, state/query/mixed-clear checks and teardown | results/deferred-clear-20260907
- 2026-09-07 | ImGui coalescing | fe337eb | pass: 899 clear+two-draw groups, pixels, ~29.97 FPS, healthy teardown | results/deferred-clear-imgui-20260907
- 2026-09-07 | batch profile | f5e2a03 | pass: 869 warm frames, poll 11.247 ms/~10 sleeps, submit+suspend 0.017 ms; healthy teardown | results/batch-profile-20260907
- 2026-09-07 | submit mode | 34e4fc1 | pass: pixels/lifecycle; no speedup (~29.97 FPS), opt-in probe removed | results/submit-mode-20260907
- 2026-09-07 | GPU presentation | 18c4d65 | pass: ~59.94 FPS, 1,793 GPU flips, pixels, healthy teardown | results/gpu-present-20260907
- 2026-09-07 | sustained GPU presentation | 13b2db9 | pass: 300 s/~59.90 FPS, 11 pixel probes, ten cadence windows, healthy teardown | results/gpu-present-soak-20260907
- 2026-09-07 | window control rerun | artifact 18c4d65 | pass: identical bytes, ~59.94 FPS, pixels/batches/flips, healthy teardown | results/window-control-repro-20260907

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

The initial G4 experiment measured completed **offscreen** ImGui rendering using the unchanged
frozen GPU-presentation SDK. The EGL window is still fixed at 1080p, so the
matrix uses RGBA8 FBOs and a fixed logical UI scaled to each resolution. Warm-up
ends after 30 frames or one second (at least two frames), followed by 30 measured
seconds per case. Every measured frame calls `glFinish` and checks the existing
native draw-status/count diagnostic;
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

The first matrix exposed a real slow path: Mesa gives user renderbuffers
`PIPE_BIND_SAMPLER_VIEW`; `ps5_linear_sampled_layout` then selects linear storage
with tiled staging. `ps5_stage_color_surface` copies the full surface on the CPU
before and after each synchronous draw, and GPU-clear/deferred-batch eligibility
excludes that storage. Ten measured cases produced correct pixels but only
~4.0/2.4/1.1 FPS at 1080p/1440p/2160p. Fixed 30-frame warm-ups exceeded the
450-second runner budget before cases 10/11 completed. No full-matrix pass is
claimed. The successor bounds warm-up time and distinguishes synchronous draw
completion from deferred batches; it does not hide or optimize this slow path.
This identifies GPU-resident render-to-texture/staging as a priority after the
window-presentation improvement, not a hardware rasterization ceiling.

- 2026-09-07 | offscreen matrix | 3795762 | inconclusive: 10/12 measured; CPU staging dominated, warm-up overran bound; title closed/health/unlock | results/imgui-matrix-20260907

#### Completed sampleable-FBO baseline

Successor `d4579e4` completed all twelve 30-second cases on the same firmware-6.02
console and unchanged frozen runtime. All 72 clear/opaque/blended pixel probes
passed. The measured frames had successful synchronous driver status after
`glFinish`; all twelve unmeasured previews had confirmed GPU flips. Native/EGL
cleanup, title-layer release, post-health and exact-token release passed.
Busy unregister remains. **Correctness passed; none of the FPS targets was met.**

| Render resolution | Target 30 FPS: achieved | Target 60: achieved | Target 90: achieved | Target 120: achieved |
| --- | ---: | ---: | ---: | ---: |
| 1920x1080 | 3.88 | 3.89 | 3.88 | 3.90 |
| 2560x1440 | 2.30 | 2.32 | 2.30 | 2.32 |
| 3840x2160 | 1.07 | 1.09 | 1.08 | 1.08 |

These are measured application frame rates including the existing CPU staging
path, not GPU-only throughput or display cadence. The workload differs from the
~60 FPS windowed TV demo and must not be substituted for its result. The baseline
contains 880 measured frames and 1,760 measured driver draws; every measured frame
missed its target budget. Nearest-rank p95 frame times span 271.19–271.44 ms at
1080p, 449.86–450.74 ms at 1440p, and 929.45–953.50 ms at 2160p. Full per-case
percentiles and counts are in the locally audited `summary.json` alongside the
receipt under `results/imgui-matrix-bounded-20260907`.

- 2026-09-07 | FBO baseline | d4579e4 | pass: 12/12 correctness, 72 probes, 880 frames; 0/12 FPS targets met; clean lifecycle | results/imgui-matrix-bounded-20260907

G5 next: extend the **original windowed scene/path**, retaining the frozen
1080p60 control above. First verify the same scene and render-only target layout
at 1080p in any revised harness; do not repeat twelve cases until that control
matches. Resolve supported output modes and presentation cadence before testing
higher-resolution/high-refresh presentation. Use 30-second cases, freeze each
candidate, and label unsupported modes and render-only throughput explicitly.

G5a adds `PS5_IMGUI_WINDOW_BENCHMARK=1` to the existing profiled TV demo,
using the frozen GPU-presentation SDK without rebuilding it. The scene, window,
clear/draw/swap path and two warm-up probes remain unchanged. Measure 30 seconds
after 30 warm-up frames; report completed frame intervals and active work
(including swap), nearest-rank p50/p95/p99, and missed budgets. The initial
1080p60 case adds no pacing sleep; the 30 FPS case uses the existing bounded
deadline helper. Audit with `summarize-imgui-profile.py RECEIPT --window-target 60`
(or `30`), including exact group/flip coverage and cleanup. Accept the revised
1080p60 harness only at 59–60.5 FPS before extending modes. This measures app
presentation completion, not independently verified physical output timing.

The matched harness (`c88497a`, unchanged frozen `18c4d65` runtime) passed both
initial 1080p cases. Each ran 30 measured seconds after warm-up, with two passing
pixel probes, exact clear/draw/flip coverage, unchanged controls and clean
native/EGL/title teardown, healthy services and exact-token release.

| Target | Achieved FPS | Measured frames | Mean frame ms | p95 ms | p99 ms | Frame-budget misses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 60 | 59.940919 | 1,799 | 16.683094 | 17.331530 | 17.581307 | 612 |
| 30 | 29.999886 | 900 | 33.333460 | 33.352348 | 33.355315 | 0 |

Misses count completed application intervals exceeding the target budget plus
0.25 ms; they are **not** proof of dropped TV frames. Active work includes swap
but excludes the target-30 sleep: 16.680966 ms at target 60 and 15.513653 ms at
target 30. Busy unregister remains. Output mode is still explicitly unverified.
These were the first two requested windowed combinations; the expanded G5b
results below supersede that coverage count. Full local summaries are beside the receipts.

- 2026-09-07 | matched window benchmark | c88497a | pass: 1080p60/30, pixels/groups/flips, healthy teardown | results/window-benchmark{60,30}-20260907

G5b coordinates `PS5_SCANOUT_HEIGHT=1080|1440|2160` across EGL, Gallium, the
runtime bridge and VideoOut registration. Each SDK has one build-time window
mode; double-buffer strides are 10/16/32 MiB per buffer, with the same 44 MiB
working arena. Default 1080p layout is unchanged. The TV demo keeps the same
logical scene and scales its viewport and probes. Profiled native builds record
registration results and read-only VideoOut full/pane/refresh status after warm-up
and before close; failures/unknown modes stay explicit. No output reconfiguration
or metadata change is part of this step. Audit with `--window-target 60
--window-height HEIGHT --output-status`. First revalidate 1080p60, then larger
render surfaces in separate bounded cycles. API status is not an independent
HDMI/TV measurement; this G5b candidate only exercised 30/60 FPS.

The coordinated-resolution candidate (`d0abe47`) passed **six matched cases**
on the same firmware-6.02 console. Each uses the original ImGui scene, 30 warm-up
frames and a subsequent 30-second sample, with no added measured-frame readbacks.

| Render size | Target FPS | Achieved FPS | Measured frames | p95 frame ms | p99 frame ms | Budget misses |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1920x1080 | 30 | 29.999889 | 900 | 33.339037 | 33.342806 | 0 |
| 1920x1080 | 60 | 59.942163 | 1,799 | 17.332412 | 17.559417 | 619 |
| 2560x1440 | 30 | 29.999889 | 900 | 33.415454 | 33.417378 | 0 |
| 2560x1440 | 60 | 59.940422 | 1,799 | 17.341313 | 17.599991 | 676 |
| 3840x2160 | 30 | 29.999889 | 900 | 33.369828 | 33.373436 | 0 |
| 3840x2160 | 60 | 59.939802 | 1,799 | 17.491500 | 17.679615 | 360 |

All 12 warm-up pixel probes passed. The 8,277 total frames had exactly 8,277
clear-plus-two-draw groups (24,831 draws) and 8,265 GPU flips; the other 12 frames
used the intentional readback fallback. All six runs passed native/EGL/title
teardown, post-health and exact-token release. Busy unregister remains, with
successful presenter close. No fresh TV/controller or independent HDMI check is claimed.

Both VideoOut status APIs succeeded at warm-up and shutdown in every case:
full/pane extents were 3840x2160 and both refresh IDs were 3 (reported 59.94 Hz).
Thus 1080p/1440p are **render resolutions**, with a reported 2160p output; the
2160p case renders at that full size. The 30 FPS cases pace the application on
the same output mode. Frame-budget misses use the existing +0.25 ms tolerance
and are not proof of dropped TV frames. These small-scene, presentation-limited
results do not predict game FPS or GPU-only throughput.

G5b measured the first six requested windowed combinations, all meeting their
throughput targets; G5c below completes the other six. Raw receipts, lifecycle records and audited summaries are
under `results/scanout{1080,1440,2160}-{30,60}-20260907`; frozen app/SDK hashes
are in the matching `build/frozen/scanout*-20260907/manifest.md` files. The
accepted SDK is unchanged; this candidate has not received a new CTS campaign.

- 2026-09-07 | G5b | d0abe47 | pass: 6/6 matched 30/60 FPS cases, 12 probes, healthy teardown; 6/12 matrix complete | results/scanout*-20260907

G5c adds opt-in `PS5_SCANOUT_FPS=120` to each resolution-specific SDK.
The profiled native ImGui window builder selects the matching ordinary HFR
title metadata (`attribute3=0x80040`); the source/default metadata remains unchanged.
VideoOut support is checked before requesting high refresh. Both application targets
use the verified fixed 120 Hz output: target 90 uses the existing absolute-deadline
pacer; target 120 retains unpaced, completion-checked swaps. This is a 90 FPS
application measurement on a 120 Hz output, not a native 90 Hz display claim. No console Settings changes,
new GPU commands, extra measured-frame readbacks or weakened retirement checks.

After drain/unregister, restore ordinary output before close, wait two vblanks
and record a third status snapshot. Setup/restore failures remain errors; the
auditor requires successful mode calls, stable high-refresh status and reported
59.94 Hz restoration. Raw output dimensions remain separate from render dimensions
and no independent HDMI timing is claimed. Audit as before with `--window-target
90|120 --window-height HEIGHT`; native HFR targets automatically require output status.

Freeze and validate 1080p120 first, then 1080p90, before larger render surfaces.
Each case measures 30 seconds after 30 warm-up frames, using one bounded, locked
PPSA99005 cycle and healthy teardown. FPS misses are benchmark outcomes, not false
passes; unsupported modes and lifecycle/health failures stop the affected hardware
path for offline analysis.

- 2026-09-07 | G5c | 29fde32 | pass: 1080p120 at 119.878624 FPS, output 119.88/restored 59.94, healthy teardown | results/hfr1080-120-20260907
- 2026-09-07 | G5c | 4f1dd3e | failed: VRR request 0x8029001c before registration/frames; restored and closed healthy | results/hfr1080-90-20260907

The VRR path is removed; do not retry it or change console Settings for this
benchmark. The 90 FPS successor reuses the frozen, proven 120 Hz SDK and metadata,
changing only the application's pacing. This also removes the extra VRR import stub.

#### Completed high-refresh window benchmark

All six 90/120-target cases completed on the same firmware-6.02 console, using
the original scene and 30 measured seconds after 30 warm-up frames. Source
`8af2ac7` supplies the fixed-output successor; the two 1080p cases reuse its
frozen, proven `29fde32` 120 Hz runtime. **Rendering checks passed in all six;
three met their average-FPS targets.** Target attainment means at least 99% of
the requested rate, not that every frame met its deadline.

| Render size | Target FPS | Achieved FPS | Measured frames | p95 frame ms | p99 frame ms | Budget misses | FPS target met |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1920x1080 | 90 | 89.985649 | 2,700 | 15.065488 | 15.625183 | 897 | Yes |
| 1920x1080 | 120 | 119.878624 | 3,597 | 8.957439 | 9.086217 | 699 | Yes |
| 2560x1440 | 90 | 89.953096 | 2,699 | 16.729819 | 16.869982 | 898 | Yes |
| 2560x1440 | 120 | 96.753294 | 2,903 | 16.731341 | 16.880567 | 1,078 | No |
| 3840x2160 | 90 | 59.941941 | 1,799 | 17.455897 | 17.687713 | 1,799 | No |
| 3840x2160 | 120 | 59.941014 | 1,799 | 17.425107 | 17.701025 | 1,799 | No |

All 12 warm-up pixel probes passed. The 15,677 total frames had exactly 15,677
clear-plus-two-draw groups (47,031 draws) and 15,665 GPU flips; the other 12 frames
used the intentional warm-up readback fallback. All six passed native/EGL/title
teardown, post-health and exact-token release. The busy-unregister warning
`0x80290009` remains; presenter close succeeded. These are bounded benchmark
results, not a long-session stability claim.

Both VideoOut status APIs reported stable full/pane extents of 3840x2160 and
refresh IDs 13/13 (119.88 Hz) during every case, then 3/3 (59.94 Hz) after the
checked restoration. The lower sizes are render resolutions, not separate
verified HDMI modes. **90 FPS uses application pacing on fixed 120 Hz, not VRR
or native 90 Hz**; its frame intervals are uneven. No screenshots, fresh
controller check or independent TV/HDMI timing measurement were performed.
Budget misses retain the +0.25 ms tolerance and are not proven dropped TV frames.

Together with G5b, **12/12 windowed combinations are measured and pass rendering
checks; 9/12 meet their average-FPS targets**. This small ImGui workload does not
establish a GPU limit or predict 3D game FPS. The accepted SDK is unchanged and
no new CTS campaign or release promotion is claimed. Local receipts and audited
summaries are under `results/hfr*-20260907`; exact candidates and receipt hashes
are indexed in `build/frozen/hfr-results-20260907.md`. The rejected VRR attempt
above is excluded from these six measured cases, not erased from the history.

- 2026-09-07 | G5c | 8af2ac7 | partial-pass: 6/6 rendered, 3/6 FPS targets met; restored output, healthy teardown | results/hfr*-20260907

Next performance focus: the 4K120 profile spends 9.93 ms in clear/draw calls
before a further 6.71 ms in swap, including 6.41 ms of batch polling. These are
CPU-wall spans including waits, not GPU timestamps. Investigate per-frame
clear/draw preparation and completion/presentation scheduling without weakening
retirement checks; do not infer a hardware ceiling or rerun all twelve cases
for each change. Retain 1080p120 as the fast control and 4K120 as the slow case.

G5d first profiles deferred draw preparation using the existing phase timestamps,
without changing commands, flushes or waits. Require `--prepare-profile` with the
window auditor: exactly three preparations per measured frame, valid phase sums,
unchanged pixels/groups/flips and clean lifecycle. Measure the frozen 4K120 slow
case before optimizing scanout cache maintenance; then compare the successor at
1080p120 and 4K120. Only remove repeated work inside a proven ownership boundary;
CPU writes/readbacks and retirement checks remain mandatory. No full matrix rerun
or accepted-SDK promotion is implied by these focused experiments.

- 2026-09-07 | G5d | 1aec3e2 | pass: diagnostic 4K120 baseline 59.942610 FPS, 5,397 preparations, clean lifecycle | results/g5d-profile2160-20260907

The diagnostic attributes 1.153146 ms per draw (3.459438 ms per frame) to the
native scanout flush, versus 0.014127 ms per draw for native setup. This does not
account for all Gallium preparation costs. The first optimization keeps the full
scanout flush for the first queued draw and skips repeats only in the existing
GPU-present batch for the same registered pool. CPU accesses drain that batch;
new batches, changed pools and the default path retain the flush. Submission,
completion polling and lifetime guards are unchanged. Validate pixels, exact
batch/flip counts, phase reduction and healthy teardown before claiming a gain.

- 2026-09-07 | G5d | 4a4a17e | partial-pass: 1080p120 119.883173 FPS; 4K120 70.089393 FPS; pixels/health/restore passed | results/g5d-flush*-20260907

The scanout-only change reduced 4K native scanout flushes from 3.459438 to
1.151136 ms per frame, improving average FPS by about 17%, but not reaching 120.
4K p95/p99 completed intervals were 17.189521/17.341554 ms; uneven pacing remains.
The successor retains this change and skips CPU cache flushing of depth/stencil
backing only when the encoded depth/stencil control is exactly zero, under the
same opt-in GPU-present build flag. All nonzero controls, CPU transfer handling,
staging, target validation/binding, completion and default-build behavior remain
unchanged.

#### G5d verified high-resolution 120 FPS candidate

Candidate `610e6a3` passed the original ImGui scene at all three render sizes,
each for 30 measured seconds after 30 warm-up frames on the same firmware-6.02
console. No scene reduction, changed GPU commands or weaker completion checks.

| Render size | Achieved FPS | Clear + draw ms/frame | p95 / p99 frame ms | Budget misses |
| --- | ---: | ---: | ---: | ---: |
| 1920x1080 | 119.883028 | 4.697100 | 9.262385 / 9.338802 | 779 |
| 2560x1440 | 119.881864 | 4.910530 | 8.874127 / 8.985345 | 733 |
| 3840x2160 | 119.882543 | 5.473047 | 9.014852 / 9.283553 | 1,122 |

The 4K diagnostic baseline was 59.942610 FPS with 9.952447 ms/frame in clear/draw.
Flushing the registered scanout pool once per batch improved it to 70.089393 FPS;
omitting depth/stencil backing flushes only while both tests are disabled reached
119.882543 FPS. Native setup measured only about 0.042 ms/frame, so resource-pool
redesign was not justified by this profile. These are CPU-wall measurements,
not isolated GPU execution times.

Each run passed both warm-up pixel probes, exactly 3,597 measured frames and
10,791 preparations, 3,627 total clear-plus-two-draw groups and 3,625 GPU flips.
Both output APIs reported 119.88 Hz and 3840x2160 full/pane extents, followed by
checked 59.94 Hz restoration. Native/EGL/title teardown, service health and
exact-token release passed. The separate existing depth-batch gate also passed
enabled-depth occlusion in four draw modes plus disabled-depth texture upload,
buffer/map updates, unrelated CPU access, query/fence and cleanup checks.

Average throughput meets the target; frame intervals still vary. The existing
`0x80290009` busy-unregister warning remains with successful close. No fresh
physical TV/controller or independent HDMI timing check, long-session claim,
new CTS campaign, accepted-SDK promotion or GitHub push is implied. The prior
12-case matrix remains historical; 4K90 has not been rerun with this candidate.
Next: frame-pacing/retirement stability and a matched heavier 3D workload;
validate 4K90 separately before claiming every matrix target is now met.

Receipts and audited summaries: `results/g5d-depth{1080,1440,2160}-20260907` and
`results/g5d-depth-gate-20260907`; frozen hashes/receipt index:
`build/frozen/g5d-depth-candidates-20260907.md`.

- 2026-09-07 | G5d | 610e6a3 | pass: 1080p/1440p/4K120 ~119.88 FPS; depth/hazard gate, restore/health passed | results/g5d-depth*-20260907

GPU-resident sampleable FBOs remain a separate optimization: avoid unnecessary
linear/tiled conversions while preserving sampling/readback, CPU-write hazards
and retirement. Start with a focused matched 1080p FBO case and affected
correctness checks; do not use its results as the windowed control or rerun its
full matrix before the slow path improves. Textured 3D and heavier workloads
also remain unmeasured, separate follow-ups.

## OpenGL-only follow-up order

G6 starts with full-port shutdown: retain bounded GPU/flip retirement and output
restoration, then close the owned VideoOut handle without first unregistering
the still-scanned buffer set. The public [PS5 SDL backend](https://github.com/ps5-payload-dev/SDL/blob/release-2.30.x-ps5/src/video/ps5/SDL_ps5video.c)
also uses close-only teardown; this is a reference pattern, not hardware proof.
Keep every resource on drain/restore/close failure. The new receipt explicitly
says `method=close`, never a fabricated successful unregister. Reuse the three-session
ImGui/EGL lifecycle gate at 1080p60, then the matched 4K120 control. Require actual
close success, pixels, recreation, output restoration and clean native teardown.
Only after those pass extend endurance and move to the focused FBO/3D workload;
notify the owner before starting an existing-game integration. No hardware fault
injection, unproven blank flip, full CTS rerun or SDK promotion at this stage.

- 2026-09-07 | G6 | ec9ac2c | pass: close-only teardown, 3 EGL sessions/18 frames; 4K120 119.880374 FPS, restored/healthy | results/g6-close*-20260907

The three-session native oracle recorded three successful drains and closes,
all pixels/state/font checks and EGL recreation. The matched 4K run retained
3,597 measured frames, exact batch/flip coverage and checked 59.94 Hz restoration.
Both cycles released runtime layers and their exact tokens with healthy services.
This resolves the redundant unregister warning for full-port teardown, not the
separate `UnregisterBuffers` API while keeping a port open. Close errors still
retain resources in host fault injection. Next reuse the existing five-minute
1080p60 profile/periodic-readback soak with this frozen runtime; exhaustive OOM,
suspend/resume, device-loss recovery and physical UI verification remain unproven.
Frozen artifacts and receipt hashes: `build/frozen/g6-close-candidates-20260907.md`.

- 2026-09-07 | G6 | ec9ac2c | pass: 300s/17,970 frames, 11 pixels, ten 59.83-59.93 FPS windows, close-only/health/unlock | results/g6-close-soak-20260907

The unchanged G6 runtime's five-minute 1080p60 run retired exactly 17,970 groups
(53,910 draws) and 17,959 GPU flips; 11 frames used the intentional readback path.
It closed successfully without busy unregister. This is bounded endurance, not
exhaustive memory-pressure, suspend/resume or device-loss recovery validation.

G7 reuses the offscreen benchmark with `PS5_IMGUI_BENCHMARK_CASE=1` (1080p60)
and requires `summarize-imgui-benchmark.py --case 1`. Default `-1` still runs
and audits all twelve cases; a single case is never accepted as a full matrix.
Freeze a matched G6-runtime baseline first. Optimize the existing CPU color
staging loop before changing GPU resource ownership, sampling or synchronization;
retain full bounds and compare all byte sizes, mip/layer slices and both copy
directions against the original offset calculation on the host. Then measure
the same native scene/pixels/completion/teardown. This remains CPU staging, not
a claim of GPU-resident sampleable FBOs or predictable game performance.

- 2026-09-07 | G7 | ee9a5b9 | pass: matched 1080p FBO 3.54→14.98 FPS, pixels/retirement/close/health; below 60 FPS | results/g7-fbo-*-20260907

The unchanged offscreen scene measured 3.538062 FPS with the G6 runtime,
11.842834 FPS after reusing CPU address terms (`6a841a1`), and 14.977898 FPS after
inlining four-byte copies (`ee9a5b9`). Mean completed render time fell from
282.636110 to 66.760823 ms. Each case retained its 30-second sample, both pixel
probes, two retired draws/frame and checked preview/teardown. No full matrix,
GPU-resident FBO or new CTS qualification is implied. Frozen identities and
receipt hashes: `build/frozen/g7-fbo-{baseline,staging,inline}-20260907/manifest.md`.

The following legacy layered-mip gate failed its final sampling oracle while
both FBO readbacks and cleanup passed (`results/g7-layered-20260907`). Console
testing stopped. Host Mesa reproduced the exact 1024/1024/0 mismatch: the test
requested mip 1 with non-mipmapped `GL_NEAREST`, which selects the base image
under [OpenGL 3.3 §3.8.11](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf#page=188).
The test now initializes all images and selects `GL_NEAREST_MIPMAP_NEAREST`.
`make test-layered-mip` passes the unchanged pixel oracle and rejects the
original filter mistake. This is a test correction, not a driver workaround;
the corrected native receipt remains required before that gate is accepted.

- 2026-09-07 | G7 | runtime ee9a5b9 / test 62c8e72 | pass: corrected mip gate 3,072 pixels; original cube 668 probes / 48 frames / 108 batches; close/health/unlock | results/g7-{layered-fixed,cubes}-20260907

The corrected native mip test passes with the same runtime binary. The original
1080p cube workload measures 32.16/20.93/10.01 FPS for 1/8/32 ordinary draws, and
about 20.06 FPS for each instanced workload. Each of six cells has only eight
timed frames; these are short CPU-wall measurements, not sustained game FPS.
It explicitly calls `glFinish` before every swap, retiring queued work before
GPU-batched presentation. Next compare a distinctly labeled swap-completed
profile; keep the old benchmark/results intact. Shader/scene simplification
and unfinished submission timings must not be counted as throughput gains.
Frozen evidence: `build/frozen/g7-gates-20260907.md` and
`build/frozen/g7-layered-fixed-20260907/manifest.md`.

The legacy static capability audit was updated for the already-validated
resolution-aware arena, current pinned compiler tree and drain-before-fence
contract. It now compares feature-define values as well as names; negative
pool/feature tests supplement the compiled three-resolution layout checks.
This repairs stale inventory checks; it is not a new hardware CTS campaign.

- 2026-09-07 | G7 | runtime ee9a5b9 / test fa985f0 | pass: 128-cube matched swap profile, instanced 19.98→29.97 FPS; ordinary 3.33 FPS; pixels/close/health | results/g7-cubes-profile{0,1}-20260907

The reviewed profile measures 30 seconds per ordinary/instanced path, retaining
2,052 pixel probes per app and exact draw deltas on every completed frame. Both
apps use identical instrumented runtime bytes. Removing the pre-swap `glFinish`
helps instancing; 128 ordinary draws end on the eight-draw batch boundary and
still measure 3.33 FPS. Normal-swap clear/submit/swap means are 20.85/263.81/15.64
ms for ordinary drawing and 19.66/1.86/11.84 ms for instancing. Mixed depth/color
clear ordering and repeated enabled-depth flushes are the next measured targets;
do not weaken retirement checks. These are CPU-wall microbenchmark results, not
game FPS. Both closes, native teardowns, health checks and exact-token releases
passed. Frozen identities: `build/frozen/g7-cubes-profile-pair-20260907.md`.

- 2026-09-07 | G7 | 163ba15 | partial-pass: batch depth-cache pixels/hazards/health pass, FPS unchanged 3.33/29.97; cache removed | results/g7-depth-cache-*-20260907

Batch-local depth-flush reuse saved no completed-frame time; measured batch
polling increased from 7.32 to 8.29 ms. The extra cache was dropped, with the
experiment retained in history and `build/frozen/g7-depth-cache-20260907.md`.
Next isolate mixed-clear ordering, preserving CPU/GPU hazards and exact pixels.

### G7 mixed-clear 3D result

- 2026-09-07 | G7 | 3f2254e | pass: 128 instanced cubes 29.97→59.94 FPS; 4.19M clear pixels, depth/hazards, 21464 retired draws, close/health | results/g7-mixed-clear-*-20260907

Completing CPU depth/stencil writes before queueing the existing GPU color clear
lets that clear share the following draw's retirement/presentation. Pixel loops,
GPU commands and waits are unchanged. The matched instrumented 1080p profile
retains 30 measured seconds per path, four pixel oracles and exact per-frame counts.

| 128 textured cubes | Before | Mixed-clear ordering | Mean clear / submit / swap ms |
| --- | ---: | ---: | ---: |
| Ordinary draws | 3.330563 FPS | 3.525890 FPS | 4.88 / 263.92 / 14.81 |
| Instanced draw | 29.969934 FPS | 59.940231 FPS | 4.88 / 1.47 / 10.33 |

The candidate passes 1,905 measured frames, 2,052 probes and 21,464 retired draws
in 4,177 independently checked batches, plus the clear sweep and depth/CPU-hazard
gates. Checked close/native teardown and post-health pass. Instanced p99 is
17.56 ms, so average 60-class throughput is not perfect frame pacing. Ordinary
submission/retirement remains slow; this tiny-texture workload is not a full game
or texture-bandwidth result. No new CTS acceptance or SDK promotion is implied.
Frozen identities and receipts: `build/frozen/g7-mixed-clear-20260907.md`.

### G7 ordinary-draw batching

- 2026-09-07 | G7 | efb494f | pass: capacity 8→32, ordinary 3.53→4.57 FPS, instanced 59.94 FPS; pixels/25592 draws/close/health | results/g7-batch32-*-20260907

The matched 128-cube profile improves ordinary throughput by 29.7%; its mean
submission time falls from 263.92 to 199.35 ms. All 2,681 batches retire, including
680 full 32-draw batches, and all 2,052 pixel probes pass. Instanced throughput is
59.940087 FPS. Depth/CPU-access/query/fence regression checks also pass. Capacity
is still bounded; eligibility and completion guards are unchanged. Ordinary
per-draw CPU preparation remains expensive. Frozen identities and receipts:
`build/frozen/g7-batch32-20260907.md`. No new full CTS or SDK acceptance is implied.

- 2026-09-07 | G7 | 1286c89 | partial-pass: 32-entry depth-cache correctness passed, ordinary 4.57→4.61 FPS (<1%); cache removed | results/g7-batch32-depth-cache-*-20260907

The repeated cache experiment shifts CPU time into retirement waits again; its
0.84% average change is not a convincing throughput improvement. Restore the
validated 32-entry implementation and investigate remaining draw preparation.

### G7 routine success tracing

- 2026-09-07 | G7 | 6105672 | pass: routine traces opt-in, ordinary 4.57→11.99 FPS, instanced 59.94; 54230 draws/pixels/close/health | results/g7-trace-policy-cubes-20260907

Three routine success traces now use the existing `AGC_RUNTIME_DIAGNOSTICS`
switch. The control wrote 127,969 allocation/depth-state messages to the native
app's unbuffered log. Removing that work from the default path lowers ordinary
mean submission time from 199.35 to 64.03 ms and improves completed throughput
2.62x to 11.988316 FPS; instanced throughput remains 59.940190 FPS. All 2,052
probes and 54,230 draws in 3,791 batches pass, with clean close/teardown/health.
Errors, allocation exhaustion, batch retirement and shutdown records remain
enabled; logger buffering and GPU behavior are unchanged. This is not blanket
quiet mode. Ordinary frame p99 is 84.26 ms: still far from 60 FPS. Frozen evidence:
`build/frozen/g7-trace-policy-20260907.md`. Full release validation remains pending.

### G7 128-entry batches

- 2026-09-07 | G7 | 64b73c3 | pass: capacity 32→128, ordinary 11.99→14.98 FPS, instanced 59.94; pixels/65840 draws/close/health | results/g7-batch128-cubes-20260907

The same 128-cube workload now retires two batches per ordinary frame instead of
five. Exact 128+1 grouping passes for all 482 ordinary frames, plus 1,831 instanced
frames: 2,795 batches and 65,840 draws total. Completed ordinary FPS is 14.984922
(25% gain), instanced 59.941304. Ordinary p99 is 67.73 ms. Private descriptor
backing grows from a 3.5 to 14 MiB ceiling; ownership/failure guards are unchanged.
Frozen evidence: `build/frozen/g7-batch128-20260907.md`; not a new full CTS result.

- 2026-09-07 | G7 | 6fab303 | pass: 128-entry depth reuse, ordinary 14.98→29.97 FPS, instanced 59.94; pixels/123890 draws/close/health | results/g7-batch128-depth-cache-cubes-20260907

At 128 entries, the same batch-local depth-flush reuse now doubles ordinary
throughput to 29.969793 FPS (p99 34.32 ms); instanced throughput is 59.940644.
All 2,052 probes and 123,890 draws in 3,695 batches pass. Cache lifetime remains
limited to retained, unsubmitted work and ends at every drain; no cross-frame
dirty cache or relaxed completion guards. Retain this measured combination.
Frozen evidence: `build/frozen/g7-batch128-depth-cache-20260907.md`.

### G7 256-entry batches

- 2026-09-07 | G7 | cef6c1b | pass: 128 ordinary cubes 29.97→59.94 FPS; instanced 59.94, 239861 draws/pixels/close/health; 512-object boundary gate | results/g7-batch256-cubes*-20260907

The bounded capacity increase lets the GPU clear and 128 public draws retire in
one batch. The same 1080p scene, two tiny textures and 30 measured seconds per
mode now reach **59.941451 FPS ordinary / 59.940062 FPS instanced**. Each mode
completes 1,799 measured frames; ordinary p99 is 17.62 ms, not perfect pacing.
Mean ordinary clear/submit/swap times are 3.78/6.16/6.74 ms.

All 2,052 probes pass. Including warmup and oracle frames, 3,662 batches retire
239,861 draws: exactly [129] per ordinary frame and [2] per instanced frame.
A preceding one-second-per-mode 512-object boundary test separately passes
8,196 probes and 23,269 draws in 227 batches, including full [256,256,1] groups.
That short gate is correctness evidence, not a sustained 512-object FPS claim.

All-marker completion, the shared timeout, CPU hazard drains and depth-cache
lifetimes are unchanged. The worst-case private descriptor ceiling is 28 MiB,
allocated on demand; the 128-object scene uses only 129 entries. Both native
titles close cleanly with healthy services and exact-token release. Retain the
measured improvement; no full CTS rerun, game claim or SDK promotion is implied.
Frozen hashes and acceptance: `build/frozen/g7-batch256-20260907.md`.

## Next steps

1. Profile the accepted workload; change one measured bottleneck at a time with
   matched pixel, retirement and lifecycle checks. Do not weaken completion guards.
2. Preserve G6's checked close-only teardown and bounded recreation/endurance.
   Exhaustive allocation pressure, suspend/resume and device-loss recovery
   remain separate, unvalidated features.
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
