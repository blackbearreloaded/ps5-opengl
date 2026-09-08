# Offscreen performance and sustained stability

## Validation plan

| Gate | Change | Acceptance |
| --- | --- | --- |
| G9 | Four-pixel copies in 32-bit color staging | Host scalar equivalence including tails/mips/layers; unchanged 30-second 1080p ImGui offscreen case 1 improves against the published SDK control; pixel probes, draw completion and clean teardown pass. |
| G10 | Native-app memory observations | Longer normal TV session and repeated EGL/native-title lifecycles; distinguish live allocation growth from fixed heap reservation, and require rendering/teardown checks. |

Keep game changes separate. Freeze each committed app/runtime before testing;
retain the released SDK as the control. One bounded native-folder run per lock
acquisition, idle preflight, remote hash verification, exact-title teardown and
healthy services before releasing only the owned token. Stop on errors or an
ambiguous foreground. No routine screenshots, fault injection or automatic full
39,544-case rerun. Raw receipts and candidate hashes stay in ignored results/build
directories; add only milestone summaries here.

## Milestones

- G9 candidate `891dab7`: full host checks and actual-helper ASan/UBSan scalar equivalence pass.
- G9 published-SDK control: 14.095217 FPS, 423 frames/30.010180 s; probes, teardown and health pass (`results/g9-fbo-control-20260907`).
- G10 instrumentation: owned-heap live/peak bytes, blocks and failure/ambiguity flags; host allocator and ImGui TV/three-session lifecycle checks pass.
- G9 `891dab7`: 19.981406 FPS, 600 frames/30.027917 s (+41.8% vs matched control); 1,200 draws, probes, teardown/health pass (`results/g9-fbo-copy4-20260907`).
- G10 `725f6eb`: first native launch passes 18 frames/three EGL sessions; post-cleanup heap 9,375 bytes/21 blocks each time, zero inter-session growth (`results/g10-lifecycle-20260907/run1`).
- G10 `725f6eb`: 600 s / 35,942 frames / 21 probes pass; 59.87–59.93 FPS per 30 s window, clean teardown/health (`results/g10-soak600-20260907`).
- G10 memory: +448 bytes/+2 blocks by 90 s, then flat through 570 s; peak 9,413,967 bytes; cleanup 9,455 bytes/22 blocks, zero failures/ambiguity.
- G10 repeated launches: **1/5 complete**; next cycle no-run because another title was active. No upload/launch; healthy services and exact-token unlock (`results/g10-lifecycle-20260907/run2`).

G9 render mean/p95/p99: **50.042 / 51.066 / 51.472 ms**, versus
70.942 / 83.484 / 85.288 ms for the control. This is completed offscreen
throughput, not TV refresh or a game-FPS claim. Full-surface CPU transfers remain;
the 60 FPS target is not met. The published SDK is unchanged.

The soak's owned heap stabilized at 8,958,261 bytes/20,708 blocks. Its receipt
records 112 widget changes, so this was a normal interactive session, not an
unchanged-input performance comparison. The early 448-byte increase is observed,
not attributed to a specific allocation site. Ten minutes and one native launch
do not establish multi-hour, GPU-memory, process-RSS or device-loss stability.

## G10 checks

Build the existing TV gate with `PS5_IMGUI_PROFILE=1`,
`PS5_IMGUI_PROFILE_SOAK=1`, `PS5_IMGUI_SOAK_SECONDS=600` and an explicitly frozen
`PS5_OPENGL_PREFIX`. Audit with `tools/summarize-imgui-profile.py RECEIPT --soak
--soak-seconds 600` and `tools/summarize-app-heap.py RECEIPT --steady-samples 20`.
Then build the existing `egl_public_core33_imgui_lifecycle.o` gate and run five
separately locked native-title cycles; each performs three complete EGL sessions.
Audit each heap receipt with `--sessions 3` and require all 18 frame oracles and
three EGL cleanups. Compare post-session live bytes and blocks across sessions
and processes; report any growth, not just whether allocations fit in the heap.

Memory observations exclude GPU mappings, foreign heaps and process RSS; ambiguous
zero-size realloc behavior invalidates the accounting instead of changing allocator
semantics. Flat owned-heap samples alone are not proof of a leak-free driver.
