# Known limitations

The September 7 Core 3.3 campaign and final SDK consumer checks are complete
within their [documented scope](validation.md). This is not proof of universal
compatibility or production-grade stability. The updated runtime includes the
instancing, constant-attribute, PrimitiveID/geometry, memory-ownership and batching
fixes; its acceptance is a fresh complete matrix, not inherited baseline results.
The [development history](performance-history.md) preserves the earlier failures.
Later source changes have [focused performance/regression evidence](performance.md),
not a rerun of that complete campaign. Keep the frozen baseline and current source
distinct when reporting compatibility.

- **Integration:** fullscreen EGL/static SDK; no GLX/WGL, SDL/GLFW platform port,
  desktop installation model or compatibility-profile guarantee.
- **Performance:** some transfers, formats, clears and other operations retain CPU
  fallbacks. Eligible draw batching is enabled by default; other paths remain
  synchronous. The final 1080p ImGui demo sustained ~20 FPS for five minutes;
  thirty FPS is only its cap. Clear/presentation waits and broader workload
  optimization remain. These measurements do not predict full-game FPS.
  See [Performance](performance.md).
  The opt-in performance candidate averages ~119.88 FPS at 1080p, 1440p and 4K
  in 30-second windowed ImGui runs. Frame-time variation remains;
  this is not a long-session or full-game result. In contrast,
  sampleable offscreen render targets still copy linear/tiled surfaces on the
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
- **Input/visual scope:** the demo has a minimal current-state pad adapter. Its
  earlier TV output was owner-confirmed; the final campaign used numerical
  readbacks, with no recorded widget changes or fresh TV/shell input observation.
  Host navigation checks passed.
- **Lifecycle:** renderer runs report VideoOut unregister `80290009` (busy), then
  successful close, EGL cleanup and runtime-layer release. A targeted development
  check passed three full EGL/ImGui sessions in one process; final renderer and
  five-minute demo teardown also passed. The warning remains.
  The later G6 candidate closes the whole port without the redundant unregister:
  three native EGL sessions and the 4K120 check pass with no busy warning and
  successful close/restoration. This is bounded full-port teardown evidence,
  not unregister-while-open support or device-loss recovery.
  Its five-minute 1080p60 endurance run also passed 17,970 frames and 11 pixel
  checks with successful close and healthy native teardown.
- **Stress:** maximum-axis framebuffers were tested, not an 8192x8192 allocation
  or deliberate hardware OOM exhaustion. Five-minute runs and bounded recreation
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
  validation; no binary release is included here.
- **Evidence:** the export can be audited offline, but cannot independently prove
  the hardware origin of raw receipts retained privately. This is not formal
  Khronos certification.

Larger application ports, performance profiling and broader recovery testing are
the next practical work. They are not missing rows in the accepted CTS matrix.
