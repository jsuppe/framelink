# Isolated oracle-phase diagnostic — does not waive original failure

Base revision `a50841aad44fbc9431234a4f326bad036d193bf5`, with the exact external
diagnostic overlay retained under `data/`. Original run:
`windows-decoder-oracle-phase-prepared/runs/oracle-phase-a50841a-lf-20261112t0836z`.

Source/overlay byte checks, isolated build and both five-second 4K30 replay
**diagnostic** arms succeeded. Both delivered **150 distinct frames / 240,000 PCM
frames**, exact terminal accounting and zero 34ms misses. This is not a production
qualification capture: probes were added and source video CSV rows buffered until
after processing/session finish, changing observation overhead.

| Diagnostic measurement | Trace off | Trace on |
| --- | ---: | ---: |
| Maximum video arrival age | 23.4155ms | 25.0974ms |
| Source Map median / max | 0.76895 / 2.4424ms | 0.89575 / 4.8582ms |
| Source SHA median / max | 12.9635 / 15.0879ms | 12.97265 / 15.3558ms |
| Source combined oracle max | 16.4214ms | 19.9039ms |

Unmap observations were tiny; full values, thread-CPU counters and identity-level
rows are preserved in local phase reports and raw CSVs. Thread CPU is quantized
and includes probe overhead; wall-minus-CPU does not prove GPU causation.

**The original 31.6217ms oracle event and failing identities 116/117 did not
reproduce.** Their [original failed pair](../windows-decoder-replay-b247e5d/4k30-control/README.md)
remains failed. There is no waiver, causal fix or endurance qualification; no
further repeat is inferred from this result. The [initial setup failure](../windows-decoder-oracle-initial-a50841a/README.md)
remains preserved separately. Exact LF patch application fixed that setup issue
without relaxing source hashes.

Final idle: **2026-11-12T08:35:56.8560221Z**. This archive was created locally
without contacting Windows or rerunning the diagnostic.

## Evidence and hashes

Exact original/overlay source, patch, provenance, scripts, configuration, host,
binary pins, build logs, raw CSV/trace files, summaries, local analysis and original
controller receipt retained. The replay tape remains external and pinned; no
source tar, executable or credentials copied. Empty `.owner.lock` files are
collected artifacts, not running-owner claims.

- Original collection: **75/75 hashes verified**, with full file coverage.
- Uploaded input manifest: **17/17 hashes verified**; source tar verified in place
  but intentionally not archived.
- `original-byte-sha256.json`: **81 copied files**, excluding itself and README.
- `git-blob-sha256.json`: separate clean-filtered staged/committed blob hashes,
  excluding itself.

Original manifests and CRLF/BOM bytes are unchanged. Original-byte hashes are
not Git-blob hashes; repository filters may rewrite environment paths at staging.
