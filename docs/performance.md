# Performance

Performance results are tied to particular SDK binaries and workloads.
A supported OpenGL operation is not necessarily GPU-native, and an application's
completed-frame rate is not automatically the HDMI refresh rate.

## SDK 0.2.0 benchmarks

Measured on one firmware-6.02 console with a direct Hisense 55U78N HDMI4
connection. Each workload ran for 30 measured seconds using the
[qualified 4K SDK](release-g62.md#frozen-identities).

| Workload at 3840×2160 | Completed FPS | Scope |
| --- | ---: | --- |
| ImGui window | 119.883 | Native 2160p119.88 HDMI; normal output restored on exit |
| ImGui offscreen, case 11 | 119.897 | Completed offscreen work; frame p95 9.137 ms |
| 128 cubes, ordinary draws | 58.09 | Low-poly, depth-tested draw-overhead workload |
| 128 cubes, instanced draws | 117.41 | Same scene using instancing |

The cube scene contains 1,536 triangles and two procedural 2×2 textures. It is
not a large-texture bandwidth test, a full game or a hardware ceiling.
Instancing and ordinary-draw batching are different mechanisms; batching does
not automatically convert application draws into instanced geometry.

The separate two-minute normal session measured 113.6 FPS in its first
30-second window and 119.83–119.87 FPS afterward. Tracked memory acceptance
passed, but startup-inclusive cadence is a **partial pass**. Short benchmark
averages must not be presented as perfect frame pacing or multi-hour stability.

The 1440p SDK in this release is host-checked only. Earlier profiles have their
own release-specific measurements; those are not substituted for new binaries.

## Current-source lifecycle regression

The [runtime reopen safeguard](lifecycle-reopen.md) was checked separately:
3,597 measured 4K window frames in 30.004338 seconds, **119.882665 FPS**.
Native HDMI negotiation matched 2160p119.88 and returned to 59.94 Hz on exit.
No reopen delay occurred in this first-open, steady-rendering workload.

This does not rerun or transfer the 0.2.0 offscreen, cube or CTS qualification
to the new source. The five-second guard applies only when reacquiring a
previously closed high-refresh presenter in the same process.

## Accelerated paths and fallbacks

The runtime batches eligible ordinary/multi-draw work and presentation while
retaining resource references and explicit completion boundaries. Eligible
color/depth transfers, resolves, clears, native color formats and mip/layer
operations use GPU paths. Eligibility depends on format, sample count, layout,
state and resource ownership; other paths retain CPU staging or conversion.

The 0.2.0 focused suite checks 22 blit/resolve cases, 46 clears, six format
cases and eight mip/layer cases, including state and neighboring-image
preservation. These counts describe qualification, not acceleration of every
possible combination. Shader compilation, API validation, some conversions and
synchronous completion waits still contribute CPU time.

Keep a live EGL window/context during normal gameplay when possible. Unnecessary
readbacks, explicit completion calls and full context recreation can dominate
otherwise small rendering workloads. Profile before changing synchronization;
removing a wait without preserving ownership is not a valid optimization.

For a synchronous diagnostic control, rebuild a **separate** SDK with:

```sh
PS5_DEFERRED_DRAW_BATCH=0 PS5_MULTIDRAW_BATCH=0 make sdk
```

Do not change or rebuild a frozen package during a hardware campaign.

## Display resolution and refresh

Render dimensions, completed-frame throughput and negotiated HDMI mode are
separate measurements. A 4K framebuffer or an FPS overlay does not prove 4K HDMI.

The reference setup qualified native 1440p120 and 4K120 when the console output
selection and display input supported the requested profile. The same 4K app
negotiated 1080p120 on one connection and 4K120 after moving to the TV's capable
HDMI input, without an application rebuild. Check the entire signal path,
including capture cards, receivers and cables.

Use `tools/summarize-display.py` to inspect captured console HDMI negotiation,
and independently check the display's input-signal information when needed.
Console logs are not per-frame panel measurements. No generic 90-Hz or arbitrary
resolution/refresh guarantee follows from the qualified 120-Hz profiles.

## Reproducing a comparison

Use the [ImGui benchmark](../examples/core33-imgui/README.md) or
[cubes profile](../examples/core33-cubes/README.md#opt-in-matched-profile).
Record the source, SDK manifest/runtime hashes, scene, dimensions, warm-up,
duration, logging settings and completion mode.

Compare one changed variable at a time. Keep numerical rendering checks outside
the timed interval and require completion, clean teardown and service health.
Report frame-time percentiles and misses alongside average FPS. API wall time
can include driver waits; it is not isolated GPU execution time.

A 30-second profile is suitable for a focused comparison; the standard sustained
check is bounded at two minutes. Neither is a claim about every application.
See [testing](testing.md) and [limitations](limitations.md).
