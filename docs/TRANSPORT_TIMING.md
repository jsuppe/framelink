# Transport boundaries versus presentation

For intermediate FrameLink GStreamer boundaries:

- Set `preserve-timestamps=true` on both video elements.
- Keep `framelinksink sync=false`. Do not put a playback scheduler or fixed
  presentation-delay queue at each hop.
- Keep a common session running-time mapping for audio and video. Do not subtract
  each track's first PTS independently. GDP PCM retains its buffer timestamps.
- Configure the final output to schedule both tracks using the agreed common
  timeline/clock mapping. An unsynchronized arrival observer is not a player.
- Pool capacity is ownership storage, not a target queue depth. It must not gate
  startup until filled. Backpressure still exists when all readers retain buffers.

Existing defaults remain compatible: without preserve-timestamps, legacy video
uses submission/arrival time. This legacy mode is unsuitable for preserving
producer A/V offsets across arbitrary pipeline boundaries.

The two-hop test sends media120seconds ahead of running time but receives it now,
with exact +/-37ms A/V offsets. Enabling sync at an intermediate sink is a negative
control: video is held there. See `m5a/transport-separation-20261101/`.

Qualification currently covers fixed-rate PTS/running-time. Legacy video duration
is reconstructed from configured source FPS; arbitrary duration/DTS and stream
metadata must be carried by the forthcoming native integration, not inferred from
this test. Ordinary GStreamer video/PCM paths still copy; native integration and
multi-hop zero-copy/performance are separate gates.
