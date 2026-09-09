# G42: bounded SDL physical input and reconnect

Hardware status: **focused physical-input/reconnect pass**, 2026-09-08, one
firmware-6.02 console. Parent operated PPSA99005 and the owner performed and
confirmed the physical actions and screen colors. No inherited G19/G25/G32 or
historical campaign acceptance applies.

Frozen source `9c124202845b9b6acdfe831d62151e18b0cb514b`, eboot SHA-256
`6e4ee48c8970dd956d829e984a2b1930f96e0c2cdb2c350b30623c422680eab9`:
Cross press/release and left-stick travel/center passed on instance 0, removal
at 23.956 seconds, reconnection as instance 1 at 28.101 seconds, then fresh
button/axis activity completed at 38.336 seconds. The 2560x1440 drawable matched
captured HDMI 1440p119.88; output restored to 1440p59.94. SDL cleanup, native-title
teardown, service health and exact-token release passed. Local evidence:
`results/g42-input-1440-20260908/PPSA99005-20260908-212312-opengl-g42-audit.json`.
This is one controller/user, not all-button, multi-controller, or 4K input coverage.

G42 criterion: within 120 seconds, observe a nonvirtual controller, button
press/release and deliberate stick motion, its removal, a new attached instance,
then fresh button and stick activity. Parent must independently corroborate the
physical actions and verify title teardown, display restoration and service
health. Timeout or quit is incomplete; rendering alone establishes no input.

One variable: opt-in SDL input consumer and necessary PS5 joystick ownership
correction on the existing fixed-profile adapter. Control: original 180-frame
consumer and frozen G32 folders. Reuse unchanged canonical G31 1440p120 SDK.
Only integration/SDL2, tools/test_sdl_sdk.py and this document are in scope.

Offline checks: real SDL event queue and PS5 backend with explicit platform
doubles under ASan/UBSan, supported/failure/reinitialization sequences, virtual
and inconsistent synthetic-event rejection, receipt compatibility and native
folder verification. No console access or environment lock during this work.

## Parent actions and run

Use the frozen `build/folder-1440p120-input-v1/dist/PPSA99005` from this clone,
after checking every file against its stage-root `candidate.json`. Build commands,
source/receipt/artifact identities and offline results live in
`build/g42-evidence/`; these ignored files accompany this local handoff.
Do not substitute a rebuild or the original G32 folder.

1. Parent owns the protocol cycle and lock. Keep the recorded 1440p output;
   this case needs no 4K switch. Start with one physical controller connected,
   Cross released and left stick centered. No Remote Play or injected input.
2. Use the TV's solid colors; stdout/READY/phase-ready is **not visible live
   through the runner**. On the initial **muted red** screen, hold **Cross for
   about one second**, then release. Wait for **green**. Hold the **left stick
   fully right for about one second**, then center it. Wait for **cyan**.
   Those holds keep both edges from collapsing into one polled SDL state.
3. **First-cycle cyan is the ready-to-disconnect cue**: it requires both the
   Cross press/release and stick travel/return. Disconnect/power off that
   controller using the owner's method; wait about two seconds with it off.
   Unplugging USB alone may leave Bluetooth connected. Cyan stays on screen
   while waiting; it is not itself proof that SDL observed removal.
4. Reconnect the same controller to the same logged-in user, initially neutral.
   Wait for **dark gray**: this reset color identifies the new neutral instance,
   distinct from first-cycle cyan. If it stays cyan, the required reconnect
   has not been observed; do not count another button press as a second phase.
   From dark gray, hold Cross about one second, release, wait for green; hold
   left stick fully right about one second, then center. The completed second
   sequence immediately closes the drawable; a second cyan frame is not
   promised. Parent verifies completion afterward from the fresh log.
   Do not log out/switch users or open Settings.
5. Input deadline: **120 seconds after SDL initialization**. Runner observation:
   **135 seconds**, allowing startup/cleanup margin. If no end record appears,
   classify incomplete/inconclusive and close the exact title; do not extend.

Parent-only PowerShell recipe, after normal protocol lock and preflight:

```powershell
& "$protocol/scripts/Invoke-Ps5Cycle.ps1" -TitleId PPSA99005 `
  -AppDirectory "$clone/build/folder-1440p120-input-v1/dist/PPSA99005" `
  -Ps5Host $recordedHost -Headless -UseExistingFolderRegistration `
  -ObservationSeconds 135 -ResultsDirectory $newResultsDirectory
```

Use a new results directory and upload the complete folder. `Headless` leaves
Chiaki alone; parent must ensure no other input producer contaminates this case.
App stdout/stderr goes to `/download0/pss-opengl.log`, not necessarily klog:
do **not** use its text as the runner's klog `ObservationStopText`. Collect the
fresh file using the parent's existing download-data workflow after the cycle,
along with klog/lifecycle/service and exact-token-release evidence. Do not nest
a self-locking download utility inside the protocol lock.

Require one `START source=native-driver-candidate`, matching READY drawable,
two connection phases with distinct IDs, a verified removal between them,
button states 1 then 0 and qualifying axis travel then center in **both** phases,
`END status=0 reason=sequence-complete-awaiting-parent`, and native
`gate completed status=0`. Require title-specific stop/runtime release and
display restoration/healthy services independently. The app intentionally
keeps `hardware_accepted=0`; SDL has no event-origin authentication, so matched
state alone cannot distinguish a well-matched injection or Remote Play input.
Only parent-observed physical actions establish physical input acceptance.

Status 2 is incomplete (timeout, quit, early disconnect or nonneutral input);
status 1 is a functional/cleanup failure. Both still use deterministic SDL
cleanup and the existing parent-controlled main-return hold. No GPU timing,
extended soak, multi-controller coverage or 4K input qualification is implied.

- 2026-09-08 | G42 | pass: host sanitizer/receipt contracts plus owner-confirmed native button/axis/disconnect/reconnect sequence; clean/healthy/unlocked | local receipt above.
