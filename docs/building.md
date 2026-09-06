# Building

Build on x86-64 Linux or WSL. This source publication does not ship prebuilt
applications or an SDK binary release.

## Prerequisites

- Git, Make, GCC/G++, binutils and CMake.
- Clang/LLD 18 for the native app adapter, plus the Clang/LLD selected by the
  public SDK wrappers (21.1.8 in the recorded Mesa/PSBC builds).
- Python 3.12+, Meson, Ninja, Mako, PyYAML and packaging.
- `glslangValidator` and SPIR-V Tools for shader-compiler checks.
- Host EGL/OpenGL development headers/libraries for optional renderer tests.
- The public PS5 Payload SDK supplied by the pinned native app boilerplate.

The recorded toolchain used Clang 18.1.8 for native-title conversion/link glue,
Clang 21.1.8 through the public SDK for Mesa, and Meson 1.10.1. Verify the SDK
wrapper's `--version` as well as `clang-18 --version`; they are distinct build
paths. Upstream commits and archive hashes are pinned in
[dependencies.json](../dependencies.json).

## Native app boilerplate

Place the repositories alongside each other:

```sh
git clone https://github.com/blackbearreloaded/ps5-native-app-boilerplate.git ../ps5-native-app-boilerplate
git -C ../ps5-native-app-boilerplate checkout 4e1d1277dd0531a9a9df8c780e446b9cc26534dd
bash ../ps5-native-app-boilerplate/tools/setup-native-dependencies.sh
```

Its setup verifies the SDK v0.42 archive and prepares build-time dependencies.
For another layout, set `PS5_NATIVE_APP_TEMPLATE` to its absolute path; Make
derives `PS5_PAYLOAD_SDK` from its `.deps/native/ps5-payload-sdk` directory.
Keep that same SDK for library and folder-app builds. The SDK installer can also
accept an explicit `PS5_PAYLOAD_SDK` for library-only workflows. Do not copy
vendor SDKs or firmware modules here.

## Fetch and build

```sh
make source-fetch
make sdk
make imgui-demo
```

Source fetch checks immutable commits and the Mesa archive hash. Unexpected
revisions or tracked edits are rejected, not reset. PSBC changes are supplied as
one patch over public commit `a92a1228`; the resulting Git tree must be
`51dcd101eb3235e6d650af5e3ee40e1a449035fc`, matching the validated compiler sources.
The local research commit `a11c1925` is provenance, not a remote prerequisite.

The Mesa patch is checked before application. `make sdk` builds host and PS5
compiler libraries, Mesa, the native backend and the installed SDK. ImGui,
NanoVG and Sokol consume that package without rebuilding the graphics stack.
Rebuild the SDK after runtime changes before testing those consumers.
`make cubes` instead links the current source runtime for performance comparisons;
it does not update the installed SDK. `make test-cubes` runs its software-Mesa
reference and deliberate fault checks locally, without console access.

| Output | Purpose |
| --- | --- |
| `build/sdk/ps5-opengl-core33/` | Relocatable headers, archives and build integration |
| `build/sdk/ps5-opengl-core33/manifest.sha256` | Installed-file identity |
| `build/native-app/PPSA99005/dist/PPSA99005/` | Native homebrew folder app |
| `build/native-app/PPSA99005/selected-test.txt` | Selected example/test identity |

`make nanovg` and `make sokol` replace the same reusable output. Archive frozen
candidates before building another if their bytes are needed for validation.

## Install and run

Use an owner-configured console that already supports native folder apps.
Upload the generated folder to `/data/homebrew/PPSA99005`, then launch its
registered title. Subsequent iterations replace files there and relaunch.
Console enablement is outside this project.

Do not submit the OpenGL application ELF to elfldr. Optional Windows/WSL managed
testing uses the separately approved title control path in [Testing](testing.md).

## Reproducibility boundary

The publication preserves validated runtime sources and supplies the previously
local compiler changes. Source reproducibility is distinct from byte-identical
executables: compiler versions, debug paths and packaging can change hashes.
A fresh executable needs its own hardware receipt. Published results belong to
the frozen identities in [the validation report](validation.md).
