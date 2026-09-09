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
| G45 | Main | Finish the already-started ten-minute 1440p heap/GPU check; use two minutes at 4K per owner preference. Reuse three-session EGL lifecycle checks. No deliberate hardware faults or memory exhaustion. |
| G47 | Build/packaging agents + main | Path-free release derivatives: preserve original SDKs, strip debug data from dependency copies, prefix-map rebuild only the runtime, verify new manifests/links and focused native receipts. No inherited CTS acceptance. |
| G46 | Main | Review and integrate isolated agent changes, freeze source/artifacts, run affected checks and publish scoped release evidence. Do not transfer old conformance claims to changed bytes. |

Only the main agent touches the console, through the existing native-folder
runner and shared exact-token lock. Prepare and audit offline; release the lock
after every bounded cycle. Use logs and numerical probes, not routine screenshots.
Stop on uncertain foreground, lifecycle or service health. After G44's first
4K-render/1440p-HDMI mismatch, the owner selected 2160p. The identical frozen
cube app then qualified native 2160p119.88 and restoration to 2160p59.94.

Physical controller actions and an independently available second console/firmware
require owner participation. Suspend/resume, device-loss recovery and hardware
OOM behavior stay explicitly unqualified unless safely validated later. CPU
staging outside the measured fast paths is a performance limitation, not an
automatic functionality failure; optimize only a demonstrated bottleneck.

Record one short milestone per completed gate, linking immutable evidence. No
full 39,544-case rerun for example, documentation or packaging-only changes.

- 2026-09-08 | G44 1440p cubes | 0826b57 | pass: 57.01 ordinary / 112.91 instanced FPS; pixels, counts, HDMI, teardown/health/unlock | results/g44-cubes-1440-20260908/; next offscreen.
- 2026-09-08 | G44 1440p offscreen | 0826b57 | pass: 119.90 completed FPS, pixels/retirement/preview HDMI/cleanup/health/unlock | results/g44-offscreen-1440-20260908/; next soak.
- 2026-09-08 | G45 1440p | partial-pass: 71,678 frames/600s, 21 pixels, zero steady tracked growth, GPU balanced, clean/healthy/unlocked; first-window cadence miss | results/g45-soak-1440-20260908/.
- 2026-09-08 | G45 1440p lifecycle | pass: three EGL sessions/18 frame oracles, zero post-session heap growth, GPU baseline restored, three HDMI restore cycles, clean/healthy/unlocked | results/g45-lifecycle-1440-20260908/.
- 2026-09-08 | G42 | pass: owner-confirmed physical Cross/stick sequence before/after removal and new instance; 38.336s, HDMI restoration, clean/healthy/unlocked | docs/sdl-input-validation.md.
- 2026-09-08 | G44 4K cubes attempt 1 | render workload passed, HDMI mismatch: 2160p render / 1440p119.88 output; clean/healthy/unlocked; not native-4K acceptance | results/g44-cubes-2160-20260908/.
- 2026-09-08 | G44 native 4K cubes | pass: 39.96 ordinary / 59.94 instanced FPS; all pixels/counts, native2160p HDMI, clean/healthy/unlocked; clear ~12ms | results/g44-cubes-2160-native-20260908/.
- 2026-09-08 | G43 | reviewed host pass: independent1440/2160 GL builds, 38 files/344 exports each, six relocated GL links and two1440 SDL links; no hardware inheritance | docs/independent-hfr-build.md.
- 2026-09-08 | G41/G46 | integrated24 packaging checks + SDL integrity/profile checks; full host suite passed; frozen HFR archives blocked by personal build paths, G47 preparing separate derivatives | docs/sdk-bundle-hfr.md.

The 1440p soak's first 30-second window was 112.17 FPS; the remaining nineteen
were 119.83–119.87 FPS. Its strict 120-Hz cadence target failed. Explicit
`--report-cadence-miss` preserves `target_met=false` while checking all pixel,
timing and retirement records; it does not turn that result into a full pass.
Tracked owned heap ended at 9,455 bytes/22 blocks; tracked GPU allocations and
mappings returned to zero. Module-internal memory, RSS and longer sessions are
outside this observation. No additional ten-minute runs are planned.

To avoid duplicate console cycles, the remaining 4K offscreen, two-minute memory
check and three-session EGL check will use G47's separately verified release
derivative. Prepared but unrun G31 versions stay preserved. New ImGui30-second
and SDL180-frame receipts bind each derivative profile to its actual binaries.
