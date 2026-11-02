# M5a.10 — memory and ownership diagnostics

Locally validated, not merged. Parent executor; sequential Linux, Windows/Caspar,
Android tablet validation. See [trace contract](../../MEMORY_TRACING.md).

- Linux final recorder5/5 + affected native/legacy/resource/lease/relay6/6 =11/11.
- Windows final recorder5/5 + native producer/NV12/relay3/3 =8/8.
- Android arm64 tablet recorder5/5 + native producer/NV12/relay3/3 =8/8;
  arm64 and x86_64 API29 builds passed. x86_64 runtime not claimed.
- Saved final-schema native and legacy boundary traces pass
  `tools/check_memory_trace.py`: native successful submissions without boundary
  payload events; legacy12x130x66 NV12 exact154440bytes for each applicable operation.
  Windows explicitly records repack, upload-issued, readback-issued and source copy.
- Checker negative controls reject removed/renamed sink-copy events, a nonzero
  dropped-event footer and an empty trace. Native checks require at least one
  positive submission (the Windows sink-pipeline fixture has one, Linux/Android two).
- Recorder tests cover bounded concurrent snapshots, thread rejection, loss counters,
  64-bit detail serialization, disabled runtime and compiled-out behavior.
- Local200000-scope microbenchmark, 3 observations: disabled6.30–6.63ns,
  compiled-out1.15–1.36ns, enabled72.68–76.23ns. Enabled capacity262144 avoids
  saturation; excludes exit flush. This is not an end-to-end throughput claim.

The test runs include deliberate rejection messages. All final exit statuses passed.
Initial Linux invocation discovered a newly generated Make target needed a second
build invocation; the final compiled-out test ran successfully. Initial checker
incorrectly expected two Windows native submissions; source fixture establishes one,
so the positive-control requirement was corrected, not the producer behavior.

Resource queries preserve known/unknown device metadata; policy evaluations carry
channel, session generation and endpoint. Copy counts are logical payload operations
at instrumented sites, not physical driver bandwidth. Reader occupancy is retained
frame records, not latency. Physical scanout, full GPU relay/three-process Android
and Windows, throughput and driver-internal copies remain tasks12–13 limitations.
