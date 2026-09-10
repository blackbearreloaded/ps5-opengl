# OpenGL 3.3 Core capability audit

This document describes the source-level capability contract. The production
Core build derives OpenGL 3.3 / GLSL 3.30 through Mesa and exposes the 344
required Core commands. Structural checks are not substitutes for the
[exact-binary validation report](../validation.md).

## How Mesa computes the version

Mesa's `st_api_query_versions()` initializes constants/extensions, derives
limits and extensions from Gallium capabilities/formats, and calls
`_mesa_get_version()`. Core and compatibility are evaluated independently.
The version ladder requires the cumulative prerequisites of earlier versions.

The EGL facade validates explicit context requests against the derived result.
It must reject unsupported versions/profiles rather than rewrite `GL_VERSION`
or apply `MESA_GL_VERSION_OVERRIDE` / `MESA_EXTENSION_OVERRIDE`.

## Current PS5 Gallium envelope

The binding implementation is `src/gallium/ps5/ps5_screen.c`.
Applications must query actual limits rather than infer them from PS5 hardware
or a desktop driver.

| Area | Implementation boundary |
| --- | --- |
| Shaders | GLSL/NIR vertex, geometry and fragment paths through Mesa/PSBC |
| Draws | Arrays, indexed/base-vertex draws, instancing and supported multi-draw lowering |
| Buffers and attributes | Typed native formats plus Mesa translation where required |
| Textures and render targets | Advertised normalized, float, integer, packed, sRGB and depth/stencil formats |
| Resources | Explicit mip/layer/sample layouts, bounds, ownership and transfer handling |
| Raster state | Depth/stencil, blend, masks, scissor, primitive conventions and multisampling |
| Queries and synchronization | Supported semantics with bounded backend completion and documented fallbacks |

Direct GPU support, CPU lowering and emulation are distinct implementations.
Timer-query behavior must not be presented as isolated hardware-GPU timing.
Optional post-3.3 capabilities are not implied by Core 3.3 acceptance.

## Mesa version predicates and present blockers

The supported Core configuration satisfies Mesa's cumulative version predicates;
there is no outstanding version-ladder blocker claimed here. This does not
establish universal correctness of every advertised limit or future code change.

`tests/ps5/verify_gl33_capability_audit.py` checks derived versions, entrypoint
coverage and the production source contracts. Unsupported capabilities must
remain unadvertised; success-returning no-ops and string overrides are invalid.

## Feature-family inventory

Public API tests cover context/state, buffers/VAOs, shaders/reflection, textures,
framebuffers, raster/blend, draws, transform feedback, queries and sync.
The [CTS overlay](../../conformance/vk-gl-cts/README.md) supplies broader
version-specific coverage with disclosed adaptations.

A command resolving or being invoked is weaker evidence than numerical behavior.
A host oracle is weaker evidence than an exact-binary native result for GPU
execution. A historical pass does not qualify a changed runtime.

## Immediate acceptance contract

For a capability, format, limit or lifecycle change:

1. Identify the Mesa/Gallium predicate and shared implementation paths affected.
2. Add positive, invalid-state, boundary and cleanup checks.
3. Run the smallest relevant host/compiler/public-API checks before hardware.
4. Freeze source, configuration and executable/SDK identities for native tests.
5. Require numerical results, safe resource retirement, title teardown and health.
6. Report only the tested scope, preserving failures and unexecuted cases.

A timeout, GPU fault, process failure, UI freeze and kernel panic are distinct
outcomes. Do not silently reclassify them as passes or use incomplete evidence
to fill a campaign. See [testing](../testing.md) and [limitations](../limitations.md).
