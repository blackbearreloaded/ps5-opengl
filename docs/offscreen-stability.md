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
- G10 instrumentation: owned-heap live/peak bytes, blocks and failure/ambiguity flags; host allocator and ImGui TV/three-session lifecycle checks pass. Hardware pending.
- G9 `891dab7`: 19.981406 FPS, 600 frames/30.027917 s (+41.8% vs matched control); 1,200 draws, probes, teardown/health pass (`results/g9-fbo-copy4-20260907`).

G9 render mean/p95/p99: **50.042 / 51.066 / 51.472 ms**, versus
70.942 / 83.484 / 85.288 ms for the control. This is completed offscreen
throughput, not TV refresh or a game-FPS claim. Full-surface CPU transfers remain;
the 60 FPS target is not met. The published SDK is unchanged.

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
