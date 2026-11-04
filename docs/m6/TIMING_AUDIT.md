# M6.2a timing and queue audit — 2026-11-04

Inspected FrameLink a722b75 plus benchmark-only changes following 9d52ee5;
renderer source / renderer/av_input.h and av_schedule.h inspected read-only.
This is code inspection and a short Linux control, not sustained qualification.

## Intentional delay versus capacity

- gst/gstframelinkavsrc.cpp: standalone session bin sets pipeline latency from
  latency-ms (default150); system-clock mapping and base time are common to A/V.
  This is scheduled presentation delay, not a wire transport requirement.
- gst/gstframelink.cpp: coordinated video sink disables BaseSink sync. Source
  waits for readiness and fences, not a target queue fill. The source applies
  min(requested depth,pool-2), with depth1 for very small pools. Silent clamping
  remains a strict-negotiation issue for M6.2c, not repaired by this audit.
- gst/gstframelinkaudio.cpp: GDP writer sync=false. Its byte/time-limited queue
  is a capacity bound (default80ms), not a minimum fill target. Socket buffering
  also exists; neither zero latency configuration nor bounded capacity proves
  zero occupancy under load. PCM sample blocks add source packetization time.
- test/fl_gst_av_harness.h: downstream appsinks sync=true by default, preceded
  by queues. Native buffers may remain leased while waiting for presentation.
- test/fl_gst_av_bench.cpp: previously fixed150ms timing, unread depth derived
  from that latency, pool at least8 and output queues capped at1second. These
  controls mask the distinction between transport and presentation.
- Renderer av_schedule.h schedules against base+latency+PTS. av_input.h adopts
  receiver clock latency; win_service.cpp retains pending frames and completion
  ownership. This is final presentation behavior, not suitable as a per-hop
  transport timing benchmark. Do not remove fences or independently zero A/V.

## Copies

Coordinated video producer still copies CPU input into native ring allocations.
CPU sources map/read/copy (Windows staging CopyResource, Linux mapped copy,
Android plane copies); native consumers retain allocation references. GDP audio
has framing/copy costs. A native-consumer label is not end-to-end zero-copy.
Native producer session integration remains M6.9; CPU memory placement M6.8.

## Benchmark changes (M6.2b partial)

Added independent --latency-ms, --transport-only and --arrival-budget-ms options.
They propagate to child processes and appear in the result. Both bin and element
paths use the requested playback latency. Existing defaults and production APIs
are unchanged. --transport-only disables only measurement output synchronization;
it must not be advertised as synchronized physical A/V playback.
Arrival budget remains independently explicit: zero configured latency does not
mean zero nanoseconds allowed to execute. Completion means identity/count/EOS
completion, not automatic passage of every performance budget.

First short Linux control: BGRA1920x1080@60, native consumer, pool2, unread1,
latency0, unsynchronized measurement outputs; 5seconds total, 2second warmup.
300/300 video and240000 PCM sample-frames, cleanEOS. Steady video arrival p95
8.03ms; audio0.27ms. Includes producer copy and source timing, not incremental
second-hop cost or physical output. Sustained and other-platform gates remain.

## Next gates

Run matched bin/element and buffered controls, distinguish deadline budgets from
playback latency, report output-queue occupancy/age and effective ring depth,
then validate strict small-pool ownership and perform sustained cases. The
1second output capacity remains visible technical debt, not a low-latency default
recommendation. No 150ms public-default migration or overload-policy decision
has been silently implemented.
