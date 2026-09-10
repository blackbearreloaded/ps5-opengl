# CTS release qualification

**Status: prerequisite checks are blocked. The new full GL33 CTS campaign has
not started and no Khronos conformance claim is made.**

This qualifies the port of `opengl-cts-4.6.8.1`, not OpenGL 4.6 support. The
target remains OpenGL 3.3 Core. These results do not replace the separate
[historical engineering campaign](validation.md) or qualify new SDK downloads.

## Measured results

| Check | Upstream result | Project disposition |
| --- | --- | --- |
| Rendering regression: six information cases, triangle, draw buffers, two packed depth/stencil clears and two nearest-edge cases | **12/12 Pass**, complete | Passed this exact 64×64 pbuffer selection |
| Unchanged negative-shader prerequisite | **1 Fail**, complete | Blocked; compute-stage applicability needs upstream disposition |
| Actual EGL configuration inventory | **1 Pass**, complete | Rejected as configuration coverage: only a synthetic default was selected |
| Preserved rendering attempt before metadata correction | 6 Pass, 2 Fail; incomplete 12-case batch | Failed; no partial results promoted |

All four native-title cycles reached clean teardown, healthy declared services
and exact-token lock release. Observation bounds were two minutes, with early
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

The historical local compute-version guard is **not applied** to this release
candidate. No accepted upstream correction or applicable waiver has been
established. Submission bug fixes require upstream acceptance; other changes
need the appropriate disposition under the
[release's porting rules](https://github.com/KhronosGroup/VK-GL-CTS/blob/067e8832315e79817ede1c4863804e440f5d1c80/external/openglcts/README.md#other-allowable-changes).

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

[Machine-readable results](../validation/cts-release-qualification/results.json)
include every selected case, upstream outcomes, preserved failures, source and
SDK identities, applied patch hashes and raw-receipt SHA-256 values. The export
was checked against the local QPA, status, input, lifecycle and runner records.
It contains no raw device logs, binaries or personal host paths. The underlying
receipts remain local; export integrity is not independent execution proof.

The binary uses runtime source `a89e668` and metadata source `b12bd18`.
The release CTS source is `067e8832315e79817ede1c4863804e440f5d1c80`;
platform/build adaptations remain disclosed in the
[CTS integration guide](../conformance/vk-gl-cts/README.md). Test bodies are
unchanged. Existing SDK 0.2.0 assets are untouched.

Follow the [campaign prerequisites](cts-campaign.md#g1-establish-the-right-target-before-a-full-run)
before freezing a full acceptance matrix. The timed, resumable batching tools
are ready; the six draft case/configuration runs forecast above two minutes
still require explicit duration exceptions. No shortened workloads or skipped
required cases count as completion. OpenGL 4.6 development follows a trustworthy
3.3 baseline, not a claim that these prerequisites have already passed.
