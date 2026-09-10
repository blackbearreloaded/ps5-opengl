# CTS release qualification

**Status: prerequisite checks are blocked. The new full GL33 CTS campaign has
not started and no Khronos conformance claim is made.**

This qualifies the port of `opengl-cts-4.6.8.1`, not OpenGL 4.6 support. The
target remains OpenGL 3.3 Core. These results do not replace the separate
[historical engineering campaign](validation.md) or qualify new SDK downloads.

## Measured results

The following original prerequisite results use unchanged release test bodies:

| Check | Upstream result | Project disposition |
| --- | --- | --- |
| Rendering regression: six information cases, triangle, draw buffers, two packed depth/stencil clears and two nearest-edge cases | **12/12 Pass**, complete | Passed this exact 64×64 pbuffer selection |
| Unchanged negative-shader prerequisite | **1 Fail**, complete | Blocked; compute-stage applicability needs upstream disposition |
| Actual EGL configuration inventory | **1 Pass**, complete | Rejected as configuration coverage: only a synthetic default was selected |
| Preserved rendering attempt before metadata correction | 6 Pass, 2 Fail; incomplete 12-case batch | Failed; no partial results promoted |

All four original native-title cycles reached clean teardown, healthy declared
services and exact-token lock release. Observation bounds were two minutes, with early
exit on completion. No routine screenshots or controller input were used.
Services remaining healthy is not an independent TV/controller responsiveness
test or a guarantee of long-session stability.

The corrected rendering selection took about 5.62 seconds of accumulated
case time; deployment and teardown are additional. It used one actual
RGBA8/D32F/S8, non-multisampled pbuffer. The runtime requested 120 Hz successfully;
that does not turn this offscreen CTS selection into a display benchmark.

## Blocking requirements

### Native configurations and official runner inventory

The runtime exposes one EGL config, with `EGL_CONFORMANT=0` and
`EGL_NON_CONFORMANT_CONFIG`. The upstream inventory excludes it and substitutes
`default(0): window`; its case can still return `Pass`. Our saved-receipt checker
rejects that synthetic fallback as evidence of a real eligible EGL configuration.

The CTS context adapter is still pbuffer-only. The required RGBA8/D24S8 window
configurations, without multisampling and with four samples, are not implemented
and qualified. User-created D24S8/MSAA FBO tests do not establish those default
framebuffer configurations. Changing reported depth/sample bits or clearing
conformance flags would not implement them. Configuration requirements and
applicable waivers are defined by the
[Khronos passing criteria](https://github.com/KhronosGroup/VK-GL-CTS/wiki/OpenGL-and-OpenGL-ES-Conformance-Submission-Passing-Criteria).

Implement and validate those defaults, or establish an applicable platform
waiver, then reconcile the native selections with the official
`cts-runner --type=gl33 --summary` inventory. The existing four size/seed profiles
alone are not proof of submission coverage.

### Negative compute-stage test

`KHR-GL33.shaders.negative.non_precision_qualifiers_in_struct_members` fails with
`glCreateShader(): glGetError() returned GL_INVALID_ENUM`.

The [selected release's test](https://github.com/KhronosGroup/VK-GL-CTS/blob/067e8832315e79817ede1c4863804e440f5d1c80/external/openglcts/modules/common/glcShaderNegativeTests.cpp#L367)
attempts its compute branch for every desktop GLSL version, including 330.
Compute shaders are core starting with GL 4.3; an unsupported shader type raises
`GL_INVALID_ENUM` according to the
[OpenGL reference](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glCreateShader.xhtml).
This points to a test applicability defect for this context, not permission to
change the observed failure to a pass.

The local compute-version guard is **not applied** by the default release
preparation path. The original failure remains unchanged. No accepted upstream
correction or applicable waiver has been established. Submission bug fixes
require upstream acceptance; other changes need the disposition defined in the
[release's porting rules](https://github.com/KhronosGroup/VK-GL-CTS/blob/067e8832315e79817ede1c4863804e440f5d1c80/external/openglcts/README.md#other-allowable-changes).

### Validated local correction

A separate diagnostic build applies the recorded
[compute-version guard](../conformance/vk-gl-cts/patches/0005-gl33-negative-compute-guard.patch):
desktop GLSL must be at least 4.30, or ES GLSL at least 3.10, before the test
attempts a compute shader. Vertex and fragment checks remain intact. Only the
negative-test object changed in the copied CTS archive; the OpenGL SDK/runtime
and support module were unchanged. This does not add compute-shader support.

The corrected negative case plus the 12 rendering regressions passed **13/13**
with exact ordered completion, zero exclusions, warnings, waivers or device
loss. Accumulated case time was **4.87 seconds**, excluding deployment and
teardown. This is a bounded local diagnostic, not an unmodified-release pass,
upstream approval or a complete CTS baseline.

An earlier attempt also reported 13 passes, but its input selection order
differed from CTS traversal order. The strict receipt audit rejected it; it is
preserved and not counted as an accepted batch. The shared selection generator
now writes custom selections in pinned inventory order. The successful successor
changed only that case-list order, using the identical executable and arguments;
the auditor was not relaxed.

Both diagnostic cycles reached clean teardown, healthy declared services and
exact-token lock release. Observation bounds were two minutes, with early exit
on completion. No TV or controller verification was performed. Host checks cover
the guard across all 15 pinned GLSL versions and verify selection ordering, duplicates and unknown
case rejection. These checks do not resolve the native configuration gap above.

## Fixes included in the tested source

- Corrected the EGL drawable callback to treat Mesa's resolve output as a single
  pointer, with a runnable host check of the actual callback.
- Made interrupted native CTS dependency copies safely resumable.
- Registered the static EGL display and upstream configuration diagnostic;
  verify actual framebuffer properties instead of a fixed descriptor.
- Shared native display-metadata generation between the example and CTS app
  builders, deriving the rate from the installed SDK profile.

The first rendering attempt's title metadata rejected the 120 Hz request. Its
incomplete QPA also contains invalid UTF-8 and remains excluded. Only the app
metadata changed for the successful successor: executable, runtime modules,
arguments and all 12 selected cases remained byte-identical. Neither that
failure nor earlier console freezes are attributed to the callback defect
without separate evidence.

## Evidence and next execution

[Original prerequisite results](../validation/cts-release-qualification/results.json)
include every selected case, upstream outcomes, preserved failures, source and
SDK identities, applied patch hashes and raw-receipt SHA-256 values. The export
was checked against the local QPA, status, input, lifecycle and runner records.
It contains no raw device logs, binaries or personal host paths. The underlying
receipts remain local; export integrity is not independent execution proof.

The original binary uses runtime source `a89e668` and metadata source `b12bd18`;
its test bodies are unchanged. The separate
[local-correction evidence](../validation/cts-release-qualification/compute-guard.json)
records both diagnostic attempts, the explicit test-body patch and unchanged
runtime identity. Its binary source is `6d3573c`, with selection tooling from
`dd17861`. Both use CTS release source
`067e8832315e79817ede1c4863804e440f5d1c80`. Platform/build adaptations remain
disclosed in the [CTS integration guide](../conformance/vk-gl-cts/README.md).

**No SDK reissue is needed for this correction:** it changes the CTS test and
selection tooling, not the application-facing OpenGL libraries or headers.
Existing SDK 0.2.0 assets and their qualification scope are untouched; earlier
source-only runtime fixes remain separate from those downloads.

Follow the [campaign prerequisites](cts-campaign.md#g1-establish-the-right-target-before-a-full-run)
before freezing a full acceptance matrix. The timed, resumable batching tools
are ready; the six draft case/configuration runs forecast above two minutes
still require explicit duration exceptions. No shortened workloads or skipped
required cases count as completion. OpenGL 4.6 development follows a trustworthy
3.3 baseline, not a claim that these prerequisites have already passed.
