# CI-built SDK archives

The **Build SDK release** GitHub Actions workflow builds a fresh developer SDK
on Ubuntu 24.04. Manual runs offer **1080p60** (default), **1440p120** or
**2160p120**; `v*` tag builds retain 1080p60. Its binaries are
**host-checked, not console-validated**.
Historical full CTS, sampled CTS, performance and stability results apply only to
their recorded binary identities, not automatically to these downloads.

## CPU and software-OpenGL CI checks

**Host checks** retains its independent `make test` job and adds a staging job on
Ubuntu 24.04, bounded to 20 minutes overall and 10 minutes for `make test-staging`.
**Build SDK release** also requires `make test-staging` to pass, with a 10-minute
step limit, after fetching sources and before building the SDK. Both workflows
retain their existing `make test` checks and read-only test/build permissions.

The staging lane installs `build-essential`, `clang-18`, the GCC ASan/UBSan
runtimes (`libasan8`, `libubsan1`), EGL/GL development headers (`libegl-dev`,
`libgl-dev`), and system Mesa (`libegl-mesa0`, `libgl1-mesa-dri`, `libglx-mesa0`).
It uses `python3 tools/fetch-sources.py` for the revisions and Mesa archive checksum
in `dependencies.json`; the release workflow keeps its existing `--sokol-samples`
option. No CTS fetch or PS5 SDK build is needed for this lane.

`PS5_OPENGL_PREFIX` points to `third_party/mesa-26.2.0` for the staging script's
headers. The executables link the system `libEGL.so.1` and `libGL.so.1`, run
surfaceless with `LIBGL_ALWAYS_SOFTWARE=1`, `GALLIUM_DRIVER=llvmpipe` and two
renderer threads, and clear library/driver/vendor overrides before execution.
No PS5 driver is injected. UBSan findings stop execution. The existing test
scripts enforce CPU assertions, sanitizer checks, pixel/API oracles and their
deliberate-fault rejection checks; failures and timeouts fail the CI lane.

On a Linux/WSL host with those packages already available, reproduce the staging
step without building an SDK:

```sh
python3 tools/fetch-sources.py
unset LD_LIBRARY_PATH LD_PRELOAD LIBGL_DRIVERS_PATH
unset MESA_LOADER_DRIVER_OVERRIDE __EGL_VENDOR_LIBRARY_FILENAMES __EGL_VENDOR_LIBRARY_DIRS
PS5_OPENGL_PREFIX="$PWD/third_party/mesa-26.2.0" \
  EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  LP_NUM_THREADS=2 MESA_GL_VERSION_OVERRIDE=3.3 MESA_GLSL_VERSION_OVERRIDE=330 \
  MESA_SHADER_CACHE_DISABLE=true UBSAN_OPTIONS=halt_on_error=1 \
  timeout 10m make test-staging
```

Passing this lane covers the selected CPU helpers and software-GL reference
oracles, not native GPU execution, cache coherence, independent MSAA sample
isolation or console qualification. Native-only tests are not run or counted as
passes. The separate `make test-depth-targets` is not part of this lane: system
Mesa can report layer zero when the 1D-array attachment selected layer two. Its
strict query oracle still exits 1 for that known discrepancy; it is not waived,
relabelled as PASS or hidden with `continue-on-error`. See [testing](testing.md)
for the individual checks and native coverage limits.

## Download and use

Use the workflow's **Run workflow** button to build any selected project branch.
Download the `ps5-opengl-sdk` Actions artifact; GitHub wraps the two files below
in a ZIP for download. The SDK itself is a `.tar.gz`, matching Linux/WSL builds:

- `ps5-opengl-sdk-<version>.tar.gz`
- `ps5-opengl-sdk-<version>.tar.gz.sha256`

```sh
sha256sum --check ps5-opengl-sdk-*.tar.gz.sha256
tar -xzf ps5-opengl-sdk-<version>.tar.gz
cd ps5-opengl-sdk-<version>
sha256sum --check SHA256SUMS
export PS5_OPENGL_PREFIX="$PWD/sdk"
```

The archive contains `sdk/` (EGL/GL/KHR headers, 17 regular static archives,
an umbrella linker script, two generated import stubs, Make/pkg-config/CMake
integration), `examples/`, `docs/`, licenses, complete project/dependency source
archives, `consumer-validation.json`, `runtime-config.txt` and `provenance.json`.
The public payload SDK remains a separate prerequisite; it and firmware modules
are not redistributed. No ready-to-launch application or console controller is
included. Native application assembly still uses the pinned boilerplate; see
`docs/consumer-build.md` and `docs/building.md` inside the archive.

The sources include upstream archives plus this project's build files and patches.
Debug information is retained and can contain build-runner paths. Source and
dependency identities are recorded; byte-identical rebuilds across runner/tool
updates are not promised. Compiler versions and SDK hashes are in the consumer
report. Selecting an HFR profile does not confer the frozen G47 binaries'
console qualification.

## Maintainer release process

1. Push and manually run **Build SDK release** on the intended source revision.
2. Inspect host/compiler checks, all 344 Core exports, three relocated consumer
   links, archive checksums and the recorded build configuration.
3. When ready, push a new version tag beginning with `v`. The same workflow builds
   from that tag and creates a **draft prerelease**, never a latest/stable release.
4. Review the draft and its validation wording before publishing it. If console
   validation is performed, retain receipts for these exact bytes and identify
   that scope explicitly. Do not attach an older SDK's acceptance to a rebuild.

Manual runs only create Actions artifacts (seven-day retention); they do not
create tags or releases. Tag builds attach the archive and checksum to the draft.
Existing releases/assets are never overwritten. Repository visibility is not
changed by this workflow. Access follows the repository's permissions.

The build uses pinned public source revisions, the hash-verified payload SDK
v0.42, LLVM 21 from its signed upstream Ubuntu repository, and versioned Python
build tools. GitHub Actions are commit-pinned; build jobs have read-only repository
permission and no console access. Only the separate draft-release job can write
release assets. The older sample-validated bundle remains a separate frozen
artifact, documented in `docs/sdk-bundle.md`.

Frozen releases such as [SDK 0.2.0](release-g62.md) use `sdk-*` tags and
preverified assets rather than this fresh-build workflow. Their archives retain
their original runtime/build provenance and exact-binary acceptance.
A source push or CI pass does not replace those downloads or qualify a new binary.
