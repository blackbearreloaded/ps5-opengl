# Khronos OpenGL CTS for PS5

This overlay runs `KHR-GL33` through public PS5 EGL/OpenGL entrypoints. It accepts
the historical development pin `cf7edb26d3be2d8763595ed08fdc41f3c1b1966f` and the
official `opengl-cts-4.6.8.1` release, commit
`067e8832315e79817ede1c4863804e440f5d1c80`.

The six recorded patches cover build/package routing, portable stdio, and one
test correction: desktop compute-shader templates require GLSL 4.30 rather
than every desktop GLSL version (`0005-gl33-negative-compute-guard.patch`).
This is a locally adapted CTS runner, not an untouched upstream executable.
The exhaustive swizzle and LOD-bias test bodies remain unchanged. A 2026-09-05
source comparison verified upstream plus exactly these patches and the seven
byte-identical platform-overlay files, with no other tracked-source changes.

The release path applies only patches 0001–0004 and 0006. It deliberately leaves
the negative-shader test unchanged, including the known compute-stage issue.
`verify-source.sh` compares tracked files against the selected base plus the
prescribed patches, and checks all seven platform overlay files byte-for-byte.
The adapter queries actual default-framebuffer bits/samples, checks them against
EGL, and rejects unsupported surface/config requests rather than substituting
a fixed target description. It still provides only pbuffer contexts; this is
not a completed Khronos submission port.

Prepare a separate release checkout and build stage, preserving historical data:

```sh
git clone --depth 1 --branch opengl-cts-4.6.8.1 \
  https://github.com/KhronosGroup/VK-GL-CTS.git third_party/VK-GL-CTS-4.6.8.1
python3 third_party/VK-GL-CTS-4.6.8.1/external/fetch_sources.py
VK_GL_CTS_ROOT="$PWD/third_party/VK-GL-CTS-4.6.8.1" \
PS5_CTS_APP_STAGE="$PWD/build/cts-release-4.6.8.1" \
PS5_OPENGL_PREFIX="$PWD/build/sdk/ps5-opengl-core33" \
bash tools/build-native-cts-app.sh
```

Set `PS5_NATIVE_APP_TEMPLATE` to the prepared native boilerplate directory.
Use `--mustpass` with both `prepare-cts-shard.py` and `verify-cts-candidate.py`
to select the release's exact inventory. Matching case names do not make
development and release test implementations interchangeable. See the
[campaign prerequisites](../../docs/cts-campaign.md) before acceptance collection.

The upstream checkout is intentionally ignored under
`third_party/VK-GL-CTS`. Prepare it with:

```sh
make cts-fetch
python3 third_party/VK-GL-CTS/external/fetch_sources.py
./conformance/vk-gl-cts/prepare.sh
```

Configure a PS5 static-package build with the native boilerplate SDK:

```sh
export PS5_PAYLOAD_SDK="$PWD/../ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk"
cmake -S third_party/VK-GL-CTS -B third_party/VK-GL-CTS/build-ps5-gl33 \
  -G Ninja -DCMAKE_BUILD_TYPE=Release -DDEQP_TARGET=ps5 \
  -DDEQP_TARGET_TOOLCHAIN=ps5-toolchain \
  -DSELECTED_BUILD_TARGETS=ps5-gl33-runner \
  -DDEQP_DISABLE_VK_VIDEO_TESTS=ON
cmake --build third_party/VK-GL-CTS/build-ps5-gl33 \
  --target ps5-gl33-runner -j8
```

`ps5-gl33-package` registers `KHR-GL33` and the upstream `CTS-Configs`
diagnostic; it excludes the monolithic GLES/EGL test registry from the app.
`ps5-gl33-runner` adds the native-title entry point. It reads one bounded CTS
argument per line from `/app0/cts-args.txt`, writes the full QPA log to
`/download0/ps5-opengl-cts.qpa`, and writes a compact machine-readable status
to `/download0/ps5-opengl-cts.status`.

This is conformance infrastructure, not a claim of conformance. The official
GL 3.3 must-pass list currently contains 9,886 cases. Results must be produced
on hardware from the installed `PPSA99005` native folder application.

Run one frozen shard with the lock-owning native-title wrapper:

```powershell
.\tools\Run-NativeOpenGLCTS.ps1 `
  -Ps5Host <console-host> `
  -ExpectedEbootSha256 <sha256> `
  -ExpectedArgumentsSha256 <sha256> `
  -ExpectedCommit <commit> `
  -ExpectedBoilerplateCommit <commit> `
  -ExpectedProtocolCommit <commit> `
  -ExpectedExecuted <case-count>
```

The wrapper checks the repository and payload identities before acquiring
`Documents\PS5\lock.txt`, verifies ports 2121/3232/9021, uploads the folder,
launches and closes the registered title, retrieves the QPA/status receipts,
checks post-run service health, and removes only its exact lock token. Routine
screenshots are disabled. The app is deployed as a native folder; protocol
`8d9639c` sends only small title-aware ELF launch/close controllers through
elfldr. The user approved this existing control path on 2026-09-05 after
disclosure; the prior claim that no ELF payload was sent was incorrect.
See the control-path clarification in
[the testing guide](../../docs/testing.md).

Targeted core shards default to zero allowed `NotSupported` results. Official
must-pass runs can set `-MaximumNotSupported` because upstream CTS treats
unsupported optional-extension cases as a successful run; failures and device
loss are never accepted.

Prepare deterministic ranges from the official 9,886-case list with
`tools/prepare-cts-shard.py --offset N --count N --configuration 0`. The four
configuration indices correspond to the two required pbuffer sizes/seeds and
the two edge-sized `rgba8888d24s8` FBO configurations in Khronos `mustpass.xml`.
The hardware wrapper verifies both the generated argument-file hash and exact
case-list hash before it acquires the console lock.

After one successful full payload deployment, `-Incremental` uploads the two
binaries plus arguments/case list. Add `-ReuseInstalledBinaries` to verify the
installed binaries by FTP readback and upload only arguments/case list. A hash
mismatch stops before launch. Readback still incurs network traffic.
The verification connection disables ftpsrv's default executable conversion
with its [documented `SELF` toggle](https://github.com/ps5-payload-dev/ftpsrv#features),
so hashes cover the original container bytes, not a reconstructed ELF view.

During development, use `prepare-cts-shard.py --suite smoke` or `--case-list`
for an affected regression. `summarize-cts-qpa.py --inventory <results> --output
<ledger.json>` preserves historical build identities and extracts timings;
`--timings <ledger.json> --budget-seconds 120` selects a measured prefix;
reserve startup margin and obtain approval before exceeding the native run bound.
The complete four-configuration matrix is a release gate, not a per-fix loop.

Use `tools/summarize-cts-qpa.py receipt.qpa --expected-list cts-shard.txt` to
verify ordered completion and list any failing case names. The command returns
nonzero for an incomplete log or any CTS failure/device-loss status.
