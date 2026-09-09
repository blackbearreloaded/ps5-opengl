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
- 2026-09-08 | G33 | a60a0cb | partial-pass: profile consumed, 119.883 FPS/cleanup pass; HDMI still 1080p120 | results/g33-title-profile-2160-20260908/display-report.json | inspect sink/settings.
- 2026-09-08 | G35 | 85f0a97 | pass: read-only installed identity; saved output requests match despite different HDMI | results/g35-prosperolight-installed-20260908/comparison.json | capture working stream.
- 2026-09-08 | G36 | a53133a | inconclusive: live ProsperoLight logs 1080p120; owner TV reports 4K120 | results/g36-prosperolight-live-20260908/comparison.json | reconcile sink evidence.
- 2026-09-08 | G36 resolved | 7e0c339 | partial-pass: TV now confirms 1080p; its game bar showed refresh, app HUD showed render size | results/g36-prosperolight-live-20260908/photo-review.json | check HDMI path.
- 2026-09-08 | G37 | f8f5949 | pass: unchanged 4K ImGui 119.884 FPS, HDMI 2160p119.88, pixels/restore/teardown/health/unlock | results/g37-hdmi4-opengl-2160-20260908/comparison.json | native 1440p next.
- 2026-09-08 | G38 | 31f77fa | pass: unchanged 4K SDL2 app, 180 frames/two exact pixels, HDMI 2160p119.88, restore/teardown/health/unlock | results/g38-hdmi4-sdl2-2160-20260908/acceptance.json | native 1440p awaits owner output selection.
- 2026-09-08 | G39 | e64574f | pass: 1440p 119.882 FPS, native HDMI/pixels/cleanup | results/g39-hdmi4-opengl-1440-20260908/display-report.json | SDL next.
- 2026-09-08 | G40 | b303873 | pass: SDL 1440p120, 180 frames/two pixels, native HDMI/cleanup | results/g40-hdmi4-sdl2-1440-20260908/acceptance.json | short display qualification complete.

## Current boundary and next check

The local SDKs `build/sdk/ps5-opengl-core33-g31-{1440,2160}p120` and their
matching SDL2 payloads now support the selected fixed render profile. ImGui's
30-second runs measured about 119.88 FPS at both sizes; the SDL checks establish
functional integration, not measured SDL frame rate or arbitrary-game performance.
No new exhaustive CTS or extended high-resolution soak is claimed. Published
G25 binaries remain unchanged; none of this work has been pushed or released.

The earlier HDMI1 runs negotiated 1080p119.88. After the owner moved the Hisense
55U78N connection to HDMI4, G37's unchanged original G31 OpenGL app rendered
3840x2160 at **119.883639 FPS** for 30.004094 seconds and negotiated
**2160P_11988**, restoring 4K59.94 afterward. Pixels, lifecycle and health passed.
The log consumed the original `attribute=0` profile (`HDR:x HFR:o`, `0x2a0057`);
the G33 multi-bit metadata change is unnecessary. No renderer fix or rebuild
was needed. The owner and logs confirmed 4K120 for the preceding ProsperoLight
control; a separate TV confirmation of the OpenGL run was requested, not assumed.

G38 also qualifies the unchanged G32 SDL2 2160p120 app on HDMI4: the drawable
was 3840x2160, all 180 frames completed, both exact pixel probes passed, and
console HDMI negotiated `2160P_11988` before restoring 4K59.94. Native teardown,
service health and exact-token release passed. This is a short SDL functional
check, not an SDL FPS measurement or a separate TV observation. The local
reused SDL auditor also rejects the earlier 1080p fallback as a 4K HDMI pass.

The owner selected 1440p and confirmed that resolution on the TV. G39 then
qualified the unchanged G31 ImGui app: **119.881660 FPS** for 30.004589 seconds,
native HDMI **1440P_11988**, restoration to 1440p59.94, passing pixels and clean
teardown/health/unlock. This is native 1440p, not a 1440p render scaled to 4K.
The owner's pre-run resolution observation is separate from the captured HFR
timing; no independent TV observation during this benchmark is assumed.

G40 completes the same short SDL check at native 1440p119.88: 180 frames, two
exact pixels, correct drawable, 1440p59.94 restoration and clean teardown,
health and exact-token release. Both resolutions now have an ImGui timing run
and an SDL functional run with matching console HDMI evidence. These four
bounded native cycles passed; extended high-resolution lifecycle/soak and
independent per-run TV refresh observations remain outside this qualification.
No renderer rebuild or full CTS rerun was needed. Keep the original profile
and request 15; do not force unsupported modes or change the owner's settings.

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

G33 outcome: the log changed to ProsperoLight's `HDR:o` / HDCP23 preference
and matching application/session attributes, so stale main metadata is not the
explanation for this run. HDMI stayed `1080P_11988`, restoring 4K59.94 afterward.
The title-profile difference alone is therefore insufficient; no production
metadata change is justified. The diagnostic folder remains local in the closed
PPSA99005 test slot; production source metadata and published SDKs are unchanged.
The owner subsequently confirmed the TV's own signal panel shows 3840x2160 at
120 Hz during an active ProsperoLight stream on the current setup (not its menu
or overlay); VRR is reported unsupported. Treat that as owner-observed support,
separate from our captured OpenGL negotiation. Do not repeat the same sink
question or infer that the 4K60 menu signal proves a 120 Hz limitation.

## G34: ProsperoLight source comparison (offline)

- `fa8e0b2` tried a separate preserve-current-resolution output API;
  `58a7e9a` removed it and used ordinary request 15, polling flip completion
  for HFR instead of waiting for another vblank. This pacing fix is not itself
  proof of HDMI resolution.
- `de95a0d` (01.000.050) removed the `requested_fps > 60` forced-1080p geometry
  fallback and changed `attribute3` from `0x40040` to `0x80040`. The retained
  main attribute is `0x62000000`. Sources: ProsperoLight
  `include/native_agc_output.hpp`, `sce_sys/param.json`, `src/native_agc_present.cpp`.
- Normal streaming tears down SDL video, waits 100 ms, then opens native
  VideoOut with the selected resolution/FPS. High-refresh configuration precedes
  flip-rate setup and buffer registration. Request 1 restores the default mode
  on close. VRR unpeg is used only for 90 FPS, not fixed 120.
- The earlier successful true-4K source oracle ran before SDL launcher setup,
  with SDR (`hdr=0`, format `0x8000000000000000`), 600 frames and HDMI
  `2160P_11988`. Therefore neither decoder activity, HDR buffers nor a prior
  launcher frame is established as necessary. Its receipt is in ProsperoLight
  `results/4k120-native-source-oracle-run/{agc-selftest.log,klog.txt,selftest.txt}`.
- ProsperoLight maps a 1440p stream into a 4K output target; that is not native
  1440p HDMI evidence. G33 already matches its 4K buffer size, request and title
  attributes. The current cause is not established by these static comparisons;
  next compare the working installed stream's actual transition with OpenGL on
  the same setup. Do not resurrect the removed mode API or ship HDR flags as a
  guessed fix. ProsperoLight's dirty performance worktree was inspected only.

## G35: installed identity and request comparison

WSL read-only inspection found installed ProsperoLight `01.000.051`, with both
executable and metadata byte-identical to its saved September 6 original backup.
This does not establish an exact source commit for that binary. No upload,
launch, close, stream, settings change or Remote Play session was performed;
declared services were healthy before/after, and the exact lock token was released.
Hashes and the reproducible local audit are retained in
`results/g35-prosperolight-installed-20260908/comparison.json` and
`.local/g35-verify.py`.

The historical working oracle and G33 both reached
`video mode(res:21ffffff ref:d opt:0x22)` with matching application/session flags;
their HDMI results still differ (2160p119.88 versus 1080p119.88). The new idle
capture contains no HDMI transition, so it cannot identify the current cause.
Capture the owner-selected working stream rather than autostarting an arbitrary
PC application. Do not replace the installed ProsperoLight or modify its dirty tree.
The old oracle wrapper is not a reusable current runner: its subsequent production
relaunch has a saved kernel-panic trace, documented upstream as a teardown/mount
race. Keep title-specific runtime-release verification mandatory.

## G36: owner-started live stream (read-only)

The owner started ProsperoLight and reported 4K120 on the TV. The capture ties
the live transition to PPSA99002, PID 1033: the same request as G33 selected
`1080P_11988 RGB444 ... 36bpp`; Shell UI separately reported 1920x1080 at
119.8801 Hz. A second capture contained no later mode change. Checksum-validated
saved preferences select 3840x2160, 120 FPS, HEVC HDR at 100 Mbps; preferences
are not proof of decoded frames or physical output. Metadata matches G35.

The two live observations conflict. Neither label this a fresh 4K120 console
pass nor dismiss the owner's TV report. The mismatch is no longer established
as OpenGL-specific. Retain both observations and inspect the requested TV
input-signal photo before selecting another code experiment. No production
code or display-auditor acceptance rule changed. Both read-only captures kept
services healthy and released their exact locks; the owner's stream was left
running, so no teardown or closed-cycle stability result is claimed. Reproduce
the local consistency audit with `.local/g36-verify.py`.

Photo/follow-up resolution: the supplied TV photo shows 120 FPS/HDR in its game
bar, but no input resolution. The 3840x2160/119.88 reading is ProsperoLight's
HUD. Its source passes `render_width`/`render_height` to `refresh_hud_surface`,
so that label is not an independent physical-output measurement. The owner then
switched away from HDMI 1 and back; the TV input banner showed **1080p**. This
agrees with the console log and supersedes the earlier owner-reported current
4K120 interpretation. Keep the earlier capture immutable; the supplemental
`photo-review.json` records the resolved evidence. Historical 4K120 receipts
remain separate and are not invalidated by this current-path result.

Model-specific blocker: the owner identified **Hisense 55U78N**. Visual inspection
of its [official quick-start guide](https://assets.hisense-canada.com/assets/ProductDownloads/486/f1ae562e0c/QSG-English-55-65-75U78N.pdf),
printed page 5 (PDF page 7), labels HDMI 1/eARC and HDMI 2 `4K@60Hz`, and HDMI 3/4
`4K@144Hz`. Its [user manual](https://assets.hisense-canada.com/assets/ProductDownloads/486/9bc8baf48f/English-User-Manual-55-65-75-85U78N.pdf)
describes Enhanced HDMI format for 4K HDR. This establishes a current-path port
limitation, not yet a successful port-change test. No app code or TV/console
configuration was modified by the agent.
