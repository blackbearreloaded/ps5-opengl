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
- 2026-09-08 | G44 native 4K cubes | pass: 39.96 ordinary / 59.94 instanced FPS; all pixels/counts, native 2160p HDMI, clean/healthy/unlocked; clear ~12ms | results/g44-cubes-2160-native-20260908/.
- 2026-09-08 | G43 | reviewed host pass: independent 1440/2160 GL builds, 38 files/344 exports each, six relocated GL links and two 1440 SDL links; no hardware inheritance | docs/independent-hfr-build.md.
- 2026-09-08 | G41/G46 | integrated 24 packaging checks + SDL integrity/profile checks; full host suite passed; frozen HFR archives blocked by personal build paths, G47 preparing separate derivatives | docs/sdk-bundle-hfr.md.
- 2026-09-08 | G47 build | both derivatives host-checked: exact manifests, six relocated consumers, privacy audit and explicit ADDRSIG exception; originals unchanged | docs/sdk-path-free-derivative.md.
- 2026-09-08 | G47 4K | five cycles closed: window 119.879 FPS, offscreen 119.901 completed FPS, three EGL sessions/18 pixels, SDL 180 frames/two pixels; 120s memory pass/cadence partial; HDMI/cleanup/health/unlock passed, no live TV observation | results/g47-{window,offscreen,soak,lifecycle,sdl2}-2160-20260908/.
- 2026-09-08 | G46/G47 | local 4K bundle verified: 242 checksummed files, extracted GL/SDL manifests and five consumer links pass; integrated host suite/31 packaging tests pass; unpublished | docs/sdk-bundle-hfr.md.
- 2026-09-08 | G47 1440p | no-run: owner requested Chiaki configuration, but Windows denied desktop access before launch; no settings changed, post-health passed and exact lock released | results/g47-chiaki-display-20260908/.
- 2026-09-09 | G47 1440p | pass: window 119.880 FPS; SDL 180 frames/two pixels; both native HDMI/restore/cleanup/health/unlock passed | results/g47-{window,sdl2}-1440-20260909/.
- 2026-09-09 | G46/G47 | both local HFR bundles verified: 242 checksummed files each, GL/SDL manifests and five extracted consumer links each; host suite passed; unpublished | docs/sdk-bundle-hfr.md.
- 2026-09-09 | G46/G47 | published: both frozen HFR SDKs and sidecars downloaded from GitHub and hash-verified; source CI passed; prior assets unchanged | docs/sdk-bundle-hfr.md.

The 1440p soak's first 30-second window was 112.17 FPS; the remaining nineteen
were 119.83–119.87 FPS. Its strict 120-Hz cadence target failed. Explicit
`--report-cadence-miss` preserves `target_met=false` while checking all pixel,
timing and retirement records; it does not turn that result into a full pass.
Tracked owned heap ended at 9,455 bytes/22 blocks; tracked GPU allocations and
mappings returned to zero. Module-internal memory, RSS and longer sessions are
outside this observation. No additional ten-minute runs are planned.

To avoid duplicate console cycles, the 4K offscreen, two-minute memory check
and three-session EGL check used G47's separately verified release derivative.
Prepared but unrun G31 versions stay preserved. New 30-second ImGui and
180-frame SDL receipts bind the 4K derivative to its actual binaries. The
equivalent two 1440p checks also passed after the owner selected 1440p,
with fresh 1440p119.88 negotiation and 1440p59.94 restoration in both logs.
No desktop control or new app build was needed. Both matching archives passed
local assembly, extraction, integrity and consumer-link checks. The focused
high-resolution release gate is complete and the exact SDK archives are published.
No console lock is held. The broader qualification limits above remain.

## Next performance gates (local development)

- G48: separate depth-fill/cache-flush costs; optimize the existing clear path
  with unchanged ownership, bounds, masks and synchronization. Reuse host
  correctness checks, a bounded clear/depth/lifecycle batch, then matched
  30-second ordinary/instanced cube runs. Preserve the released G47 bytes.
- G49: diagnose first-window cadence using existing receipts, then instrument
  only the missing timing boundaries. Retain startup costs and original failed
  receipts; a warm-up exclusion is not a fix. Use short bounded checks.

Only the main agent uses the console through the exact-token lock. Start at
the owner's recorded 1440p; native 4K comparisons require matching output.
No game changes, whole-CTS rerun, release replacement or automatic publication.

G48's first candidate replaces only the unscissored tiled depth fill with
Mesa's existing `util_memset32`; cache flushing, masks, layer bounds and queue
ordering stay unchanged. The local SDK changes only `ps5_screen.o` in G47's
runtime archive; the other four objects and all dependencies remain byte-identical.
Run the existing clear sweep, layered-depth gate, three-session lifecycle gate
and 30-second-per-mode cube profile. Require their pixel/status/retirement
oracles and clean teardown; compare stage times, not just refresh-capped FPS.
The host fill/flush microbenchmark is screening evidence, not PS5 performance.

G49 uses unchanged G47 runtime bytes and the existing startup-inclusive
30-second profiling mode (not a shortened soak). Add frame-0, first-30-frame
and first-window stage/clock receipts, preserving the existing probes and
acceptance. Historical cadence misses remain partial passes.

- 2026-09-09 | G48 precheck | clear sweep passed; layered draws missed identically on candidate and G47 control; both clean/healthy/unlocked | results/g48-layered{,-control}-1440-20260909/.

The layered test assigned `gl_Layer` only before its vertex loop, although
[GLSL 3.30 sections 7.1/8.10](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.3.30.pdf)
leave outputs undefined after `EmitVertex`. It also assumed untouched texture
contents were zero. Define every emitted layer and initialize the attachment;
keep the same per-layer pixel expectations. Compare this corrected test on the
unchanged release and the clear candidate before resuming performance tests.
The two original failed receipts remain unchanged; no driver fix is claimed.

The corrected oracle still failed on G47 (`g48-layered-fixed-control-1440-20260909`).
The next candidate adds the missing layer XOR to CPU depth/stencil addressing,
including transfers, mip staging and scissored clears. Pinned AMD 16-pipe
`64KB_Z_X` tables exactly match the existing X/Y/sample masks, but include
Z3..Z0 at address bits 8..11. Host checks cover both formats, 1x/4x samples,
32 layers and tile boundaries; device qualification is still pending.
