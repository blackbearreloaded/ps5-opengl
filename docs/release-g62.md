# G62 release consolidation

Complete a scoped stable 0.x release using the G57–G61 optimized runtime.
Do not change graphics behavior while qualifying it. Reuse exact-binary
evidence; a different binary requires its own acceptance. No full CTS rerun.

## Frozen inputs and acceptance

- 4K runtime source: `b42f77102fce612cfcdd46a233a79aaaed02320b`.
- 4K runtime SHA-256: `f80b80b05ba7e82f763c7a08c78976b0e63fc03b0284c63393db90a84fd70714`.
- Reuse the [82 focused GPU cases](gpu-blit-performance.md#final-focused-qualification--september-9-2026)
  only for those unchanged bytes.
- Build the matching 1440p profile and SDL2 pairs; verify complete manifests,
  source provenance, all Core exports and relocated GL/SDL consumer links.
- Run 202 compact CTS executions: all 51 smoke cases on the two ordinary-sized
  targets, and 50 each on the two extreme-axis targets. Defer only the latter
  two `multisampled_to_singlesampled_blit_color_config_test` executions because
  their previous single-case times exceeded the owner's 120-second bound.
  This is a new explicitly scoped sample, not the historical 204-case gate.
  Run ImGui window/offscreen and 3D checks on the same frozen 4K runtime.
  Require all selected results and pixel/state oracles.
- Run one 120-second normal ImGui session and three launch/exit cycles, each
  with three EGL sessions. Require balanced tracked GPU allocations/mappings,
  no steady tracked memory growth, clean close and healthy services.
- Run the matching SDL2 native smoke (180 frames/two pixel checks). No new
  physical-input or independent TV qualification is inferred.
- Package verified bytes, sources, notices, checksums and scoped evidence.
  Check a fresh extraction and its consumers before publishing new assets.

The owner requires only 4K hardware qualification; 1440p remains explicitly
host-checked. Full-profile/GLFW portability, multi-hour sessions, suspend/resume,
device-loss recovery, exhaustive allocation pressure and other firmware are
outside this release claim. CPU fallbacks are not missing API functionality.

## Console protocol

Use the existing PPSA99005 native folder and WSL on the owner-configured
firmware-6.02 console. Acquire the exact-token lock for each bounded cycle,
verify idle/services/files, capture numerical and lifecycle evidence, close
the exact title, verify health and release only that token. Stop on any
functional/lifecycle/health failure; preserve the receipt and investigate
offline. No graphics ELF injection, settings changes or routine screenshots.

## Milestones

- 2026-09-09 | G62 preparation | reused b42f771 runtime and 82-case evidence; owner capped stability session at 120 seconds; remaining gates pending.
- 2026-09-09 | G62 SDL | dccc5af | 4K | pass: matched SDL2, 180 frames/two probes, HDMI/restoration, clean/healthy/unlocked | results/g62-sdl-2160-20260909-v1.
- 2026-09-09 | G62 integrations | daa29c5 | 4K | pass: ImGui window 119.882 FPS, offscreen 119.900 completed FPS; 128 cubes ordinary/instanced 57.36/116.28 FPS, 30 seconds per mode; clean/healthy/unlocked.
- 2026-09-09 | G62 stability | daa29c5 | 4K | 120 seconds: no steady tracked heap/GPU growth, GPU allocations/mappings balanced, five pixel probes, clean/healthy/unlocked. Cadence remains partial: first window 113.53 FPS, later windows 119.83–119.87 FPS.
