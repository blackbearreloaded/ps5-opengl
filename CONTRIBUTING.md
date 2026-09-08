# Contributing

Start with [Building](docs/building.md), [Architecture](docs/architecture.md),
[Testing](docs/testing.md) and [Limitations](docs/limitations.md).

- Keep changes small and focused. Reuse the existing Mesa/PSBC/backend paths;
  do not add a second state tracker, shader parser or speculative abstraction.
- Preserve validation, error propagation and safe resource lifetime. Never fix a
  failing case by hiding errors, overriding version strings or weakening the oracle.
- Match the surrounding C/C++ style. Avoid unrelated formatting/refactoring of
  the validated runtime; pin compiler/driver changes together when required.
- Run `make test` and relevant targeted regressions. For renderer changes run
  the host renderer oracle before a bounded native case. Use the full CTS matrix
  for frozen acceptance candidates, not every edit.
- Report host vs hardware evidence separately. Include exact identities, results
  and known limits. Exclusions, timeouts and unexecuted tests are not passes.
- Do not commit SDKs, firmware modules, console keys, proprietary captures,
  generated binaries, raw device logs or personal machine paths.
- Keep upstream license notices. Changes to derived compiler/Mesa files belong
  in their recorded patch with clear provenance, not anonymous copied source.
- Use project copyright/SPDX headers only for original code whose rights you own.
  Credit upstream adaptations separately, preserving existing notices and licenses.

Pull requests should explain the problem, the smallest relevant change, checks
performed and any remaining uncertainty. Console testing requires owner approval
and the shared lock/lifecycle protocol. CI does not operate hardware.
