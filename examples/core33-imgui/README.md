# Installed-SDK Dear ImGui renderer check

The September 7 final SDK passes the six-frame oracle below. Its separate
five-minute 1080p demo produces 5,997 frames (~19.99 FPS), 11 passing readbacks
and 5,997 successful two-draw batches. See [current validation](../../docs/validation.md).
The later opt-in windowed candidate reaches ~119.88 FPS at 1080p, 1440p and 4K
in 30-second runs; see [high-refresh builds](#high-refresh-window-benchmark-opt-in)
and [measurement limits](../../docs/performance.md#g5d-verified-high-resolution-120-fps-candidate).

Uses unmodified [Dear ImGui](https://github.com/ocornut/imgui/tree/v1.91.9b)
v1.91.9b, commit `f5befd2d29e66809cd1110a152e375a7f1981f06` (MIT), including
its upstream OpenGL3 backend. Source stays in the ignored dependency checkout.

```sh
make source-fetch
make sdk
bash tools/build-native-test-app.sh egl_public_core33_imgui
bash tools/test-imgui-host.sh
python3 tests/ps5/test_imgui_egl_cleanup.py
```

The builder verifies the dependency commit/clean tree and installed SDK
manifest, compiles with the native boilerplate's Clang 18 wrapper, and links
only the installed GL package. The backend's supported custom-loader option
uses exported public GL functions; no backend source patch or private GPU
header is needed. Relocatable linking uses plain `ld.lld-18` because the SDK
link wrapper injects a final-executable linker script even for `-r`.

Run with `tools/Run-NativeOpenGLGate.ps1`, frozen hashes/commits,
`-ExpectedGate egl_public_core33_imgui.o -Incremental -ObservationSeconds 60
-ObservationStopText '[ps5-imgui] finished'`.

Acceptance: six passing frames at 320x240 and 640x480, device-object
recreation, 60 exact/toleranced color probes, font alpha coverage, and restored
GL bindings/enables/blend/viewport/scissor/polygon state. Shapes, clipped
geometry, a moving uploaded image, text, and standard widgets all use ImGui's
draw lists and renderer. The validated image is also presented, but screenshots
are not the oracle. Require completion status 0 and clean native teardown,
post-health, and exact-token release. EGL cleanup failures also fail the app;
the host regression injects each cleanup error and checks that all cleanup
calls are still attempted and earlier rendering failures remain failures.

The same six-frame oracle runs on host software Mesa (EGL pbuffer instead of
the native window). The default bitmap font has binary coverage at 1x; only
2x requires partial alpha. Frames 1 and 4 separate the two overlapping quads
into distinct draw commands, while the other frames keep upstream batching.
Frames 2 and 5 attach renderbuffers to compare direct tiled storage with the
texture staging path. The first nongray text pixel is logged on failure.
Pixel mismatches collect the rest of the bounded batch but preserve failure;
GL/setup/presentation errors stop immediately.

This is one renderer integration, not CTS coverage, a complete input backend,
or a claim that every existing OpenGL application is compatible.

## Same-process EGL lifecycle check

`bash tools/test-imgui-host.sh --lifecycle` runs the complete six-frame example
three times in one process. The native target is
`bash tools/build-native-test-app.sh egl_public_core33_imgui_lifecycle`.
It reuses the existing oracle without changing the renderer or driver: each
session initializes EGL, creates its surface/context, renders six checked frames,
destroys everything and terminates EGL. Host and native commit `b0235c5` pass
18 frames/180 probes, including three successful presenter closes and clean title
teardown. Busy unregister remains. Stop observation only on
`[ps5-imgui-lifecycle] finished`, not an inner session's completion marker.
This is bounded recreation coverage, not exhaustive leak or device-loss testing.

## G10: visible TV demo

```sh
bash tools/test-imgui-host.sh --tv-demo
bash tools/build-native-test-app.sh egl_public_core33_imgui_tv
```

The separate `imgui_tv` target preserves the six-frame validation oracle and
uses the same installed SDK and unmodified upstream renderer. It draws directly
to the 1920x1080 EGL window: large text, an animated circle, blended rectangles,
a triangle, a speed slider, and palette buttons. D-pad navigates/adjusts, Cross
selects, and Circle backs out of a widget. No controller is required to watch.
The native controller ABI subset is based on the independently authored
`ps5-input-investigation/include/ps5_pad.hpp`; only standard current-state
input is used, with disconnected/intercepted input neutralized.

Each launch runs for five minutes at a maximum requested 30 FPS, then releases
GL/EGL and controller resources. Use the existing locked native-folder runner
with `-ExpectedGate egl_public_core33_imgui_tv.o -Incremental
-ObservationSeconds 330`, recording its usual commit/hash pins. It closes the
title at the end of that bounded window. The installed folder is still
`/data/homebrew/PPSA99005`; no application ELF is sent to elfldr.

Host acceptance: 12 full-HD frames across simulated elapsed times 0..275 seconds,
ten exact shape readbacks, two gamepad-driven checkbox changes, and successful
EGL cleanup. Hardware acceptance: shape readbacks at frames 0/10 and every
30-second progress interval, sustained frame receipts, TV-visible animation,
and clean teardown. The separate 30-second profiling mode retains two probes.
Controller hardware interaction requires observing a widget change, not merely
opening a pad handle. No CTS rerun is needed for this example-only change.
The control is the previously validated six-frame ImGui app and frozen SDK;
only the example is changed. The recorded run used firmware 6.02. Managed runs
require an explicit `-Ps5Host` and the [testing prerequisites](../../docs/testing.md).
Stop on any render/presentation error or uncertain console health.

- 2026-09-06 | G10 | ca0dbf4 | 6.02 | pass: TV-confirmed animation, 2851 frames/300s (~9.5 FPS), clean teardown; controller connected, zero recorded widget changes.
- 2026-09-07 | final SDK | 6.02 | pass: 5997 frames/300s, 11 readbacks, 5997 two-draw batches; clean teardown/health/unlock; no fresh visual or controller interaction claim.

The [validation report](../../docs/validation.md) distinguishes the frozen runs;
raw device receipts remain local. Thirty FPS is a pacing ceiling, not measured
throughput. The user confirmed the earlier TV demo worked; the final campaign
used numeric rendering/lifecycle checks and recorded no widget changes. Host
navigation checks passed. VideoOut still reports busy unregister followed by
successful close and runtime-layer release.

## Matched windowed benchmark

`PS5_IMGUI_WINDOW_BENCHMARK=1` extends the original TV demo, not the offscreen
scene below. Keep the separate frozen GPU-presentation SDK selected; the native
builder always rebuilds the example when changing its benchmark flags:

```sh
bash tools/test-imgui-host.sh --window-benchmark
PS5_IMGUI_PROFILE=1 PS5_IMGUI_WINDOW_BENCHMARK=1 PS5_IMGUI_WINDOW_TARGET=60 \
  bash tools/build-native-test-app.sh egl_public_core33_imgui_tv
python3 tools/summarize-imgui-profile.py RECEIPT --window-target 60
```

The native run excludes 30 warm-up frames and measures the next 30 seconds.
It preserves the two warm-up pixel probes and the original clear/draw/swap
ordering, with no added timed readback or `glFinish`. Active time includes swap;
completed frame intervals additionally include pacing and loop overhead. Both
have nearest-rank percentiles and missed-budget counts (0.25 ms tolerance for
frame pacing). The 60 FPS target uses the original swap pacing; target 30 adds
bounded deadline pacing. Controller changes invalidate a native benchmark.
Host checks run ten measured frames without pacing and retain simulated input.

Use the usual frozen, locked `imgui_tv` cycle with a 60-second observation cap.
Require the new harness to reproduce 59–60.5 FPS at 1080p before extending it.
Separate SDK builds can select `PS5_SCANOUT_HEIGHT=1080`, `1440` or `2160` when
running `toolchain/install-ps5-opengl-core33.sh SEPARATE_PREFIX`. Select that
frozen prefix with `PS5_OPENGL_PREFIX` for the app build. All layers use matching
render dimensions and display-buffer strides. The scene stays logically
1920x1080 and scales to the queried surface. Host checks select the same size
with `PS5_IMGUI_HOST_HEIGHT`; the auditor uses `--window-height`.
Profiled native SDKs additionally support `--output-status`, requiring matching
registration/offset receipts and two raw VideoOut status snapshots. The default
60 Hz build requests no output reconfiguration; high-refresh builds are separate.
No independent physical output-mode or GPU-timer claim follows from the
application timing. The accepted release SDK remains unchanged.

### High-refresh window benchmark (opt-in)

After the [normal dependency and SDK build](../../docs/building.md), build a
separate runtime and native app; do not overwrite a frozen validation SDK:

```sh
PS5_DRAW_PROFILE=1 PS5_GPU_PRESENT_BATCH=1 \
  PS5_SCANOUT_HEIGHT=2160 PS5_SCANOUT_FPS=120 \
  bash toolchain/install-ps5-opengl-core33.sh build/sdk/ps5-opengl-core33-2160p120
PS5_OPENGL_PREFIX="$PWD/build/sdk/ps5-opengl-core33-2160p120" \
  PS5_IMGUI_PROFILE=1 PS5_IMGUI_WINDOW_BENCHMARK=1 PS5_IMGUI_WINDOW_TARGET=120 \
  bash tools/build-native-test-app.sh egl_public_core33_imgui_tv
python3 tools/summarize-imgui-profile.py RECEIPT \
  --window-target 120 --window-height 2160 --prepare-profile
```

Use the same locked native-folder protocol, `PPSA99005` and 60-second observation
cap as above. The builder supplies the benchmark's high-refresh title metadata;
the runtime checks output support, requests fixed 120 Hz and restores normal
output at shutdown. Do not change console Settings or retry an unsupported mode.
For 1080p or 1440p, change the runtime height, separate prefix and auditor height
together. Every rebuild creates a new candidate requiring its own verified run.

The original scene averages ~119.88 FPS at all three render sizes on the recorded
firmware-6.02 console; frame intervals still vary. Both VideoOut APIs report
3840x2160 at 119.88 Hz even for the lower render sizes. These are not three
independently verified HDMI modes. No fresh physical display/input check is claimed.
Target 90 changes only the app flag to `PS5_IMGUI_WINDOW_TARGET=90` and the auditor
target to 90, retaining the 120 Hz SDK. It uses application pacing, not VRR or
native 90 Hz. The latest 4K90 candidate has not been measured; do not infer a
completed twelve-target matrix from the three new 120 FPS results.

To audit the HDMI negotiation separately, retain the exact runner receipt and
its adjacent `-klog.log`, `-result.json` and `-runner.json` files:

```sh
python3 tools/summarize-display.py PATH/PPSA99005-TIMESTAMP-opengl.log --height 2160
```

The audit requires one clean native-title cycle, a stable captured high-refresh
mode and restoration to 60 Hz. It reports `verified-match`, `verified-mismatch`
or `inconclusive`; a successfully measured mismatch is not a failed renderer.
A 3840x2160 render buffer or VideoOut status alone does not prove 4K HDMI output.
The HDMI log is still console-side evidence: TV/capture-device signal information
is a separate check, and a capture card can constrain negotiation. Do not change
console Settings or force a mode unsupported by the connected display path.

## Bounded offscreen performance matrix

```sh
bash tools/test-imgui-host.sh --benchmark
# Select a separately frozen SDK via PS5_OPENGL_PREFIX; do not replace the accepted SDK.
bash tools/build-native-test-app.sh egl_public_core33_imgui_benchmark
python3 tools/summarize-imgui-benchmark.py RECEIPT
```

The distinct lightweight UI workload measures 1080p, 1440p and 2160p at target
rates of 30/60/90/120 FPS in one launch. Each case warms for at most 30 frames
or one second (at least two completed frames), then runs
30 measured seconds; `glFinish` confirms GPU completion per frame. Three pixels
(clear, opaque geometry, alpha overlap) are checked before and after measurement.
The host check shortens each case to two warm-up and six measured frames without
pacing; its timing is not a PS5 performance prediction.

Only a static 1080p preview is presented between cases. The measured rendering
uses offscreen RGBA8 buffers: **no 1440p/4K or 90/120 Hz display claim** follows
from these results. See the [measurement scope](../../docs/performance.md).
Use the locked native-folder runner with gate `egl_public_core33_imgui_benchmark.o`,
`-Headless -Incremental -ObservationSeconds 450` and its normal commit/hash pins.
No screenshots or controller input are required. Stop on correctness, retirement
or lifecycle failure; missing an FPS target alone is an ordinary benchmark result.
