# SDL2 / frozen G19 integration (local candidate)

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

Run from the G26 clone; choose a **new** output directory for each build:

```sh
sdl=/path/to/cached/SDL
g19=/path/to/frozen/ps5-opengl-core33-g19-blit-swizzle
template=/path/to/ps5-native-app-boilerplate
python3 integration/SDL2/build.py host \
  --sdl-source "$sdl" --g19-prefix "$g19" --out build/g26-host

python3 integration/SDL2/build.py native \
  --sdl-source "$sdl" --g19-prefix "$g19" --out build/g26-native \
  --payload-sdk "$template/.deps/native/ps5-payload-sdk" \
  --compiler-wrapper "$template/tooling/prospero-clang18"

ctest --test-dir build/g26-host/cmake --output-on-failure

python3 integration/SDL2/folder.py --native-build build/g26-native \
  --template "$template" --g19-prefix "$g19" --out build/g26-folder
bash tools/verify-native-test-app.sh build/g26-folder
```

No fetch, install or canonical SDK rebuild is performed. Inputs are hash-checked
before and after building. The expected G19 manifest is
`344673952a789cae7e4a6d4c6670e3cf8c6bbf595fb07ebf27a8ffe3680014be`;
`lib/libps5_opengl_core33.a` is
`627857a44a8101b0ab0df319293a554143e96405be8a1ec55fc48bbd1830caa5`.
`lib/libPS5OpenGLCore33.a` (capital letters) is the SDK's linker GROUP script,
not the runtime archive. Use the whole verified SDK and its public linker flags.
Each completed build writes source/input/output hashes in `receipt.json`.
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

For another application, build this SDL archive once, consume the generated
`cmake/sdl/include/SDL2` and `cmake/sdl/include-config-release/SDL2` headers,
and link `cmake/sdl/libSDL2.a` with the SDK's documented static C++ dependency
group and the public payload SDK's Pad/UserService imports. Define
`SDL_MAIN_HANDLED`, call `SDL_SetMainReady`, and supply your ordinary native-app
entry point. Do not link `SDL2main` or OSMesa. The native folder requires the
existing complete `native-app/app_heap.c` allocator wrap set and native CRT;
cross-compiling an object alone does not produce a runnable application.

## Supported boundary and tests

One fixed 1920x1080 window, one unshared 3.3 Core context, default context flags,
RGBA8 double buffering, config-checked depth/stencil and interval 0 or 1.
G19 always owns the physical scanout. SDL's requested interval is reported;
this is not a new timing or swap-tearing guarantee. Window/context operations
belong on the video thread. The example is bounded to 180 frames and also exits
on quit, Escape events or joystick button input. It checks load/create/make/swap
failures and releases its joystick, context, window and SDL subsystems.
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

Keyboard/IME hooks exist upstream but are omitted here (their dialog, service
and presentation contract has not been qualified with G19). No mouse, audio,
software window surface, SDL_Renderer, dynamic GL loading, resize, multiple
windows/contexts, context sharing, high DPI, HDMI-mode change or suspend/resume
coverage is claimed. Upstream PS5 joystick hotplug/multi-user edge cases are
unchanged and unqualified. No new device-loss recovery is supplied; failed EGL
cleanup retains resources rather than freeing anything still current. Hardware
qualification and any bounded native-folder launch belong to the parent.
