# Architecture

The public boundary is OpenGL 3.3 Core / GLSL 3.30 plus the native EGL facade.
Version and extension reporting are derived from Mesa capabilities, not version
string overrides.

| Layer | Location | Responsibility |
| --- | --- | --- |
| Application | `examples/` | Standard EGL/GL calls and application-owned platform glue |
| EGL facade | `src/egl/` | Context/surface validation, binding, presentation and lifetime |
| Mesa frontend | Pinned Mesa + `toolchain/mesa-ps5.patch` | GL semantics, state tracking, GLSL and NIR |
| PS5 Gallium driver | `src/gallium/ps5/` | Resources, state/shader translation, draws, queries and transfers |
| Shader compiler | Patched OpenGNM PSBC | NIR/ACO compilation and backend metadata |
| Native backend | `src/platform/` | Shader packaging, submission, synchronization and VideoOut |
| Native app adapter | `native-app/` + boilerplate | Runtime/linking, receipts and folder packaging |

Mesa and PSBC must agree on their compiler interfaces. Do not upgrade one in
isolation or omit the PSBC patch. [dependencies.json](../dependencies.json) pins
both sources and the exact patched compiler tree.

The driver validates dimensions, formats, bindings and supported state before
submission. Some operations use CPU fallbacks: support does not imply complete
GPU acceleration. Unsupported features must remain unadvertised, not represented
by successful no-ops. See [Limitations](limitations.md).

`tests/ps5/` contains targeted public API and structural checks;
`conformance/vk-gl-cts/` holds the disclosed CTS overlay. `validation/` contains
portable evidence, not firmware or raw device captures.

The [capability audit](development/capability-audit.md) documents version
reporting and source contracts. Behavioral qualification belongs to the
frozen identities in the [validation report](validation.md).
