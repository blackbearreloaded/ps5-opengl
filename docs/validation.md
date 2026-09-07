# OpenGL 3.3 Core validation report

**Campaign completed: September 7, 2026.** The updated native runtime passed a
fresh, frozen four-configuration CTS campaign and final installed-SDK consumer
checks. This is project acceptance, **not Khronos certification**.

## Results

| Configuration | Actual target | Pass | Reviewed NotSupported | Accounted |
| --- | --- | ---: | ---: | ---: |
| 0 | 64x64, D32S8, seed 1 | 9,351 | 535 | 9,886 |
| 1 | 113x47, D32S8, seed 2 | 9,351 | 535 | 9,886 |
| 2 | 64x8192, D24S8 FBO, seed 3 | 9,351 | 535 | 9,886 |
| 3 | 8192x64, D24S8 FBO, seed 3 | 9,351 | 535 | 9,886 |
| **Total** | | **37,404** | **2,140** | **39,544** |

No gaps, duplicates, required-case failures, warnings, waivers or incomplete
results remain in the accepted set. Each exclusion has an exact expected message
and pinned-source review in [candidate.json](../validation/2026-09-07/candidate.json).
They concern optional/newer features or upstream-defined inapplicable context,
format and access combinations—not accepted omissions in mandatory Core 3.3.
**39,544 accounted results does not mean 39,544 passes.**

Full upstream swizzle matrices and LOD-bias workloads ran in every configuration.
The strict audit passed without `--allow-incomplete`. Measured timings sized
contiguous batches; no workload reduction, old-binary passes or incomplete
receipts filled coverage. All 15 CTS cycles completed uneventfully with verified
foreground ownership, native teardown, healthy services and exact-lock release.

## Acceptance lanes

| Lane | Result |
| --- | --- |
| Core command structure and linked exports | 344/344; structural evidence, not 344 separate GPU tests |
| Make, pkg-config and CMake consumers | Compile/link passed with the final installed SDK |
| Frozen CTS matrix | 39,544 accounted; four complete configurations |
| CTS lifecycle | 15/15 uneventful completed cycles |
| Final external renderers | ImGui, NanoVG and Sokol: 3/3 passed after the matrix |
| Practical 3D sample | Sokol cube: 180 frames, five poses / 2,596 probes; original 8.3 MB heap allocation |
| Five-minute demo | ImGui: 5,997 frames, 11 readbacks, 5,997 successful two-draw batches |

Upstream renderer algorithms were unchanged. Native entry-point, EGL, build and
scene/oracle glue are project-owned. Sokol uses its existing 3.3 fallback by
undefining two 4.x header macros; no GL functions are stubbed.

| Renderer | Upstream revision | Final native oracle |
| --- | --- | --- |
| Dear ImGui 1.91.9b | `f5befd2d29e66809cd1110a152e375a7f1981f06` | 6 frames, 60 color probes, font coverage, state restoration and device-object recreation |
| NanoVG GL3 | `ce3bf745eb2d2dbc14a50bf2446783f691ac4353` | 3 frames, 45 probes, zero dirty stencil pixels |
| Sokol | `48c85905aeaa1350feb17515961aecb6c75447d8` | 3 frames, 1,843,200 component comparisons, zero mismatches/log errors |

The practical cube and five-minute demo supplement the matrix; they add no CTS
coverage. Their counts and raw-log hashes are recorded in the
[evidence guide](../validation/2026-09-07/README.md). The demo sustained ~19.99 FPS
at 1080p; its 30 FPS setting is a ceiling. No fresh TV scanout, controller input or
shell responsiveness observation is claimed. Earlier TV output was owner-confirmed;
host navigation checks exercise widget changes. See [Performance](performance.md).

## Frozen identity and defaults

Runtime implementation: `0a15d8fa82f3f96cf071be927ad11422964a16ba`. Later release
changes update documentation, verification and build defaults, not the tested
runtime implementation. Native, multi-draw and deferred batching were enabled
throughout this matrix. The GPU-clear minimum is 16,384 pixels.

```text
CTS eboot  7cad32744b80e5eeafbc6d6188686c10ef9c0bfc882b4a001972b5eecdccf077
Runtime    2508f7c66a7f752f70c90f9523176ef3cb3be634203bdb5ac24e7ff1a30c3663
SDK index  2457f914f1faeb88ff43be1ddb2fe84c602c0d12ee3de0747cb30b4db254576a
libc       e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036
```

The CTS runtime archive matches the installed SDK byte for byte. After enabling
the tested defaults, regenerating the default SDK reproduced the same runtime
archive and complete package manifest; its consumer checks passed again.
Compiler, Mesa, PSBC, CTS, boilerplate, protocol and renderer pins are in the
candidate manifest. Another toolchain or build path is not promised to reproduce
these executable bytes.

CTS has [six disclosed adaptations](../conformance/vk-gl-cts/README.md), including
a negative compute-shader version guard; it is not an untouched upstream binary.
The long swizzle and LOD-bias bodies are unchanged. The LF must-pass inventory
matches the older CRLF export's 9,886 ordered names exactly.

## Evidence and limits

```sh
make test
python3 tools/verify-published-validation.py
```

These commands verify host checks and the [published export](../validation/2026-09-07/README.md),
not another console run. The export includes every case result, exact exclusion
reviews, sanitized lifecycle summaries, renderer oracles and provenance hashes.
Raw QPA/device logs and binaries remain private local artifacts. Export integrity
does not independently prove physical execution or constitute certification.

- A predecessor executable reached its 1,410-second observation bound after
  4,656 passes and 239 reviewed exclusions. Its incomplete receipt was excluded;
  the small-clear cutoff was corrected and the complete matrix restarted with
  the frozen successor above. No case failure or kernel panic is asserted for
  that timing-bound event.
- Final renderer/demo runs still report VideoOut unregister `80290009` (busy),
  followed by successful close, EGL cleanup and runtime-layer release—not
  successful unregister.
- One recorded firmware-6.02 console, native fullscreen EGL/static-SDK integration,
  CPU fallbacks and bounded stress tests define this release's scope. Universal
  application compatibility, full-game FPS, exhaustive OOM/suspend/resume and
  device-loss recovery remain unproven. See [Limitations](limitations.md).

## Historical baseline

The original September 6 [report](validation-baseline.md) and
[dataset](../validation/2026-09-06/README.md) remain available separately. Their
39,544 results, 21 completed CTS receipts and 20 uneventful cycles belong to that
older executable. They were not reused as acceptance for this release.
