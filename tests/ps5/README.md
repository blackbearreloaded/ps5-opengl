# Native and host regression cases

This directory retains the implementation's targeted regressions. The accepted
release evidence is the [four-configuration CTS report](../../docs/validation.md),
not the number of source files or historical gate labels here.

| Group | Use |
| --- | --- |
| `egl_public_*.c` | Public EGL/OpenGL behavior gates, built into `PPSA99005` |
| `test_*.py` | Focused host checks; some compile real driver helpers and NIR/ACO shaders |
| `verify_gl33_capability_audit.py` | Current source-level Core feature/compiler contract |
| `verify_gl33_link_surface.py` | Installed SDK's 344 required Core entry points |
| Other `verify_*.py`, early `agc_*`/`psbc_*` probes | Development-stage assertions and research history, not a current aggregate test suite |
| `native-app.mk` | Native runtime archive used by the SDK and folder-app builder |

From the repository root, use `make test`, `make test-compiler`, `make test-imgui`
and the [testing guide](../../docs/testing.md). The native catalog is listed by
`bash tools/build-native-test-app.sh --list`; do not send standalone probe ELFs
to the console.

Some historical assertions intentionally describe earlier compiler contracts
(for example metadata ABI 6 in `verify_egl_public_triangle.py`). The current
compiler is ABI 7. Do not count an old check as passing, or change runtime code
to satisfy an obsolete assertion. Optional `--baseline` experiments referencing
private research commit IDs require the original research checkout; they are
not prerequisites for building or validating this source snapshot.
