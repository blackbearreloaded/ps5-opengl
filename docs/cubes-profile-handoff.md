# Tasks 3/4 benchmark handoff

Historical handoff, 2026-09-07 | host/target-link checks passed; native measurements were pending.
The profile was subsequently integrated and measured; see the [G7 results](performance.md#g7-mixed-clear-3d-result).

Base: `ee9a5b93f873a08d66eca82ace0e580a09ccb2f9`. Work is isolated in
`workspace/dev/ps5-opengl-benchmarks`, branch `task34-benchmarks`. The runtime
owner reserved the cube benchmark files for this work and requested host-only
validation while investigating another gate. No canonical runtime, installed
SDK, native packaging controller, console or lock was modified.

The opt-in profile extends the existing renderer rather than introducing another
benchmark engine. It provides 32/128/512 visible cubes, a matched 30-second run
per ordinary/instanced path, named finish-before-swap versus swap-completed
semantics, post-swap checks, native draw-counter accounting, buffered samples and
strict offline interval/phase analysis. Historical and UV receipts remain on the
original path and parser.

The schema v2 revision calibrates native draw accounting after the first
excluded oracle swap. CPU color clear adds no internal draw; GPU color clear
adds one. Every later completed frame must match that exact calibrated delta,
and the strict receipt parser checks both individual measured deltas and the
aggregate including both oracles and all warm-ups. Calibration and logging
remain outside the measured interval; no query splits clear from draws.

## Passed checks

- Full existing software-EGL depth/texture/instance oracles and their deliberate
  failures, plus UV positive/fault controls.
- New profile positives: completion 0 / 128 objects, completion 1 / 128 objects,
  and completion 1 / 512 objects; one measured second per mode after 30 warm-ups.
- New profile depth/texture/instance faults rejected by numerical probes.
- Default-duration host profile completed 30 measured seconds per mode with
  strict interval accounting; raw samples/report are `profile-30s.log/.json`.
- Parser tests through project unittest discovery: exact timing/draw accounting,
  before/after oracles, duration boundary, missing/reordered/duplicate records,
  failures, calibrated budgets, spike percentiles and comparison scope.
- Mock native-counter checks execute the production profile loop for both
  completion modes and legitimate clear paths, rejecting lost/extra draws and
  native-status failures at every oracle, warm-up and measured frame in both
  draw modes, plus counter wrap and invalid calibration. These are accounting
  tests, not proof of native GPU retirement.
- Native target compiled and linked in both completion modes using payload SDK
  Clang 21.1.8, C11, `-Wall -Wextra -Werror`, and the unchanged accepted SDK
  manifest `2457f914f1faeb88ff43be1ddb2fe84c602c0d12ee3de0747cb30b4db254576a`.
  Link checks are not packaged native-folder applications or hardware receipts.

Local evidence lives under `build/cubes-host`: `suite.log`, profile raw logs,
`comparison.json`, native build logs and `native-sha256.txt`. Host FPS is not
reported as native performance. Commands are in the
[example guide](../examples/core33-cubes/README.md#opt-in-matched-profile).

## Integrate and measure

Apply the benchmark commit to the current runtime branch; changes touch only
the cube example/header/readme, its Make rules, host check and profile parser/test.
The added handoff is documentation. The native folder builder already discovers
the new `egl_public_core33_cubes_profile.o` target. That path builds from source,
not from `PS5_OPENGL_PREFIX`; the standalone cross-link check used the installed
SDK only to prove symbol/link compatibility without rebuilding shared outputs.

For the first native case, freeze 128 cubes / 30 seconds / completion mode 1
against the owner's chosen runtime. Use identical logging flags, metadata,
resolution and scene for the matched control. The owner selected tested G7
runtime archive `5bb3491f3ea2c2627f858f2db2e82bfe3ae7bd2b63ae7028efc3220c5ee94ef6`,
`PS5_GPU_PRESENT_BATCH=1 PS5_DRAW_PROFILE=1`, and 1080p60 for the first pair.
Keep `PS5_RUNTIME_QUIET` unset: it also suppresses close/batch evidence and the
native Make path does not expose it. Label results as instrumented, since driver
logging affects timings. This contribution adds no quiet-mode plumbing.
The cycle needs time for both 30-second windows, warmups, 2,052 pixel probes and
deferred log output; 30 seconds is not a complete-title timeout.
Respect existing exact-token locking, idle-foreground preflight, verified native
folder deployment, completion receipt, exact-title teardown and health checks.
The parser requires the native gate success record but does not replace those
external lifecycle checks.

No native profile was executed here. Completion-after-swap depends on the actual
runtime's wait/presentation contract and still needs the owner's bounded hardware
validation. The workload targets draw overhead with two tiny textures; it does
not establish texture bandwidth, full-game speed, 4K90 or physical HDMI pacing.
