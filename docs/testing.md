# Testing

Use risk-based checks during development and for the current optimized release
candidate. The full 39,544-execution matrix is deferred at the owner's request;
it is not a release blocker for this explicitly sample-validated candidate.
Documentation/example edits do not justify repeating all CTS cases.

## Host-only

```sh
make test
make test-imgui             # requires dependencies, installed SDK and host EGL/GL
make test-compiler          # requires the built host PSBC library
bash tools/verify-installed-sdk.sh  # rebuilds a fresh SDK; never use on frozen bytes
```

`make test` runs Python unit checks, strict-auditor self-tests and the published
evidence verifier. It needs no SDK or PS5. CI runs this lane, not new GPU tests.
The SDK verifier checks Make/pkg-config/CMake links and 344 Core exports;
behavioral evidence comes from CTS and native renderer oracles.
For a frozen SDK, use `tools/check-sdk-consumers.py` instead; the
[bundle guide](sdk-bundle.md#verify-and-link-a-consumer) gives the non-rebuilding
recipe. Verify an archive after extracting it to a new directory outside the
source checkout, not only its staging directory.

`test-compiler` runs existing real NIR/ACO regressions for framebuffer exports,
vertex inputs and geometry descriptors, including invalid-input rejection.
Compiler checks also cover implicit PrimitiveID export liveness,
zero-stride constant inputs and byte-bounded buffer descriptors. These host checks
do not replace the native draw/current-attribute/cube regressions or affected CTS.
`make test-glsl` exercises math, texture fetch, live PrimitiveID/flat/smooth inputs
and program restoration, including seven deliberate fault checks. It needs the
installed Zink and software Vulkan renderer; direct software GL failures for this
case are recorded in [the development history](performance-history.md), not accepted as passing.
The historical upstream `tests/verify_sb.py` still asserts metadata ABI 6; the frozen
historical PS5 compiler uses ABI 7; the validated September 7 compiler uses ABI 8.
That stale suite is retained in the pinned source for
provenance, not silently patched or reported as passing. It is not invoked by
the publication build. Current compiler checks and the Core capability audit
are explicit `make sdk` steps.

## Native examples and targeted cases

```sh
make imgui-demo
bash tools/build-native-test-app.sh --list
bash tools/build-native-test-app.sh egl_public_core33_texture_rectangle
```

All cases reuse `PPSA99005`. Freeze source/executable/runtime/metadata hashes
before deployment. Never send the application ELF to elfldr.

Optional Windows/WSL runners use the separate
[homebrew protocol](https://github.com/blackbearreloaded/ps5-homebrew-dev-protocol).
That repository is access-controlled and is not required to build the SDK or
examples. The managed wrappers require access to it; they are not a standalone
console setup tool. Folder deployment can use the owner's existing app workflow.
The reference layout is `workspace/dev/ps5-opengl`, its sibling boilerplate, and
`workspace/docs/ps5-homebrew-dev-protocol`. Supply the console host explicitly.
The final campaign's protocol `7195c969` uses small owner-approved title launch/close
controllers on 9021; the actual graphics app remains a native folder title.

Each bounded cycle must own its exact lock token, establish idle foreground,
verify uploaded bytes, observe the exact title, close it, check declared services,
and release only its own token. Never change settings, approve updates, close
another app or blindly retry a suspected panic. Use only owner-started services.

## CTS

```sh
make cts-fetch
bash conformance/vk-gl-cts/prepare.sh
```

See the [overlay guide](../conformance/vk-gl-cts/README.md) for its build. The
runner contains six disclosed adaptations; full swizzle/LOD-bias bodies remain.
Use `prepare-cts-shard.py --suite smoke` or affected case lists while developing.
Measured timings and `--budget-seconds` support efficient bounded batches.
Long cases need adequate independent windows. Never merge different binaries
to fill coverage gaps.

### Current optimized candidate: sampled validation

Reuse the existing `smoke` suite: 51 distinct cases in each of the four target
configurations, **204 executions total**, on one frozen binary. This is a
deterministic, risk-based sample, not a statistical confidence estimate or full
Core 3.3 coverage. It exercises buffer objects, draw buffers, framebuffer blits,
depth/stencil clears, texture addressing/LOD/swizzle, interpolation and transform
feedback across small, non-square, tall and wide targets.

Accept this sampled gate only when every selected case is Pass, all four actual
render-target reports match, and all cycles have clean teardown/health/unlock.
For G7/G8, retain their host lifetime/hazard checks, native 512-object batch
boundary, 128-cube ordinary/instanced pixel checks, SDK links, Sokol renderer and
repeated EGL-session results alongside CTS; the 51 cases alone do not exercise
every optimized presentation path. Already verified frozen artifacts need not
be rebuilt or rerun without an affected change.

The later frozen G13 SDK has its own 204/204 sample and G15/G16 memory,
ten-minute soak, texture-copy and layered-mip evidence; see the
[enhancement gates](enhancement-plan.md). Keep those identities separate from G7/G8.

Run one bounded batch per configuration, reuse remotely verified binaries/data,
upload only changed selection files and stop observation at completion. No routine
screenshots or five-minute repetitions. Release the console lock before offline
analysis. After a change, rerun the affected named tests and add a regression for
any new failure; widen to the affected CTS family for unexplained failures or
major compiler/resource/synchronization changes. Do not silently expand to the
full matrix or omit a slow selected case to obtain a pass.

Continue using `verify-cts-candidate.py --allow-incomplete` for strict identity,
ordered-result and lifecycle auditing. Its full-matrix `complete=false` must stay
false for a sample; record sampled-gate completion separately. Keep historical
baseline coverage separate and label any resulting SDK **sample-validated**.
Remaining unsampled cases are untested on this candidate, not inherited passes.

### Full matrix (deferred)

A full-matrix claim still requires all 9,886 cases once in each configuration,
no required-case failures and reviewed optional exclusions, with clean lifecycles
and renderer oracles. A timeout is incomplete—not a pass or an automatic kernel
panic. The historical baseline retains its original full-matrix evidence.

## Reporting

Report identities, selected/ordered results, lifecycle and health, with one of:
pass, partial-pass, failed, inconclusive, transport-failure, or no-run. Keep raw
captures local and milestones short.

The [published results](validation.md) belong to one frozen candidate.
`tools/export-published-validation.py` first reruns the strict raw audit, then
exports allowlisted results/provenance without raw device logs or local paths.

Export each accepted candidate into a **new** directory; do not replace historical
evidence. Supply the exact audited manifest, result root, and three final renderer
receipt prefixes (the common filename without `-opengl.log` / `-result.json`):

```sh
python3 tools/export-published-validation.py candidate.json \
  --results results/release --destination validation/YYYY-MM-DD \
  --renderer imgui=results/final-imgui/PPSA99005-TIMESTAMP \
  --renderer nanovg=results/final-nanovg/PPSA99005-TIMESTAMP \
  --renderer sokol=results/final-sokol/PPSA99005-TIMESTAMP
python3 tools/verify-published-validation.py validation/YYYY-MM-DD
```

Record any eventful but otherwise accepted cycles in the manifest's
`eventful_receipts` mapping, using exact receipt paths and concise review reasons.
An empty mapping means no such incidents were observed. Export refuses incomplete
coverage, damaged renderer oracles and existing destinations. Batch counts are
derived from the receipts; test coverage and exclusion reviews remain strict.
