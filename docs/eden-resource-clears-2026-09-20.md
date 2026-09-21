# Resource hazards and queued GPU clears

OpenGL candidate `13e1a62`, branch `eden-framebuffer-fallbacks`.

- Texture CPU accesses now check retained backing allocations instead of
  always draining. Checks cover primary/stencil aliases in both directions;
  missing or invalid spans still conservatively synchronize.
- A hazard confined to the in-flight batch retires that batch without
  submitting unrelated queued work.
- GPU color clears no longer start with a global drain. Conditional rendering
  retains its result wait; depth/stencil operations and CPU color fallbacks
  synchronize the affected attachments before access.
- Tracking remains at allocation granularity. Byte ranges, mip/layer access,
  binding-change drains and additional GPU copy/upload paths remain future work.

Validation: full `make test`, PS5 SDK cross-build, and Eden native build/package
checks passed. Production-source tests cover unrelated texture uploads,
primary/stencil aliases, queued versus in-flight ownership, GPU clear ordering,
CPU fallback ordering, conditional rendering and failure/no-replay behavior.

Eden candidate `1d2038d` is frozen with this SDK and the prior offline profiler
fix. Case `results/headless-fw602-20260920-215916` acquired the shared lock but
found `PPSA99003` active at foreground preflight. No deployment or launch occurred;
services remained healthy and the owned lock was released. No performance result
is claimed. The installed Eden package remains the previous `9c8bed4` candidate.
