# Standalone consumer deadline/drain separation

Baseline: `703c22d`, Linux Release, NVIDIA, 160x90 50fps decoded MJPEG/PCM,
100 frames per session, BGRA and NV12. No Caspar GPU use.

## Reproduced cause

`appsink sync=false max-buffers=3 drop=true` receives decoded frames continuously.
The standalone application pulled one sample and synchronously slept until its
shared PTS deadline (including the configured 100ms latency). While asleep,
appsink silently discarded earlier samples. A sink pad probe observes arrival
**before** this eviction and therefore falsely appears complete.

With injected 700ms audio preroll on the coordinated producer-clock path, both
formats had 100 producer submissions and 100 sink-pad arrivals, but only 52
application pulls in this Linux reproduction (before.json). The Windows report
of 95/100 is the same failure boundary, not a platform-independent loss count.

## Fix

The application now drains without waiting on each presentation deadline. A
separate local pending list services due PTS samples each iteration. PTS and
shared clock/base/latency are unchanged. Appsink remains three buffers, with
silent eviction disabled. Pending count is bounded by the negotiated latency
frame budget plus three slots, and payload bytes independently capped at 128 MiB.
Excess and late frames receive explicit terminal reasons. RAII releases all
samples on success, cancellation, or failure. This is the existing standalone
scheduling example, not a new visible renderer or screen-presentation claim.
Native externalVideo renderer ownership/scheduling is unchanged.

Opt-in --trace-media records `video-schedule` receipt and terminal disposition by
running PTS. Always-on final summary records counts and queue/byte high water.
Trace hashing is not a performance benchmark; this verifies exact identities.

## Verification

Run `python3 test/m5_consumer_drain.py --build BUILD --output EVIDENCE`.

Eight cases passed (two formats each):
- startup with 700 ms preroll: 100 received/100 scheduled,zero late/overflow;
- normal startup/steady run: 100/100;
- deliberate500 ms scheduling stall: 100 received = 70–71 scheduled + 8 late + 21–22 overflow;
- new session after stalled run: 100/100.

Every expected PTS has exactly one receipt and one terminal disposition. Queue
high water 5 normal /8 stalled, configured bound 8. Payload high water460800 bytes
maximum at this resolution. Audio/video hashes, PTS, durations remain unchanged.
The stalled case proves bounded behavior and reason accounting, not losslessness
under unbounded stalls. Existing startup peer-death/restart test remains separate.

Artifacts: before.json,after.json,accounting.jsonl. `scheduled` means software
scheduler decision, not physical scanout. This does not qualify Windows or 60 fps.

Callbacks are reported as `handed-off`, not scheduled: callback ownership does not prove presentation. The 128 MiB ceiling covers the pending list only; appsink retains at most three additional buffers and the existing upstream transport remains bounded separately.

Affected regressions passed: m5_startup (including cancellation/restart), m4_av_recovery, m4_authenticated_offsets, m4_source_drift.
