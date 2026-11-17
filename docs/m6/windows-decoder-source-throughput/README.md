# Source generation/encoding diagnostic

Original run: `windows-decoder-source-throughput-prepared/runs/source-throughput-20261112t0815z`.
**Source-only diagnostic**, not decoder, relay or media-rate qualification.
All four remote arms exited **0**; recovered local analysis succeeded.

| Arm | Observation |
| --- | --- |
| Generation, clean | 5.1418495s wall time; 150 requested, not independently counted |
| Generation+encoding, clean | 5.5626633s wall time; 150 requested, not independently counted |
| Generation, instrumented | Exactly 150 buffers / exact PTS; first-to-last 5063.3607ms, median interval 33.8578ms |
| Generation+encoding, instrumented | Exactly 150 buffers / exact PTS; first-to-last 5489.6438ms, median interval 36.5459ms |

Clean wall times **include process/plugin startup and drain**. Instrumented arms
include verbose/tracer overhead and are **not performance qualification**.
Count/PTS proof belongs to instrumented arms, not silently to the clean arms.
These results do not exonerate or isolate combined-load contention and do not
establish a repaired 4K decoder path. See [recovered analysis](recovered-analysis.log)
and the unchanged [original plan](data/README.md) for exact scope.

## Original controller failure and recovery, no rerun

[Controller status](controller-status.json) remains **failed**: the original
[idle receipt](data/idle-final.json) serialized empty `activeRuntime` as `{}`,
not the required `[]`. Execution, packaging and pulling had all exited zero.
The failure was not overwritten or reclassified in-place.

[Recovery receipt](recovery-receipt.json) separately records full collection and
uploaded-identity verification, successful local analysis and **no rerun**.
[Fresh idle](recovered-idle.json) reports `activeRuntime:[]` at
**2026-11-12T08:15:09.6637198Z**. This archive involved no Windows contact.

## Preserved evidence and byte domains

All **20 original collected hashes** and **5 uploaded-input hashes** verified
against original files before copying. Scripts, original plan, status, host,
commands, clean logs, exact count logs and both raw tracer logs are retained.
The approximately 1.57MB archive excludes only the binary `results/registry.bin`
from collected payloads; it was hash-verified in place, and its original hash
remains in the unchanged collection manifest. No executable or credentials copied.

- `data/artifact-sha256.json`: original uploaded-input hashes.
- `data/collected-sha256.json`: original 20-entry Windows collection manifest,
  including the intentionally excluded registry binary.
- `original-byte-sha256.json`: **24 copied files**, excluding itself and this README.

Original CRLF/BOM bytes are retained. Original/pre-filter byte hashes are **not
Git-blob hashes**. Repository clean filters may rewrite environment paths; any
parent-added post-filter Git manifest must remain a separate domain.
