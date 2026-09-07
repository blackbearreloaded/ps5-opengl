# Known limitations

The September 7 Core 3.3 campaign and final SDK consumer checks are complete
within their [documented scope](validation.md). This is not proof of universal
compatibility or production-grade stability. The updated runtime includes the
instancing, constant-attribute, PrimitiveID/geometry, memory-ownership and batching
fixes; its acceptance is a fresh complete matrix, not inherited baseline results.
The [development history](performance-history.md) preserves the earlier failures.

- **Integration:** fullscreen EGL/static SDK; no GLX/WGL, SDL/GLFW platform port,
  desktop installation model or compatibility-profile guarantee.
- **Performance:** some transfers, formats, clears and other operations retain CPU
  fallbacks. Eligible draw batching is enabled by default; other paths remain
  synchronous. The final 1080p ImGui demo sustained ~20 FPS for five minutes;
  thirty FPS is only its cap. Clear/presentation waits and broader workload
  optimization remain. These measurements do not predict full-game FPS.
  See [Performance](performance.md).
- **Input/visual scope:** the demo has a minimal current-state pad adapter. Its
  earlier TV output was owner-confirmed; the final campaign used numerical
  readbacks, with no recorded widget changes or fresh TV/shell input observation.
  Host navigation checks passed.
- **Lifecycle:** renderer runs report VideoOut unregister `80290009` (busy), then
  successful close, EGL cleanup and runtime-layer release. A targeted development
  check passed three full EGL/ImGui sessions in one process; final renderer and
  five-minute demo teardown also passed. The warning remains.
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
