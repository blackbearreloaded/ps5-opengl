# Supported boundaries and limitations

PS5 OpenGL is an experimental OpenGL 3.3 Core implementation, not a
Khronos-certified driver or a guarantee of universal application compatibility.

## API and integration

Applications use fullscreen EGL and a static SDK in an already configured native
homebrew environment. Desktop projects still need entry-point, build, window,
input and lifecycle adaptation. GLX, WGL and reusable GLFW integration are absent;
a desktop compatibility profile is not a supported product claim.

The [SDL2 bridge](../integration/SDL2/README.md) supports one fixed-size window
and one unshared Core 3.3 context. It is not a complete SDL platform port.
The [physical-input check](sdl-input-validation.md) covers one controller/user,
selected buttons/stick actions and reconnect at 1440p—not all input devices or
automatic acceptance for every SDK version.

## Performance

Eligible rendering, transfer, clear and subresource paths are accelerated.
Other operations retain CPU fallbacks, conversion or synchronous waits.
A CPU fallback is not inherently missing OpenGL functionality.

The [benchmarks](performance.md) demonstrate specific workloads, not general
game FPS, maximum GPU throughput or perfect pacing. Offscreen completed frames
are not displayed frames. Startup cost remains visible in startup-inclusive
measurements even when post-warmup averages reach the target rate.

## Lifecycle and recovery

Current source [enforces a five-second high-refresh reopen interval](lifecycle-reopen.md).
The guard survives EGL teardown within one process. First acquisition, reuse of
an open presenter, steady rendering and final close do not pay this extra delay.

**SDK 0.2.0 downloads predate the guard:** the qualified workaround is an
application-owned five-second interval between presenter sessions. Immediate
recreation produced HDMI reconnects and is not qualified for those binaries.

The guard does not detect sink readiness, persist across processes or implement
hotplug, suspend/resume or device-loss recovery. GPU submission/completion and
memory-ownership failures use the documented [fail-stop contract](consumer-build.md#unrecoverable-gpu-submission-errors),
not recovery guarantees.

Two-minute sessions and bounded launch/recreation checks do not establish
multi-hour stability or exhaustive memory-pressure behavior. Tracked memory
counters exclude process RSS, foreign heaps and module-internal allocations.

## Verification scope

- The [official-release CTS prerequisites](cts-qualification.md) are blocked:
  required default/window configurations and upstream disposition of a local
  compute-stage test correction remain unresolved. The patched diagnostic
  passed 13/13; it is not a complete new CTS baseline or upstream acceptance.
- The [full campaign](validation.md) belongs to one frozen runtime: 37,404 passes
  plus 2,140 reviewed exclusions, not 39,544 passes.
- [SDK 0.2.0](release-g62.md) has a 202-execution sample and 82 focused GPU cases
  on its 4K runtime. Two explicitly identified extreme-axis executions were
  deferred because prior single-case times exceeded the two-minute bound.
- The newer lifecycle fix has its own focused evidence; it does not inherit
  the older full campaign or every 0.2.0 application check.
- The current 1440p packages are host-checked, not separately hardware-qualified.
  Fresh CI builds likewise require their own native evidence.
- Hardware results cover one firmware-6.02 console and the recorded HDMI4 setup.
  Broader firmware, independent systems and full application compatibility are
  not established. HDMI negotiation is not independent panel verification.

These are distribution boundaries, not a promise of production-grade stability.
Exact release provenance and preserved validation datasets remain authoritative.
