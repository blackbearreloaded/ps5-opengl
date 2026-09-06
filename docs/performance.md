# Performance work

The accepted correctness campaign remains tied to the frozen identities in
[Validation](validation.md). Performance candidates do not inherit its results.

## Current candidate — 2026-09-06

Runtime/compiler `2df7d88` passes the combined indexed/instanced/restart/base-vertex
draw regression, mixed float/integer constant attributes, and the original
textured/depth-tested cube benchmark. All three native cycles closed cleanly.
The final 1080p cube rerun measured 21.89 / 6.00 / 1.76 FPS for 1 / 8 / 32 ordinary
draws and approximately 20.06 FPS for each instanced workload (eight measured
frames per cell). This is a small draw-overhead benchmark, not expected game FPS.

Before promotion: validate PrimitiveID-consuming fragment shaders and affected
CTS families, rebuild the installed SDK and recheck external renderers, run longer
sessions, then freeze and complete the four-configuration release matrix. The
installed SDK and historical validation export have not been replaced. Batching
remains opt-in; per-draw completion waits remain the main scaling limitation.

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

G3 instrumentation covers submission setup, display-pool flush, video setup,
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

### First real multi-draw candidate (opt-in, not promoted)

`PS5_MULTIDRAW_BATCH=1` batches at most eight subdraws within one `glMultiDraw*`
call and drains every chunk before returning. Each draw owns its native work
allocation and cloned vertex/constant descriptors (including inline uniform
bytes). Referenced buffers and targets stay retained until every attempted
submission's unique marker completes. One shared bounded wait replaces per-draw
waits; existing command construction, release operations and cache policy stay
unchanged. This uses multiple existing submissions followed by one suspend,
which is distinct from the earlier repeated-command-buffer diagnostic.

Initial eligibility is direct triangle draws with native indices or no indices,
no shader texture use, a single non-staged RGBA8 2D level-zero target, no enabled depth/stencil,
geometry shader, query, conditional rendering or stream output. Other paths
stay synchronous. Submission/retirement failures poison this process's queue
and quarantine bounded resources until teardown; no speculative reset or replay.
An unused, non-staged depth/stencil attachment is allowed and retained, since
the EGL surface always supplies one. Staged depth remains excluded even when disabled.
The default build and installed SDK remain unchanged. Force-rebuild when
toggling this flag.

Gate: `egl_public_core33_multidraw_batch`. Compare ordinary draws against four
multi-draw modes (arrays, u16, u32, base vertex), with reverse vertex ranges,
a zero-count entry, chunk rollover and a final overlapping quad. Require all
27,648 exact pixel comparisons, immediate readback, ordinary-draw uniform
restoration, native batch telemetry, cleanup, title teardown and healthy services.
Host tests inject allocation/preparation/submit/suspend/retirement failures;
software Mesa validates the pixel oracle. CPU call timings are preliminary
single samples, not a benchmark. This does not yet accelerate separate ImGui
draw calls or transfer the historical CTS results to this candidate.

The first native gate used the default EGL pbuffer, which supplies an unused
depth/stencil attachment and correctly excludes batching. Its 27,648 pixels
passed but no batch ran. A color-only renderbuffer FBO also passed pixels but
remained serial: Mesa marks it sampleable, requiring this driver's staging path.
The successor instead allows and retains an inactive non-staged EGL depth/stencil
attachment, with enabled depth/stencil still excluded. Audit with
`python3 tests/ps5/test_multidraw_lifetime.py RECEIPT` to require native chunks as
well as pixel success. Both earlier frozen apps remain exclusion controls.

#### First real batching receipt — 2026-09-06

Candidate `8eb024f971b5623266f47de1abe6f64604f6d7b6` passed all four modes,
27,648 exact pixels and eight native chunks (7 + 3 draws per mode). Immediate
readback, overlapping-draw ordering, uniform restoration and ordinary draws
passed. Preliminary CPU call timings, one sample per mode:

| Mode | Serial | Batched | Ratio |
| --- | ---: | ---: | ---: |
| Arrays | 150.149 ms | 32.735 ms | 4.59x |
| u16 indices | 150.362 ms | 33.613 ms | 4.47x |
| u32 indices | 165.164 ms | 33.106 ms | 4.99x |
| Base vertex | 166.668 ms | 34.052 ms | 4.89x |

This is a small synthetic workload, not an ImGui/application benchmark or
release-wide result. Receipt: `results/multidraw-batch-egl/PPSA99005-20260906-121541-opengl.log`;
eboot SHA-256: `ec594b209deea0e0be56b17d844686688606cab414c89f60c804ad3481dde634`.
Exact-title teardown, post-health and exact-token release passed. No SDK promotion.

The follow-up gate at `dd9905eac2067351ae8d437ff91702d823e3dcf5` also passed:
active-query exclusion (2,560 samples), fence wait and VBO orphan/reuse;
32,256 total exact pixel comparisons. Eight native chunks ran, with no extra
batch during the active query. Timings repeated at 150–167 ms serial versus
33–34 ms batched (4.44–5.09x); these remain preliminary small-workload samples.
Receipt: `results/multidraw-postchecks/PPSA99005-20260906-122150-opengl.log`;
eboot: `ab65f94bdde319cad4cad3ecd0b21bd83dbd7b75885d85f775620ee6f86ff453`.
Exact-title teardown, post-health and exact-token release passed.
Audit with `python3 tests/ps5/test_multidraw_lifetime.py RECEIPT --postchecks`.

Next: handle staged targets/textures and resource/state changes before extending
batching across ordinary draw calls (including ImGui). Then measure real workloads
and run affected CTS families before release promotion. Keep batching opt-in;
the installed SDK is unchanged and the default local runtime is rebuilt without
batching or diagnostic flags after freezing these apps.

## Milestones

G4 release-readiness case: `examples/core33-cubes` measures complete 1080p frames
with 1/8/32 lit, textured, depth-tested cubes. Keep the runtime unchanged for the
baseline; require six numerical oracles (334 probes), 24 measured frames and
clean lifecycle/health. One 60-second headless PPSA99005 cycle, with the endpoint
and lock from `.local/ENVIRONMENT.md`. Audit with `tools/summarize-cubes.py`.
Optimize measured costs next, then affected CTS and longer real-app tests before
promotion. No public release or repository visibility change is part of this case.

Runtime configuration changes now invalidate local objects automatically,
including switching experimental flags off; identical settings remain cached.
This removes the manual force-rebuild requirement described by earlier cases.

G4 baseline (`af98e68`): 334 probes and 24 measured frames passed. At 1080p,
1/8/32 cubes measured 20.93/6.00/1.76 FPS (47.79/166.65/567.06 ms/frame).
Clear was 18–20 ms and swap ~16 ms; drawing grew from 13 to 531 ms. Receipt:
`results/cubes-control/PPSA99005-20260906-124439-opengl.log`; frozen artifact:
`build/frozen/cubes-control/manifest.md`. Title teardown/health/unlock passed.
The successor compares ordinary calls and standard instancing in the same app:
six workloads, 668 probes and 48 measured frames. Both paths use identical
shaders and two persistent texture bindings, unlike the earlier baseline's
per-object texture bind. Compare the successor's paired results, not just old
versus new binaries. This exercises existing instancing with no new driver
behavior or experimental flags. The first native comparison failed one texture
probe in the eight-instance scene after all ordinary workloads and one-instance
checks passed. This is not an accepted instancing benchmark or speedup result.
The diagnostic successor records all probe RGBA mismatches before stopping;
it does not change rendering or relax the oracle. Root cause remains open.

- 2026-09-06 | G4 | b6429a9 app / 356ea89 runner | failed: eight-instance texture probe; ordinary/one-instance pass, healthy teardown/unlock | results/cubes-instanced/130057 | classify full probe mismatches
- 2026-09-06 | G4 | 6f1e1d6 | failed: five right-texel probes read left texel; healthy teardown/unlock | results/cubes-diagnostic/131120 | explicit-LOD fragment-shader control

The next control changes only `texture` to `textureLod(..., 0.0)` in both
material branches; textures are single-level nearest-filtered 2x2 images.
This isolates implicit sampling from the existing geometry/instance inputs.
It does not establish a driver fix or dismiss the original failure as undefined
behavior. The explicit-LOD control reproduced the same five mismatches, with
clean lifecycle/health/unlock. The next shader-only control uses integer
`texelFetch` coordinates derived from the same interpolated UVs, equivalent to
the intended nearest/clamp sampling for these 2x2 calibration textures. This
separates normalized sampling from UV/geometry while retaining the oracle.

- 2026-09-06 | G4 | 2570886 | failed: explicit LOD reproduces all five mismatches; healthy teardown/unlock | results/cubes-lod0/131824 | direct texel-fetch control
- 2026-09-06 | G4 | 5862af1 | failed: direct texel fetch reproduces the same five mismatches; healthy teardown/unlock | results/cubes-fetch/132623 | first-provoking-vertex control

The next control changes only the provoking-vertex convention to first.
Material IDs are constant across every triangle, so expected output is unchanged;
this exercises the alternate flat-input compiler path. It retains direct texel
fetches to compare against the last frozen control. It reproduced the same five
errors. The example is restored to ordinary implicit sampling/default last
provoking vertex; frozen diagnostic artifacts retain each prior control.

- 2026-09-06 | G4 | 35b0fad | failed: first provoking vertex reproduces five mismatches; healthy teardown/unlock | results/cubes-first/133413 | UV-coordinate diagnostic

`egl_public_core33_cubes_uv` reuses the scene with a UV/material fragment output.
Its oracle inverts projection at each probed pixel center, independently of the
GPU's interpolated coordinates. It preserves normals/lighting and the flat
material input. Use `summarize-cubes.py --uv`; its receipts are explicitly not
accepted as texture validation. Host reference must pass before a native case.

- 2026-09-06 | G4 | 249963d | failed: encoded U is zero on instances 1/5 (RGBA8 clamps negatives); V/material intact; ordinary pass, healthy teardown/unlock | results/cubes-uv/134227 | compiler IO/NIR trace

UV output confirms the corruption precedes texture access. The trace successor
changes diagnostics only, not shaders/rendering. Both native log streams now
append after initial truncation: stdout previously could overwrite stderr's
compiler diagnostics. A host test reproduces the old loss and verifies retention.

- 2026-09-06 | G4 | ce9adb5 | failed: same UV probes, complete NIR/IO receipt; healthy teardown/unlock | results/cubes-trace/135443 | isolate unused implicit PrimitiveID export

The standalone NGG compiler conservatively exports PrimitiveID with no fragment
consumer attached. This scene does not read it. The next candidate omits only
that implicit export when the bound FS proves it unused, includes that choice
in the VS cache key, and preserves explicit outputs/unknown consumers. Real
NIR/ACO host checks precede hardware. This is a hypothesis, not a confirmed fix;
the original textured benchmark must still pass before accepting instancing.

- 2026-09-06 | G4 | fc143fc | pass: 668 UV probes, six workloads/48 frames; trace confirms unused export removed; healthy teardown/unlock | results/cubes-unused-primitive/142524 | original texture oracle

Removing that export eliminates all observed UV failures, including 32 instances.
The driver keeps the conservative variant for unknown or PrimitiveID-reading
fragment consumers. Their per-primitive linkage is not validated by this result.

- 2026-09-06 | G4 | af19be8 | pass: 668 texture/depth probes, six workloads/48 frames; healthy teardown/unlock | results/cubes-textured-fixed/143002 | draw regressions and longer workload validation

Original textured scene, 1920x1080, eight measured frames per cell:

| Cubes (12 triangles each) | Ordinary calls, ms/frame (FPS) | One instanced call, ms/frame (FPS) |
| --- | ---: | ---: |
| 1 | 43.60 (22.93) | 49.86 (20.06) |
| 8 | 166.64 (6.00) | 49.86 (20.06) |
| 32 | 567.04 (1.76) | 49.85 (20.06) |

Both paths use the same shaders and texture bindings. These are short,
CPU-observed clear/draw/swap timings with completion waits, not game performance
or maximum GPU throughput. One-cube medians are ~49.8 ms in both paths; the
ordinary mean includes faster frames. At 32 cubes, instancing removes repeated
per-draw waits and gives an 11.4x frame-throughput ratio. Approximately 20 ms of
clear, 14 ms of draw/wait and 16 ms of swap still dominate the instanced frame.
The previously failing instanced UV/texture scene is fixed; broader regressions,
long runs, affected CTS and the final release matrix remain before promotion.

- 2026-09-06 | G4 | fe1c604 | failed: indexed instancing passes; disabled/current attribute rejects VS compilation; healthy teardown/unlock | results/draw-matrix-unused-primitive/144258 | zero-stride compiler/descriptor fix

Host reproduction confirms zero-stride rejection with either PrimitiveID option;
the guard also exists in the original publication source. Mesa represents current
attributes as zero-stride uploads. The successor permits this PS5 compiler input
and selects byte-bounded RAW descriptors only for zero stride, preserving CPU
range checks and the ordinary descriptor path. Both native attribute regressions
and the unchanged cube benchmark now pass with this fix.

- 2026-09-06 | G4 | 2df7d88 | pass: combined draw matrix, 256 pixels; clean teardown/health/unlock | results/draw-matrix-constant-input/150601
- 2026-09-06 | G4 | 2df7d88 | pass: mixed float/uint constants, 1,024 pixels and queries; clean teardown/health/unlock | results/current-attrib-constant-input/150921
- 2026-09-06 | G4 | 2df7d88 | pass: 668 texture/depth probes, six workloads/48 frames; ~20 FPS instanced; healthy teardown/unlock | results/cubes-constant-input/151315

Final compiler tree: `267ff517bd902e4b35a1b40fd50ee984a118eb55`; native archive
SHA-256: `4ac8aa5221eb24dd4492799808939d667b55268075162fc4d4e20e9e9557dc12`.
The existing source/toolchain/archive guard verified reuse for the latter two
builds. All experimental runtime flags were off. Final executable identities:

| Check | eboot.bin SHA-256 |
| --- | --- |
| Combined draw matrix | `02d8247ff8538f80d214b3ab55168e2ecd8aeea04ee4b6f12cba71a7ec5d9f56` |
| Mixed current attributes | `6345c81f70cbf16257116e6750b49491da14de28ad962772d38468c11e445d53` |
| Textured cubes | `ffc6aeb35dff9755e2be5134485a2ad178ccc8571b4aefeef6d675827e3dcee0` |

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
- 2026-09-06 | G3 | 9fd90a5 | partial-pass: 27,648 pixels, clean teardown/health; EGL depth attachment excluded batching | results/multidraw-batch/120153 | use color-only FBO
- 2026-09-06 | G3 | 82df5ab | partial-pass: FBO pixels pass, staged renderbuffer excluded batching; healthy teardown/unlock | results/multidraw-batch-fbo/120752 | retain inactive EGL depth
- 2026-09-06 | G3 | 8eb024f | pass: real multi-draw, 27,648 pixels; 150–167 to 33–34 ms/call; healthy teardown/unlock | results/multidraw-batch-egl/121541 | query/fence/reuse regression
- 2026-09-06 | G3 | dd9905e | pass: 32,256 pixels, query exclusion, fence/orphan; 4.44–5.09x small-workload ratio; healthy teardown/unlock | results/multidraw-postchecks/122150 | broader draw-state batching
