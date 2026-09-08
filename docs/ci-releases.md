# CI-built SDK archives

The **Build SDK release** GitHub Actions workflow builds a fresh **1080p60**
developer SDK on Ubuntu 24.04. Its binaries are **host-checked, not console-validated**.
Historical full CTS, sampled CTS, performance and stability results apply only to
their recorded binary identities, not automatically to these downloads.

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
report. This fixed scanout profile is not the separate 120 Hz benchmark build.

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
