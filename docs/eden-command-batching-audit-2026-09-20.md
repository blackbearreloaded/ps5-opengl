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
