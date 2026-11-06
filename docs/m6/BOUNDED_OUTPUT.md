# M6.2b bounded output queue instrumentation

2026-11-06. Benchmark-only change; no production default or overload policy change.

`--output-queue-buffers N` caps each output queue in buffer units. Zero retains
the legacy time-only cap (one second); primary transport controls explicitly
select1. This bounds queued buffers, not total allocations: a buffer in flight
through a downstream sink and source-held buffers are separate ownership.
The option is forwarded to children and included in the summary.

Each queue sink-pad observation records monotonic admission time, and its
source-pad probe matches the same buffer identity and records elapsed time.
`admission_residence.ms` includes time blocked entering a full queue as well as
time inside it; it excludes downstream scheduling after exit. It must not be
called pure queue residence or transport-only latency. `sampled_max_buffers`
reads current-level-buffers at admission and is a sampled lower bound on the
peak, not an exact high-water mark. Zero sampled occupancy does not mean no
allocation or no transit time. The paired observations run in the benchmark,
not production FrameLink; their overhead still needs M6.6 qualification.

`queue.accounting=complete` requires no unmatched exits, no remaining admissions,
and paired count equal to arrival count on both tracks. Overall completion now
also requires this accounting. Actual performance budgets remain a separate gate.

Persistent source: dev/framelink-m6; persistent build: dev/build/framelink-m6-linux.
The recreated Linux build uses Ninja, RelWithDebInfo, GStreamer/tests enabled.
The receive_queue core test passed (1.60s). The short bounded transport control
completed300/300 video and240000 PCM sample-frames. Its video admission/residence
p950.06ms, audio0.07ms; no unmatched or pending records. A scheduled150ms control
with the same output bound completed with video p9533.36ms and audio20.01ms,
and observed a one-buffer queue level. These are diagnostic controls, not final
cross-platform qualification or zero-copy producer evidence.

Run fl_gst_av_bench <plugin> --seconds 60 --size 1920x1080 --fps 60
--format BGRA --native --pool 2 --video-queue 1 --latency-ms 0
--arrival-budget-ms 17 --transport-only --output-queue-buffers 1
--out <evidence-directory> --label bounded60.

Next: strict effective-depth/ownership review (M6.2c), expanded low-latency
platform tests and30-minute soaks. A17ms arrival diagnostic is not an accepted
incremental-hop latency budget; retain that distinction in reports.

## 60-second result
Linux BGRA1080p60 completed3600/3600 video and2880000 PCM sample-frames,
all paired queue records complete, zero arrivals beyond the diagnostic17ms
budget. Video arrival p958.15ms; video/audio admission-residence p950.05ms.
See [saved summary](bounded-output/bounded60.txt) and
[scheduled control](bounded-output/scheduled.txt). This is not the30-minute gate.
