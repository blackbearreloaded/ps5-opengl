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

- G9 candidate `891dab7`: full host checks and actual-helper ASan/UBSan scalar equivalence pass; console comparison pending.
