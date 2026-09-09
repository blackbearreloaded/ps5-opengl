# Known limitations

## Current SDK 0.2.0 — September 9, 2026

The [0.2.0 release](release-g62.md) has 202 sampled CTS passes, 82 focused GPU
cases and bounded application/lifecycle qualification on its final 4K runtime.
It is still experimental, not Khronos certified or exhaustively stable.
The 1440p archive is host-checked only; it does not inherit 4K hardware results.

- **Performance:** eligible GPU transfer/clear and native format/mip/layer paths
  are optimized; other operations retain CPU fallbacks and synchronous waits.
  Final 4K ImGui window/offscreen workloads reached about 119.88 FPS, while
  128 cubes reached 58.09/117.41 completed FPS for ordinary/instanced draws.
  These are workload-specific results, not a general 120-FPS guarantee.
- **Bounded stability:** the two-minute session had no steady tracked heap/GPU
  growth and balanced GPU allocations. Its first-window 113.6 FPS is a cadence
  partial pass. Three lifecycle runs passed with five-second inter-session
  gaps; earlier immediate recreation produced HDMI reconnects. Rapid HFR mode
  churn, multi-hour sessions, suspend/resume, device-loss recovery, exhaustive
  OOM, foreign heaps and process RSS remain unqualified.
- **Verification and portability:** the 202-case sample explicitly defers two
  extreme-axis executions whose earlier single-case runtimes exceeded 120
  seconds. This is not the historical full campaign. No new independent TV or
  controller qualification, broader SDL platform support, GLFW integration or
  other-firmware coverage is inferred. Existing applications still require
  platform adaptation.

These are explicit release boundaries, not an ever-growing list of required
work before distributing this experimental SDK.

## Earlier G55 release — September 9, 2026

The [G55 release](sdk-g55-release.md) fixes mip-chain depth/stencil blits and
packed mip clears. Its final 4K GL/SDL pair passed six focused native checks;
the 1440p pair is host-checked only. By owner decision, passing 4K gates remove
the requirement for duplicate 1440p console runs, not the distinction between
tested binaries. These CPU paths
are functional fixes, not GPU-copy optimizations. Uniform MSAA test samples do
not establish sample isolation. Startup-inclusive ImGui timing is diagnostic,
not a sustained 120-FPS result; broader recovery/stability limits below remain.

## Earlier G47/G25 release scope

The [G47 HFR downloads](sdk-bundle-hfr.md) provide separate fixed 1440p120 and
4K120 GL/SDL2 SDKs. Both have fresh timing/pixel/SDL/HDMI qualification for
their exact bytes and verified extracted consumer builds. The older
[G25 1080p60 download](sdk-bundle-g25.md) retains its own sampled acceptance.
Neither is a new full CTS campaign or a guarantee of arbitrary application compatibility.

- **Display and throughput:** native 1440p119.88 and 2160p119.88 HDMI modes
  match the tested profiles. ImGui averages about 119.88 FPS over 30 seconds;
  SDL's 180-frame checks do not measure FPS. No independent per-run TV
  measurement or perfect frame-pacing claim is made. The selected G47 4K
  offscreen case reaches 119.90 completed frames/s, not necessarily displayed FPS.
- **Heavier workloads:** the separate G44 128-cube benchmark reaches about
  57/113 FPS at 1440p and 40/60 FPS at 4K (ordinary/instanced). Its 4K clear
  takes about 12 ms. Those older exact-binary receipts are not G47 game results.
  Some formats, mip levels and layers retain CPU staging; optimize measured
  workloads rather than generalizing the fast paths.
- **Stability:** bounded memory/lifecycle checks found no steady growth in
  tracked owned heap/GPU memory and returned tracked GPU allocations to zero.
  The G45 1440p and G47 4K soaks missed their first-window cadence targets;
  their memory checks pass, but overall cadence remains a partial pass.
  Foreign/module heaps, process RSS, multi-hour sessions, suspend/resume,
  device-loss recovery and exhaustive OOM remain unqualified.
- **Integration and input:** SDL2 supports one fixed window and one unshared
  Core 3.3 context, not a complete SDL platform. G42 separately passed
  owner-confirmed Cross/stick/disconnect/reconnect at 1440p on one controller/user.
  This is not all-button, multi-controller, 4K input or automatic G47 input
  acceptance. GLFW, GLX/WGL and compatibility-profile guarantees remain absent.
- **Hardware and rebuilds:** hardware qualification covers one firmware-6.02
  console and the recorded HDMI4 connection. Independent clean source builds
  passed host/consumer checks, not hardware acceptance. Fresh CI binaries do
  not inherit the frozen SDKs' results. G47's documented linker-metadata
  exception remains part of its [build identity](sdk-path-free-derivative.md).

## Historical baseline and successor evidence

The records below preserve earlier identities; figures such as the baseline's
20-FPS demo, old offscreen copies and busy-unregister warning are not a statement
that the current SDK still has those measured limitations. Current
qualification is summarized above; detailed comparisons are in [Performance](performance.md).

The September 7 Core 3.3 campaign and final SDK consumer checks are complete
within their [documented scope](validation.md). This is not proof of universal
compatibility or production-grade stability. That frozen runtime includes the
instancing, constant-attribute, PrimitiveID/geometry, memory-ownership and batching
fixes; its acceptance is a fresh complete matrix, not inherited baseline results.
The [development history](performance-history.md) preserves the earlier failures.
Later source changes have [focused performance/regression evidence](performance.md),
not a rerun of that complete campaign. Keep the frozen baseline and current source
distinct when reporting compatibility.

- **Integration:** fullscreen EGL/static SDK; the [SDL2 bridge](../integration/SDL2/README.md)
  passed 180 native frames and two pixel checks with the distributed G25 pair
  ([exact scope](sdk-bundle-g25.md)), separately from its older G19 run. It supports one
  fixed 1080p Core 3.3 window/context and SDL's existing event/joystick path, not a
  complete SDL platform port or verified physical input. GLX/WGL, GLFW, a desktop
  installation model and compatibility-profile guarantees remain absent.
- **Performance:** some transfers, formats, clears and other operations retain CPU
  fallbacks. Eligible draw batching is enabled by default; other paths remain
  synchronous. The frozen-baseline 1080p ImGui demo sustained ~20 FPS for five minutes;
  thirty FPS is only its cap. Clear/presentation waits and broader workload
  optimization remain. These measurements do not predict full-game FPS.
  See [Performance](performance.md).
  The opt-in performance candidate averages ~119.88 FPS at 1080p, 1440p and 4K
  in 30-second windowed ImGui runs. Frame-time variation remains;
  this is not a long-session or full-game result. In contrast,
  older sampleable offscreen render targets copy linear/tiled surfaces on the
  CPU around each draw. Initial high-resolution ImGui FBO measurements are much
  slower; window throughput must not be generalized to render-to-texture workloads.
  The G7 CPU-copy optimization improves the matched 1080p FBO case from 3.54 to
  14.98 FPS, still below 60. The later normal-swap 128-cube profile reaches
  59.94 FPS for both ordinary and instanced draws after mixed-clear, batching,
  depth-flush reuse and logging improvements, over 30 measured seconds per mode.
  It uses two tiny textures; neither result predicts game performance or establishes
  a hardware ceiling. Larger draw counts and broader render-to-texture workloads
  still need optimization; a short 512-object run validates batch boundaries,
  not sustained performance at that count.
  The later G9 copy4 change improves a matched 1080p offscreen run from 14.10 to
  19.98 FPS, still CPU-copy limited. See [G9/G10 results](offscreen-stability.md).
  The local G13 successor removes staging for eligible single-mip 2D RGBA8
  images: the matched case reaches 59.95 FPS, with p95 17.20 ms. Other formats,
  mips and layers retain staging. This focused improvement is not a universal
  render-to-texture or full-game speed guarantee, and is not in the older SDK
  download. See [the separately identified local candidate](sdk-bundle-g13.md).
  G19's own focused qualification reproduces 59.95 FPS offscreen and 59.94 FPS
  for both 128-cube modes; its [new sample and bounded stability checks](sdk-bundle-g19.md)
  do not extend these results to arbitrary formats, games or HDMI modes.
  G25 additionally verifies native storage for single-level 2D sRGB textures and
  a [seven-case format/mip/layer batch](performance.md#broader-format-and-subresource-coverage-g25-local).
  Its copy-heavy sRGB throughput remains ~20 composite cycles/s: no measured
  speedup is claimed. Other staging paths and broader optimization remain.
  The consolidated G25 SDK has its own 204-execution sample, ~59.94-FPS matched
  offscreen/3D profiles, ten-minute tracked-memory soak and three EGL sessions;
  these bounded checks do not establish exhaustive stability or game compatibility.
- **Input/visual scope:** the demo has a minimal current-state pad adapter. Its
  earlier TV output was owner-confirmed; the final campaign used numerical
  readbacks, with no recorded widget changes or fresh TV/shell input observation.
  Host navigation checks passed.
- **Lifecycle:** frozen-baseline renderer runs report VideoOut unregister `80290009` (busy), then
  successful close, EGL cleanup and runtime-layer release. A targeted development
  check passed three full EGL/ImGui sessions in one process; final renderer and
  five-minute demo teardown also passed. The warning remains.
  The later G6 candidate closes the whole port without the redundant unregister:
  three native EGL sessions and a check rendering 4K at ~120 FPS pass without
  busy warnings, with successful close/restoration. This is bounded full-port teardown evidence,
  not unregister-while-open support or device-loss recovery.
  Its five-minute 1080p60 endurance run also passed 17,970 frames and 11 pixel
  checks with successful close and healthy native teardown.
  G10 adds a ten-minute ~59.90 FPS TV-demo run and five native launch/exit cycles
  (15 EGL sessions). Owned-heap use stabilized during the soak and returned to
  the same post-session level across lifecycle runs. This accounting excludes
  direct GPU mappings, foreign heaps and process RSS.
  Local G15/G16 diagnostics extend accounting to linked-title GPU direct
  allocations/mappings: three sessions return tracked bytes/counts to zero, and
  a ten-minute G13 soak has no steady heap/GPU growth. Foreign/module-internal
  allocations and process RSS remain unmeasured; diagnostic builds do not
  establish performance or replace fresh physical-control acceptance. The owner
  reports that the local 4K app's earlier unsupported-refresh result occurred
  with a capture card and TV-only launches always worked. Its three successful
  receipts show 4K rendering at ~120 FPS but HDMI `1080P_11988`, then restoration
  to `3840_2160P_5994`; they establish neither 4K120 HDMI nor a new timing defect.
  That unshipped app uses an older G6 SDK; see [its separate scope](sdk-bundle-g13.md).
  G19 separately passes a ten-minute tracked-memory soak and three launch/exit
  cycles (nine EGL sessions), without steady heap/GPU growth or teardown failures.
- **Stress:** maximum-axis framebuffers were tested, not an 8192x8192 allocation
  or deliberate hardware OOM exhaustion. Ten-minute runs and bounded recreation
  are tested, not exhaustive long sessions, suspend/resume or device-loss recovery.
- **Application memory:** the final SDK passes the original 8.3 MB live-malloc
  cube scenario through shader setup and 180 frames/2,596 pixel checks. Native
  examples share a fixed 128 MiB process-lifetime heap; large transfer staging
  uses owned mappings. The standalone graphics SDK does not replace the caller's
  allocator. This resolves the observed failures, not maximum capacity or
  graceful OOM recovery. See [the integration contract](consumer-build.md).
- **Hardware:** the final campaign covers one firmware-6.02 research console, not every model
  or firmware. Earlier experiments do not expand the final candidate's scope.
- **Builds:** source pins/patches are published. Historical executable hashes are
  not promised for another compiler/path. Fresh binaries need their own hardware
  validation. The [sample-validated SDK prerelease](sdk-bundle.md) preserves its
  own frozen bytes and narrower validation scope; it is not a full-matrix successor.
  [CI-built SDK archives](ci-releases.md) pass host and compile/link checks only;
  they do not inherit any console acceptance.
- **Evidence:** the export can be audited offline, but cannot independently prove
  the hardware origin of raw receipts retained privately. This is not formal
  Khronos certification.

Larger application ports, performance profiling and broader recovery testing are
the next practical work. They are not missing rows in the accepted CTS matrix.
