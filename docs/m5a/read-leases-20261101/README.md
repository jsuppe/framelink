# Bounded reader leases — November 1, 2026

Initial synchronous-completion implementation. Linux 14/14 affected tests,
Windows/Caspar 14/14; Android tablet two new tests passed. Both Android ABI builds
passed. Standalone ledger additionally passed ASan/UBSan and warnings-as-errors.
Logs are compressed alongside this file. No skipped cases in these results.

The fixed ledger holds at most 16 acquired frames and 256 individual readers.
Distinct tokens reject duplicate, stale and foreign-channel completions. A later
completed frame cannot advance a monotonic consumed timeline past an earlier
reader. Retirement peek/ack preserves retryability after backend signal failure.
The native test verifies a two-buffer pool remains exhausted while one retained
reader survives, then becomes reusable after completion/collection. Resize rejects
old tokens. Existing ownership, external pool and reconnect regressions pass.

This API currently requires the caller to wait for its actual CPU/GPU access to
finish before flCompleteReadLease, then call flCollectReadLeases. It does not itself
wait for GPU completion. Native asynchronous fence handoff, cross-process relay,
and pipeline integrations remain pending. Do not mix manual consumed signalling
with managed leases. Finish all leases before closing the channel. Tests are not
throughput or physical-output qualification.
