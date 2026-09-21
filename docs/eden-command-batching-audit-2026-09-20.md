# Binding waits and command batching

Branch: `eden-framebuffer-fallbacks`. Offline only; installed Eden still uses
OpenGL `13e1a62` from the resource/clear comparison.

Binding callbacks validate the whole update before changing references. They
now drain only when a non-compute buffer/image binding actually changes.
Buffer comparison includes resource, offset and size. Image comparison includes
the complete bound view; padding differences conservatively preserve a wait.
Repeated empty bindings ignore irrelevant metadata. The existing invalid-state
recovery and reference updates still run for identical valid bindings.

Full host tests and PS5 SDK cross-build passed (`build/binding-noop-host.log`,
`build/binding-noop-install.log`). Added production-source tests cover identical
buffers/images, empty updates, invalid-then-identical recovery, changed image
access metadata, trailing unbinds and reference counts. Actual binding changes
retain their synchronization; this does not introduce asynchronous storage draws.

## Submission audit

`runtime_batch_queue` retains each draw's independent allocation and submission.
`ps5_agc_gate2_batch_submit` loops over those submissions. Each draw allocates
direct memory, copies shader code, builds commands at offset `0x8000`, and appends
both the color-cache release and its own completion-marker release before being
queued. Batch retirement checks all markers before releasing allocations.

The native runtime identifies event45 as FLUSH_AND_INV_CB_DATA_TS. The backend
also emits explicit color/depth-to-texture barriers. These dependency barriers
must survive any reduction of per-draw releases. Keeping all existing tails and
merely concatenating commands would reduce submit calls but retain their cache
and completion operations; measured submit CPU time alone does not justify that
as the main performance intervention.

Next coherent implementation: separate a draw's command body from its retirement
tail, build bounded batch command storage, preserve required inter-draw resource
barriers, and retire retained resources from batch completion. Preserve the
synchronous standalone path, presentation order, failure ownership and bounded
timeouts. Validate render-to-texture, depth sampling, blending, alternating
targets, query collection and readback after the batch. Do not remove cache
operations based solely on fewer API calls or assume polling time proves GPU
saturation. No hardware run should be spent solely measuring this no-op binding
change; combine it with a validated submission change.

## Implemented candidate

OpenGL `3509265` implements bounded grouping within an existing leader draw's
command allocation. Capacity stops at `command.down`, preserving separately
reserved data. Compatible adjacent command bodies are copied into that space;
all original allocations remain retained. Interior completion-release tails
are omitted, leaving the final draw's tail and marker per group. Color releases
and explicit texture/depth dependency barriers remain in each body. This moves
the completion release's cache operations to group boundaries and therefore
requires GPU cache-visibility qualification, not only host ownership tests.
Unannotated streams, incompatible flags and capacity boundaries retain separate
submissions. Appended presentation commands and their final marker stay last.

Full host suite and PS5 SDK build passed. The new production-source host test
covers1..256 draws, bounded groups, exact capacity, opt-out/flag boundaries,
presentation tails, delayed completion, and submit/suspend/timeout failure
without premature cleanup. It does not simulate GPU caches.

Eden candidate `2699169` includes grouping and the prior binding no-op fix.
Case `results/headless-fw602-20260920-223124` acquired the shared lock, then
foreground preflight found `PPSA99003` active. No deployment or launch occurred;
post-case services passed and the owned lock was released. Installed Eden
remains the resource/clear candidate. Hardware correctness/performance pending.
