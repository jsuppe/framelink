# Native-memory boundary instrumentation

Enable `FRAMELINK_TRACE=/absolute/prefix` for core operations and
`FRAMELINK_GST_TRACE=/absolute/prefix` for GStreamer boundary operations. Each
process writes `prefix.PID.csv` on orderly exit or an explicit control-thread
flush. A crash may leave no complete file. Do not flush on a frame thread.
`*_TRACE_EVENTS` sets per-thread capacity (16–1,048,576, default 65,536).

## What the records mean

Schema 2 retains the original ten columns and appends typed details. A `details`
mask distinguishes unavailable data from an observed zero:

- 1: observed logical payload bytes and operation mask. CPU events follow the
  actual row copy/conversion; GPU `*_issued` events follow issuing a driver copy
  command. Neither proves physical memory traffic, cache behavior or GPU completion.
- 2: generation. Policy records use session generation; resource/ownership records
  use pool generation. These are different domains, not interchangeable counters.
- 4: occupancy is retained **frame records**, including completed readers awaiting
  ordered retirement. It is not token count, filled FIFO depth or latency.
- 8: reader token, scoped to the process. Frame sequence/slot/fence/PTS are retained
  on reader acquisition; completion and retirement connect via token/fence.
- 16: resource/usage/operations. `gst.memory_policy_evaluation` describes declared
  paths; `image.memory_resource` describes the actual queried resource. On resource
  records honor `resource_known_fields`, especially device/direct-usage/map flags.
- 32: policy decision diagnostics: missing required usage, unmet preferences,
  rejected resource type and forbidden operations. `endpoint` is 0 (local) or 1
  (peer); channel hash and session generation correlate the evaluation. A successful
  evaluation alone is not successful pipeline startup or an observed copy.
- 64: known device ID and kind. `device_lo/hi` encode the 16 resource ID bytes in
  little-endian order; unknown devices are not fabricated.

Image records use process-local image addresses in the historical `channel_id`
column; channel-scoped events use the existing channel-name hash. Do not equate
pointer values across processes or use either as authenticated allocation identity.
Resource snapshots are emitted when `flImageMemoryInfo` is queried, not by adding
an expensive device query to every frame.

CPU sink/source copies, Windows NV12 repacking, Windows staging/upload commands,
and Android BGRA/YUV normalization are instrumented at their operation sites.
Counts exclude row padding and ancillary zero-fill, allocator/driver-internal work,
and application operations outside these sites. The decoder example's converter,
validation readbacks and external GStreamer elements are separate accounting domains.
No copy events in an empty or truncated trace do **not** prove zero-copy.

Existing `flAcquireBuffer`, `flAcquireFrame`, platform wait spans and return codes
measure waits. Frame receive/replacement and drop events account for latest-wins
behavior. Reader occupancy shows why slots remain retained; it does not turn the
ring into an ordered delivery queue. Size/capacity does not imply added frames of
latency. Arrival-age enforcement and media timestamps remain distinct from scanout.

## Cost and completeness

Disabled tracing does not read a clock, allocate a recorder or write events.
`FL_TRACE_COMPILED_OUT` removes runtime enabling for that compiled component.
Enabled tracing has a cost: first use allocates/attaches under a lock; subsequent
records append to per-thread buffers without per-event locks or file I/O. Schema 2
Event is 168 bytes on the qualified 64-bit builds: default reserved array capacity
is 10.5 MiB/thread, capped at 64 attached threads (672 MiB capacity). Choose a smaller
capacity for constrained devices. Arrays are not eagerly initialized; each written
record is fully initialized. Buffers never wrap or overwrite published records.

Always inspect `dropped_events` and `rejected_threads`. A saturated recorder is not
valid exhaustive accounting. Enabled/disabled end-to-end throughput remains a
separate qualification gate; no claim of zero tracing overhead is made.

`tools/trace_report.py` summarizes spans, observed payload totals and occupancy.
`tools/check_memory_trace.py` verifies the native-producer fixture has successful
native submissions with no boundary payload events, or checks the exact 12-frame
130x66 NV12 legacy copy totals (154,440 bytes per operation). It rejects missing
positive controls, missing/lossy footers and incorrect payload totals. Use
`--windows` for the explicit repack/upload/readback/copy path. CPU pixel-oracle
reads in core fixture traces must not be confused with native GStreamer boundaries.
