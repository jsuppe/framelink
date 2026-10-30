# M4 playback runtime — integration checkpoint

The `framelink_av_playback` static target exports `samples/av_playback.h`.
This is a checkpoint, **not full M4 qualification**.

* Reuses private rendezvous and authenticated control/audio channels.
* Consumer negotiates streaming mode; producer does not wait for per-pair ACK.
  The wire mode word changes the sample protocol: use matching peer revisions.
* `externalVideo=true` omits GStreamer video reception. The renderer exclusively
  creates the native FrameLink consumer using the supplied channel/configuration.
  No CPU image bridge is inserted. Callback must complete setup before returning.
* Audio uses GStreamer GDP, a bounded queue, conversion/resampling, volume, and a
  synchronized output sink. An audio sink-provided clock is selected if available;
  otherwise the status explicitly reports `system-clock-fallback`.
* Both tracks use a single receiver clock/base mapping and common configured
  pipeline latency. Native video deadline is base + media running PTS + latency.
  GPU readiness fences are **not** deadlines. Clock is borrowed; ref to retain.
* Session clock mapping is established via the existing checked exchange. Failure
  to open/start before the negotiated epoch fails explicitly, not arrival-restamps.
* Optional mute is applied through the volume element. `onEnd` must retire native
  imports. Callback handlers run on the receiver worker, not the render thread.
* Audio sample timestamp gaps/resets and clock loss fail the session explicitly.
  Cancellation is polled during playback; control sockets have bounded timeouts.
  Caller callbacks must themselves be bounded.

## CLI smoke

Start matching producer normally; consumer adds
`--stream --audio-sink fakesink --latency-ms 100` for software-only testing.
`autoaudiosink` requests actual output, but a sink that provides no clock is
reported as a fallback. Streaming summary reports observed frames, not a claim
that every generated frame was displayed. The old verifier path is unchanged.

## Verified so far

Linux full pre-existing suite: 38/38. Streaming 100-frame synthetic smoke passed
with explicit system fallback and bounded/latest-wins video reception. This is
not physical playback qualification or a drop-free result.

## Remaining M4 obligations

* Real audio-device clock selection and latency-query qualification.
* Active correction of independent producer/audio-device clock drift. Merely
  including `audioresample` does **not** implement an adaptive drift controller.
* Coordinated pause/resume and automatic discontinuity/clock-loss re-anchoring;
  current renderer must reject pause while an A/V session is active.
* Arbitrary upstream nonzero GStreamer segments (the bundled producer generates
  already-normalized running PTS; it is not a general pipeline ingest adapter).
* Physical speaker/scanout measurement and platform runtime qualification.

Do not label this checkpoint a completed M4 milestone.
