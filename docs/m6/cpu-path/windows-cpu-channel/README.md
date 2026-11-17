# Additive Windows shared-CPU channel/lease API

New `framelink_shared_cpu_win.h` exposes `flSharedCpuOpenConsumerPipe` and
`flSharedCpuOpenProducerPipe`. Both accept a broker-authenticated connected local
OVERLAPPED message pipe, a pinned peer process and optional cancellation event.
They consume the pipe even on invalid arguments, borrow/duplicate other handles,
and require an explicit timeout, memory policy and feature flags. The existing
portable opaque channel/lease operations are now exported on Windows. Native GPU
APIs remain separate; no automatic transport selection or copy fallback was added.

The common lease implementation is shared, not forked: reservations return BUSY,
write submit/cancel consumes the write lease, retained reads delay release, and
closing a channel preserves all existing mapped leases while preventing reuse.
Consumer maps are OS read-only. Ordered EOS rejects reserved writes and preserves
outstanding reads. Metadata is strict opt-in. POSIX FD factories, wire protocol and
behavior are unchanged. Windows descriptor allocation sizes remain page-explicit.

The caller must authenticate the **exact pipe and peer** before these factories;
PID comparison is only a binding check. This API does not implement discovery,
credential exchange, coordinator binding or GStreamer selection. Windows
WaitReadable uses PeekNamedPipe with requested1ms poll slices; actual scheduling
can be coarser. No sub-ms timing or primary-profile budget is inferred. Timeout
leaves the channel live; cancellation/EOF are surfaced for receive to resolve.
Per-I/O and allocation-preparation deadlines are bounded separately, not one
end-to-end factory deadline. These remain explicit integration/timing work items.

## Validation and compatibility

- Windows **7/7** focused tests, including public C11 compilation and100 real child
  sessions/800 exact BGRA/NV12 frames; VirtualQuery confirms read-only maps,
  metadata survives, retained read/write views survive close, handle baseline
  returns exactly, and reserved-write EOS is rejected.
- Linux **8/8** and Android **8/8** focused shared-change regressions: C11, opaque
  leases, reconnect, EOS, GStreamer source/roundtrip/A-V/events. Android arm64 and
  x86_64 builds both pass. These are not resumed performance campaigns.
- DLL export comparison against exact609cb74 baseline: all59 existing exports
  preserved,16 additive CPU exports,75 total. No removals. Full logs and symbol
  names in exports.json/export-comparison.log. Staged source hashes in provenance.

Next: clean exact-revision integrated Windows build/compatibility suite, then
coordinator/authenticated delegation and GStreamer integration. CPU allocation/
transport functionality is not full-resolution rate qualification. Existing
D3D11 lifecycle growth, timing, overload decisions, M6.9 real native producers and
cross-platform acceptance remain open. Hold before M7; API is not frozen.
