# SDK preview: depth and stencil fixes

[Download the frozen depth-fix preview](https://github.com/blackbearreloaded/ps5-opengl/releases/tag/sdk-0.1.0-perf20260909-g55-hfr-sdl2-focused).

Separate fixed 4K120 and 1440p120 GL/SDL2 packages fix depth/stencil mip blits
and packed mip clears. The 4K pair passed six focused native checks, including
a 112-case mip-blit batch and ImGui/SDL display checks. The 1440p pair is
host-checked only. This preview has no new sampled/full CTS acceptance.

The downloadable archives preserve their exact libraries, sources, checksums
and qualification. Prefer [SDK 0.2.0](release-g62.md) for new integrations.

## Build provenance and distribution

Frozen packaging does not rebuild or modify tested libraries. The packager
checks SDK/SDL manifests, source companions, raw evidence identities, required
native results and consumer reports before creating an archive.

Only reconstructed numerical evidence and provenance are distributed; raw
device logs, private paths and native title assets remain local. After packaging,
verify the archive/root checksums, both installed manifests, and three GL plus
two SDL consumer links from a fresh extraction.

See the [current release audit](release-g62.md#reproducing-the-release-audit)
and [CI release process](ci-releases.md). Fresh CI builds and historical frozen
packages are different evidence classes.
