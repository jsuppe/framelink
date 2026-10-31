# Windows producer-clock and scheduled-queue regression fixes

Target: Caspar RTX5090, Windows, GStreamer1.28.0. Native offscreen renderer,
no browser JPEG requests, no physical display cadence claim. Runs were sequential.
See `../windows-rate-gate/README.md` for prior matched M4/M5 native baselines.

## Two independently measured causes

1. Legacy AudioPreferred uses a newly started WASAPI clock. Zero in PAUSED is
   normal; the decisive observation is that **after PLAYING** it remained zero
   for ~302ms while source system time/PCM reception advanced. Over the repeated
   decoded run system advanced10.473s and device10.120s, leaving ~383ms media
   phase lag relative to the producer. With ring8, pending6 and100ms latency the
   source repeatedly hit the unchanged5ms ring-acquire deadline.
2. Scheduled renderer checked queue space before removing its due front. When
   full, it skipped acquisition even though a slot became available that same
   iteration. Latest-wins acquisition then skipped every ~7th source sequence
   at1080p60. Refill after pop restores cadence, still at most one nonblocking
   acquisition attempt per iteration. GPU retirement is unchanged.

Clock A/B with identical queue-refill code and decoded500-frame source:

- AudioPreferred: BGRA13.05fps / NV1213.50fps acquired.
- ProducerMapped: BGRA49.59 / NV1249.57fps acquired.
- Diagnostic **system clock + legacy now+20ms reanchor**: BGRA48.26 / NV1249.52.
  This separates clock-source startup phase from anchoring. The control anchor
  differed from mapped producer epoch by28–31ms, not383ms.

Control build changed only `mappedBase` to
`gst_clock_get_time(selected)+20*GST_MSECOND` while selecting producer policy.
It is NOT shipped; production source was restored before final tests. Raw
`playback-clock` records prove actual selected clocks/bases. Initial resample
property readback was1 concrete sink in WASAPI runs. Device/system progression
in producer-policy repeat differed by at most2.1ms across10.09s. This is not an
instrumentation count of resampled samples or physical speaker latency.

## Queue refill A/B

Same ProducerMapped policy,5s synthetic fixture:

- BGRA1080p60:237→286 acquisitions;49.10→59.17fps. Successful submissions288→297.
- BGRA/NV121080p30:~30.16fps each.
- BGRA/NV12160x90@50:~49.45fps each.
- NV121080p60:36.40fps, consistent with prior legacy37.33fps CPU upload/conversion
  path, NOT a60fps qualification.

Startup/end losses remain. Event-span FPS excludes pre-first/post-last event
waiting but includes intervening stalls; do not divide frame count by caps FPS.
Raw event traces include gaps/p99/max. Some source pacing catches up after
startup so sink event-span rates exceed requested caps; that is not a display FPS.

## Validation

- Linux full suite50/50, followed by3/3 changed targeted tests.
- Deterministic renderer queue-budget test: full queue replenishes every due
  cycle; held-full queue does not acquire; nonfull path never double-acquires.
- Windows real WASAPI offsets:37ms and123.456789ms audio origins and end offsets
  preserved,76800 PCM samples each. Timestamp-reset negative control rejected.
- `m5_assert_rate_gate.py` passes6 queue-refill cases, rejects both legacy-clock
  decode and pre-refill BGRA60 negatives. Floors are regression floors, not zero
  loss promises or physical scanout qualification.

Tests/logs from the final build and remaining qualification limits are recorded
alongside these directories. Browser preview remains a separate150ms JPEG poll
path and cannot represent native50/60fps presentation.

## Final repeat and sustained run

Final shared-budget implementation: BGRA1080p60 **59.47fps**, NV1260 **36.89fps**,
both1080p30 **30.24–30.26fps**,160x90@50 **49.21–49.83fps**. All6 measured
regression floors passed. Windows `vkr_av_schedule_test` passed.

45-second BGRA1080p60 run:2700 sink inputs,2685 successful submissions,
2676 acquisitions at **59.66fps**. Audio completed2700 blocks/2160000 samples.
Device-clock elapsed45.09387s versus system45.09189s (~1.98ms difference).
The largest sampled device/system phase difference was18.66ms; these are
software clock reads, not actual audio/scanout timestamps. Startup and later
ring pressure still account for losses; this is not lossless transport.

Real-WASAPI decoded hash test: each format preserved all100 PCM blocks/96000
samples and all94 received video payload hashes, originalPTS and durations.
Standalone CPU consumer scheduled50 frames in this correctness fixture; it is
NOT the native renderer throughput path and is not claimed50fps-qualified.
Producer-policy fakesink offsets/reset also passed3/3.

Remaining qualification limits: no direct counter of inserted/removed resampled
samples or physical device underruns; no induced delayed-device-preroll test;
no multi-minute/independent-device clock drift qualification. NV12CPUconversion
and browser JPEG preview limits remain unchanged. The tested rate gate covers
native Windows offscreen renderer acquisition and not physical presentation.

Runtime recovery: producer-policy native BGRA import-failure injection and
pause/fresh-session recovery passed. The GPU-delay+generation fixture was
**not qualified**: its runtime checks reached the final validation-layer gate,
but Release intentionally disables `VK_LAYER_KHRONOS_validation`; retained
`m5-policy-generations.log` records this failure. Do not count it as a passed
GPU lifetime validation run.

Plain two-generation switch recovery also passed; raw records retained.
