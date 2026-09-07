# Known limitations

The frozen Core 3.3 acceptance campaign is complete within its documented scope,
not a proof of universal compatibility or production-grade stability. Later
real-workload testing exposed an instanced-varying bug and rejection of constant
vertex attributes. The performance branch fixes pass native UV/texture, combined
draw and mixed float/integer current-attribute checks, but still need broader
regressions and release-candidate revalidation. A corrected GPU PrimitiveID path
passes ID 0/1, draw reset and instance reset with smooth and genuinely flat inputs
in both provoking-vertex modes. Explicit geometry-shader IDs/user varyings and
returning to the implicit program now also pass after supplying the missing
NGG LDS-layout argument. The initial CTS smoke was 46/51 passing; the corrected
runtime passes all 24 geometry cases, including the five prior zero-input GS
transform-feedback failures. These separate binaries do not constitute a new
full-campaign acceptance. A separate rebuilt SDK passes Make/CMake/pkg-config and
native ImGui/NanoVG/Sokol checks and a five-minute TV session with periodic
readbacks. Broader stress/lifecycle work and release acceptance remain pending. See
[the current candidate and failure history](performance.md).

- **Integration:** fullscreen EGL/static SDK; no GLX/WGL, SDL/GLFW platform port,
  desktop installation model or compatibility-profile guarantee.
- **Performance:** some transfers/format paths use CPU fallbacks. The accepted
  1080p TV demo measured about 9.5 FPS for five minutes; a newer GPU-clear
  candidate measured about 15 FPS in a short profile; the G6 SDK sustains about
  15 FPS for five minutes with periodic shape checks. Ordinary draws still incur
  substantial per-call synchronization cost. A short 1080p textured-cube test
  measured about 20 FPS with 1/8/32 instanced cubes. Thirty FPS is only the demo's cap;
  these measurements do not predict full-game FPS. See [Performance](performance.md).
- **Input:** the demo has a minimal current-state pad adapter. Hardware logs show
  connection but no widget changes; host navigation checks passed.
- **Lifecycle:** renderer runs report VideoOut unregister `80290009` (busy), then
  successful close, EGL cleanup and runtime-layer release. The warning remains.
- **Stress:** maximum-axis framebuffers were tested, not an 8192x8192 allocation
  or deliberate hardware OOM exhaustion. Exhaustive long sessions, suspend/resume
  and device-loss recovery are not established.
- **Application memory:** the upstream cube's first native run aborted in CPU-side
  GLSL built-in allocation with an additional 8.3 MB readback buffer live. A small
  scanline-buffer control is prepared; the cause/capacity of the allocator failure
  and larger application-memory workloads are not yet validated.
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
