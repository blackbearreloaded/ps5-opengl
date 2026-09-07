# Release validation evidence — September 7, 2026

One frozen runtime: **39,544 accounted results = 37,404 Pass + 2,140 individually
reviewed NotSupported**, across four complete configurations. All 15 CTS cycles
were uneventful, with completed teardown, post-run service checks and exact-lock
release. The three final installed-SDK renderer checks also passed.

| File | Meaning |
| --- | --- |
| [candidate.json](candidate.json) | Runtime/compiler/SDK identities and 535 exact exclusion reviews |
| [audit.json](audit.json) | Complete strict four-configuration audit |
| [mustpass-gl33.txt](mustpass-gl33.txt) | Pinned upstream inventory: 9,886 ordered unique names |
| [cases.csv.gz](cases.csv.gz) | All 39,544 case/configuration results and receipt references |
| [receipts.json](receipts.json) | Sanitized counts, lifecycle records and raw-evidence hashes |
| [renderers.json](renderers.json) | Final ImGui, NanoVG and Sokol numeric oracles and identities |
| [SHA256SUMS](SHA256SUMS) | Integrity of the six exported data files |

From the repository root:

```sh
make test
python3 tools/verify-published-validation.py validation/2026-09-07
gzip -dc validation/2026-09-07/cases.csv.gz
```

The verifier checks exact inventory coverage, duplicates, binary consistency,
exclusion messages, render-target metadata, numeric renderer oracles and lifecycle
summaries. Export occurred only after the complete raw-receipt audit passed
without allowing missing cases. Full upstream swizzle and LOD-bias bodies ran.
Timing data changed batch sizes, not test bodies or acceptance criteria.

The inventory uses pinned upstream LF bytes; its ordered names match the
historical CRLF export. The previous dataset remains separately available.

## Additional final-SDK examples

These checks supplement the matrix; they are not extra CTS coverage:

- Sokol cube: 180 frames, five poses / 2,596 pixel checks, zero mismatches, and
  the original 8,294,400-byte heap allocation live before shader setup.
  Raw log SHA-256: `a823aec1d7255b0dc2639c6b496561405f785c3bcd58335afd0b9dfb0e81853a`.
- ImGui TV demo: 300 seconds / 5,997 frames (~19.99 FPS), 11 passing readbacks,
  ten progress intervals, and 5,997 successful two-draw batches. No recorded
  controller interaction or fresh visual confirmation.
  Raw log SHA-256: `5b2a470323d9afb7616ba5e646eb88543fdea66048fd3fdecc8b875949e90dc4`.

Both closed their presenter and title successfully, with healthy post-run services
and exact-lock release. They retain the known busy-unregister warning described
in [the report](../../docs/validation.md).

Raw QPA/device logs and binaries remain private local artifacts. Their hashes
preserve provenance, not independent proof of physical execution. This export
is **not Khronos certification** or a universal compatibility/stability guarantee.

The inventory and test identifiers originate from Khronos VK-GL-CTS commit
`cf7edb26d3be2d8763595ed08fdc41f3c1b1966f`; its
[Apache-2.0 license](../../LICENSES/VK-GL-CTS.txt) is retained.
