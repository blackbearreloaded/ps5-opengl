# 1440p120 and 2160p120 qualification

G30: export each SDK's fixed render dimensions and nominal presentation rate;
make native title metadata and SDL2 follow that profile. Preserve G25 releases.
Check all six supported height/rate pairs on the host; retain the legacy 1080p60
SDL path for SDKs without metadata. Runtime drawable checks remain mandatory.

G31: freeze separate current 1440p120 and 2160p120 SDK/apps. Per size, measure the
existing ImGui workload for 30 seconds, require at least 114 completed FPS, passing
pixels and clean restoration/teardown/health/unlock. Audit the same title's HDMI
negotiation independently. 1440p rendering scaled to 4K HDMI is not 1440p HDMI.

G32: qualify the updated SDL2 adapter with real-SDL host sanitizer contracts and
one bounded 180-frame/two-pixel native check per size. Check relocated consumers.
Extend focused lifecycle checks after the display path passes; no full CTS rerun.

Use WSL, PPSA99005 native folders, existing title-aware runner and exact-token
lock on 192.168.4.30. No graphics ELF injection, Settings or forced unsupported
modes. Stop on functional/lifecycle/health failure or suspected panic; preserve
receipts. Do not retry an unsupported display path. Direct sink confirmation is
separate from console logs. Keep changes local unless publication is requested.

## Milestones

- 2026-09-08 | G30 preparation | existing renderer supports both dimensions; SDL and generic native HFR metadata need SDK-profile integration | no console run yet.
- 2026-09-08 | G31 | 3cdc90b | partial-pass: 1440p 119.881 FPS, 4K 119.886 FPS; pixels/cleanup pass, HDMI remains 1080p120 | results/g31-display-{1440,2160}-20260908/display-report.json | resolve display path.
- 2026-09-08 | G31 host | pass: both 38-entry SDK manifests, 344 exports and six relocated consumer links | .local/g31-consumers-{1440,2160}.log | SDL integration next.
- 2026-09-08 | G32 host | ac2a52a | pass: main host suite, three real-SDL sanitizer profiles, legacy G25 receipt, 51 integrity mutations/six tar cases and four relocated SDL consumer links | offline SDL handoff retained.
- 2026-09-08 | G32 native | ac2a52a | pass: 180 frames/two exact pixels per size, correct SDL drawable, restoration/teardown/health/unlock | results/g32-sdl2-{1440,2160}-20260908/acceptance.json | HDMI still 1080p120.

## Current boundary and next check

The local SDKs `build/sdk/ps5-opengl-core33-g31-{1440,2160}p120` and their
matching SDL2 payloads now support the selected fixed render profile. ImGui's
30-second runs measured about 119.88 FPS at both sizes; the SDL checks establish
functional integration, not measured SDL frame rate or arbitrary-game performance.
No new exhaustive CTS or extended high-resolution soak is claimed. Published
G25 binaries remain unchanged; none of this work has been pushed or released.

All four native cycles negotiated **1920x1080 at 119.88 Hz** over HDMI, then
restored 3840x2160 at 59.94 Hz. VideoOut's reported 4K dimensions alone do not
override that evidence. This is a rendering pass, not end-to-end 1440p120 or
2160p120 output acceptance; independent sink verification is still absent.

Next: confirm the owner's current TV/monitor, HDMI port and whether a capture
card/receiver is in the path. Qualify a direct, supported high-refresh connection
and repeat only the two display checks after a relevant condition changes;
record both negotiated timing and the sink's signal information. A 1440p render
upscaled to 4K HDMI must remain labelled as such. Do not force an undocumented
mode or repeat identical runs to turn the existing mismatch into a pass.

## G33: title-profile negotiation diagnostic

Both implementations use request 15, SDR buffer format `0x8000000000000000`
and `attribute3=0x80040`. The known-good ProsperoLight title also uses main
`attribute=0x62000000`; OpenGL uses zero. Its successful log has `HDR:o` and
HDCP23 preference, while G31 has `HDR:x` and no preference. These are correlated
differences, not proof that HDR or a particular protection mode is required.

Freeze one diagnostic copy of the G31 2160p120 ImGui app, changing only the
main JSON `attribute` from zero to that already-tested profile. Preserve the
executable, libc, shaders, rendering, title identity and HFR request. Run one
30-second measurement under the same bounded protocol and require the usual
pixels/cleanup/health plus a changed title-profile log and independently parsed
HDMI timing. Record whether registration consumed the metadata. Do not ship
this multi-bit profile as a minimal fix or claim HDR correctness from this test.
Candidate: `.local/g33-title-profile-2160/candidate.json`; results:
`results/g33-title-profile-2160-20260908`.

If negotiation stays low, retain the failed hypothesis and inspect the owner's
display path. Sony's [output guide](https://www.playstation.com/en-us/support/hardware/ps5-4k-resolution-guide/)
specifically addresses 4K120 falling back to 1080p120: the direct supported HDMI
input/cable and owner-selected resolution/transfer-rate settings matter.
Its separate 1440p output test/selection is necessary to qualify native 1440p
HDMI; a 1440p framebuffer alone does not select it. Agents do not enter Settings.
