# OpenGL 3.3 Core validation report

**Campaign completed: September 6, 2026.** The project acceptance criteria were
met by one frozen native CTS executable and runtime module, plus final SDK
consumer checks. This is project validation, **not Khronos certification**.

## Results

| Configuration | Actual target | Pass | Reviewed NotSupported | Accounted |
| --- | --- | ---: | ---: | ---: |
| 0 | 64x64, D32S8, seed 1 | 9,351 | 535 | 9,886 |
| 1 | 113x47, D32S8, seed 2 | 9,351 | 535 | 9,886 |
| 2 | 64x8192, D24S8 FBO, seed 3 | 9,351 | 535 | 9,886 |
| 3 | 8192x64, D24S8 FBO, seed 3 | 9,351 | 535 | 9,886 |
| **Total** | | **37,404** | **2,140** | **39,544** |

The accepted set has no gaps, duplicates, required-case failures, warnings,
waivers or incomplete results. Every `NotSupported` has an exact expected
message and pinned-source review in [candidate.json](../validation/2026-09-06/candidate.json).
These exclusions cover optional newer features or upstream-defined inapplicable
context/format/access combinations, not accepted omissions in mandatory Core 3.3.

The **full upstream swizzle matrices and LOD-bias workload** ran in all four
configurations. Reduced diagnostic loops do not count. The strict raw audit ran
without `--allow-incomplete`, and was rerun before exporting these results.

## Acceptance lanes

| Lane | Result | Published evidence |
| --- | --- | --- |
| Core command structure/export audit | 344/344 | Source checks and SDK integration tools |
| Make, pkg-config, CMake consumers | Compile/link passed | Historical build checks; reproducible verifier included |
| Frozen CTS matrix | 39,544/39,544 accounted | [Audit](../validation/2026-09-06/audit.json), [case data](../validation/2026-09-06/cases.csv.gz) |
| CTS lifecycle | 21 completed; 20 uneventful | [Receipt summaries](../validation/2026-09-06/receipts.json) |
| External renderer builds | 3/3 using the same installed SDK | Pinned examples |
| External hardware oracles | 3/3 passed after the CTS series | [Renderer receipts](../validation/2026-09-06/renderers.json) |

The 344-command audit is structural/export evidence—not 344 separate GPU tests.

## Existing renderer compatibility

Upstream renderer algorithms were unchanged. Native entry-point, EGL, build and
scene/oracle glue are project-owned. Sokol uses its existing 3.3 fallback by
undefining two 4.x header macros; no GL functions are stubbed.

| Renderer | Upstream revision | Native oracle |
| --- | --- | --- |
| Dear ImGui 1.91.9b | `f5befd2d29e66809cd1110a152e375a7f1981f06` | 6 frames, 60 pixel probes, font coverage, state restoration, EGL cleanup |
| NanoVG GL3 | `ce3bf745eb2d2dbc14a50bf2446783f691ac4353` | 3 frames, 45 probes, zero dirty stencil pixels, EGL cleanup |
| Sokol | `48c85905aeaa1350feb17515961aecb6c75447d8` | 3 frames, 1,843,200 components, zero mismatches/log errors, EGL cleanup |

Native results report Mesa 26.2.0 Core 3.3; NanoVG explicitly reports GLSL 3.30 and
`PS5 AGC`. Host software-Mesa references are separate and do not substitute for PS5
results. A subsequent ImGui TV demo was owner-confirmed and completed 2,851 frames
over five minutes (~9.5 FPS), with exact readbacks and clean teardown. It is an
example milestone, not extra CTS coverage. Its hardware log recorded pad
connection but zero widget changes; host navigation checks passed.

## Frozen identity

Research runtime source: `92bcfbbd255c405c6342bf03d6d2676a27d879cb`.
The publication is a curated source snapshot, not the private research Git history.

```text
CTS eboot  cbb5b718c7727484d60b0a1c7b0c1d421a7bf1eda2f1a75e28fc14fa961e361a
libc       e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036
SDK index  bd0b2bfaf0861723f3a095c866c796cd0dd067eca0cae2873050167e9d9c55f7
ImGui      1082dbf57d051cecf2bbfb2918103ee1cc5fbd6866d0fccdfd12f4485a81f327
NanoVG     590b28ace377f25aaf6901b88e3d3040a12585816c0bf7317347c6bababd1e9f
Sokol      a5a8af203a4387602fc6985ab8770dc1f5452ffec6fd3ade76feba16fbd0ab91
```

Compiler/Mesa/PSBC/CTS/boilerplate/protocol pins are in the candidate manifest.
The complete PSBC source delta is now published over its public upstream base;
the patched tree is identical to the historical compiler revision.

CTS has **six disclosed adaptations**, including a negative compute-shader
version guard, not an untouched upstream executable. See the
[overlay guide](../conformance/vk-gl-cts/README.md). Long swizzle/LOD bodies remain
unchanged. An incremental historical rebuild reproduced the frozen CTS hash;
no independent byte-for-byte clean rebuild of that historical executable is claimed.

## Verify the publication

```sh
python3 tools/verify-published-validation.py
```

The [evidence guide](../validation/2026-09-06/README.md) explains the export and
its limits. It includes every case result, the original review manifest, strict
audit, sanitized lifecycle summaries and original receipt hashes. Raw QPA,
kernel/app logs and binaries remain private local artifacts. The export verifies
accounting and consistency, not independently the physical execution of a PS5.

## Exceptions and remaining limits

- Config-2 run `20260905-222823` reached its 300-second observation bound after
  4,428 passes. Its incomplete receipt is **excluded**. The interrupted singleton
  and remaining cases subsequently passed; no driver change was made.
- Config-1 run `20260905-234856` passed cases and teardown but switched out an
  already-active foreign title. Its results remain valid coverage, but it does
  **not** count among the 20 uneventful cycles. The audit's `clean_cycles=21` is
  a technical receipt count, not 21 incident-free foreground acquisitions.
  A shared read-only, lock-owned foreground guard was added to both runners.
- All three final renderer logs show busy VideoOut unregister `80290009`, followed
  by successful close, EGL cleanup and runtime-layer release—not successful unregister.
- Native fullscreen/static-SDK scope, CPU fallbacks, untested full-area allocation,
  limited firmware coverage and incomplete long-session/recovery evidence remain.
  See [Limitations](limitations.md).
- Numeric readback and lifecycle records were the primary campaign oracles;
  no routine screenshots or final shell-input responsiveness test was performed.

Passing this campaign is a useful implementation milestone, not a universal
application-compatibility, game-performance or production-stability guarantee.
