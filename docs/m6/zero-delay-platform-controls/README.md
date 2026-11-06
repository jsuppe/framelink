# Zero-added-delay platform controls — 2026-11-06

Settings: native consumer,1080p60, pool2/unread1/output queue1, latency0,
unsynchronized measurement sinks,17ms diagnostic arrival budget,60seconds.
Windows BGRA:3600/3600 frames +2880000 PCM sample-frames; complete queue
accounting; no deadline misses; video arrival p955.71ms.
Android RGBA: same complete counts/accounting, but2video arrivals exceeded17ms,
maximum19.10ms. Instrumented --raw repeat completed all counts with zero misses
(max9.97ms). This does NOT explain or repair the original tail events. Both
results are retained; the next30-minute raw run is intended to capture recurrence.

Benchmark record vectors for queue timing are now preallocated/prefaulted like
the existing arrival/lateness vectors so their growth does not masquerade as
transport leakage. Linux/Windows/arm64 benchmark builds passed. Post-change
Android5second preflight output is included. No production behavior changed.

Zero-copy producer, final playback and physical output are not claimed.17ms is
an explicit diagnostic threshold, not approval of a universal latency contract.
