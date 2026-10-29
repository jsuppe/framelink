# Bounded performance flight recorder

## Requirement and audit

Provide detailed opt-in timing without per-frame printf, disk writes or a
shared logging lock. Existing FRAMELINK_DEBUG describes lifecycle/errors, not
performance distributions. Logging must preserve results and buffer ownership,
include failed/timeout operations, and expose capture loss instead of silently
making throughput look better. Enabled instrumentation is not literally free.

Coverage on Windows and Linux:

- Channel creation/open and geometry-request CPU elapsed times and return codes.
- Producer acquire, begin/end submit; consumer acquire/release, including timeout,
  disconnect and other failures. Successful acquire includes slot, fence, PTS and
  consumer arrival sequence. Submit and receive share channel/fence/PTS identity.
- IPC send/receive elapsed time (receive includes idle time waiting for a message).
- `ipc.frame_received`: timestamp at pending-queue update, frame identity, and
  result 1 if it replaces an unconsumed pending frame (latest-frame-wins), else 0.
- Windows existing ready/consumed fence waits, including the actual wait result.
- Linux image mapping/plane mapping/unmapping CPU durations and map results.
- GStreamer sinkRender/srcCreate complete CPU spans and flow results;
  D3D11 staging-map span isolates the blocking readback map from total delivery.
  GStreamer srcCreate carries original frame identity before arrival-time PTS is
  assigned to its output buffer. Windows staging copies/upload and CPU row copies
  are included in the outer spans, not individually GPU-timestamped.
- Renderer-specific scopes and optional Vulkan timestamps live in vkrender;
  see its `docs/PERFORMANCE_TRACING.md`.

Scopes are nested and must not be summed as independent costs. This is a bounded
application-level audit, not hardware occupancy/cache/bandwidth or actual screen
presentation instrumentation. Android backend operations are not instrumented
beyond shared IPC in this change and were not tested.

## Enable and capture

Set nonempty file prefixes before process start; parent directories must exist:

```sh
export FRAMELINK_TRACE=/tmp/transport
export FRAMELINK_GST_TRACE=/tmp/gstreamer
export FRAMELINK_TRACE_EVENTS=262144
# Run finite producer/consumer samples or the normal application, then exit cleanly.
python tools/trace_report.py '/tmp/transport.*.csv' '/tmp/gstreamer.*.csv'
```

```powershell
$env:FRAMELINK_TRACE='C:\captures\transport'
$env:FRAMELINK_GST_TRACE='C:\captures\gstreamer'
$env:FRAMELINK_TRACE_EVENTS='262144'
```

Each component writes `<prefix>.<pid>.csv` on clean exit. Prefixes must differ
between recorder instances in the same process (library, plugin, renderer).
`flTraceFlush()` explicitly snapshots the library recorder from a control thread;
returns FL_OK when disabled/successful, FL_INTERNAL on export failure. It is not
an in-frame operation. Plugin/renderer integrations can call their recorder's
C++ `flush()` similarly. Snapshotting does not clear buffers. Export takes a
registration mutex and performs file I/O; event writers never acquire that lock
after their first event, but OS I/O contention can still affect the application.
Measure first, export afterward. Forced termination/crash loses unsaved records.
Close/join component threads before unloading their library or exiting normally.

Unset the environment variables to disable. `FRAMELINK_DISABLE_TRACE=ON` in
CMake compiles recording out (also available as FL_TRACE_COMPILED_OUT for users
of the header). Existing error/debug logs remain separate.

## Hot-path and memory contract

- Disabled: cached enabled check, no clock calls, allocation, locks or I/O after
  initialization. Small branch/stack overhead is measured, not assumed zero.
- Enabled: two monotonic clock reads per scope and one append-only record. Channel
  hashing and per-field bookkeeping add cost beyond the bare scope benchmark.
- One-time per-thread buffer allocation and mutex-protected registration. No
  dynamically formatted strings and no background flushing thread. Operation
  names are static literals. No frame pixels are captured by the recorder.
- Default 65,536 events/thread; `<PREFIX_ENV>_EVENTS` accepts 16..1,048,576.
  For the plugin use FRAMELINK_GST_TRACE_EVENTS. At 72 bytes/event the default
  allocation is about 4.5 MiB per participating thread, allocated lazily. Maximum
  64 threads per recorder, including threads that have already exited. Threads
  are not recycled during the capture. Worst-case budget must be planned.
- A full buffer stops accepting new events and increments dropped_events; a new
  thread beyond the bound increments rejected_threads. Capture is a prefix,
  not a rolling window. Any nonzero loss invalidates exhaustive-rate claims.
- Export reads release/acquire-published, immutable records. Concurrent snapshot
  tests verify committed records and intentional loss accounting. Registration
  or allocation failure does not throw into the application.

## Schema and interpretation

CSV comment header: process ID, monotonic origin and approximate wall-clock
anchor. Footer: dropped events, rejected threads and actual event capacity.
Rows: logical thread ordinal, operation, start_ns, duration_ns, channel_id,
sequence, fence, slot, pts_ns, result.

CPU times are steady-clock elapsed nanoseconds, not CPU cycles. Clock resolution
and scheduling prevent a nanosecond-accuracy claim. Different threads/processes
on the same host/boot can be correlated when their steady clocks share an epoch;
never compare clocks across machines. Wall time is a rough anchor, not precise
synchronization. No hardware-calibrated CPU/GPU timeline is provided.

For channel API events, channel_id is FNV-1a-64 of the channel name (same across
processes). IPC internal calls and image maps instead use process-local object
addresses; do not join those as channel identities. Creation metadata packs
width/height into sequence (high/low 32 bits), format into fence, pool size into
slot and flags into pts_ns. Most unused fields are zero. Scope result -9999
means no return-code annotation, not an application error. API codes retain their
native enums; Windows wait codes remain WAIT_OBJECT_0/WAIT_TIMEOUT/etc.

Frame sequence is a consumer arrival count, not producer render count. Channel
reconnection/reallocation can reset fence/sequence values. The report joins only
unambiguous channel/fence/PTS tuples and ignores duplicates. Its latency is
submit-call entry to successful acquire-call return, not render-to-scanout.
FL_GPU_SYNC delivery may precede GPU completion. A pending-frame replacement is
not automatically a visible dropped frame; presentation must be measured
separately. At saturation, first-prefix histograms can be biased.

Audit note: existing Windows flAcquireFrame's bounded ready-fence wait does not
propagate its wait result to the API result. This patch exposes that wait result
without changing acquisition semantics. A wait timeout must not be interpreted
as evidence of completed GPU work merely because the outer call returned FL_OK.

## Tests and overhead

`trace_disabled` and `trace_bounded_concurrent` cover disabled recording,
concurrent snapshots, four writers, capacity enforcement (64 retained / 448
lost events), and loss footer. `trace_thread_limit` verifies the 64-thread bound and rejects the 65th thread without blocking. Existing cross-process tests exercise format,
geometry, slow consumers, disconnects, reconnects, corruption and GPU-sync.

2026-10-29: Linux 17/17 and Windows 21/21 tests passed. Enabled Linux integration
subset 4/4 and enabled Windows subset 7/7 passed. Actual integration trace files
were parsed; their loss footers were zero. Renderer validation is documented in
its repository.

Recorder-only Release microbenchmark, 200,000 scopes and a volatile increment,
first-use initialization excluded, capacity 262,144 so it does not saturate:

- Melchior/Linux: runtime disabled ~1.18 ns/scope; enabled ~49.35 ns/scope.
- Caspar/Windows: runtime disabled ~2.31 ns/scope; enabled ~53.94 ns/scope (repeat runs: ~58.02 and ~61.84 ns).
- Linux compiled-out build: ~1.08 ns/iteration, largely the loop/increment itself.

These are single-run diagnostics, not statistically established overhead bounds.
They exclude API hashing, GPU queries, capture export and whole-pipeline effects.
Measure repeated A/B runs on the workload of interest before asserting no
throughput/latency regression. Benchmark commands:

```sh
./build/fl_trace_test bench
FRAMELINK_TRACE=/tmp/bench FRAMELINK_TRACE_EVENTS=262144 ./build/fl_trace_test bench
```

The report prints sample counts, p50/p95/p99/max durations, result distributions,
API delivery intervals and unambiguous transport joins. It deliberately does
not label callback/delivery counts as fresh display FPS.
