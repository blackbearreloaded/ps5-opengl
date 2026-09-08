# Local SDK candidate 0.1.0-perf20260908-sampled

This **local-only, sample-validated 1080p60 SDK** preserves the frozen G13
runtime. It is not published, a full CTS rerun, or Khronos certification.
The September 7 prerelease remains unchanged. The separate PPSA77800 app and
its private artwork are not included; its 4K120 results do not qualify this SDK.

## Evidence and identity

- Runtime source: `16e651b3d6e871c3986dc5710a9ca897fbb6e4ab`.
- SDK manifest: `5dcdd41a1e26d714de88e8628e449098055f72dce842bd70106ada73140d9827`.
- Runtime archive: `dd636df0119009173ace9b196b4903fd5da5e515e7e9ae3f1bea5273a871251a`.
- **204/204 sampled Pass results**, 51 per configuration, on one recorded
  firmware-6.02 console. The four complete receipts require clean teardown,
  service health and lock release. An incomplete 240-second attempt is excluded;
  the accepted replacement completed within a 450-second ceiling.
- Matched 1080p offscreen ImGui: **19.98 to 59.95 FPS** after native single-mip
  RGBA8 storage reuse; p95 17.20 ms, not perfect 60 FPS frame pacing.
- Ten-minute G13 soak: 35,912 frames, 21 probes, no steady tracked heap/GPU
  allocation growth; linked-title direct/mapped bytes and counts return to zero.
  Native texture-copy and layered-mip regressions also passed.

The machine-readable `sample-validation.json` contains the accepted individual
results and evidence hashes, not raw console logs. It explicitly retains
`full_matrix_complete=false`. The historical 39,544 results apply to another
binary. See [enhancement gates](enhancement-plan.md) for the broader test scope.

Other formats, mips and layers retain staging; CPU fallbacks remain. This is not
multi-hour, deliberate hardware OOM, suspend/resume, device-loss, cross-firmware
or universal-application acceptance. SDL/GLFW platform adaptation is not supplied.
Manual cold-launch acceptance for the separate local 4K app remains open.

## Contents and use

The archive includes compiled SDK libraries/headers, examples, dependency sources,
licenses, `provenance.json` and `SHA256SUMS`. No proprietary runtime modules,
console-enablement code, payload toolchain binaries or ready-to-launch app are
included. Build flags are recorded in provenance; scanout is fixed at 1080p60.
The SDK has no GPU-memory ledger; optional diagnostics are application-side.
The newer source companion includes those helpers without changing frozen SDK
sources or bytes; its separate commit is recorded in provenance.

From the extracted root, use the verification and Make/pkg-config/CMake commands
in [the SDK guide](sdk-bundle.md#verify-and-link-a-consumer). That guide's September
7 performance and validation claims are historical, not this bundle's identity.
Do not run `make sdk` merely to validate these frozen binaries. Rebuilding creates
a new candidate and does not inherit their hardware acceptance.

## Prepare without publishing

From the project checkout with private receipts and the already frozen SDK:

```sh
python3 tools/build-sdk-bundle.py \
  --sample-version 0.1.0-perf20260908-sampled \
  --sdk build/sdk/ps5-opengl-core33-g13-tiled-rgba8 \
  --source-commit FULL_SOURCE_SNAPSHOT_COMMIT \
  --candidate .local/g16-candidate.json \
  --results results/g16-cts-smoke-20260908 \
  --destination build/bundles/new-g13-output
```

The packager verifies pinned SDK/candidate identities, SDK source equivalence,
ordered case lists and completed receipts, then writes a tar.gz and checksum.
It refuses existing destinations. Packaging neither rebuilds nor publishes.
