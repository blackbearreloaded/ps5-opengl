# G47 SDK private-build-path derivative

Both 1440p120 and 2160p120 derivatives are host-checked and frozen, with the
explicit linker-metadata exception below. Each passes the existing 38-entry
manifest, 344-export and relocated Make/pkg-config/CMake consumer checks with
`--forbid-root` against canonical. All six resolved linker commands request no
ICF. No console operation, hardware acceptance, old acceptance inheritance,
SDL rebuild, packaging, release or remote write is part of this report.

## Inputs and transform

Source: `23a594c3a5fda4135599d0dea8d2bf4dcef47a39`. Runtime sources and Make
inputs compare unchanged against G31 source
`3cdc90bbc14def6bc3025b4460fba0892841114a`. Existing canonical source/generated
headers and public payload tools were read only; all 271 discovered compile
inputs hash identically before/after and at final verification.

Only five runtime objects per profile were compiled: `ps5_egl.o`,
`ps5_screen.o`, `ps5_agc_package.o`, `ps5_agc_runtime_backend.o`,
`u_framebuffer.o`. The existing `tests/ps5/native-app.mk` and toolchain fragment
used G31's actual public `prospero-clang` wrapper, Ubuntu Clang 21.1.8, and exact
recorded flags: C11, `-Os -g`, section splitting, warnings and Core33 defines;
native title, 120 FPS, profile height, draw profiling, GPU-present, multidraw
and deferred batching enabled. Assertions remain. The only compiler additions
are file/debug/macro prefix maps: canonical source/build to `ps5-opengl`,
clone/build to `g47`, payload root to `ps5-payload-sdk`.

Both reviewed `make -n -j2 --assume-old=ps5-opengl-mesa` plans contained exactly
five compiles and one archive operation, with no Ninja or import-stub build.
The same guarded commands performed the builds. No Mesa/PSBC dependency was
compiled. Copies of the 16 shared G31 dependency archives were transformed
with `llvm-strip-21 --strip-debug -o NEW COPY`; imports, headers and metadata
otherwise retain their original bytes. New manifests identify the derivatives.

## Qualified archive and privacy checks

The 16 shared archives contain 1,267 members. Ordered membership, archive
symbol index, retained symbols (including local/undefined), relocations,
groups and other retained sections compare after index normalization; debug
sections and debug-only symbols are removed.

Exception: 1,263 `.llvm_addrsig` tables per profile, including 585 nonempty
tables, change `sh_link` from `.symtab` to zero. Their exact bytes, sizes and
other attributes remain unchanged. Every exception is asserted to be
`SHT_LLVM_ADDRSIG`, `SHF_EXCLUDE`, and not `SHF_ALLOC`. This is **not strict
semantic equivalence**. Stripping changes symbol indices; [LLD 21.1.8 ignores
the invalidated tables](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.8/lld/ELF/InputFiles.cpp),
conservatively limiting safe ICF and potentially warning/changing future ICF
layout. The actual six consumer link commands request no ICF. No ELF was
handpatched, and no general optimization-equivalence claim is made.

G41's existing `private_markers` / `require_distributable_tree` scanner plus
decoded ELF-section checks pass across all 78 installed files and 34 archives:
**no actual private build roots**. This is not absence of all absolute paths.
The original generic-regex failure is preserved: EGL header offset 360 matched
`p:/` inside its public registry URL. The two reviewed upstream runtime
templates `/tmp/mesa_%s_%d_XXXXXX.log` and `/tmp/fileXXXXXX` remain unchanged.

## Frozen identities and local evidence

All artifact paths here are local to the isolated G43 clone, under
`build/g47-path-free-v1/`. They are ignored artifacts retained in that clone,
unavailable in a published checkout. Raw logs and private-root commands remain
local; `provenance.json` contains portable commands and original-to-derived
hashes for every installed file.

| Profile / SDK path | Manifest SHA-256 | Runtime SHA-256 |
| --- | --- | --- |
| 1440p120: `relocated/1440p120/gl` | `5c9da7020167a604400f9a378d20689c78cd8d9d8ec48d889b74aa698e63965c` | `38072f5d0ed30c43b273fac36ebceabbaf943bfe72a956f38e4d0ad157f2ac8a` |
| 2160p120: `relocated/2160p120/gl` | `1585e458b2ce884bc68c3e0b9439955e0e47e1d895e0ce76023ccd5739a7e577` | `fcca06d1701edae0881105c3cdb24eb92a152397e024184c2eabe9254d79a675` |

| Local report | SHA-256 |
| --- | --- |
| `provenance.json` | `fbb0cbc4b3fb872ceb6c9e265c489e92e57a659288b404bff9d4460a4a74d9ed` |
| `dependency-audit.json` | `c63bae420269c417b827a284d128fa0c16873b78ede0cbb85863e7c922dc0fb1` |
| `addrsig-guard.json` | `98c6d8ed52de707da4b8719fd0744ddd6f885819423da838d47392e19376a2b4` |
| `privacy-audit.json` | `8b5ce6da11d7af00cef9c8c4ca2a6fcad1a560b28a06cf454bd46ec7cd1b8864` |
| `consumer-audit.json` | `86dec50d7b7f0e7cbde2fd82725e37a3b9311648c249de609f02db0623f097ed` |

Local replay uses `.local/g47-build.py prepare`, then `build`;
`.local/g47-audit.py strip`, then `guard`, then `privacy`; finally
`.local/g47-finalize.py` (all via WSL `python3`, two compile jobs maximum).
These scripts also remain only in G43. Select a new unoccupied `OUT` in the
local build script before replay; never rerun a strip/build over these frozen
outputs. `plan.json` retains exact original commands; consumer directories
retain all six ELF hashes and link traces. Original G31 manifests were
reverified unchanged. Initial failures and tool probes were retained, not
rewritten into passing receipts.

Subsequent parent review qualified both exact derivative profiles through new
focused hardware checks; see [bundle qualification](sdk-bundle-hfr.md).
Those later receipts do not change this host-only build audit or inherit CTS
acceptance. The recorded address-significance exception remains part of the
derivatives' identity and review contract.
