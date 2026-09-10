# Documentation

Start with the SDK release guide or build from source, then use the public
GL/EGL interfaces and the platform integration appropriate to your application.

## Build and integrate

- [SDK 0.2.0](release-g62.md): downloads, checksums and exact-binary qualification.
- [Building](building.md): dependencies, source setup and native folder apps.
- [Using the SDK](consumer-build.md): Make, pkg-config, CMake and ownership rules.
- [SDL2 integration](../integration/SDL2/README.md): window, context and input bridge.
- [Examples](../README.md#examples): small applications using public APIs.
- [CI releases](ci-releases.md): fresh builds and the maintainer release process.

## Understand behavior

- [Architecture](architecture.md): frontend, compiler, driver and platform boundaries.
- [EGL lifecycle](lifecycle-reopen.md): supported reopening behavior and focused results.
- [Performance](performance.md): measured workloads, acceleration and display verification.
- [Limitations](limitations.md): compatibility, recovery and hardware scope.
- [Physical input](sdl-input-validation.md): controller/reconnect qualification.

## Verify and contribute

- [Testing](testing.md): risk-based host and bounded native checks.
- [CTS campaign](cts-campaign.md): efficient batching and submission prerequisites.
- [Validation](validation.md): frozen full-campaign results and evidence boundaries.
- [Capability audit](development/capability-audit.md): version reporting and source checks.
- [SDK build provenance](sdk-path-free-derivative.md): retained linker-metadata exception.
- [Contributing](../CONTRIBUTING.md) and [third-party notices](../THIRD_PARTY_NOTICES.md).

Version-specific reports and checksums describe their exact binaries; newer
source does not inherit those results. Development plans, dated progress diaries
and raw console receipts are maintained locally, not as the public status guide.
