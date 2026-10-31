# Producer-mapped versus audio-device playback clocks

`PlaybackOptions::clockPolicy` and consumer `--clock-policy audio|producer`
select a common A/V playback timeline. Renderer exposes `--av-clock audio|producer`.
Default remains `audio` for compatibility; the Windows external-pipeline demo
explicitly selects `producer`.

- **audio**: the previous audio-device-preferred clock and joint post-preroll
  re-anchor. A device with a stable running clock may use this. This policy is
  not throughput-qualified for the decoded Windows demo on the tested WASAPI path.
- **producer**: retain the negotiated producer epoch mapped to the receiver
  system clock. Both audio and video use that same base and latency. The audio
  device is slaved using GstAudioBaseSink RESAMPLE. `audio_master=false` and
  `producer-system-clock` status are intentional, not a missing audio device.

The Windows decoded fixture showed recurring ring backpressure with the legacy
policy and substantially higher throughput with the producer-mapped policy.
The initial WASAPI clock read was zero in PAUSED preroll, which is normal and is
not evidence of a faulty device clock. This comparison changes both clock source
and epoch anchoring; diagnostic controls must separate their effects before
attributing the root cause to either one. The policy changes no original media
PTS or relative A/V offset, buffer count, or acquisition timeout. Physical
speaker/display alignment remains separately unqualified. RESAMPLE property
readback confirms configuration, not the amount of actual resampling performed.

The default transport remains latest-wins and may drop on backpressure. This
change does NOT make every frame reliable. Startup, end-of-session and 1080p60
NV12 conversion limits must be measured separately. Do not infer achieved FPS
from configured caps, successful exit, audio output, or `due_decisions` alone.

At EOS `framelinksink` now posts `framelink-sink-stats` with `received`, `submitted`,
`dropped` and `ring-full-drops`. Counters are snapshotted on the serialized stream
thread, without per-frame formatted logging or a reporting lock. External
producer prints `video-transport-summary` from that message. GstBaseSink's generic
rendered count alone includes inputs discarded by the transport and is insufficient.

For comparisons use `test/m5_decoded_rate_probe.py --clock-policy audio|producer`
with explicit `--build`, `--renderer`, and a fresh `--output` directory. Traces
separate sink input, successful transport submission/acquisition, and renderer
iteration. `test/summarize_rate_trace.py` reports event-span rates and interval
statistics. The known-bad audio policy is a negative control for the decoded
fixture; do not treat it as an acceptable result merely because it exits normally.

Measured clock-source/reanchor controls and queue cadence results: [evidence](m5/clock-policy-fix/README.md).
