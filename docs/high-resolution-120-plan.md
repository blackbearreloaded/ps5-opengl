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
