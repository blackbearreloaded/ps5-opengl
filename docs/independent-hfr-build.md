# Independent HFR source build: G43

Source: `23a594c3a5fda4135599d0dea8d2bf4dcef47a39`. Both independent
source exports match all 389 tracked Git blobs. No graphics, SDL, toolchain,
packager or shared source was edited. Only this report is a tracked change.

The referenced `.local/` and `build/` artifacts and helper scripts are retained
only in the G43 clone, `ps5-opengl-g43-independent-20260908`, and are unavailable
in the published checkout. Artifact paths below are relative to that clone;
the recorded hashes identify those retained local files.

- 2026-09-08 | G43 | 23a594c | host pass: two GL profiles, 344 exports each, six GL links, two 1440 SDL links | `.local/evidence.json` | console no-run.

| Profile | GL manifest / exports | Relocated GL consumers | Fresh SDL2 |
| --- | --- | --- | --- |
| 2560x1440, nominal 120 Hz | 38 files / 344 of 344 | Make, pkg-config, CMake PASS | Native build and pkg-config/CMake links PASS |
| 3840x2160, nominal 120 Hz | 38 files / 344 of 344 | Make, pkg-config, CMake PASS | Optional build omitted within the parent task's scope |

Both GL runs used the existing `check-sdk-consumers.py --forbid-root` verifier.
The final audit additionally rejects operational header/link inputs from either
build source tree, the first Windows build tree, or canonical OpenGL. The five
host compiler contracts and the existing static GL capability audit passed;
SDL's existing integrity/profile checks passed. These are host checks, not
HDMI, rendering, frame-rate, lifecycle or CTS acceptance. No console traffic,
console lock access, GitHub mutation, global installation or extra agent occurred.

Inputs reused read-only:

- Installed GCC/LLVM and the public payload SDK v0.42, relocated into the private
  WSL build directory. Boilerplate pin:
  `4e1d1277dd0531a9a9df8c780e446b9cc26534dd`; SDK archive SHA-256:
  `8cfbc7cd5811e719eb4f0c47eea668d3dc7b40bc8ab11c4a5031d40c23ec02da`.
  Consumer traces identify reused CRT, libc/libc++/libc++abi/libunwind and public
  platform imports; their exact paths/hashes are in `reused_payload_link_inputs`
  in the local artifact `.local/evidence.json`.
- Cached Git objects for opengnm-psbc, opengnm, SPIRV-Headers, Vulkan-Headers,
  ImGui, NanoVG and Sokol, fetched at the exact [dependency pins](../dependencies.json).
  PSBC upstream is `a92a1228ea3a64e4be9f0e61c2a65a5aa7ffed92`; the supplied
  patch verifies tree `595822fc23fd4895e25f83f65acb40d615d813b0`.
  ImGui/NanoVG/Sokol were fetched, not compiled in this lane.
- Mesa 26.2.0 source archive, SHA-256
  `efd4bb08cdb7c365a812cd4e6c9202ab55b2f22cdcd13c7d6c4f9647b799a4ef`.
- SDL 2.30.12 source exported from cached commit
  `8c56053f13ca13a0c050de613706ff69eb615836`; clean tar SHA-256
  `9dc445a5add6a6abccbad323d173881fe489ec5fe196c26bc08f127c47f00ee4`.

Newly compiled: host PSBC and target PSBC; Mesa's 15 installed dependency archives
(`blake3`, `compiler`, `gallium`, `glapi`, `glapi_bridge`, `glcpp`, `glsl`,
`glsl_util`, `mesa`, `mesa_sse41`, `mesa_util`, `mesa_util_c11`, `mesa_util_simd`,
`nir`, `vtn`); both profile-specific GL runtimes and their two import stubs;
1440p SDL2 and its example object; eight linked consumer executables.
2160p reuses this task's freshly compiled, resolution-independent Mesa/PSBC
dependencies through links into the 1440p build tree. It is independent of G31,
not a second compilation of those common dependencies. No canonical compiled
GL/Mesa/PSBC library was used as an independent output.

Tools: GCC/G++ 15.2.0; target Clang/LLD 21.1.8; SDL Clang 18.1.8;
Git 2.53.0; Make 4.4.1; Python 3.14.4; Meson 1.10.1; Ninja 1.13.2;
CMake 4.2.3; Bison 3.8.2; Flex 2.6.4; pkg-config 2.5.1;
glslang 16.2.0; SPIRV-Tools 2026.1; Mako 1.3.10.dev0, PyYAML 6.0.3,
packaging 26.0. Full versions and compiler-wrapper hashes are retained in
`.local/logs/tool-versions.txt` and
`.local/logs/staged-public-toolchain.txt`.

Flags: host PSBC uses GNU C11/C++17, `-O2 -g -Wall`; target PSBC adds `-fPIC`
and `OPENGNM_PSBC_ORBIS=1` (C++ also `NDEBUG`). Mesa is static
`debugoptimized`, optimization 2, debug enabled, LTO disabled, with the pinned
PS5 cross file and unchanged `build-mesa-ps5.sh` feature switches; full values
are in `.local/logs/mesa-build-options.json`.
The runtime uses `-Os -g -Wall -Wextra -Werror`, the pinned Core definitions,
`PS5_NATIVE_TITLE_RUNTIME=1`, `PS5_SCANOUT_FPS=120`, the selected height,
`PS5_MULTIDRAW_BATCH=1`, and `PS5_DEFERRED_DRAW_BATCH=1`.
`PS5_DRAW_PROFILE` and `PS5_GPU_PRESENT_BATCH` are not enabled in this lane.
Exact runtime commands/configuration are in the logs and evidence JSON.
SDL uses Release, `__PROSPERO__`, PIC, function/data sections, and the existing
`-ffile-prefix-map=<SDL-output>=.`. Build concurrency never exceeds two:
local Make/Ninja wrappers cap jobs; the runtime Make invocation serializes
its sibling recipes around the two-job Ninja prerequisite.

New GL SDKs are `build/relocated/{1440,2160}p120/gl`; each runtime below means
`lib/libps5_opengl_core33.a`. New SDL is `build/sdl-native-1440p120/sdk`.

| Artifact | SHA-256 |
| --- | --- |
| 1440 GL manifest | `e043a462434d869a6f997e1f4e1e665d0fb6366187ba0bf19435905197e768fd` |
| 1440 GL runtime | `d5e8b920d50d2a8736bcbdc4e17862b1175ccf43d96545cfcf2f4385fada070c` |
| 2160 GL manifest | `d90d842ae8edd631d09b7711a7285b135a8c636bd804bb6478a64abb29d819e1` |
| 2160 GL runtime | `7384d8aa3db5d4921e556bf01892cc39095174984ba2f39290a3102ab179b93f` |
| 1440 SDL manifest | `8d81859c4bb015d227de18eae92c750e8792250faa9f4c4b8bfb3cb74ae28007` |
| 1440 SDL `lib/libSDL2.a` | `f5a239bea082317b579d4bf0c86eb5c36d03f3cfcf8a5ff3d2c9820890ac0af4` |
| 1440 SDL outer `receipt.json` | `bda7c3437f4d624bfc81b74e89ec26028d7385a6c348939b1bf259de0b4e62dc` |

Relative consumer executable paths and all hashes are in
the local artifact `.local/evidence.json`, SHA-256
`f5df8c6de14170ea423fab8e3cc4a1b3d297e4c610958af8eda9463a7261e329`.
Raw GL reports are `build/consumers/gl-{1440,2160}p120/summary.json`;
SDL commands are `build/consumers/sdl-1440p120/commands.json`.
The WSL intermediate root is recorded in `.local/wsl-build-root.txt`;
installed SDKs, consumer artifacts and logs are retained in this clone.

Compared with G31: graphics/toolchain source has no diff from `3cdc90b` to the
chosen pin. Each new GL package differs in all 17 archives; its other 21 entries
match. This is not a byte-equivalence claim: paths and the explicitly selected
runtime flags matter. The newly compiled 1440 SDL archive and integration inputs
match the frozen matching SDL build exactly; its new receipt/manifest bind it
to the new GL SDK and confer no hardware acceptance. Both frozen GL manifests
and corresponding frozen SDL build receipts verify unchanged.

Path finding: GL has no file/debug prefix maps. All 17 GL archives per profile
contain the private WSL build root, including allocated runtime
`.rodata.str1.1` strings as well as debug paths. Debug stripping alone does not
make these runtimes path-free. None of the 41 inspected installed library/linker
files contains the selected personal-directory markers; the SDL archive also
has no matched WSL build root. This literal-root scan is not proof that arbitrary
absolute paths are absent. Local artifacts `.local/path-audit.json` and
`.local/logs/runtime-rodata-paths.txt` retain the findings. Publication
derivatives remain outside this task; no frozen bytes or receipts were rewritten.

Recorded WSL sequence, using the scripts retained locally under `.local/` and new
output roots for any reproduction (existing outputs must be preserved):

```sh
# Initial clone used git clone --no-hardlinks --no-checkout, then checkout --detach SOURCE_PIN.
bash .local/stage-wsl.sh > .local/logs/build-gl-1440-wsl.log 2>&1
bash .local/extend-2160.sh > .local/logs/build-gl-2160.log 2>&1
bash .local/build-sdl.sh 1440 -retry1 > .local/logs/build-sdl-1440-retry1.log 2>&1
python3 .local/audit-paths.py
python3 .local/evidence.py
```

The GL scripts export the pinned source, fetch only pinned cached Git objects,
verify/extract the cached Mesa archive, run `make source-fetch`, `make sdk`,
the existing installer and `check-sdk-consumers.py --forbid-root`. The SDL script
uses unchanged `integration/SDL2/build.py native` and `tools/test_sdl_sdk.py`.
Script hashes, source-archive hash, flags, commands and results are retained in
the evidence. The successful lane commands above and final audits returned zero.

Preserved interruptions: the first Windows-filesystem GL attempt was deliberately
terminated for I/O stalls (`.local/logs/build-gl-1440.log`); its partial tree remains.
The first SDL configure returned 1 because the local Ninja wrapper appended
`-j2` to `-t restat` (`.local/logs/build-sdl-1440.log`). Passing tool mode through
unchanged fixed that local wrapper; `ninja -t list` and the distinct SDL retry
passed. Neither required a GL rebuild. No dependency/network blocker remains.
2160 SDL is unattempted, and all new bytes remain host-only pending parent review.
