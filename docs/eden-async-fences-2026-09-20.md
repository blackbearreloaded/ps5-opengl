# Eden asynchronous fences and bounded in-flight batches

Previous fence creation drained all GPU work and returned an already-signaled
fence. Native/Gallium queues retained only one submitted batch, so another flush
also waited for completion. The new matching FIFO queues retain up to eight
batches (up to256 draws each), retiring only the oldest on capacity pressure.
Each batch retains command allocations, descriptor snapshots, resources and query
storage until its native completion markers have been observed.

Fences capture a monotonic submission sequence under the queue lock. Creation
submits without draining; polls retire ready prefixes without blocking on the
GPU. Finite waits check elapsed monotonic time and preserve resources on timeout;
infinite waits poll at100us with the driver lock released between polls. Mutex
acquisition/driver cleanup can add overhead beyond the requested GPU-wait timeout.
Fence references are atomic. Old fence identities survive FIFO slot reuse.

CPU/resource hazards and query reuse scan every retained batch. Teardown and
presentation still drain. Vertex-storage shaders remain synchronous because their
storage bindings are not fully retained by deferred snapshots. This change does
not remove all barriers, CPU staging, or synchronous standalone draws.

Production-extraction checks cover native/Gallium FIFO wraparound, full-queue
rejection preserving staged ownership, delayed/out-of-order markers, resource
pins, non-head CPU hazards, zero/finite/infinite fence waits, old-fence isolation
and failure-before-free behavior. Full host suite and PS5 SDK cross-build pass.

Hardware comparison will retain Eden's loading/FPS UI and CPU/JIT configuration,
using `headless-fw602-20260920-231117` as the historical control. Bounded native
`[ps5-inflight]` peak messages establish whether the multiple-batch path ran.
No speedup or hardware cache-visibility qualification is claimed offline.
