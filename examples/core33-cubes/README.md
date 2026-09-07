# Textured 3D cubes / frame benchmark

Standard OpenGL 3.3 shaders, perspective, directional lighting, depth testing,
two procedural textures and rotating cubes. No assets or additional dependencies.
The native app presents on the TV; automated validation uses numerical probes,
not screenshots or Remote Play.

Run `make cubes` to build PPSA99005 using the current source runtime; this does
not modify the installed SDK. Follow the normal folder deployment and testing
protocol. Host reference: `make test-cubes` (requires the existing SDK headers
and host software Mesa).

Three 1080p workloads render 1, 8 or 32 cubes (12 triangles per cube), first
using ordinary draws (`mode=0`), then one `glDrawArraysInstanced` (`mode=1`).
Both paths use the same shaders, lighting, geometry, alternating materials and
object positions. Instancing reads a per-instance placement attribute instead
of changing object uniforms. The two textures stay bound in both modes.
Each workload has two warm-up frames and eight measured frames. The short low-poly scene measures
draw-call overhead, **not a full game's expected FPS or maximum GPU throughput**.
Different object counts also change coverage; it is not a fixed-fill-rate test.

Frame timings include color/depth clear, draws, one `glFinish`, and EGL swap.
Numerical depth/texture checks run before and after each workload, outside the
timed interval: 668 probes in total. Host tests deliberately disable depth,
upload the wrong texture or omit instances and require these checks to fail. Do not use host timings
as PS5 results. CPU wall-time throughput is not a TV refresh-rate measurement.

Audit a saved native receipt:

```sh
python3 tools/summarize-cubes.py results/your-cycle-opengl.log
```

Require all probes, all 48 measured frames, cleanup, exact-title teardown and
healthy post-run services. Eligible ordinary draws use bounded batching by
default; see [Performance](../../docs/performance.md) for measured results and
the synchronous diagnostic opt-out. This reduces submission overhead but does
not convert ordinary draws into instancing. Applications must group compatible
objects themselves to use the instanced path.
The audit tool also accepts the original ordinary-only baseline receipts.

## Opt-in matched profile

The historical benchmark and its receipt format remain the default. A separate
`egl_public_core33_cubes_profile.o` target measures one chosen workload for **30
seconds per draw mode**, after 30 warm-up frames. It uses the same two 2x2
procedural textures, lighting, positions and frame-indexed rotation in ordinary
and instanced paths. Choose 32, 128 (default), or 512 visible cubes. Larger counts
shrink the grid spacing and cubes to keep an approximately constant screen
footprint; this isolates draw overhead more usefully than adding offscreen cubes.
It remains a low-poly workload, not a texture-bandwidth test or a real game.

Two completion modes are explicitly identified in every profile receipt:

- `PS5_CUBES_SWAP_COMPLETED=0`: issue draws, `glFinish`, then swap. This retains
  the historical pre-swap completion boundary in a longer matched profile.
- `PS5_CUBES_SWAP_COMPLETED=1` (native target default): issue draws and swap, then
  check EGL/GL errors and native draw status. The inspected PS5 EGL implementation
  uses `ST_FLUSH_WAIT` and native presentation inside swap. This mode permits
  batching presentation with the draws. It is **not** a general claim that EGL
  swap on every platform completes GPU work. The host pbuffer adds a post-swap
  `glFinish` and labels its receipts `host=1`.

Both modes retain before/after texture/depth probes and checked cleanup. Native
profiles additionally verify the actual driver draw-counter delta, including
oracle and warm-up frames. The first excluded oracle frame calibrates its
completed delta after swap: public draws (`ordinary ? objects : 1`) for CPU
color clear, or public draws plus one for the internal GPU color-clear draw.
Every subsequent oracle, warm-up and measured frame must retire exactly that
calibrated count. The total per mode is
`(measured_frames + 30 + 2) * native_draws_per_frame`.
Schema v2 records the calibrated count and `clear_path=cpu|gpu|host`, plus each
measured frame's actual delta. Host counters are zero. The parser requires all
counts to agree; a changing clear path fails the run. No status query splits
clear from drawing inside a timed frame.

Frame samples are buffered and printed after the measured interval and final
oracle. A fixed 16,384-frame capacity fails explicitly instead of silently
truncating a measurement. The first measured interval starts at the last warm-up
completion; the last is the first completed frame reaching the requested time.
Application logging does not occur in that loop. Driver logging is independent:
use identical frozen logging settings and record them with the runtime identity.
The first paired native case keeps `PS5_RUNTIME_QUIET` unset to preserve runtime
close/batch receipts, and labels timings as instrumented. The native Make path
does not expose that macro; this profile adds no quiet-mode plumbing.

### Build and audit

After preparing the repository's normal dependencies, the existing native folder
builder discovers the new target without changes to deployment tools:

```sh
PS5_CUBES_OBJECTS=128 PS5_CUBES_SECONDS=30 PS5_CUBES_SWAP_COMPLETED=1 \
  bash tools/build-native-test-app.sh egl_public_core33_cubes_profile
```

This cube target uses the **source runtime** through `tests/ps5/native-app.mk`;
setting `PS5_OPENGL_PREFIX` does not switch that builder to an installed SDK.
Freeze the resulting native folder and runtime hashes for each comparison. The
profile object always rebuilds when requested, so changing its variables cannot
silently reuse the other completion mode. Runtime build/deployment ownership
and the existing lock protocol still apply.

After one coordinated native run:

```sh
python3 tools/summarize-cubes-profile.py results/profile-opengl.log \
  --objects 128 --seconds 30 --swap-completed 1 --budget-hz 59.94
```

The report includes clear, submission, finish, swap, active-frame and completed
frame-interval means, nearest-rank p50/p95/p99/max, and budget misses. Completed
FPS uses the entire measured interval including inter-frame loop overhead.
Submission is draw **API wall time**, which can contain driver waits; it is not
an isolated CPU utilization or GPU timer. Budget frequency is an analysis input,
not a display-mode setter. Use nominal 60/120 or a separately established actual
rate such as 59.94/119.88. These counts are not observed HDMI missed refreshes.
Fewer than 100 samples gets an explicit p99 sample-size caveat.

Compare a frozen explicit-finish receipt with the normal-swap candidate:

```sh
python3 tools/summarize-cubes-profile.py candidate-opengl.log \
  --objects 128 --seconds 30 --swap-completed 1 --budget-hz 59.94 \
  --compare baseline-opengl.log --compare-swap-completed 0
```

For comparisons between runtime versions using the same completion mode, omit
`--compare-swap-completed`. The checker requires matching scene, duration,
warm-up, host/native scope and budget; output records both receipt SHA-256 values.
Keep a separate artifact/SDK/configuration manifest with each receipt: a log
digest does not identify its runtime. Compare one changed variable at a time.

### Host checks and scope

```sh
PS5_OPENGL_PREFIX=/path/to/existing/sdk bash tools/test-cubes-host.sh
python3 -m unittest discover -s tools -p test_cubes_profile.py
```

The host lane preserves the historical and UV checks, tests both profile modes
at 128 cubes and the 512-cube upper limit, and requires deliberate depth,
texture and missing-instance faults to fail the new path. Short host profiles
run one second per mode; use `--host --seconds 1` to audit them. They validate
the workload, timing accounting and oracles, not PS5 throughput or retirement.
Parser tests also reject incomplete/reordered samples, bad phase sums, missing
native completion/counts, mismatched comparisons and invalid timing budgets.
The host script also runs the real profile loop against a mock native counter,
checking both clear paths and lost/extra draws at every frame in both modes.

The profile remains 1920x1080 and does not select a display mode. It makes no new
4K90, HDMI pacing, suspend/recovery, release-acceptance or physical-controller
claim. Start with the single matched 128-cube native case after the runtime owner
clears its current hardware investigation, rather than launching another matrix.
