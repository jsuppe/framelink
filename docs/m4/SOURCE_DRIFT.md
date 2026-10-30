# Source pacing, bounded recovery and diagnostics

`framelink_av_producer --pacing-ppm N` changes only source wall-clock pacing.
Positive values run faster; range is -200000..200000 ppm. Audio sample positions,
PCM, durations, and both track PTS remain unchanged. Injection bypasses the video
transport sink's redundant clock wait so it cannot mask a faster source. Defaults
are unchanged. This diagnostic is not a claim that a physical clock was adjusted.

The receiver's audio queue is bounded at 4 buffers / 80 ms (one final buffer can
cross the time threshold). Faster-source data is scheduled by GStreamer's sink
clock; queue backpressure eventually propagates through TCP/appsrc. TCP and GDP
also buffer data; receiver queue bounds alone do **not** bound total transport
latency, and a short test may not fill OS socket buffers.

For slower input, three consecutive PCM blocks arriving over 100 ms after their
shared-clock playout deadline invalidate BOTH tracks and initiate the existing
bounded session renegotiation. It does not stretch timestamps, silently slip one
track, or claim seamless recovery. The live policy can lose media at reset. A
persistently unsupported source rate exhausts retries; replacing the bad source
with a healthy source permits re-anchor with fresh identity/clock. This is a
bounded recovery policy, **not adaptive source-clock resampling**.

`PlaybackCallbacks::onMetrics` (receiver thread, at most 10 Hz plus final sample)
reports blocks/sample count, current and sampled peak receiver audio queue depth,
queue-empty/full events, late audio blocks, and arrival drift. Arrival drift is
`(lastArrival-firstArrival) - (lastPTS-firstPTS)` at decoded PCM admission (not
raw socket receipt) and therefore includes network
jitter and backpressure; it is not a clock-drift estimate or lip-sync error.
Queue-empty includes startup and EOS and is **not a physical device underrun**.
JSON `playback-metrics` exposes the final summary; `producer-pacing` includes the
maximum observed application push duration. Probe callbacks do not format logs.

Validation: `ctest -R m4_source_drift --output-on-failure` runs separate authenticated
peers with +/-1000 ppm and accelerated +/-100000 ppm, checks exact intentional
37 ms audio offset, counts exact audio samples, verifies bounded receiver queues,
rejects sustained lateness, verifies the same slow source succeeds with a 2-second
latency budget, then replaces the slow producer and verifies actual
production API reconnect/re-anchor. Uses fakesink with real GStreamer clock
scheduling, NOT physical device output. The default tests cover six-second windows; `--frames 2000 --rates=-1000,1000
--skip-recovery` extends each direction to 40 seconds. Neither establishes
long-term oscillator stability or physical sink resampling. Device-clock drift
compensation qualification and seamless independent-source resampling remain
separate, unverified capabilities.

## Optional virtual-device clock-slaving probe

`fl_m4_device_slaving` requires PulseAudio and must be run against a dedicated/null
sink (`PULSE_SINK=...`), never silently against a user's speakers. It is built on
Linux but deliberately not registered as an unconditional hosted CTest. The probe
forces a separate external playback clock, changes its calibration +1000 then
-1000 ppm without a timestamp jump, and observes the **executing** GStreamer
`gst_audio_base_sink_resample_slaving` path and non-unity ratios. `--no-resample`
is the negative control: the same clock stimulus must produce zero resample-path
callbacks. Non-unity debug ratios establish that clock correction executes;
they do not prove exact oscillator-error estimation, acoustics, or independent
producer-clock discipline. Debug callback names depend on GStreamer; an unsupported
version fails this diagnostic rather than reporting false qualification.

On Linux GStreamer 1.24.2 with the existing PipeWire/PulseAudio `auto_null` sink:
600 slaving callbacks, 570 non-unity ratios, EOS succeeded. Negative control:
zero slaving callbacks/ratios, EOS succeeded. This qualifies the underlying
external-clock device-slaving mechanism in a virtual environment. Normal FrameLink
selects the audio clock as master; source pacing is handled by queueing/re-anchor,
not by this separate forced-external-clock experiment.

## 2026-10-30 evidence

- Full Linux suite: 46/46 passed (including initial five drift scenarios).
- Expanded focused gate: six scenarios passed, including the increased-latency
  control and actual production API recovery (2 sessions, 2 clocks, 1 retry).
- Longer windows: +/-1000 ppm, 40 seconds/2000 blocks per direction, exactly
  1,920,000 audio samples and unchanged 37 ms audio offset. Sampled queue peak:
  4 buffers/80 ms, zero late audio blocks.
- Latest argument/help/bounds checks: 3/3 passed after overflow guard addition.
- Virtual-device probe and negative control passed as described above.
- Evidence is in `source-drift-20261030/`. These are Linux results; Windows runtime
  validation of these additions is not claimed.
