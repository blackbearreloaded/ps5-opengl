# G56: GPU-resident color copies (local experiment)

Control: the unchanged G55 2160p SDK. Change only `ps5_screen.o`: reuse Mesa's
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
memset, partial masks and unsupported layouts retain the existing CPU paths.
Mip chains and arrays still require further work; G60 is not implemented yet.

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
