# Testing

Use risk-based regression selection. Documentation and example changes do not
justify repeating the full CTS matrix; compiler, resource, ownership and
synchronization changes require broader affected-family coverage.

## Host checks

```sh
make test
make test-imgui
make test-staging
make test-depth-targets
make test-compiler
```

`make test` needs no SDK or console. It runs unit/structural checks, strict
auditor self-tests and the published evidence verifier. Other targets need
the dependencies documented by their scripts and the [build guide](building.md).

`test-staging` runs production CPU helpers under ASan/UBSan and public GL
oracles on software Mesa. It covers layout, depth/stencil preservation, mip
generation, transfers, clears, array/sample addressing and deliberate-fault
rejection. Mocked queue/cache operations do not establish GPU execution or
cache coherence. Uniform per-sample data does not establish independent MSAA
sample isolation.

`test-depth-targets` retains a strict attachment-layer query oracle. Unpatched
system Mesa can return layer zero for a 1D-array attachment selecting layer two;
that known discrepancy exits with failure, not a waived pass. The PS5 Mesa
patch and its production query-arm regression are checked separately.

Compiler checks exercise real NIR/ACO paths and invalid-input rejection.
They supplement, rather than replace, native shader/rendering checks.
See [CI execution](ci-releases.md#cpu-and-software-opengl-ci-checks) for the
software-renderer environment and required host packages.

## SDK consumers

```sh
bash tools/verify-installed-sdk.sh
python3 tools/check-sdk-consumers.py --help
python3 tools/test_sdl_sdk.py --help
```

The first command installs/rebuilds a fresh SDK. **Do not use it on a frozen
candidate.** Use the existing-prefix consumer checker instead, with a new output
directory. Verify GL exports and Make/pkg-config/CMake links; when distributing
SDL, also verify its manifest/provenance and relocated consumers.

Archive acceptance requires a fresh extraction, all file checksums, both
applicable prefix manifests and consumer links using the extracted libraries.
Host linkage does not execute the GPU.

## Native tests

```sh
bash tools/build-native-test-app.sh --list
PS5_OPENGL_PREFIX=/path/to/frozen/sdk \
  bash tools/build-native-test-app.sh egl_public_core33_texture_rectangle
```

Freeze source, executable, runtime and title-metadata hashes before deployment.
Use the native folder app `PPSA99005`; never send the graphics executable to
an ELF loader. The optional Windows/WSL wrappers use the separate
[homebrew development protocol](https://github.com/blackbearreloaded/ps5-homebrew-dev-protocol),
which is not required to build the SDK.

Each cycle owns its exact lock token, establishes idle foreground, verifies
uploaded bytes, observes the intended title, closes it, checks declared services
and releases only its token. Use existing owner-started services. Do not change
settings or blindly retry faults. Preserve failed and incomplete receipts.

Batch compatible numerical checks in one launch, upload only changed verified
files and stop observation at completion. Use 30-second performance samples and
at most two-minute normal-session checks. Screenshots or physical interaction
are needed only when the assertion concerns visible display or input behavior.

The [lifecycle contract](lifecycle-reopen.md) requires runtime settling for
reopened HFR presenters. Check ordered reopen/output events as well as rendering,
memory ownership and final teardown.

## CTS selection and acceptance

For a fresh complete campaign and the additional submission prerequisites, use
the [CTS campaign workflow](cts-campaign.md). Its offline planner batches the
existing inventory without promoting historical results to new acceptance.

```sh
make cts-fetch
bash conformance/vk-gl-cts/prepare.sh
```

The [overlay guide](../conformance/vk-gl-cts/README.md) describes the pinned
upstream inventory and six disclosed adaptations. Full swizzle/LOD-bias
workloads are retained.

Use `prepare-cts-shard.py --suite smoke` or affected case lists during development.
The smoke suite has 51 cases across four target configurations, 204 executions.
A release may declare explicit exclusions before running, with reasons;
[SDK 0.2.0](release-g62.md#final-4k-qualification) declares two slow extreme-axis
executions deferred and therefore claims 202, not 204, passes.

Freeze selection and binary identities. Require every selected result, actual
target dimensions, safe teardown and health. Do not merge different binaries,
omit slow cases after the fact or count incomplete work as passing.
Known frozen results need not be rerun without an affected change.

Use `verify-cts-candidate.py --allow-incomplete` for a strict sample audit.
Its full-matrix `complete=false` remains false; sampled completion is a separate
claim, not inherited full coverage. Widen coverage for unexplained failures or
major compiler/resource/synchronization changes.

A full campaign still requires all 9,886 cases in each of four configurations,
no required-case failures, reviewed optional exclusions and clean renderer/
lifecycle checks. A timeout is incomplete, not an automatic kernel panic.

## Reporting and public evidence

Report exact identities, selection, ordered results, rendering/ownership,
lifecycle and health. Use explicit classifications: pass, partial-pass, failed,
inconclusive, transport-failure or no-run.

Keep raw captures and dated development milestones locally. Public reports
summarize supported behavior, results, identity and limits; do not turn them
into running work diaries.

The [published full-campaign export](validation.md) is immutable evidence for
one candidate. Its exporter reruns the raw audit and includes allowlisted
results/provenance, not raw device logs or private paths:

```sh
python3 tools/export-published-validation.py candidate.json \
  --results results/release --destination validation/NEW-CAMPAIGN \
  --renderer imgui=results/final-imgui/PPSA99005-TIMESTAMP \
  --renderer nanovg=results/final-nanovg/PPSA99005-TIMESTAMP \
  --renderer sokol=results/final-sokol/PPSA99005-TIMESTAMP
python3 tools/verify-published-validation.py validation/NEW-CAMPAIGN
```

Never overwrite an earlier export. Eventful accepted cycles require explicit
review in `eventful_receipts`; an empty mapping asserts none were observed.
Incomplete coverage, damaged rendering oracles and existing destinations are
rejected. Published evidence integrity is not independent hardware reproduction.
