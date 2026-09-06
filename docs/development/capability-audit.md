# OpenGL 3.3 Core capability audit

> Development assertions retained for source-level regression checks. Historical
> gate states below are not current status. The [final report](../validation.md)
> records the completed campaign and current limitations.

Status: native Khronos GL33 campaign candidate  
Goal: `G8`  
Mesa baseline: 26.2.0  
Target: retail PS5 firmware 6.02  
Last updated: 2026-09-03

## 1. Decision

The production Core build now reports Mesa-derived OpenGL 3.3 Core and GLSL
3.30, exposes all 344 required Core commands, and uses no version or extension
override. The remaining completion gate is the official 9,886-case Khronos
GL33 must-pass list across its four pinned configurations.

## 2. How Mesa computes the version

`st_api_query_versions()` in
`third_party/mesa-26.2.0/src/mesa/state_tracker/st_manager.c` evaluates Core and
compatibility independently. For each API it:

1. creates fresh `gl_constants` and `gl_extensions` objects;
2. calls `_mesa_init_constants()` and `_mesa_init_extensions()`;
3. derives limits with `st_init_limits()`;
4. derives extensions from Gallium caps and formats with
   `st_init_extensions()`;
5. calls `_mesa_get_version()`.

`compute_version()` in `src/mesa/main/version.c` is a strict ladder. Each
version depends on the previous one. If the result for `API_OPENGL_CORE` is
below 3.1, Mesa returns zero. Therefore a screen that computes OpenGL 2.0 or
3.0 does not expose a Core context.

The default PS5 EGL facade asks this state-tracker query and creates only the
reported compatibility context. A default-off integration candidate advertises
`EGL_KHR_create_context`, parses an explicit 3.3 Core request, and passes it to
`st_api_create_context()` only when Mesa derives Core 3.3 from the gated screen
caps. Neither path sets a version/extension override or rewrites `GL_VERSION`.

## 3. Current PS5 Gallium envelope

The binding source is `src/gallium/ps5/ps5_screen.c`.

| Area | Current advertised value | Directly validated boundary |
|---|---|---|
| API class | graphics, UMA, GPU-accelerated (`PS5 AGC`) | Mesa contexts execute normal AGC submissions |
| shaders | VS, GS, and FS NIR | runtime GLSL 3.30 compilation, linkage, and hardware execution |
| GLSL cap | 330 Core/compat feature level | broad public VS/GS/FS gates passed; CTS pending |
| render targets | 8 | four-target indexed state and rendering hardware-proven |
| texture size | 8192 2D | 1D/2D/3D/cube/array dimensional gates passed |
| viewports | 1 | one viewport/scissor path |
| varyings | 16 | two-float-attribute/varying public probes |
| vertex buffers | 16 | bounded float32 VBO layouts |
| vertex formats | R32/RG32/RGB32/RGBA32 float, signed integer, and unsigned integer | native integer fetch plus hardware-proven BGRA8 and 2_10_10_10 paths |
| texture formats | Core 3.3 normalized, float, integer, packed, sRGB, depth/stencil, and RGTC fallback set | representative format matrices hardware-proven; CTS pending |
| depth formats | D32F and D32F+S8 native backing for Core depth/stencil storage | writes, raw/shadow sampling, transfer, and blit hardware-proven |
| samples | native 4x noninteger color plus D32F and D32F+S8 depth/stencil | exact AddressLib 1/2/4/8/16-byte color and depth/stencil layouts; broad resolve CTS pending |
| primitives | point, line, line strip, triangle, fan, strip | exact bounded receipts exist |
| restart | advertised for Mesa lowering | public U16 restart emulation proved; native restart rejected |
| mapping alignment | 64 bytes | direct buffer range map has bounded offset/size checks |
| samplers | 16 per VS/GS/FS | multi-unit, sampler-array, border, cube, and geometry-stage sampling passed |
| constant buffers | 12 public per VS/GS/FS | 16 KiB blocks, 16-byte ranges, three-stage binding and live update proved |

Indirect draws and shader-visible DrawID are post-3.3. Mesa lowers primitive
restart and supported multi-draw paths before the native boundary; arrays,
indexed widths, signed base vertex, first vertex, instancing, divisors, and
transform feedback all have public hardware receipts.

## 4. Compiler and vertex-fetch boundary

`PsbcVertexFormat` in
`third_party/opengnm-psbc/libpsbc/psbc_compile.h` contains the proven float,
32-bit integer, and packed formats:

- `R32_FLOAT`;
- `R32G32_FLOAT`;
- `R32G32B32_FLOAT`;
- `R32G32B32A32_FLOAT`;
- one- through four-component R32 `SINT` and `UINT`;
- `B8G8R8A8_UNORM`;
- R/B `10G10B10A2` UNORM, SNORM, USCALED, and SSCALED.

`psbc_compile.c` maps these values into RADV/ACO pipe formats. The ordinary PS5
screen still advertises only the four float formats; the packed set is exposed
only by `PS5_ENABLE_PACKED_VERTEX_CANDIDATE`.

Mesa's `u_vbuf` can translate unsupported public vertex types into native
formats. It is already in the state-tracker/CSO path and has proved U8 index
lowering. The first hardware-proven route is two-component half-float:
`GL_HALF_FLOAT` becomes `PIPE_FORMAT_R16G16_FLOAT`, `u_vbuf` falls back to
`PIPE_FORMAT_R32G32_FLOAT`, and `translate_generic.c` selects
`emit_R16G16_FLOAT`. The public discriminator and exact claim boundary are in
`tests/ps5/egl_public_half_float_vertex.c` (from the repository root).
The second hardware-proven route is normalized `GL_SHORT` `vec2`:
`PIPE_FORMAT_R16G16_SNORM` falls back to `PIPE_FORMAT_R32G32_FLOAT`, and the
generic translator selects `emit_R16G16_SNORM`. Its public candidate is defined
in
`tests/ps5/egl_public_normalized_short_vertex.c` (from the repository root).
Both routes pass public state/error checks and exact hardware rendering. Other
translated types still need bounded public hardware results before becoming
behavior claims. Direct packed support now extends both the PSBC typed ABI and PS5 validator and has
deterministic compiler/build proof. Its interleaved public BGRA8 plus signed
2_10_10_10 oracle still needs hardware validation.

## 5. Mesa version predicates and present blockers

`yes` below means the predicate is currently derived by Mesa, not that every
corner of the public feature has completed Gate 7 validation. `no` is a real
version blocker. `default` means Mesa enables the extension independently of a
screen cap and the PS5 behavior still needs an explicit test.

### OpenGL 2.1 prerequisite

| Predicate | State | Reason |
|---|---|---|
| prior OpenGL 2.0 predicates | yes | current derived compatibility version |
| `EXT_texture_sRGB` | yes | exact hardware decode receipt; enabled by default |

The OpenGL 2.1 ladder is complete. OpenGL 3.0 remains blocked by the cumulative
predicates below.

### OpenGL 3.0

| Predicate or limit | State | Required backend work |
|---|---|---|
| GLSL >= 130 | derived yes | GLSL 1.30 feature tests still required |
| color attachments >= 4 | hardware-proven four-target RGBA8 | broader formats and layered targets |
| samples >= 4 or fake software MSAA | native 4x RGBA8 color plus D32F and D32F+S8 depth/stencil hardware-proven | broader color formats and sample-mask combinations remain |
| color-buffer float | waived for Core | compatibility would still require it |
| depth-buffer float | hardware-proven | native D32 descriptor, dynamic extent, DB release barrier, raw sample, and shadow compare passed exact public oracles |
| half-float vertex | hardware-proven | tightly packed binary16 `vec2` passes public state/error and exact `u_vbuf`-translated rendering |
| map-buffer-range | hardware-proven | exact invalid-range, offset write/flush/readback, and mapped-VBO render |
| shader-texture-LOD | hardware-proven | exact explicit LOD 0/1, min/max LOD clamp, and generated-level sampling passed with linear mip layouts |
| texture float | hardware-proven sampling | RGBA16F/RGBA32F passed exact public sampling oracles; rendering remains |
| texture RG | hardware-proven render/sample | exact R8/RG8 sampling and direct R8 render-to-sample oracles pass |
| texture compression RGTC | hardware-proven CPU fallback | Mesa decompression into R/RG UNORM/SNORM passed all four RGTC1/RGTC2 public oracles |
| draw-buffers2 | hardware-proven four-target slice | exact indexed masks plus target-3 zero blending passed; broader state matrix remains |
| framebuffer object | hardware-proven mixed-size slice | per-target RGBA8 extents plus padded 128x96 color/64x80 depth attachments pass exact depth-write/mask oracles; broader formats remain |
| framebuffer sRGB | hardware-proven | GFX10 SRGB number type plus destination conversion control passed disabled/enabled encoding oracles |
| packed float | hardware-proven gated Core 3.3 slice | exact R11G11B10 float upload/sample and render/sample oracle passed once |
| texture array | hardware-proven sampled/rendered slice | exact 3-layer selection at a reported 256-layer limit, RGBA8 render-to-layer/resampling, and exact per-layer mipmaps |
| 1D textures | hardware-proven sampled slice | RGBA8 1D and 1D-array size, layers, base sampling, and generated level-6 sampling passed exactly |
| texture integer | hardware-proven RGBA8/16/32 | signed/unsigned tiled upload plus render/sample pass exact public oracles, including full-range RGBA16 values; CPU detile remains unsupported |
| shared exponent | hardware-proven sampling | RGB9_E5 passed an exact public sampling oracle |
| transform feedback | hardware-proven gated Core 3.3 slice | no-GDS NGG streamout passed interleaved/separate ranges, append, instancing, capacity overflow, all seven Core draw topologies, intact canaries, and generated/written primitive queries |
| conditional render | hardware-proven gated Core 3.3 slice | GFX10 ZPASS counter/predicate and synchronous normal/inverted conditions passed exact output |

### OpenGL 3.1

| Predicate or limit | State | Required backend work |
|---|---|---|
| all 3.0 predicates | no | see above |
| GLSL >= 140 | derived yes | correctness not yet proven |
| draw instanced | hardware-proven | exact one/two-instance `InstanceID` output plus `NUM_INSTANCES` packet evidence |
| uniform buffer object | hardware-proven | vertex block 3 plus fragment block 5, reflection, ranges and live update |
| texture SNORM | hardware-proven sampling | RGBA8 SNORM passed an exact public sampling oracle |
| primitive restart | derived yes | broaden emulation tests beyond current U16 case |
| texture rectangle | hardware-proven | RECT resource lowering to normalized 2D sampling passed the pixel-coordinate discriminator |
| >=16 VS texture image units | derived yes: 16 | only one simple texture binding proved |

### OpenGL 3.2

| Predicate | State | Required backend work |
|---|---|---|
| all 3.1 predicates | no | see above |
| GLSL >= 150 | hardware-proven Core 3.3 slice | runtime GLSL 3.30 VS/FS plus merged NGG geometry varyings, UBOs, and textures passed exact public oracles |
| depth clamp | hardware-proven gated Core 3.3 slice | PA_CL_CLIP_CNTL near/far bits, rasterizer discard, and exact clamp/discard/clip discriminator passed once |
| draw-elements base vertex | hardware-proven | exact base 0, +3, and -3 RGB overwrites plus signed bounds and indexed packet evidence |
| fragment coord conventions | hardware-proven | exact lower-left quadrant orientation and half-integer pixel-center oracles passed over a 128x96 FBO |
| provoking vertex | hardware-proven | first/last conventions passed exact flat-color oracles for points, lines, line strips, triangles, triangle strips, and triangle fans |
| seamless cube map | hardware-proven | global enable clears sampler bit 28 and passed an exact 50/50 cross-face linear edge oracle; per-texture extension remains unproved |
| sync | hardware-proven serialized backend | each native draw waits for its completion marker before returning; fence identity, immediate signaled wait, all four queried properties, server wait, and deletion passed publicly |
| texture multisample | hardware-proven 4x RGBA8 allocation, fixed-location query, FBO attachment, raster, resolve, and independent samples 0–3 fetch | broader formats and arrays |
| vertex array BGRA | hardware-proven | normalized BGRA8 color fetch passed an exact public render oracle |

GLSL 1.50 must not be advertised before geometry shader creation, linking,
ABI, command generation, and limits are implemented. Raising only the GLSL
number would make the Core result misleading.

### OpenGL 3.3

| Predicate | State | Required backend work |
|---|---|---|
| all 3.2 predicates | hardware-proven | after geometry UBO limits were completed, Mesa's unmodified version query returned Core 33 and created a real 3.3 Core context |
| GLSL >= 330 | hardware-proven baseline | GLSL 3.30 vertex and indexed-output fragment shaders compiled, linked, and submitted; broader conformance coverage remains |
| Core API entry points | offline-complete candidate | all 344 Khronos `gl.xml` commands required by a 3.3 Core profile are generated and checked through `eglGetProcAddress`; hardware receipt remains |
| dual-source blend | hardware-proven | indexed fragment output 1 lowers to a compressed MRT1 export; normalized RGBA8 uses the Mesa-required packed FP16 MRT0/MRT1 contract (`SPI_SHADER_COL_FORMAT=0x44`) and passed the exact 4,096-pixel `SRC1_COLOR` oracle |
| explicit attribute location | hardware-proven | Core 3.3 `layout(location=0)` vertex input and fragment output passed exact raster output |
| instanced arrays | hardware-proven | divisor-one attribute fetch produced exact per-instance positions/colors |
| shader bit encoding | hardware-proven | Core 3.3 `floatBitsToUint`/`uintBitsToFloat` round trip passed exact raster output |
| RGB10_A2UI | hardware-proven gated Core 3.3 slice | asymmetric packed-uint render/sample oracle passed once; integer shaders use Core 3.3 semantics without requiring the legacy EXT token |
| timer query | hardware-proven emulation | positive 64-bit timestamp/time-elapsed objects use monotonic host time |
| 2_10_10_10 vertex | hardware-proven | signed normalized packed position fetch passed an exact public render oracle |
| texture swizzle | hardware-proven | native descriptor route passed exact `B/A/1/0` oracle |
| sampler objects | hardware-proven | independent state plus transparent-black, opaque-black, opaque-white, and arbitrary RGBA clamp-to-border passed exact oracles |
| integer vertex attributes | hardware-proven | signed `ivec2` and unsigned `uvec4` direct 32-bit fetch passed state queries and an exact 4,096-pixel oracle |

## 6. Gate 7 feature-family inventory

| Family | Implemented/proven slice | Emulatable candidate | Missing foundation |
|---|---|---|---|
| API/context | real 3.3 Core context; all 344 required commands resolve and now have at least one public behavioral invocation; dedicated identity/limit, program/reflection, object/query/state, vertex-attribute, and dimensional transfer matrices carry stronger oracles | none | hardware receipts for the new offline API matrices; invocation coverage is not a substitute for CTS conformance |
| buffers/VAO | VBO, U8/U16/U32 IBO, two VAOs, float32, signed/unsigned integer32, packed 2_10_10_10, BGRA8, divisors, signed base vertex, selected `u_vbuf` conversions, offline-built mapped buffer copy/readback, non-default PixelStore PBO transfers, and every Core generic float/integer/packed current-attribute setter family with typed getter checks | narrow integer formats through `u_vbuf` | hardware receipts for offline-built buffer/API paths plus broader streamed formats and limits |
| GLSL | runtime 1.20 and 3.30 VS/FS, explicit locations, bit encoding, attributes, varyings, default uniforms, exact merged NGG GS raster/varying, and geometry-stage UBOs/textures | none | broader 3.30 conformance and non-binary GS interpolation stress |
| textures | arbitrary-size RGBA8, R/RG, sRGB, 8/16-bit SNORM, 16-bit UNORM, one/two/four-channel 16/32-bit float, one/two/four-channel 8/16/32-bit integer, RGB9_E5, R11G11B10 float, RGB10_A2/UI, mip/LOD, 1D/1D-array, rectangle, RGTC CPU fallback plus offline block-subimage/compressed-readback coverage, swizzle, depth raw/shadow plus an offline-built Z32 depth-array mip/shadow path, all Core wrap modes including arbitrary border color, sampler objects, two bindful units, six-face cube sampling/mipmaps with seamless edges, 256-layer 2D-array sampling/mipmaps, RGBA8 array-layer rendering, and 256³ 3D sampling/mipmaps | 16-slot VS/GS/FS descriptors | GLSL 3.30 sampler-array indices are correctly limited to constant integral expressions |
| framebuffers | RGBA8 color, Z32F and packed D32F+S8 depth/stencil; native uncompressed render-to-texture transition; RGBA8 `glFramebufferTextureLayer`; geometry-driven whole-layer mipmapped RGBA8 array rendering; four dynamic RGBA8 MRT attachments; padded mixed-size FBO; scaled-nearest RGBA8 blit; offline Core color-render, format-converting blit, 1D/2D/3D framebuffer-to-texture copy/readback, tiled depth/stencil upload/readback, level-0 1D/1D-array/cube/2D-array/3D depth slice routing, and canonical-linear/tiled-staging Z32 mip-level rendering/readback across all six texture classes plus a whole-layer depth candidate | none | hardware proof of broad color/depth transfer formats and remaining whole-layer slice routing; mipmapped depth-target staging awaits hardware proof |
| raster/blend | documented cull, masks, depth, stencil-candidate, polygon modes/offset, fixed/program point sizes, variable line width, point coordinates with both origins, `gl_FrontFacing` for both windings, dual-source blend, indexed four-target masks/blend, depth clamp, and rasterizer discard; offline combined gates cover smooth 4x line/polygon coverage, scissored masked clears, separate RGB/alpha blending, front/back stencil, culling, polygon modes, and depth offset | none | hardware proof for the three combined offline matrices |
| draws | arrays, indexed widths, six topologies, line-loop/restart lowering, positive/negative base vertex, range boundaries, instancing, and multi-draw splitting; one combined offline gate composes fixed-index restart, divisor-one instancing, signed base vertex, and base-vertex multi-draw with invalid-count checks | selected index translations | hardware receipt for the combined gate; indirect path is post-3.3 |
| uniform blocks | 12 public blocks per VS/GS/FS, 16 KiB size, aligned ranges, live updates, and combined texture/UBO descriptors | none | broader binding/size stress |
| query/sync | hardware-proven timer, occlusion counter/predicate, conditional render, bounded completion, and serialized GL sync lifecycle | synchronous CPU conditional evaluation | queue-backed fence lifetime when asynchronous submission or shared contexts are introduced |
| transform feedback | deterministic no-GDS VS/ACO stores, typed descriptor/stride metadata, interleaved/separate and instanced hardware capture, append, whole-primitive capacity overflow, all seven Core draw topologies, and primitive queries | none | geometry-stage query stress |
| geometry shaders | public Core context, merged VS+GS compilation, exact pass-through/varying raster, unity GFX9+ ESGS multiplier, live UBO updates, texture-dependent position, transform feedback, and clean retired submissions | none | broader varying interpolation and topology/query coverage |
| multisample/blit | native 4x RGBA8 renderbuffer/texture storage, raster, independent sample fetch, CPU resolve, sample coverage/mask composition, 4x D32F per-sample depth, and 4x packed D32F+S8 per-sample stencil; same-format scaled-nearest and RGBA8 scaled-linear CPU blits; full-surface and scaled/subrect D32F+S8 nearest blits | no fake-MSAA dependency; packed depth/stencil tiling and coordinate copies hardware-proven | none for the advertised 4x path |
| remaining formats | RGBA8/BGRA8/Z32F plus sRGB, broad Core sampled scalar/vector formats, RGB9_E5, R11G11B10 float, RGB10_A2/UI, RGTC CPU fallback, and an offline-built broad color-render candidate | none | hardware validation of expanded sampled/render matrices and native compression |

## 7. Implementation order

The order is chosen by dependency and observability, not by how easily a cap
can be toggled.

1. Prove `glMapBufferRange`, explicit flush, unmap, and bounded offsets through
   a public VBO draw; do not change the version.
2. Prove `u_vbuf` half-float and normalized/integer conversions one format at a
   time; keep PSBC's direct ABI unchanged unless translation is insufficient.
3. Prove the completed offline indexed base-vertex candidate: the existing
   RADV/ACO SGPR route plus exact signed underflow/overflow checks avoids CPU
   index rewriting. Add a separate non-indexed first-vertex discriminator.
4. Prove the offline instance-count/InstanceID slice, then implement
   start-instance and vertex divisors as the remaining coordinated ABI feature.
5. Build the sRGB plus format/resource ladder needed for the 2.1/3.0 texture
   predicates; distinguish sampling from rendering support.
6. Expand framebuffer color attachments and fragment exports to at least four,
   then independent target state.
7. Add real UBO bindings and transform feedback.
8. Add multisampling, query/sync behavior, cube/array resources, and the
   remaining 3.0/3.1 predicates.
9. Implement geometry shaders before raising GLSL to 1.50, then complete the
   GLSL 3.30 and 3.2/3.3 predicates.
10. Only after Mesa computes at least 3.3, add `EGL_KHR_create_context` and a
    strict `EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR` request path. Reject
    unsupported versions/profiles rather than falling back.

## 8. Evidence rule

For every newly enabled format, cap, or limit, retain:

- the exact Mesa predicate and Gallium input that changes;
- a static guard preventing a speculative version override;
- positive, invalid-state, boundary, and cleanup tests;
- one immutable public-API hardware artifact and receipt;
- proof that the console title exited and all declared services remained
  healthy;
- the narrower claim that the test actually establishes.

Only direct kernel-panic evidence is classified as a crash. GPU faults,
timeouts, loader failures, process failures, UI freezes, transport failures,
and functional mismatches keep their protocol classifications and do not
silently become crashes.

Milestone 2026-09-03: native 4x D32F textures passed exact scissored-clear,
depth-resolve, and `sampler2DMS` fetch oracles in PPSA99005.

Milestone 2026-09-03: native 4x sRGB texture raster and resolve passed its
exact PPSA99005 image oracle.

## 9. Immediate acceptance contract

The first buffer-map artifact must:

- import only public EGL/OpenGL plus libc calls;
- retain the derived OpenGL 2.1 / GLSL 1.20 identity;
- require `GL_ARB_map_buffer_range` in the extension string;
- allocate one VBO, map a nonzero bounded subrange for write with explicit
  flush, verify its map offset/length/access state, flush and unmap it;
- map the same range for read and verify the exact bytes;
- render only from that subrange and produce an exact 64x64 readback oracle;
- exercise one invalid range and require the specified GL error without GPU
  submission;
- complete GL/EGL teardown and the standard one-shot protocol lifecycle.

A pass promotes only the tested map/update slice. It does not promote the GL
version, direct half-float fetch, persistent/coherent maps, concurrent mapping
and execution, hardware base-vertex behavior, instancing, or a Core context.
