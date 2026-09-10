# G63: bounded high-refresh presenter recreation

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

## Milestones

- 2026-09-09 | G63 | planned | offline | shared HFR reopen guard; preserve 0.2.0 and existing close ownership.
- 2026-09-09 | G63 | host-pass | unguarded reopen reproduced; checked runtime wait and zero-delay ImGui oracle pass; native qualification pending.
- 2026-09-09 | G63 | a62bd48 | 4K | pass: immediate app recreation, runtime waits, 18 pixels, balanced GPU memory, no HDMI reconnect | results/g63-lifecycle-0-2160-20260909-v1.
- 2026-09-09 | G63 | a62bd48 | 4K | repeated pass: another three sessions/18 pixels, two runtime waits, no post-session tracked heap growth or HDMI reconnect; clean teardown/health/unlock | results/g63-lifecycle-1-2160-20260909-v1.
