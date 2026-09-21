# Eden submission/retirement separation

Branch: `eden-framebuffer-fallbacks`. No hardware performance claim yet.

Reuses the runtime and retained-batch implementation from `6c5a35a` without
its unrelated compute fixture change, preserving the later direct-format,
scanout-flush and combined-query clear fixes. Unlike that capacity-only
integration, ordinary fence-free flushes now submit without immediately
retiring the submitted batch.

Mesa `st_glFlush` calls `st_flush(st, NULL, flags)`; `st_finish` requests a
fence. The latter still drains before creating a signaled fence. Memory
barriers explicitly drain rather than inheriting the new flush behavior.

One submitted batch retains its command allocations, descriptors, resource
references and per-draw occlusion buffers while the CPU builds its successor.
Overlapping CPU accesses, synchronous submissions, query operations and
teardown retain their completion waits. Failed submissions and timeouts
terminate before unsafe cleanup. Query values are collected at retirement.

Validation:

- `make test`: passed, including submission errors, delayed/wrong markers,
  timeout, resource aliases, presentation/shutdown and query isolation.
- Extended production-source lifetime check: empty flush stays nonblocking;
  unrelated CPU access does not retire; two batches retain their resources;
  alias access waits for both; occlusion values appear only after retirement.
- PS5 OpenGL 4.6 SDK cross-build: passed, output `build/sdk/async-flush`.
- `git diff --check`: passed.

No PS5 connection or deployment was made for this change. Eden's shared
token-owned console lock was restored separately in Eden commit `7ae2883`.
The installed candidate remains the previous combined-query clear build.

Counter begin/end and query enable/disable now preserve queued work. Only
reuse of an occlusion object referenced by queued or in-flight slots drains
before resetting its value. Primitive counters are accounted at draw-build
time; timestamps, query result reads and query destruction retain their waits.
Production-source checks cover successive queries, queued and in-flight reuse,
and reject mutations that remove reuse protection or restore global waits.
The full host suite and SDK cross-build passed (`build/sdk/query-scopes`).

Remaining major serialization points include query result reads,
clear entry, barriers and standalone draws. Do not interpret the previous
completion-poll wall time as hardware GPU execution time, or attribute a
speedup to this patch without a frozen candidate and a hardware comparison.
Audit these remaining waits before spending another full game run; the
current patch establishes bounded safe overlap rather than completing
asynchronous fences or query availability.
