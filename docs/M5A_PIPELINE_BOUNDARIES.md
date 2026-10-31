# M5a: shared-buffer pipeline boundaries

Status: **required design/acceptance contract, not an implementation claim**.
Updated 2026-10-31 from Jon's clarification. M5 performance/deployment work remains
active; this contract refines the already approved M5a milestone after M5.

## Intended use

FrameLink connects stages of a local pipeline in separate processes. A supported
pass-through boundary should exchange access to the existing image allocation,
its readiness/completion synchronization and its timing metadata. It should not
copy the pixel payload or behave as another independently buffered player.

Example target:

```text
decoder process -> FrameLink -> processing process -> FrameLink -> final output
                   shared GPU                        shared GPU    A/V schedule
                   allocation                        allocation    applied here
```

A pass-through stage forwards the same compatible allocation. A real processing
stage may write changed pixels into a different negotiated shared output. That
necessary transform is distinct from an extra copy solely to cross a process
boundary. Neither sharing a GPU handle nor rendering on the GPU alone proves
end-to-end zero-copy.

## Capacity, queueing and presentation are different controls

- **Allocation capacity:** storage available for writers, readers and unfinished
  GPU work. Allocating twelve images does not require keeping twelve queued.
- **Queue occupancy:** actual pending work. Delivery must not wait for the pool
  to fill. A stalled downstream stage can still cause wait/backpressure; larger
  capacity does not remove a latency limit or grant permission to queue forever.
- **Presentation policy:** when the final output plays a sample. The current
  A/V demo's common 100 ms delay is application policy, not FrameLink's mandatory
  transport cost. Do not replicate that delay at each pipeline boundary.

Two-image ping-pong can be suitable if a writer waits for the prior reader to
finish. Three images can permit more overlap. Minimum safe capacity depends on
actual GPU/decoder/reader lifetimes; it cannot be promised solely from a setting.
The current demo's six future-frame owners plus active/retiring GPU readers
cannot simply be placed into a two-image pool. Moving held frames upstream also
moves, rather than removes, buffering and must remain visible in measurements.

## Required data and ownership behavior

1. Negotiate resource type, adapter/device compatibility, format/planes/layout,
   permitted usages, import/export/forwarding capability and synchronization.
   Prefer a compatible producer or consumer pool through GStreamer ALLOCATION
   negotiation; an API usage flag does not prove physical memory residency.
2. With strict zero-copy selected, never silently map/read back/upload/copy the
   payload to make an incompatible path work. Fail with a capability reason.
   Explicit copy/conversion fallback remains a separate supported policy.
3. Keep the allocation alive until every authorized downstream reader, including
   queued GPU work, finishes. Propagate completion to the original pool owner;
   local descriptor delivery or CPU release alone does not make reuse safe.
   Re-export restrictions and cross-device incompatibility must be negotiated,
   not assumed away. A copied descriptor/duplicated handle is not a pixel copy.
4. Bound pending buffers and in-flight readers independently of pool capacity.
   Define whether overload blocks upstream, drops selected frames, or rejects
   the configuration. Lossless delivery and always-nonblocking delivery cannot
   both be guaranteed for an arbitrarily stalled consumer. Count every drop.
5. Preserve media PTS, duration, stream/segment mapping, session generation and
   the relative audio/video offset. Carry explicit common clock-domain mappings
   if needed; do not independently subtract each track's first timestamp or
   replace media time with reception time. Readiness fences are not media clocks.
6. Intermediate transport stages add no intentional fixed presentation delay.
   The selected final playback endpoint schedules both tracks on their shared
   timeline. A stage that explicitly requires processing lookahead must expose
   that latency separately; zero-copy is not a claim of zero elapsed time.

## Current implementation boundary

These are the observed paths before M5a, not the target support matrix:

- The native video API shares ring resources and GPU synchronization on supported
  platform paths. An application that can render directly into the provided
  image can avoid a boundary payload copy. General external-decoder allocation
  import/forwarding and memory negotiation are not yet qualified.
- `gst/gstframelink.cpp::sinkRender` maps the input `GstVideoFrame`. Windows
  uploads into a FrameLink D3D11 image; NV12 additionally repacks its planes.
  Other current paths map/copy into the destination. The ordinary GStreamer sink
  is therefore not the required GPU-native decoder handoff.
- `srcCreate` allocates an output GStreamer buffer and copies mapped/read-back
  pixels into it. The ordinary source also needs native-memory integration for
  a GPU-native multi-process pipeline.
- The current Windows NV12 consumer renderer includes a CPU conversion/upload
  fallback. Native shared transport alone does not make that whole path zero-copy.
- PCM currently uses GDP over GIO and is serialized/copied. It preserves the
  session timeline but is not a shared-PCM zero-copy transport. Its memory/copy
  policy must be stated independently; do not label the A/V session zero-copy
  solely because video resources are shared. Evaluate measured audio copy cost
  and a negotiated shared-PCM path if required, rather than replace it silently.

## Deliverables and acceptance evidence

1. Versioned memory/usage and ownership contract, with explicit strict/fallback
   policies and compatibility behavior.
2. GStreamer native-memory sink **and source**, pool/allocation integration,
   supported Windows GPU-decoder example, and per-platform capability matrix.
3. Pass-through multi-process relay using supported allocation forwarding,
   plus safe transform-output allocation. Release, crash and stale-generation
   tests must prevent premature reuse or leaked ownership.
4. Matched one-boundary and multiple-boundary tests. For each boundary record
   source identity, media timestamps, send/receive/ready/release observations,
   queue occupancy, acquisition waits and payload copy/conversion counts.
   Compare latency distributions on a defined clock domain; PTS equality proves
   timing preservation, not arrival latency or physical speaker/display alignment.
5. Demonstrate that relative A/V media timing is unchanged and separately report
   arrival skew/jitter. The final output must compensate within its declared
   budget or report lateness/drops; preserved PTS alone is not playback proof.
6. Exercise small pools, delayed readers, forced fallback, incompatible device/
   format/layout, disconnect/reconnect and generation changes. Strict-mode
   negative controls must reject a path that would require a payload copy.
7. Publish measured per-boundary latency and supported throughput, not a promise
   of zero latency. No pool-fill gating or fixed per-hop playback reservoir is
   acceptable for the ordinary transport mode. Numeric regression budgets must
   be defined for the chosen hardware/workload before qualifying the path.

See [the milestone roadmap](REMAINING_ROADMAP.md#m5a--memory-capability-negotiation-and-gpu-native-pipeline-integration)
for sequencing. No live demo timing, pool default or source code is changed by
this document.
