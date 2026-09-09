# G42: bounded SDL physical input and reconnect

Offline candidate only; hardware status is **no-run**. Parent alone operates
PPSA99005 on the environment recorded in canonical `.local/ENVIRONMENT.md`.
No inherited G19/G25/G32 or historical campaign acceptance applies.

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
