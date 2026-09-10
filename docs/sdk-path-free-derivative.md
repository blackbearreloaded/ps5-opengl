# SDK build provenance and linker metadata

The high-refresh release lineage includes a reviewed derivative that removes
private build paths while retaining frozen dependency provenance. This is a
build/distribution record, not an independent hardware qualification.

## Transformation

Five runtime objects per profile were rebuilt with the recorded public
toolchain and file/debug/macro prefix maps. Copies of 16 dependency archives
were processed with `llvm-strip-21 --strip-debug`; imports, headers and other
metadata retained their bytes. New manifests identify the resulting artifacts.

Ordered archive membership, symbol indexes, retained symbols, relocations,
groups and retained sections were compared after index normalization.
Debug sections and debug-only symbols were removed. Subsequent releases
preserve or explicitly identify their changes relative to this lineage.

## Retained exception

For each profile, 1,263 `.llvm_addrsig` tables, including 585 nonempty tables,
had `sh_link` changed from `.symtab` to zero. Their bytes, sizes and other
attributes remained unchanged; each is `SHT_LLVM_ADDRSIG`, `SHF_EXCLUDE`
and not `SHF_ALLOC`.

This is **not strict semantic equivalence**. LLD 21.1.8 ignores invalidated
address-significance tables, conservatively limiting safe identical-code folding
and potentially changing future ICF layouts or warnings. The six checked consumer
link commands requested no ICF. No general optimization-equivalence claim is made.

The packager retains this exception in provenance and does not silently strip
or rewrite frozen libraries. Each release must still satisfy its own source,
manifest, consumer and native-evidence checks.

## Verification scope

Private-path scanning and decoded ELF-section checks cover installed payloads
and nested sources. This does not assert absence of every absolute string:
reviewed upstream temporary-file templates remain. Complete sources, licenses,
original/derived hashes and the exception accompany the relevant SDK archives.

See [SDK 0.2.0 identities](release-g62.md#frozen-identities),
[archive verification](sdk-bundle.md) and the [release process](ci-releases.md).
Detailed build commands and original audit artifacts remain local.
