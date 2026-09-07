# Published validation evidence

Historical baseline. The [September 7 dataset](../2026-09-07/README.md) validates
the updated runtime. This directory's original data and checksums are retained.

This directory exports one strictly audited PS5 campaign. It contains **39,544
accounted results: 37,404 Pass and 2,140 individually reviewed NotSupported**.

| File | Meaning |
| --- | --- |
| [candidate.json](candidate.json) | Original frozen identity and 535 per-case exclusion reviews |
| [audit.json](audit.json) | Complete strict four-configuration audit |
| [mustpass-gl33.txt](mustpass-gl33.txt) | Pinned upstream inventory, 9,886 unique names |
| [cases.csv.gz](cases.csv.gz) | All 39,544 rows: configuration, case, status, exclusion message, receipt |
| [receipts.json](receipts.json) | Sanitized per-run counts, identity, lifecycle and raw-evidence hashes |
| [renderers.json](renderers.json) | Selected final ImGui, NanoVG and Sokol output/identity |
| [SHA256SUMS](SHA256SUMS) | Integrity of these exported data files |

```sh
python3 tools/verify-published-validation.py validation/2026-09-06
gzip -dc validation/2026-09-06/cases.csv.gz     # standard CSV, no custom format
```

The verifier checks file hashes, exact inventory coverage, no duplicates, binary
consistency, counts, expected exclusion messages and lifecycle summaries. It
reports 20 uneventful cycles separately from 21 technically complete receipts.

Export was performed only after rerunning the original strict raw-receipt audit.
Raw device logs, binaries, local paths and console identifiers are not distributed.
Their hashes preserve provenance but are **not independent proof of hardware
execution**. This is a derived result export, not a full raw evidence bundle and
not a Khronos certification submission. See [the report](../../docs/validation.md).

The must-pass inventory and upstream test identifiers originate from Khronos
VK-GL-CTS at `cf7edb26d3be2d8763595ed08fdc41f3c1b1966f`; its
[Apache-2.0 license](../../LICENSES/VK-GL-CTS.txt) is retained.
