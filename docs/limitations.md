# Known limitations

The defined Core 3.3 implementation and acceptance campaign are complete. No
required Core 3.3 feature gap is known in the accepted test scope. This does not
prove universal compatibility or production-grade stability.

- **Integration:** fullscreen EGL/static SDK; no GLX/WGL, SDL/GLFW platform port,
  desktop installation model or compatibility-profile guarantee.
- **Performance:** some transfers/format paths use CPU fallbacks. The accepted
  1080p TV demo measured about 9.5 FPS for five minutes; a newer GPU-clear
  candidate measured about 15 FPS in a short profile. Ordinary draws still incur
  substantial per-call synchronization cost. Thirty FPS is only the demo's cap;
  these measurements do not predict full-game FPS. See [Performance](performance.md).
- **Input:** the demo has a minimal current-state pad adapter. Hardware logs show
  connection but no widget changes; host navigation checks passed.
- **Lifecycle:** renderer runs report VideoOut unregister `80290009` (busy), then
  successful close, EGL cleanup and runtime-layer release. The warning remains.
- **Stress:** maximum-axis framebuffers were tested, not an 8192x8192 allocation
  or deliberate hardware OOM exhaustion. Exhaustive long sessions, suspend/resume
  and device-loss recovery are not established.
- **Hardware:** the final campaign covers one research console, not every model
  or firmware. Earlier experiments do not expand the final candidate's scope.
- **Builds:** source pins/patches are published. Historical executable hashes are
  not promised for another compiler/path. Fresh binaries need their own hardware
  validation; no binary release is included here.
- **Evidence:** the export can be audited offline, but cannot independently prove
  the hardware origin of raw receipts retained privately. This is not formal
  Khronos certification.

A complete existing 3D application port, performance profiling and long-session
testing are the next practical milestones. See [Validation](validation.md).
