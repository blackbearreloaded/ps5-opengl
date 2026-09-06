# Testing

Use targeted checks during development and the full matrix for a frozen release
candidate. Documentation/example edits do not justify repeating all CTS cases.

## Host-only

```sh
make test
make test-imgui             # requires dependencies, installed SDK and host EGL/GL
make test-compiler          # requires the built host PSBC library
bash tools/verify-installed-sdk.sh
```

`make test` runs Python unit checks, strict-auditor self-tests and the published
evidence verifier. It needs no SDK or PS5. CI runs this lane, not new GPU tests.
The SDK verifier checks Make/pkg-config/CMake links and 344 Core exports;
behavioral evidence comes from CTS and native renderer oracles.

`test-compiler` runs existing real NIR/ACO regressions for framebuffer exports,
vertex inputs and geometry descriptors, including invalid-input rejection.
Performance-branch checks also cover implicit PrimitiveID export liveness,
zero-stride constant inputs and byte-bounded buffer descriptors. These host checks
do not replace the native draw/current-attribute/cube regressions or affected CTS.
The historical upstream `tests/verify_sb.py` still asserts metadata ABI 6; the frozen
PS5 compiler uses ABI 7. That stale suite is retained in the pinned source for
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
The recorded protocol `8d9639c5` uses small owner-approved title launch/close
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

Release acceptance requires all 9,886 cases once in each of four configurations,
no required-case failures, reviewed optional exclusions, repeated clean lifecycles
and external renderer oracles. A timeout is incomplete—not a pass or an automatic
kernel panic.

## Reporting

Report identities, selected/ordered results, lifecycle and health, with one of:
pass, partial-pass, failed, inconclusive, transport-failure, or no-run. Keep raw
captures local and milestones short.

The [published results](validation.md) belong to one frozen candidate.
`tools/export-published-validation.py` first reruns the strict raw audit, then
exports allowlisted results/provenance without raw device logs or local paths.
