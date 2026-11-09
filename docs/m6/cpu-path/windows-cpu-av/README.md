# Windows broker-bound shared-CPU A/V and bins

The portable A/V fixture now runs on Windows, using an explicitly broker-owned
message-pipe pair and separately authenticated event/PCM loopback streams.
Windows 4/4 focused tests passed: CPU coordinator, source, roundtrip, A/V.
The A/V case executes 12 thin/bin variants, covering padded BGRA/NV12 pixels,
exact PCM, common mapped 37 ms offset, both EOS orders, empty tracks,
contradictory terminal accounting, PCM death and sub-second teardown.
Bin inputs must be released without waiting for the next frame.
Five invalid selection checks include rejecting unsupported Windows discovery.
No production default or native fallback changed.

Linux and Android each pass the affected A/V test (20 variants plus existing
discovery negatives); Android arm64 and x86_64 builds pass. These are focused
shared-fixture regressions, not deferred platform performance campaigns.
Initial Windows fixture build failure (unguarded POSIX-only local setup calls)
is retained in initial-build-negative.log and corrected with compile guards.
Loom context was attempted; missing chromadb prevents traceability lookup.

Source hashes are in final-provenance.json; scripts record exact commands.
The pre-existing Windows CPU implementation supports broker-context A/V without
a further production change. This test/build integration does NOT implement
Windows authenticated discovery or endpoint transfer, prove cross-process A/V
endurance, or close M6.8/M6.9. API candidate remains unfrozen; hold before M7.
