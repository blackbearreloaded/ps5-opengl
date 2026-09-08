# September 8 release consolidation

One graphics runtime, compatible SDL2 inputs, focused acceptance and a versioned
SDK archive. Existing releases, native apps and historical receipts stay intact.

## Frozen graphics inputs

Reuse the already built G25 SDK: the graphics build inputs have no changes since
`61a0919bff9e6944caef2b8ad6c6ef9fd9a30279`. Rebuilding merely to change its name
would discard useful exact-binary evidence.

- SDK manifest SHA-256: `749057e84def5ead284614db5d81a0a7e9c037f474afd95cd88ebceea5d65cda`.
- Runtime archive SHA-256: `5b6129538328ab8b950dfbdb2f1395774b4a91e7d4030d1acafbabe043829d9e`.
- Native title runtime, 1080p60, presentation/draw batching and draw profiling.
- Existing G25 format/sRGB/seven-case native batch remains applicable to these
  bytes; its composite timing is not game FPS. Older G19 sample/soak results do
  not qualify G25.

## Gates

| Gate | Acceptance |
| --- | --- |
| G27: current runtime | Host regressions; 51 selected CTS cases on four configurations (204 executions); matched 30-second offscreen and 128-cube profiles; 600-second tracked-memory soak; three EGL sessions in one lifecycle run. Freeze app identities before each bounded run. |
| G28: reusable SDL2 | Build against a supplied checksum-verified SDK, record actual identity, reject mixed/tampered inputs, install relocatable headers/library/consumer metadata; real-SDL host sanitizers, relocated native links, 180 native frames/two pixel checks against G27's runtime. |
| G29: distribution | Fresh archive extraction, all file checksums and Make/pkg-config/CMake consumers; include SDL2 sources/notices and its scoped evidence. Publish only the exact qualified bytes with a new version, never replace old assets. |

G27 requires all selected cases to pass, no incomplete/waived results; matched
offscreen/cube throughput at least 57 FPS; no steady tracked-memory growth and
balanced GPU allocations/mappings after cleanup. The four CTS observation caps
are 180/180/450/450 seconds and stop on completion. The soak cap is 660 seconds.
Release the shared lock between bounded native cycles, before analysis/builds.
Stop at a functional, lifecycle or health failure; preserve the failed receipt.

Console: owner-configured 192.168.4.30, recorded firmware 6.02, WSL traffic,
PPSA99005 native folder, required services 2121/3232/9021. Use the existing
title-aware protocol and exact-token lock. No graphics application ELF injection,
Settings, enablement changes or automatic retries after a suspected fault.

Physical SDL controller events, disconnect/reconnect and TV signal confirmation
need separate owner-observed checks. They are not inferred from numerical or
virtual-joystick results. Actual 4K120 HDMI, other firmware, multi-hour sessions,
suspend/resume, device-loss recovery and exhaustive OOM remain outside this
1080p60 release's claim. GLFW and broader staging optimization are follow-ups.

## Milestones

- 2026-09-08 | preparation | frozen G25 graphics inputs match published source; no runtime rebuild needed | existing G25 native receipts retained.
- 2026-09-08 | G27 passed | 204/204 sampled CTS executions; 59.95 FPS offscreen and 59.94 FPS ordinary/instanced 128-cube profiles; 600-second tracked-memory soak and three EGL sessions passed | all native cycles closed, services healthy, exact locks released.
