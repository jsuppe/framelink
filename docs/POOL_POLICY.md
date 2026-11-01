# Pool capacity and overload policy

These independent controls do not introduce a fixed presentation reservoir:

- `framelinksrc pool-size=2` (or3): image allocation capacity, range1..16,
  default4. Starts with the first available frame; never waits to fill the pool.
  Adopted channels retain their actual capacity, reported by this property.
- `framelinksink buffer-wait-ms=0`: immediate acquisition. Range0..1000, default5.
  A positive value waits only when readers retain every image; it is a maximum,
  not an unconditional delay. It also bounds that acquisition's shutdown wait.
- `error-on-ring-full=false`: counted drop when the wait expires (legacy default).
  Set true for an explicit RESOURCE_BUSY pipeline failure instead of continuing
  with frame loss. A bounded wait cannot promise lossless service for a consumer
  that stalls indefinitely.
- `framelinksrc max-queue-age-ms=50`: optional receiver-local announcement/readiness
  age limit; default0 disables it. Expired frames are safely released, counted in
  read-only `expired-frames`, and traced as `gst.frame_drop.expired` when enabled.
  This is not media lateness. Future/backward media PTS never determine queue age.

Example bounded transport boundary:

```text
framelinksrc pool-size=2 preserve-timestamps=true max-queue-age-ms=50 ...
framelinksink preserve-timestamps=true sync=false buffer-wait-ms=100 error-on-ring-full=true ...
```

Choose drop/error policy deliberately. Core latest-frame delivery can skip
unacquired frames; sequence gaps and IPC replacement traces expose that separately
from producer ring-full drops. For loss-intolerant processing, also control upstream
production and downstream ownership; these options alone do not turn the transport
into an unbounded reliable FIFO.

All properties are configured before allocation/streaming. Native users opt into
`FL_TRACK_ARRIVAL` and query `flFrameAgeNs` while holding a frame. No arrival-clock
read is performed for this feature when tracking is disabled. Adopted channels
must have enabled tracking if a source age policy is requested; unsupported
tracking fails explicitly rather than using PTS as a substitute.

Bounds apply to their named stage. Receiver age does not include upstream queues,
kernel time before receipt, CPU copy after acquisition, or final presentation.
Those remain observable application/integration budgets. Two buffers are not a
universal full-rate guarantee; safe overlap depends on real reader/GPU lifetimes.
