# 1440p120 and 2160p120 qualification

G30: export each SDK's fixed render dimensions and nominal presentation rate;
make native title metadata and SDL2 follow that profile. Preserve G25 releases.
Check all six supported height/rate pairs on the host; retain the legacy1080p60
SDL path for SDKs without metadata. Runtime drawable checks remain mandatory.

G31: freeze separate current 1440p120 and2160p120 SDK/apps. Per size, measure the
existing ImGui workload for30seconds, require at least114completedFPS, passing
pixels and clean restoration/teardown/health/unlock. Audit the same title's HDMI
negotiation independently. 1440p rendering scaled to4K HDMI is not1440p HDMI.

G32: qualify the updated SDL2 adapter with real-SDL host sanitizer contracts and
one bounded180-frame/two-pixel native check per size. Check relocated consumers.
Extend focused lifecycle checks after the display path passes; no full CTS rerun.

Use WSL, PPSA99005 native folders, existing title-aware runner and exact-token
lock on192.168.4.30. No graphics ELF injection, Settings or forced unsupported
modes. Stop on functional/lifecycle/health failure or suspected panic; preserve
receipts. Do not retry an unsupported display path. Direct sink confirmation is
separate from console logs. Keep changes local unless publication is requested.

## Milestones

- 2026-09-08 | G30 preparation | existing renderer supports both dimensions; SDL and generic native HFR metadata need SDK-profile integration | no console run yet.
