# Performance work

The accepted correctness campaign remains tied to the frozen identities in
[Validation](validation.md). Performance candidates do not inherit its results.

## G1: Measure the existing frame path

Control: publication commit `6c2e928`, unchanged graphics runtime and compiler.
Use the existing 1080p ImGui TV scene with an opt-in, 30-second profiling mode.
Record CPU wall time for UI generation, clear, draw, readback and swap; exclude
warm-up frames and disable only the example's artificial 30 FPS sleep. Swap
still follows the existing presentation contract. These are not GPU timer values.

Acceptance: both numeric readbacks, successful renderer/EGL cleanup, sufficient
post-warm-up frames, and clean native-title teardown. Preserve exact build hashes.
Build with `PS5_IMGUI_PROFILE=1 make imgui-demo`; run with the native runner's
`-Headless` option. Audit the app receipt with
`python3 tools/summarize-imgui-profile.py PATH_TO_RECEIPT`.
First validate headless transport with the unchanged six-frame renderer oracle;
only then run the profiling build. Both use the same installed graphics SDK.

## G2: Accelerate the largest measured bottleneck

Change one implementation path, retaining the existing CPU path for unsupported
cases and comparison. Reuse Mesa's existing helpers where their state/lifetime
contracts fit. Do not remove synchronization merely to improve a benchmark.
Run focused host regressions, the same timed scene, and the existing external
renderer oracle. Report timings and correctness independently.

## G3: Broaden only after the first fast path is proven

Profile the successor. Extend format/operation coverage in bounded batches;
run affected CTS families after driver changes, then the full frozen release
matrix when accepting a new release candidate. Keep uncommon-case fallbacks.

## First GPU-clear result — 2026-09-06

Full, unmasked, single-target/layer RGBA8 color clears now use Mesa's existing
GPU blitter when eligible. Other formats, scissored/masked clears, multisampling,
staging resources, active queries and stream output retain the existing path.
The blitter's internal TGSI shaders use Mesa's TGSI-to-NIR conversion and IO
lowering before the existing PS5 compiler. Application NIR handling is unchanged.
No GPU synchronization was removed.

One 30-second 1080p ImGui profile per build, excluding 30 warm-up frames:

| Mean CPU wall time per frame | CPU-clear control | GPU-clear candidate |
| --- | ---: | ---: |
| UI generation | 0.069 ms | 0.034 ms |
| Clear | 63.189 ms | 17.751 ms |
| Draw | 24.818 ms | 32.802 ms |
| Readback/error check | 0.001 ms | 0.001 ms |
| Swap | 15.981 ms | 16.144 ms |
| **Total** | **104.059 ms** | **66.732 ms** |
| Measured frames | 257 | 421 |

Clear time decreased **71.9%** (3.56x); total frame time decreased **35.9%**
(1.56x equivalent throughput, approximately 9.61 to 14.99 frames/s). These are
CPU-observed phase timings including waits, not GPU timestamps or measured TV
refresh rates. This is one before/after sample of the same animated scene, not
a multi-workload benchmark or confidence interval. Draw time increased, so the
clear-only ratio must not be presented as the overall speedup.

Focused native checks passed: 256 clear colors / 524,288 exact pixel comparisons;
live uniform, program and viewport restoration; active-query clear exclusion;
all six ImGui oracle frames; and 4,096 scissored/masked color, depth and stencil
pixel checks. Both timed builds passed their readback and cleanup checks. Every
accepted cycle reached exact-title teardown, healthy service checks and
exact-token lock release. No routine screenshots or UI-input checks were used.
Host state/IO/compiler regressions, ImGui checks and installed SDK consumers also
passed.

This remains a performance candidate: the earlier **39,544-result CTS campaign
was not rerun on this runtime**, and its results do not transfer to this change.
The next measured bottleneck is drawing (32.802 ms/frame). Profile submission,
resource preparation and waits before changing them; then run affected CTS
families and freeze the complete release matrix before promotion.

### Reproducibility

Control checkout: `71e048384aef1841fcde23079104e6d65973366f` (original runtime).
Candidate runtime: `73949bc271bf8a6c7a03e179c5eb37c09b5cc7f6`; final app packages
built at `58d4cb77b3f326184183ac7278103935a17ad8bd` (documentation/parser changes
only after the runtime commit). Native boilerplate:
`4e1d1277dd0531a9a9df8c780e446b9cc26534dd`; headless protocol:
`7195c969e60735f158d46b5034cd53ae62ef0ebc`.

Raw receipts remain local under `results/`; IDs below identify
`PPSA99005-20260906-ID-opengl.log` and matching lifecycle/runner records.

| Build/check | Receipt directory / ID | eboot.bin SHA-256 |
| --- | --- | --- |
| CPU-clear profile | perf-baseline / 082420 | `230665b6906b0d42460170533abd2e1abe80e90e53e6d41ba27b26f724b52ae8` |
| GPU clear sweep/state/query | gpu-clear-io / 090704 | `6c165f422d68b5ffc18345a3d736182332ee0e3746e82c6192fb2b5e27085c81` |
| GPU-clear profile | gpu-clear-profile / 092128 | `d2b0e424707f90eddc44861b253175800fad29df0d3a4fb0d46df204bd3a8c30` |
| ImGui oracle | gpu-clear-imgui / 092403 | `27611ee14ff1c6429ac156d9569ae15ea231c82bf624a0f59eb939184450e1f0` |
| Masked clear regression | gpu-clear-masked / 092542 | `3e07d941b2704000bbad534f9f3823e333f280710d1f1378ac18f6d67f5bc71b` |

## Console boundary

Current G3 case: instrument submission setup, display-pool flush, video setup,
command construction/flush, submit+wait, and cleanup. Opt-in `PS5_DRAW_PROFILE=1`
uses the same 30-frame warm-up as the ImGui profile and leaves submission/cache
operations unchanged. Force-rebuild the native runtime when toggling this build
flag. Require `summarize-imgui-profile.py --submit-profile` plus the usual pixel,
cleanup, lifecycle and health checks; compare against the frozen G2 timing app.

Batching diagnostic: `PS5_DRAW_BATCH_PROBE=1` is restricted to the
`egl_public_core33_submit_batch` gate. It reuses the RGBA8 clear oracle for 36
interleaved command buffers containing 1, 2 or 8 identical opaque GPU draws.
Each submission retains both release operations, the completion marker, suspend
point and bounded polling. This deliberately repeated work is **not a production
GL path**. Audit with `python3 tests/ps5/test_submit_batch_probe.py RECEIPT`;
discard three warm-up cycles and compare nine samples per size. Require 73,728
exact pixel checks and clean title teardown/health. Force-rebuild without the
diagnostic flag afterward; do not install its runtime into the distributable SDK.

### Batching feasibility result — 2026-09-06

The frozen probe passed all 36 submissions and 73,728 exact pixel comparisons.
After discarding three warm-up cycles, nine samples per batch size gave:

| Identical opaque draws per command buffer | Median submit/suspend/completion wait |
| ---: | ---: |
| 1 | 15.399 ms |
| 2 | 15.432 ms |
| 8 | 15.458 ms |

For this small workload, additional draws barely changed the per-submission
wait. This supports amortizing that cost through batching, **not an eightfold
application-speedup claim**. These are CPU-observed waits, not GPU timestamps.
The probe deliberately repeats identical draw state; batching different shaders,
textures and geometry remains unvalidated. Production rendering is unchanged.

App source: `75c0e196af33d0491d3d8ae055a3934aaac0c1a1`; eboot SHA-256:
`a3359acefbb839febb616f808ebdffabc66014fbd4ad2803cbb5c6021522fe48`.
Receipt and matching lifecycle records:
`results/submit-batch-probe/PPSA99005-20260906-112328-opengl.log`.
Runner: `8015523c5d3b677dd5c30ad81cdcb63c76ffab73`; exact-title teardown,
post-health and exact-token release passed. Normal runtime/SDK remain free of
diagnostic instrumentation. No full CTS campaign was rerun.

Next implementation boundary: a bounded batch must own each draw's command and
descriptor storage, retain referenced resources until the final fence completes,
and drain before CPU read/write fallbacks, synchronization, presentation or
teardown. Uploaded-index release currently follows each synchronous draw;
descriptor storage is reused by subsequent draws. Simply deleting the wait is
not safe. Validate distinct-state draw ordering and resource reuse first, then
rerun the same ImGui profile and affected query/sync/resource tests.

Use the owner-designated console and lock in ignored `.local/ENVIRONMENT.md`.
Title: `PPSA99005`, deployed as a folder. Required owner-started services: FTP,
klog and approved title control. Observe for at most 60 seconds per profile run.
Stop on a rendering/lifecycle/health failure, uncertain foreground, or suspected
panic. No routine screenshots, settings changes, service changes or raw app ELF
submission. Release the exact token before offline analysis/building.

## Milestones

- 2026-09-06 | G1 | 71e0483 | headless six-frame control: pass; clean teardown | results/perf-control
- 2026-09-06 | G1 | 71e0483 | 257 warm frames: clear 63.189, draw 24.818, swap 15.981, total 104.059 ms | results/perf-baseline
- G2 scope: full single-target RGBA8 GPU clears via Mesa u_blitter; driver synchronization unchanged. Other cases retain CPU fallbacks.
- 2026-09-06 | G2 | d0c55ef | failed before GPU submission: helper TGSI rejected; clean teardown/health | results/gpu-clear | add Mesa TGSI-to-NIR adapter
- 2026-09-06 | G2 | 791574e | failed pixel check: helper IO lacked explicit color output; healthy teardown | results/gpu-clear-tgsi | normalize helper IO
- 2026-09-06 | G2 | 73949bc | pass: 256 RGBA8 clears / 524,288 pixels, uniform+viewport restoration, query exclusion; clean teardown/health | results/gpu-clear-io
- 2026-09-06 | G2 | 58d4cb7 | 1.56x demo throughput; six-frame ImGui and masked-clear regressions pass; all titles closed, healthy services, locks released | results/gpu-clear-profile, gpu-clear-imgui, gpu-clear-masked
- 2026-09-06 | G3 | 0810d38 | 1,263 submissions: pool flush 0.360 ms, submit+wait 15.416 ms/call; clean pass | results/draw-profile-control | split wait timing; leave cache policy unchanged
- 2026-09-06 | G3 | 6e36f33 | no-run: WSL preflight found 2121/3232/9021 closed; no upload/launch; exact lock released | build/frozen/draw-wait-control | owner service recovery required
- 2026-09-06 | G3 | 9732033 | no-run: services recovered, fresh log lacks title lifecycle; home screen observed, background idle unconfirmed; locks released | results/draw-wait-control-recovery
- 2026-09-06 | G3 | 8a3caa7 | no-run: owner confirmed idle, but log has PPSA02121 start without stop; no upload/launch, healthy services, exact lock released | results/draw-wait-owner-confirmed
- 2026-09-06 | G3 | 6e36f33 app / 5ad81b4 runner | pass: 1,260 draws, poll 15.433 ms/call, ~14 sleeps/call; submit 0.007 ms, suspend 0.006 ms; clean teardown/health | results/draw-wait-owner-confirmed/105943
- 2026-09-06 | G3 | 75c0e19 | batching probe built/host-tested; no-run: PPSA02121 restarted at 11:17:54; no upload/launch, healthy services, lock released | results/submit-batch-probe
- 2026-09-06 | G3 | 75c0e19 | pass: 73,728 pixels; 1/2/8 draws wait 15.399/15.432/15.458 ms median; clean teardown/health, lock released | results/submit-batch-probe/112328
