# G63: bounded high-refresh presenter recreation

**Completed locally on September 9, 2026.** The G63 successor enforces settling
inside the shared runtime; applications no longer need the example's sleep.
Published SDK 0.2.0 archives are unchanged and do not contain this guard.

Goal: applications may recreate their EGL window/display without an application
sleep. Enforce the known five-second settling interval before a new HFR VideoOut
open after successful close. First acquisition and reuse of a live presenter
must not wait. This is conservative process-local pacing, not sink-readiness
detection, a minimal-delay claim or device-loss recovery.

## Acceptance

- Reuse the actual shutdown/acquisition host harness. Show an immediate reopen
  violates the new wait contract before the fix; verify first open, live reuse,
  successful and failed close, idempotent teardown, interrupted settling and
  retry ownership afterward. Retain 60-Hz and queued-presentation checks.
- Change only the presenter object in copies of the released 0.2.0 SDKs. Keep
  dependency bytes intact, record manifests/source hashes, and host-check both
  4K120 and 1440p120 consumers. Do not mutate the published archives.
- Remove the example-only delay. On the fixed 4K SDK, run two bounded native
  lifecycle cycles, three immediate EGL sessions each. Require 18 pixel checks,
  balanced GPU memory, no post-session tracked heap growth, two successful
  runtime settle events, exactly three HFR/restoration pairs and no logged
  HDMI disconnect/reconnect. Then run one 30-second window throughput check.
- Every console cycle uses PPSA99005, the shared exact-token lock, WSL and the
  existing protocol. Observation is at most 120 seconds; stop and investigate
  any functional, display, teardown or health failure. No duplicate 1440p run,
  settings change, graphics ELF injection or routine screenshots.

Leave Yamagi unchanged. Its agent will qualify the successor SDK separately;
these lifecycle checks do not transfer 0.2.0 CTS or real-game acceptance to new
runtime bytes. No stable-release promotion is part of this gate.

## Supported lifecycle

- On a successful HFR VideoOut close, preserve a process-local reopen guard
  across EGL teardown. The next acquisition waits five seconds before opening
  or registering another presenter. Window-surface destruction, EGL termination
  and presenter replacement share this path.
- Acquisition is lazy: the wait can occur at the first rendering operation of
  the recreated session, not necessarily in `eglInitialize` or surface creation.
  The full five seconds is conservative, even if the application already waited.
  Keep a live EGL presenter for ordinary menu/level changes when practical.
- First acquisition, live-presenter reuse, steady rendering, final close and
  60-Hz builds do not incur this additional wait. Required flip draining and
  output restoration remain unchanged.
- Failed close retains the existing resources/ownership. An interrupted settle
  returns an error without opening/registering a new port and leaves the guard
  armed. Applications must honor failures; this is not automatic device recovery.
- This is a supported paced lifecycle, not unrestricted rapid HDMI mode churn.
  The guard is not persistent across processes, a sink-readiness detector,
  hotplug/suspend/device-loss recovery or proof that a shorter delay is unsafe.

The implementation reuses the existing acquisition/shutdown path and test
harness; no new platform imports, timer subsystem or application-specific guard.

## Qualification

Two native 4K lifecycle runs passed **six EGL sessions / 36 pixel checks** with
zero application delay and four successful runtime-owned five-second waits.
Each fresh process's first session incurred no reopen wait. Tracked GPU direct
allocations/mappings returned to zero after every session; tracked owned heap
remained at 9,455 bytes after the first session, with zero subsequent growth.
These counters exclude foreign heaps, module-internal GPU allocations and RSS.

Console HDMI logs contain the expected 2160p119.88 to 2160p59.94 pair for every
session, with **no logged HDMI disconnect/reconnect**. The generic single-session
display summary is inconclusive for these repeated sessions; the dedicated
`tools/lifecycle_reopen_evidence.py` check verifies all six ordered modes per run
without filtering events. This is not a new independent TV measurement.

The separate window check passed **3,597 measured frames in 30.004338 seconds
(119.882665 FPS)**, matching logged 2160p119.88 output and final 59.94-Hz
restoration. No reopen-wait marker occurred. This preserves the tested average
throughput, not perfect frame pacing or arbitrary-game performance. All three
native cycles closed cleanly, passed service health and released their exact lock.

Local evidence (each directory contains the frozen candidate, hashed raw
receipts and `*-opengl-successor-audit.json`):

| Run | Results directory | Receipt prefix |
| --- | --- | --- |
| Lifecycle 0 | `results/g63-lifecycle-0-2160-20260909-v1` | `PPSA99005-20260909-195304` |
| Lifecycle 1 | `results/g63-lifecycle-1-2160-20260909-v1` | `PPSA99005-20260909-195542` |
| Window | `results/g63-window-2160-20260909-v1` | `PPSA99005-20260909-200150` |

Host regressions passed, including the actual-C ownership/interruption checks,
the zero-delay software ImGui oracle and malformed-evidence rejection. Both
SDK profiles passed 344 export checks and five native consumer compile/link
checks (three GL and two relocated SDL); SDL manifests and source/build receipts
also verified. These consumer binaries were not executed on the console.

## SDK handoff for Yamagi

Use this **local G63 post-0.2.0 candidate**, source companion
`a62bd48d3b7fefe6f4028973a491d1e60a5ddb56`. Paths below are relative to the
`ps5-opengl` checkout; all build outputs are local and ignored by Git.

| Profile | GL SDK prefix | Matching SDL2 prefix |
| --- | --- | --- |
| 4K120, focused native qualification above | `build/g63-reopen-2160-v1/gl` | `build/g63-sdl-2160-v1/sdk` |
| 1440p120, host-only | `build/g63-reopen-1440-v1/gl` | `build/g63-sdl-1440-v1/sdk` |

| SHA-256 | 4K120 | 1440p120 |
| --- | --- | --- |
| GL manifest | `e94082986af6b83ffcd3352ea5eda247276d246afcca3f7e6a055019afd13d2d` | `fb1ab56f8e9da18b54889d38660826c13bca5dd11884818845bb62ac2aff9d6a` |
| Runtime archive | `44d471b305b62838f2922549e9cd80542a56c9ec0a3fa89cda9eae32e447adfe` | `dcdb1318ad04c30806556841bc766d2d490a83c4f06d8f1577a13b89cc23128a` |
| SDL build receipt | `450e5c42e412d59b75272f7c961cb3704ed59562dbaec33faf2b481c40cbec42` | `c94044dae2f3ec0b3c8734dcba7bc4b35805e176a89251edfe37d7dd0996c833` |

Only `ps5_agc_runtime_backend.o` changed in each GL runtime archive relative to
the corresponding 0.2.0 SDK. Other runtime members and dependency bytes are
unchanged. The companion `build/g63-reopen-HEIGHT-v1/candidate.json` records this
derivation; `build/g63-sdl-verification.json` records matching SDL identities.

For the Yamagi agent:

1. Keep changes in the separate game checkout. Pin one complete GL/SDL pair,
   verify each prefix's `manifest.sha256`, then cleanly relink; do not combine
   old/private runtime objects with the new archive. Use the existing
   [consumer interfaces](consumer-build.md) and
   [SDL metadata](../integration/SDL2/README.md).
2. Honor the fixed display profile. Do not infer that the older 60-FPS game
   result applies to a new 4K workload. Keep game resolution/performance changes
   separate from validating this SDK update.
3. Run a bounded game check covering menu, gameplay, a level transition and
   clean exit, under the shared console protocol/lock. Limit each observation
   to 120 seconds. If the game recreates EGL, expect the runtime wait; do not
   remove the guard or add a duplicate application sleep.
4. Report the game commit, SDK hashes, native receipt, rendering/timing results,
   any EGL recreation/wait, HDMI sequence, teardown and post-health. Stop on
   errors and retain failed receipts. Prior Yamagi, CTS and SDL hardware results
   are not automatically acceptance of these new bytes.

## Milestones

- 2026-09-09 | G63 | planned | offline | shared HFR reopen guard; preserve 0.2.0 and existing close ownership.
- 2026-09-09 | G63 | host-pass | unguarded reopen reproduced; checked runtime wait and zero-delay ImGui oracle pass; native qualification pending.
- 2026-09-09 | G63 | a62bd48 | 4K | pass: immediate app recreation, runtime waits, 18 pixels, balanced GPU memory, no HDMI reconnect | results/g63-lifecycle-0-2160-20260909-v1.
- 2026-09-09 | G63 | a62bd48 | 4K | repeated pass: another three sessions/18 pixels, two runtime waits, no post-session tracked heap growth or HDMI reconnect; clean teardown/health/unlock | results/g63-lifecycle-1-2160-20260909-v1.
- 2026-09-09 | G63 | a62bd48 | complete locally: 4K window 119.882665 FPS/no reopen wait; both GL/SDL pairs verified, five consumer links per profile; no stable promotion or game changes.
