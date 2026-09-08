# PS5 OpenGL SDK 0.1.0-perf20260907-sampled

Local distribution candidate containing the unchanged, tested **1080p60
performance SDK**. It is **sample-validated**, not a full CTS rerun or a
Khronos-certified implementation. Nothing is automatically uploaded or installed.
Commands below run from an extracted bundle's root, not this document's directory.

## What is included

- `sdk/`: the original headers, 17 regular static archives, GROUP linker script,
  two project-generated import stubs, Make/pkg-config/CMake metadata and manifest.
- `examples/`: triangle, ImGui, NanoVG, Sokol and cube benchmark source. The
  triangle directly consumes the installed SDK; native examples use the source
  companion and external native-app boilerplate described below.
- `native-app/`: the matching heap helper and import-stub sources.
- `sources/`: project source snapshot, Mesa archive and pinned shader-compiler,
  header and example dependency sources. The project snapshot includes patches
  and build tools; source archives preserve content, not Git checkout metadata.
- `LICENSE`, `LICENSES/`, `THIRD_PARTY_NOTICES.md`, `dependencies.json`: notices
  and source pins. Native boilerplate and public payload toolchain remain external.
- `sample-validation.json`: all 204 sampled case results, actual target layouts,
  clean lifecycle assertions and receipt hashes, without raw console logs.
- `provenance.json`, `SHA256SUMS`: source/build identities and file integrity.

No firmware modules, payload toolchain binaries, native application executables
or ready-to-launch console folder are included. The `.so` files are generated
linker stubs, not vendor implementations.

## Validation and limits

All 51 selected cases passed on each of four targets: 64x64 and 113x47 pbuffers,
plus 64x8192 and 8192x64 depth/stencil FBOs. All four cycles had clean teardown,
service health and lock release on one recorded firmware-6.02 console.
The report intentionally retains `full_matrix_complete=false`.

This runtime also passed the 512-object batch-boundary check, a 128-cube 1080p
profile at 59.94 FPS for ordinary and instanced draws, native Sokol readbacks,
three EGL recreation sessions and relocated SDK consumer linking. These are
bounded checks, not game-performance or universal-stability guarantees.
The historical 39,544-accounted campaign in the source documentation belongs
to a different SDK. Unsampled cases are not inherited passes.

The included binary is fixed to the recorded 1080p60 profile with GPU-present
batching, draw profiling and ordinary/deferred batching enabled. The separate
120 Hz and higher-resolution measurements are not claims for this artifact.
Render-to-texture still uses CPU copies; the measured 1080p FBO workload is
about 15 FPS. SDL/GLFW, exhaustive OOM, suspend/resume, device-loss recovery,
fresh TV/controller interaction and broader hardware coverage remain outside
this candidate's verified scope.

## Verify and link a consumer

Use Linux/WSL, Python 3.11+, GNU Make/binutils, pkg-config and CMake, plus the
external public PS5 Payload SDK v0.42 and its compiler prerequisites. The
recorded runtime/compiler checks use Clang 21.1.8 targeting `x86_64-sie-ps5`.

```sh
sha256sum --check --strict SHA256SUMS
(cd sdk && sha256sum --check --strict manifest.sha256)
export PS5_OPENGL_PREFIX="$PWD/sdk"
export PS5_PAYLOAD_SDK=/absolute/path/to/ps5-payload-sdk
make -C examples/core33-triangle -f Makefile.installed \
  PS5_OPENGL_PREFIX="$PS5_OPENGL_PREFIX" PS5_PAYLOAD_SDK="$PS5_PAYLOAD_SDK"
python3 tools/check-sdk-consumers.py --sdk "$PS5_OPENGL_PREFIX" \
  --payload-sdk "$PS5_PAYLOAD_SDK" --example-dir examples/core33-triangle \
  --registry verification/gl.xml --output ../new-consumer-check
```

The checker uses a new output directory, validates the exact SDK file set and
344 Core exports, and compiles/links via Make, pkg-config and CMake. It does not
rebuild the SDK or access a console. Optional `--forbid-root /original/checkout`
rejects operational dependencies on that checkout. Linking does not execute
the resulting program; do not send the linked graphics ELF to an ELF loader.

## Native applications and sources

Unpack `sources/ps5-opengl.tar` into a new directory for the complete project,
then follow its `docs/building.md` and `docs/consumer-build.md`. The source
snapshot contains the actual native-app builders and all example integration
files. Use the pinned external boilerplate's native-folder workflow in an
already configured owner-controlled environment. Window/input/lifecycle
adaptation remains application-owned.

Use the included SDK as `PS5_OPENGL_PREFIX` when building an installed-SDK
consumer. Do not invoke `make sdk` or the source-tree
`tools/verify-installed-sdk.sh` just to inspect this frozen package: they rebuild
the runtime. Any rebuild is a separately identified candidate. The exact frozen
runtime, source snapshot, archive hashes and build flags are in `provenance.json`.

Native integrations using `native-app/app_heap.c` require all six wraps together:

```text
--wrap=malloc --wrap=calloc --wrap=realloc --wrap=free --wrap=posix_memalign --wrap=malloc_usable_size
```

The bundle preserves absolute build/debug path strings in the tested archives;
relocated link checks establish that consumers do not require those paths.
The inner pkg-config version remains `0.1.0`; the outer version/provenance
identifies this candidate without rewriting tested bytes. Applicable per-file
licenses are retained in the source archives. Fresh consumer builds are not a
bit-for-bit rebuild of the whole runtime, and local package preparation is not
GitHub publication.

## Reproduce the bundle locally

From the source checkout, use the already frozen SDK and original private
receipts; do not rebuild the SDK. Supply the full source snapshot commit to
archive and a new output directory. The packager rejects runtime-source
changes, wrong SDK/receipt hashes, incomplete samples and existing outputs.

```sh
python3 tools/build-sdk-bundle.py \
  --sdk build/sdk/ps5-opengl-core33-g7-60fps-20260907 \
  --source-commit FULL_SOURCE_SNAPSHOT_COMMIT \
  --candidate build/frozen/g8-60fps-cts-smoke-20260907/candidate.json \
  --results results/g8-60fps-cts-smoke-20260907 \
  --destination build/bundles/new-output
```

This release-specific recipe reuses the strict CTS auditor and installed-SDK
checker. It writes a deterministic tar/gzip archive and adjacent SHA-256 file;
it neither regenerates the historical full-matrix export nor publishes anything.
