# SDL2 / public PS5 OpenGL SDK

New builds have **no inherited hardware qualification**. The exact G25/SDL2 pair
in the [September 8 SDK guide](../../docs/sdk-bundle-g25.md) passed a fresh native
180-frame/two-pixel check, teardown and service-health verification. Physical
controller/reconnect behavior remains unverified. G19 acceptance below belongs
only to its historical binary. The adapter's `ps5-g19` driver/log names are unchanged.

This is an SDL2 video-device implementation, compiled into **real SDL2**. The
consumer calls `SDL_CreateWindow(SDL_WINDOW_OPENGL)`, `SDL_GL_CreateContext`,
`SDL_GL_GetProcAddress`, `SDL_GL_MakeCurrent`, `SDL_GL_SwapWindow` and
`SDL_PollEvent`. No public SDL function is replaced. No SDL3/GLFW code is involved.

## Existing hooks and source

The cached [PS5 SDL fork](https://github.com/ps5-payload-dev/SDL) of
[upstream SDL](https://github.com/libsdl-org/SDL) is SDL **2.30.12**, commit
`8c56053f13ca13a0c050de613706ff69eb615836`. Its `LICENSE.txt` is the SDL zlib
license. The builder exports that exact commit and retains its license notices;
it ignores the dirty cached `src/dynapi/SDL_dynapi.h`. `static-ps5.patch` applies
the already-used static PS5 dynapi switch to the isolated source copy, marked as
an alteration. New integration code is GPL-3.0-or-later, like this repository.

The fork already registers all nine relevant `GL_*` video hooks in
`src/video/ps5/SDL_ps5osmesa.c`, but they load `libOSMesa.so.8`, render into
`SDL_GetWindowSurface`, and present via SDL's separate VideoOut allocation.
`src/video/ps5/SDL_ps5video.c` opens that scanout during video initialization.
Simply replacing `GL_GetProcAddress` would leave conflicting presentation owners.
SDL's generic EGL helper also assumes a dynamically loaded EGL implementation;
the frozen SDK is static and requires native window handle **zero**, with NULL
window attributes. This driver calls that public EGL interface directly.

Yamagi's `src/ps5/ps5_gl3.c` delegates to its `ps5_egl.c` for this same public
EGL sequence, but its `ps5_video.c` hands the renderer a game-specific sentinel.
Its `ps5_input.c` separately calls `SDL_JoystickUpdate` on a polling thread.
Here SDL owns the window/context handles, and `SDL_PollEvent` invokes SDL's
existing joystick update path. The original PS5 joystick implementation and
SDL's event queue are retained, with no game-specific polling thread.

The cached Yamagi package `ps5-payload-dev-v0.40.2.tar.gz` has SHA-256
`a85f65de418a8e6a898c6c3e3c870d50fff7618a200e4dd59ea9692af6ecec4d`.
It is reference material only: this candidate builds SDL from the pinned source.

## Reproduce locally (Linux / WSL, Python 3.12+, CMake, Ninja, C compiler)

Run from the repository root; choose a **new** output directory for each build:

```sh
sdl=/path/to/cached/SDL
sdk=/path/to/verified/ps5-opengl-core33-g25-srgb
template=/path/to/ps5-native-app-boilerplate
python3 integration/SDL2/build.py host \
  --sdl-source "$sdl" --sdk-prefix "$sdk" --out build/host-build

python3 integration/SDL2/build.py native \
  --sdl-source "$sdl" --sdk-prefix "$sdk" --out build/native-build \
  --payload-sdk "$template/.deps/native/ps5-payload-sdk" \
  --compiler-wrapper "$template/tooling/prospero-clang18"

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build/host-build/cmake --output-on-failure

python3 integration/SDL2/folder.py --native-build build/native-build \
  --template "$template" --sdk-prefix "$sdk" --out build/sdl-folder
bash tools/verify-native-test-app.sh build/sdl-folder
python3 tools/test_sdl_sdk.py

# Three real-SDL sanitizer builds; profile SDK copies are host fixtures only.
python3 tools/test_sdl_sdk.py --sdl-source "$sdl" \
  --host-sdk-prefix "$sdk" --host-out build/host-mode-matrix
```

No fetch, system install or supplied SDK rebuild is performed. `--g19-prefix`
remains an alias for `--sdk-prefix`; neither spelling pins private G19 bytes.
The existing `tools/check-sdk-consumers.py` verifier checks the complete supplied
GL manifest, canonical relative paths, symlinks and archive format; the SDL
builder also requires the public headers, libraries and consumer metadata.
Inputs are hash-checked before and after building; a changed identity fails.
The manifest-covered `include/ps5_opengl_display.h` selects the fixed render and
presentation profile: 1920x1080, 2560x1440 or 3840x2160, at nominal 60 or 120 Hz.
This is not negotiated HDMI timing and introduces no runtime ABI. SDKs without
that header explicitly retain legacy **1920x1080 at nominal 60 Hz**. Malformed
or unsupported headers fail validation; a present header never falls back.
`lib/libPS5OpenGLCore33.a` (capital letters) is the SDK's linker GROUP script,
not the runtime archive. Use the whole verified SDK and its public linker flags.
Each completed build writes source/input/output hashes in `receipt.json`.
Source checksums provide integrity, not authentication or hardware evidence.
The pinned source tar SHA-256 is
`9dc445a5add6a6abccbad323d173881fe489ec5fe196c26bc08f127c47f00ee4`.
It retains exactly two upstream Android symlinks into the same source tree;
the verifier permits only those exact name/target pairs, rejecting other links,
unsafe paths, duplicate members and special files.
Native configuration uses SDL's existing fallbacks for `wcslcpy`, `wcslcat`,
`wcscasecmp` and `wcsncasecmp`: the cached SDK declares them, but the native
folder libc does not export them. SDL's fallback case comparison is ASCII-only.
The folder assembler reuses this repository's heap, CRT layout, import-stub
sources and existing native-app builder. It copies writable dependencies,
disables dependency setup and runtime rebuild, and verifies cached libc's
checksum. `candidate.json` identifies every produced folder file and the reused
libc. The folder uses the existing gate identity **PPSA99005** and its main-return
hold for parent-controlled teardown; it is never installed or launched here.
The assembler writes stage-root `selected-test.txt` as `egl_public_core33_sdl2.o`
and runs the existing `tools/verify-native-test-app.sh`. Unchanged runtime shims
supply `pss-opengl.log` and the gate-return marker for the shared native runner.
The verified SDK profile sets `attribute3=0x80040` for nominal refresh above
60 Hz; 60 Hz retains the ordinary template metadata. `candidate.json` records
`display_profile` with `width`, `height` and `fps` from that same verified SDK.

## Installed payload and receipts (schema version 1)

The native build installs into `native-build/sdk`, separately from the GL SDK:

```text
native-build/
  receipt.json                 build receipt
  sdl-source.tar               unmodified exact-commit git archive
  integration/                 integration sources used by this build
  sdk/
    include/SDL2/              public and generated configuration/revision headers
    lib/libSDL2.a              regular static archive, not a thin archive
    lib/pkgconfig/sdl2.pc
    lib/cmake/SDL2/            upstream CMake config and static target exports
    share/licenses/SDL2/LICENSE.txt
    share/licenses/SDL2-PS5/LICENSE
    share/SDL2/README.md
    share/SDL2/receipt.json    installed receipt
    manifest.sha256           every installed file except this manifest itself
```

Both receipts contain `schema_version: 1`, `mode`, `hardware_run: false`,
`sdl_commit`, `sdl_source_tar_sha256`, `sdk_manifest_sha256`,
`sdk_runtime_sha256`, `sdk_files`, `integration_inputs` (filename to SHA-256),
`receipt_tool_sha256`, and `artifacts` (relative path to SHA-256).
New receipts also contain `display_profile` (`width`, `height`, `fps`) and cover
the integration's `ps5g19_display.h`. Existing schema-1 G25 receipts without
those additions still verify against their matching legacy SDK; the SDK
identity remains its original three fields. Profile-bearing SDKs require a
matching profile in the SDL receipt.
The outer receipt identifies the build archive and example object, and adds
`payload_receipt_sha256` and `payload_manifest_sha256`. Installed `artifacts`
cover every payload file except the installed receipt and manifest, avoiding
a hash cycle; the manifest includes the installed receipt. No absolute paths
or hardware acceptance are recorded. The source tar is the clean upstream
snapshot; the integration source and marked static patch describe alterations.
Keep the matching source tar, integration sources and licenses with distribution
source materials; the binary payload alone is not the complete source tree.

`build.verify_native_build(native, prefix)` accepts resolved `Path` objects and
returns the outer receipt after checking source, native artifacts, the complete
installed payload and exact receipt-to-GL-SDK identity. `folder.py` uses this
before and after assembly. It rejects legacy G19 receipts; rebuild them using
this lane. The SDL manifest describes an SDL payload, **not a full GL SDK**.
Snapshot hashes are checked against the recorded build, not the current mutable
integration README. Later acceptance documentation is a separate companion;
it must not rewrite build receipts or reassign results to different binaries.

The `sdk` tree can be copied to a new prefix or staged with `DESTDIR`; no
original checkout is a consumer dependency. Use its already completed tree
when packaging: a raw repeat of upstream `cmake --install` does not regenerate
the final receipts. `sdl2-config`/Autoconf metadata is omitted because its
upstream flags do not describe this adapter. Standard supported consumers:

```sh
export PKG_CONFIG_LIBDIR="$sdl_prefix/lib/pkgconfig:$gl_prefix/lib/pkgconfig"
pkg-config --cflags --libs --static sdl2
```

```cmake
find_package(SDL2 CONFIG REQUIRED) # CMAKE_PREFIX_PATH contains both prefixes
target_link_libraries(app PRIVATE SDL2::SDL2) # SDL2::SDL2-static also supported
```

The metadata composes with `PS5OpenGLCore33::OpenGL` / `ps5-opengl-core33`
and Pad/UserService/SystemService imports. It supplies `SDL_MAIN_HANDLED=1`;
manual consumers must define it too, call `SDL_SetMainReady`, and supply their
ordinary native-app entry point. Link using the PS5 C++ compiler driver, or
include the public SDK's static C++ group (`libc++`, `libc++abi`, `libunwind`
and compiler builtins) when driving the linker directly. The payload toolchain
provides platform imports. Do not link `SDL2main` or OSMesa. The native folder requires the
existing complete `native-app/app_heap.c` allocator wrap set and native CRT;
cross-compiling an object alone does not produce a runnable application.

## Supported boundary and tests

One fixed window at the selected SDK dimensions, one unshared 3.3 Core context, default context flags,
RGBA8 double buffering, config-checked depth/stencil and interval 0 or 1.
G19 always owns the physical scanout. SDL's requested interval is reported;
this is not a new timing or swap-tearing guarantee. Window/context operations
belong on the video thread. The example is bounded to 180 frames and also exits
on quit, Escape events or joystick button input. It checks load/create/make/swap
failures and releases its joystick, context, window and SDL subsystems.
SDL advertises exactly the selected desktop/current/display mode. The driver
rejects other window dimensions and any actual EGL drawable-size mismatch,
including for legacy SDKs; attempted resize restores the selected size.
The example checks `SDL_GetDesktopDisplayMode` and uses its dimensions, checks
the drawable, and logs `nominal_refresh` explicitly as not negotiated HDMI.
That refresh value is the profile target, not a measured application frame rate.
At frames 0 and 179 it resolves/calls `glReadPixels` through
`SDL_GL_GetProcAddress`, reading one center RGBA8 pixel before swapping. Expected
values are `0,38,102,255` and `254,38,102,255`, with at most one byte of error
per channel. Both probes log actual/expected bytes and pass/fail. Full parent
rendering acceptance requires both passing probe lines and the final
`[sdl2-g19] frames=180 probes=2 status=0`. Early user exit remains successful
application behavior but is not full 180-frame acceptance.

The host check uses ASan/UBSan and links upstream SDL and the production adapter, with only EGL/GL
replaced by an explicit contract double. It exercises partial-init failures,
config/surface/query/context/make-current errors, failed second-window isolation,
unsupported requests, proc lookup, detach/rebind and failed-detach ownership,
thread rejection, swap error reporting, cleanup,
reinitialization, real SDL queue and virtual-joystick delivery, and the example.
The example checks cover both probe coordinates/formats/frame numbers, a
one-byte tolerance, two-byte mismatches at either frame, and early quit.
It does not execute GPU rendering or physical input.
The optional host matrix runs legacy 1080p60, 1440p120 and 2160p120 against
separate SDK copies with regenerated manifests. These copies are explicitly
host profile fixtures, not native SDKs: their runtime archives are unchanged
and are not linked into the host contract. Every case checks advertised modes,
window/drawable sizes, both center coordinates, mismatches, resize and cleanup.
The lightweight distribution checks also compile all six profile constants,
reject invalid profiles, check HFR metadata and preserve legacy receipt verification.

Keyboard/IME hooks exist upstream but are omitted here (their dialog, service
and presentation contract has not been qualified with G19). No mouse, audio,
software window surface, SDL_Renderer, dynamic GL loading, resize, multiple
windows/contexts, context sharing, high DPI, HDMI-mode change or suspend/resume
coverage is claimed. Upstream PS5 joystick hotplug/multi-user edge cases are
unchanged and unqualified. No new device-loss recovery is supplied; failed EGL
cleanup retains resources rather than freeing anything still current. Hardware
qualification and any bounded native-folder launch belong to the parent.

## Historical G19 acceptance (September 8; not these new binaries)

Source companion `9cf0daf` / agent source `f25a3a6`, unchanged frozen G19 SDK:
**180 frames and two exact center-pixel checks passed** at a 1920×1080 drawable.
Frame 0 read `(0,38,102,255)`; frame 179 read `(254,38,102,255)`. SDL cleanup,
native-title teardown, service health and exact-token release passed.
The bounded run used `egl_public_core33_sdl2.o`, `PPSA99005` and a 30-second
observation cap with the existing native-folder runner; no screenshots were needed.

The tested eboot SHA-256 is
`ddd2b0b1acf16ef0772298a517b560349ef9929b205cfb4929e239e6b4ee861e`;
hashed acceptance is retained in `results/g26-sdl2-20260908/acceptance.json`.
This proves the small standard-SDL consumer, not physical controller events,
HDMI timing, arbitrary SDL applications or G25 runtime qualification. The
upstream event queue/virtual joystick and failure contracts passed host sanitizers;
they do not establish console hotplug behavior. SDK bundles remain unchanged.
