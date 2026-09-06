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

## Console boundary

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
