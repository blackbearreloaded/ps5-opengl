# G56: GPU-resident color copies (local experiment)

Control: the unchanged G55 2160p SDK. Change only `ps5_screen.o`: reuse Mesa's
blitter for sufficiently large, nearest, unscaled RGBA8 copies between distinct
native 2D images. Keep synchronous submission and the existing CPU fallback for
other layouts, formats, masks, queries, scissors, mip levels and layers.
The initial 512×512 floor is provisional, not a measured optimum.

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

Local-only; published G55 artifacts and acceptance are unchanged. No hardware
speedup or successor qualification is claimed until the paired receipts pass.

Host checks: `python3 tests/ps5/test_gpu_blit.py`, `make test`, and
`bash tools/test-gpu-blit-host.sh` (also included in `make test-staging`).
