# PPSA99005 native OpenGL test app

All PS5 OpenGL hardware gates run as the installed native title `PPSA99005`.
Submitting the OpenGL application to a raw ELF loader is unsupported; managed
title control is a separate, explicit dependency described in [Testing](../docs/testing.md).

List and build public gates with WSL:

```sh
bash tools/build-native-test-app.sh --list
bash tools/build-native-test-app.sh egl_public_core33_texture_rectangle
```

The selected gate replaces the single reusable app folder at
`build/native-app/PPSA99005/dist/PPSA99005`. The build also records the exact
object name in `build/native-app/PPSA99005/selected-test.txt`.

Before hardware use, commit the exact source candidate and record the
`eboot.bin` SHA-256 printed by the build. Run it only through the lock-owning
wrapper:

```powershell
.\tools\Run-NativeOpenGLGate.ps1 `
  -Ps5Host <console-host> `
  -ExpectedGate egl_public_core33_texture_rectangle.o `
  -ExpectedEbootSha256 <64-hex-hash> `
  -ExpectedCommit <40-hex-commit> `
  -ExpectedBoilerplateCommit <40-hex-commit> `
  -ExpectedProtocolCommit <40-hex-commit>
```

Use `-FirstRegistration` only for the first upload of `PPSA99005`. Later runs
atomically replace files under `/data/homebrew/PPSA99005` and use the existing
folder registration.
Use `-Incremental` for an already deployed folder to replace only the executable,
runtime module, and metadata. Uploads are verified before promotion; health and
receipt traffic use WSL. The supplemental `egl_public_core33_render_target_limits`
gate adds 14 sequential depth allocations (at most 64 MiB requested per target),
edge/center pixel checks, and a recovery case. These are not official CTS cases.
The `egl_public_core33_color_target_limits` gate preserves the original 128x128
MSAA image oracle and adds 20 bounded texture/renderbuffer cases across five
color formats, including masked 4x resolves and scissored clears. Each logical
source image is at most 64 MiB; staging/resolve storage is additional. The batch
stops on its first failure. Host check: `python3 tests/ps5/test_color_target_limits.py`.

`tools/audit-native-test-catalog.sh` compiles every catalogued gate and checks
that all gate-level imports are supplied by the native OpenGL runtime or PS5
SDK stubs.

Each run writes an unbuffered `/download0/pss-opengl.log` receipt. The managed
runner retrieves it after teardown and accepts only `gate completed status=0`.
Ordinary failed gates remain in safe idle. A native GPU submission/suspend error
or unconfirmed completion instead terminates the application before buffer
cleanup or exit handlers can reuse potentially in-flight memory. This fail-stop
path is tested with host mocks, never by deliberately faulting the console.

Build the official bounded Khronos GL33 CTS runner into the same title ID with:

```sh
bash tools/build-native-cts-app.sh
```

Its default `/app0/cts-args.txt` runs only `KHR-GL33.info.*`. Later bounded
shards replace that file in `/data/homebrew/PPSA99005`, then relaunch the
folder app. Results are written to `/download0/pss-opengl-cts.qpa` and
`/download0/pss-opengl-cts.status`.
