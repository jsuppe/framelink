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
`(lastArrival-firstArrival) - (lastPTS-firstPTS)` and therefore includes network
jitter and backpressure; it is not a clock-drift estimate or lip-sync error.
Queue-empty includes startup and EOS and is **not a physical device underrun**.
JSON `playback-metrics` exposes the final summary; `producer-pacing` includes the
maximum observed application push duration. Probe callbacks do not format logs.

Validation: `ctest -R m4_source_drift --output-on-failure` runs separate authenticated
peers with +/-1000 ppm and accelerated +/-100000 ppm, checks exact intentional
37 ms audio offset, counts exact audio samples, verifies bounded receiver queues,
rejects sustained lateness, then replaces the slow producer and verifies actual
production API reconnect/re-anchor. Uses fakesink with real GStreamer clock
scheduling, NOT physical device output. The tests cover six-second windows, not
long-term oscillator stability or physical sink resampling. Device-clock drift
compensation qualification and seamless independent-source resampling remain
separate, unverified capabilities.
