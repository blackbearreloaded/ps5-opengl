# High-resolution completion work

Starting point: G37–G40 qualified the unchanged G31/G32 artifacts at native
1440p119.88 and 2160p119.88 on one firmware-6.02 console. These are focused
application receipts, not new CTS campaigns or universal game-performance claims.

| Gate | Owner | Work and acceptance |
| --- | --- | --- |
| G41 | Packaging agent | Frozen GL/SDL2 bundles for both heights; complete public sources, licenses, checksums, provenance and focused receipts; reject mismatched evidence; relocated consumers pass. Preserve old releases. |
| G42 | SDL agent | Bounded physical-input/reconnect example and host contract checks. Actual button, axis, disconnect and reconnect observations required; timeout is incomplete. |
| G43 | Independent-build agent | Separate clean build trees, pinned inputs, explicit reused/rebuilt inventory, manifest and relocated consumer checks. Newly built bytes remain host-checked until hardware tested. |
| G44 | Main | Reuse cubes ordinary/instanced profiles and selected ImGui offscreen cases. 30 seconds per measured workload, before/after pixels and exact retirement/cleanup. Report measured bottlenecks without extrapolating to games. |
| G45 | Main | Reuse ten-minute normal-session heap/GPU diagnostics and three-session EGL lifecycle checks at each high-resolution profile. No deliberate hardware faults or memory exhaustion. |
| G46 | Main | Review and integrate isolated agent changes, freeze source/artifacts, run affected checks and publish scoped release evidence. Do not transfer old conformance claims to changed bytes. |

Only the main agent touches the console, through the existing native-folder
runner and shared exact-token lock. Prepare and audit offline; release the lock
after every bounded cycle. Use logs and numerical probes, not routine screenshots.
Stop on uncertain foreground, lifecycle or service health. Current owner-selected
output is 1440p; an actual 4K HDMI cycle needs the owner to select 4K first.

Physical controller actions and an independently available second console/firmware
require owner participation. Suspend/resume, device-loss recovery and hardware
OOM behavior stay explicitly unqualified unless safely validated later. CPU
staging outside the measured fast paths is a performance limitation, not an
automatic functionality failure; optimize only a demonstrated bottleneck.

Record one short milestone per completed gate, linking immutable evidence. No
full 39,544-case rerun for example, documentation or packaging-only changes.

- 2026-09-08 | G44 1440p cubes | 0826b57 | pass: 57.01 ordinary / 112.91 instanced FPS; pixels, counts, HDMI, teardown/health/unlock | results/g44-cubes-1440-20260908/; next offscreen.
