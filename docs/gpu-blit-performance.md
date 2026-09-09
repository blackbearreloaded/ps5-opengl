# G56–G61: GPU transfer and clear optimization history

The [SDK 0.2.0 release](release-g62.md) incorporates these optimizations and a
later depth-only framebuffer fix. Its final runtime independently passed all
82 focused GPU cases and 202 sampled CTS executions. The paired timings and
local-only milestones below retain their original binaries and scope; the
older G55 downloads are unchanged.

G56 control: the unchanged G55 2160p SDK. Change only `ps5_screen.o`: reuse Mesa's
blitter for sufficiently large, nearest, unscaled RGBA8 copies between distinct
native 2D images. Keep synchronous submission and the existing CPU fallback for
other layouts, formats, masks, queries, scissors, mip levels and layers.
The 512×512 floor is measured below; it is not a measured optimum for smaller copies.

Acceptance: compile/test the actual guards; run the public GL pixel/state oracle
on software GL; compare the same frozen native app against control/candidate
SDKs. One short batch checks a tiny fallback, the threshold and two larger copy
footprints, full-image pixels, application state and a GPU-produced source.
Measure completed copies separately from setup, readback and presentation.
Do not describe copy throughput as game FPS or overall GPU bandwidth.

Use the existing PPSA99005 native folder runner, exact-token shared lock,
firmware-6.02 console and WSL endpoints in `.local/ENVIRONMENT.md`. Require
healthy services, idle preflight, verified files, clean native exit and exact
unlock. Stop on any pixel, driver, lifecycle or health failure. No graphics ELF
injection, display settings changes, duplicate 1440p runs or whole-CTS rerun.

Local-only; published G55 artifacts and acceptance are unchanged. Qualification
is limited to the focused checks below, not a fresh CTS or release campaign.

Host checks: `python3 tests/ps5/test_gpu_blit.py`, `make test`, and
`bash tools/test-gpu-blit-host.sh` (also included in `make test-staging`).

## Paired PS5 result — September 9, 2026

Both frozen native folders passed, with clean teardown, healthy services and
exact-token release. Each run checked **32,019,456 pixels**, including copied
images, untouched borders, application-state restoration and a GPU-cleared source.
The same app source uses G55 as control and changes only the driver object in
the candidate SDK; all other runtime objects and dependencies are byte-identical.

| Actual copy footprint | G55 CPU ms/copy | G56 ms/copy | Observed speedup |
| --- | ---: | ---: | ---: |
| 80×80 (CPU fallback retained) | 1.004 | 0.963 | No meaningful change claimed |
| 512×512 | 21.477 | 8.347 | 2.57× |
| 1904×1064 | 126.034 | 8.340 | 15.11× |
| 3824×2144 | 462.743 | 8.343 | 55.46× |

One warmed sample per size: control completed 996/47/8/3 copies and candidate
1039/120/120/120, each over at least one second. These are short measurements,
not sustained statistics. Each large candidate copy recorded one driver draw;
the control and tiny candidate recorded none during the timed interval.
Setup, upload, readback and presentation are excluded; normal runtime logging,
`glFinish` and status checks remain included. Throughput is not game FPS or raw
GPU bandwidth. The roughly 8.34 ms floor remains a separate submission/wait
optimization opportunity; this change deliberately preserves synchronization.

Frozen implementation/app source: `17612adb906148bb93101733241de4b621eb197e`.
Candidate SDK manifest: `6f43002f8b8ee9aeea9d64105035c60d659edc5d8a85cc61fdf9b46ccab3798b`;
runtime: `dbb08eb70494ee9547599ee573d6da12f760feea8a133b485fc84de0c88223c5`.

| Identity | G55 control | G56 candidate |
| --- | --- | --- |
| Native eboot SHA-256 | `ef389a0abeeea5a3281279020faacf0b4d3b9a9b003712a4e049e48d44206e2b` | `7fc0dd84294d4c85a8c96bc5eb15c807c3c0f386ff14e2c1b2f50a6bfc8d0b8b` |
| App receipt SHA-256 | `779cb754d6a7de20b49ca24c1fdfcf30cc5985bf5a1dd60ad8e012069ad31490` | `d3c4b92265574fc80fe5dd2288f7281e779dab397cbd572188709d303ffc815f` |

Local receipts and identity/lifecycle audits:
`results/g56-gpu-blit-control-2160-20260909-v1/` and
`results/g56-gpu-blit-candidate-2160-20260909-v1/`.
Host guards, unchanged transfer/clear regressions, full fast host suite, software
GL oracle and an injected wrong-coordinate rejection passed. No duplicate 1440p
run, new release bundle, push or GitHub publication was performed.

## Requested follow-on work (local)

- G57: scaled/filtered/flipped/scissored RGBA8 GPU blits; pixel/state oracle and short paired timing.
- G58: remove more format/mip/layer staging using existing native layout/transfer machinery; validate ownership and neighboring images.
- G59: RGBA8 MSAA color resolve on GPU; distinguish sample values and preserve exact resolve semantics.
- G60: GPU mip generation where storage permits; compare independent mip/layer pixels and retain guarded unsupported paths.
- G61: extend large depth/stencil/color GPU clears; validate masks, scissor, mixed clears, state and cleanup.

These are separate optimization gates, not changes to G56 acceptance or published G55.
Keep submission/retirement unchanged, test host-first, and batch bounded numerical
checks in PPSA99005 under the shared lock. Main owns driver integration and console;
agents prepare disjoint tests and offline layout analysis. No automatic publication.

Local implementation milestone: scaled/flipped/scissored blits and 4x RGBA8
resolve compile; extended software-GL oracles pass (22 blit/resolve cases,
46 clear cases plus state reuse and injected-error rejection). The candidate
also promotes single-level 2D R8/RG8/RGBA16F storage and large scissored/full-mask
depth/stencil clears. Host checks are not native qualification. Full-depth
memset and unsupported layouts retain existing fallbacks; partial masks keep
Mesa's existing clear routing.
At this milestone, mip chains and arrays still required further work (G60).

- 2026-09-09 | G57 control | 442ec97 | 4K | failed: inset LINEAR edge pixels; clean/healthy/unlocked | results/g57-blit-control-2160-20260909-v1 | fix CPU filter halo before pairing.

The control exposed an existing fallback error: interpolation clamped to the
copied rectangle, not the source image. The corrected CPU path maps a one-texel
halo, preserving image-edge clamping and untouched destination pixels, as required
by the [OpenGL blit rules](https://registry.khronos.org/OpenGL/specs/gl/glspec46.core.pdf).
The successor pair uses identical corrected source; the CPU control sets the
existing `PS5_GPU_BLIT_MIN_PIXELS` compile-time threshold to `UINT32_MAX`.
This disables accelerated blits for these workloads without changing their oracle.

- 2026-09-09 | G57/G59 | 9a43127 | 4K | pass: paired 22-case blit/MSAA oracle, 19,558,400 pixels each; clean/healthy/unlocked | results/g57-blit-{control,candidate}-2160-20260909-v2.

The GPU candidate completed scaling, nearest/linear filtering, both-axis flips,
scissoring, image-edge filtering, state reuse and distinct-sample 4x RGBA8 resolves.
Every admitted operation recorded one GPU draw; the tiny fallback recorded none.
This is focused local qualification, not a new whole-CTS or display certification.

- 2026-09-09 | G61 | 9a43127 | 4K | control pass; candidate rejected unused trailing vertex attribute before submission; clean/healthy/unlocked | results/g61-clear-{control,candidate}-2160-20260909-v1.

The control's partial-mask clears already use Mesa's GPU quad; the audit now
accounts for those draws separately from the new clear helper. The successor
validates/fetches only shader-consumed vertex elements, allowing Mesa's shared
two-element blit layout with its position-only depth-clear shader. Host tests pass;
the original failed candidate remains preserved and is not accepted.

- 2026-09-09 | G61 | daf289b | 4K | pass: 46 clears, 8 state checks, strict GPU counters; clean/healthy/unlocked | results/g61-clear-candidate-2160-20260909-v3.

- 2026-09-09 | G58 formats | daf289b | 4K | pass: 6 cases/60 pixel-sampling probes, paired native storage; clean/healthy/unlocked | results/g58-format-{control,candidate}-2160-20260909-v{1,3}.

Short completed-operation samples (eight operations, setup/readback excluded):
scissored 970x696 depth clears improved from 19.43/19.46 ms to 8.40/8.39 ms
for D32/D32S8. Full-depth CPU clears remained approximately 0.77 ms. Ordinary
1024x768 draws into R8, RG8 and RGBA16F improved from 16.67, 16.74 and 25.00 ms
to 8.38, 8.25 and 8.33 ms, respectively. These isolate tested operations, not
game FPS or sustained throughput. The single-level format promotion removes
draw-time staging; explicit CPU upload/readback still converts the tiled image.

G60 local candidate reuses canonical linear storage for selected single-target
2D/array mip/layer rendering, and checked large exact-halving blits for mipmap
generation. Small mip tails, odd reductions, depth/integer formats, layered
draws and MRT retain existing fallbacks. Allocation and sampling layouts are
unchanged. Full fast host/staging checks pass; paired native qualification and
final regression are recorded below. Mixed-layout backend register checks are
host evidence only.

- 2026-09-09 | G60 | a619262 | 4K | partial-pass: all 8 pixel cases, GPU layer blit; mip generation incorrectly fell back to CPU | results/g60-mipmap-candidate-2160-20260909-v1.

The strict counter audit caught positional `pipe_box` initialization using an
obsolete field order. The successor uses designated fields; its host regression
extracts the pinned Mesa declaration and rejects the old initializer. Both G60
v1 cycles closed cleanly with healthy services and exact-token release.

## Final focused qualification — September 9, 2026

- G60 | b42f771 | 4K profile | pass: paired 8-case mip/layer oracle, all levels and neighboring layers, immediate sampling/state checks, strict per-operation GPU counters | results/g60-mipmap-{control,candidate}-2160-20260909-v{1,2}.
- G57–G61 | b42f771 | 4K profile | pass: final-runtime combined batch, 22 blit/resolve cases + 46 clears/8 state checks + 6 format cases/60 probes | results/g60-transfer-regression-2160-20260909-v2.

All five requested categories now have guarded implementations and focused
native evidence. The mip batch checks RGBA8/R8/RG8/RGBA16F 2D chains and RGBA8
array views. Large exact-halving levels recorded one GPU draw per layer;
the small NPOT control recorded none. The selected array-layer copy preserved
every other level/layer. The final regression reuses the three existing public
GL oracles in one app launch, with separate strict acceptance audits.

| Tested operation | CPU control (ms) | Final candidate (ms) |
| --- | ---: | ---: |
| LINEAR upscale, 1,978,624 destination pixels | 264.098 | 8.342 |
| RGBA16F draw, 1024×768 | 25.001 | 8.402 |
| Scissored 4× RGBA8 resolve, 512×512 | 106.472 | 8.194 |
| Complete RGBA8 mip chain, 2048×1024 | 74.361 | 24.730 |
| Scissored D32S8 clear, 970×696 | 19.464 | 8.401 |

These are short completed API wall-time samples, not game FPS or GPU bandwidth:
eight operations for upscale/draw/clear, four complete mip generations, and one
resolve. Setup and pixel readback are excluded. Full-depth CPU clears remain
faster (about 0.72 ms in the final batch) and are intentionally retained.
Small mip tails, odd reductions, unsupported formats/layouts, layered draws and
MRT retain fallbacks; explicit CPU uploads/readback still require conversion.
This does not mean every transfer or clear is now GPU-native.

Final source: `b42f77102fce612cfcdd46a233a79aaaed02320b`;
runtime: `f80b80b05ba7e82f763c7a08c78976b0e63fc03b0284c63393db90a84fd70714`.
Mipmap receipt SHA-256: `b6781da9c387ba4fa6349cf20c2f743ac22218a0fe6952694466da4cc6e2ee26`;
combined receipt: `3bd32256f7ee5ad9d8361c4022a0a6e2dd9c8c4c7cb4c5ad74984a4b0fdc4cd4`.
Frozen app/SDK identities and lifecycle evidence accompany those local receipts.
Both final cycles closed normally, restored output mode, passed post-health,
and released only their exact lock tokens. Final `make test`, actual-Mesa-box
mipmap regression and combined software-GL/fail-stop checks passed. No new
whole-CTS campaign, duplicate 1440p run, release bundle or publication is claimed.
