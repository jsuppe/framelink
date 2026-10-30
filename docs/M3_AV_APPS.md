# Independent A/V verification applications

`framelink_av_producer` and `framelink_av_consumer` are separate desktop
processes. Existing `framelink_producer` / `framelink_consumer` video-only tools
are unchanged. Build with GStreamer and samples enabled.

```sh
build/framelink_av_consumer build/libgstframelink.so --session /tmp/my-private-av --frames 150 --format auto
build/framelink_av_producer build/libgstframelink.so --session /tmp/my-private-av --frames 150 --format NV12
```

Either launch order works. The producer creates the private rendezvous directory;
pre-existing directories must already be owner-only. Windows uses a current-user
DACL. Both peers mutually prove knowledge of the private random rendezvous key
with fresh HMAC-SHA256 challenges before exchanging configuration or GDP bytes.
No secret is sent or logged. Audio authentication derives a per-session key
from the root key, session identity and generation; a queued previous-session
audio connection cannot attach to a replacement session. Other processes running as the same OS user and
administrators are not isolated by this scheme. `--session` is a directory, not
a public TCP address. Stale producer records are replaced only while owning the
OS-backed lifetime lock. Persistent lock/directory are intentionally retained.

Video uses the existing FrameLink image ring. PCM uses stock GStreamer GDP over
an authenticated GIO socket; this is not a new PCM protocol. GDP's outer transport
segment is non-seeking TIME for giostreamsink (which otherwise rejects seeking
on sockets); encoded media segment, PTS and durations are unchanged. Receive
uses bounded appsrc and arbitrary-length socket chunks, avoiding giostreamsrc's
fixed-read tail delay. Each receive validates segment-to-running-time conversion.

## Options and bounds

Both peers: `--frames N`, `--width W`, `--height H`, `--fps N`, `--rate N`,
`--channels 1|2`, `--timeout-ms N`, `--sessions N`. Defaults 150 frames,
160x90, 50 fps, 48 kHz S16LE stereo, 15-second **whole-process** deadline,
one successful session. Bounds: 13–240 fps, 8–192 kHz, maximum4096x2160,
100000 frames, at most100 sessions and60000ms whole-process deadline.
Choose a deadline sufficient for all requested media and startup.

Producer `--format BGRA|NV12`; consumer additionally accepts `--format auto`
(offers both, otherwise exact-match). NV12 requires even dimensions. Audio
sample rate/layout and video size/rate must match explicitly; no implicit
format conversion is negotiated. Reconfigure dimensions by relaunching both
peers. A persistent auto-format consumer can accept a replacement producer's
BGRA/NV12 selection. Every connection creates a fresh session identity and
starts generation1; it never reuses an old media ring/clock epoch.

`--sessions N` explicitly authorizes successive independent sessions and bounded
recovery. N counts **fully completed** sessions; interrupted sessions are logged
and do not count. There are at most N+3 media attempts plus deadline-bounded
rendezvous/connect polling. Without this option any established-session failure
is fatal. Producer can remain available when a consumer restarts; consumer can
wait for a restarted producer to publish its replacement protected endpoint.

The producer supports `--fault missing-audio` and `--fault audio-pts-reset`
at block10. Use more than10frames. A normal consumer must exit nonzero when
these faults occur. Source watchdog also bounds blocked operations/teardown.

Consumer `--play` additionally opens GStreamer automatic video/audio sinks;
NV12 is converted to RGB by videoconvert. This emits a modest440Hz tone.
Default mode has no windows or sound. Both modes still verify every visible
pixel, PCM sample, PTS, duration and selected audio caps. First buffers must
carry discontinuity. Audio timestamps derive from cumulative first-sample
indices, including nonintegral samples-per-video-frame rates.

## Timing and interpretation

Clock IDs are independent. A control roundtrip measures paired source/target
anchors and RTT/2 uncertainty (maximum50ms), then maps the producer's future
epoch through M2 checked helpers. This midpoint estimate assumes approximately
symmetric local scheduling; it is **not hardware audio-clock discipline**.
Playback uses the mapped system clock, not an audio-device-master drift loop.

This is deliberately a correctness-oriented example: producer waits for an ACK
only after consumer verifies each video/audio pair. It does not measure maximum
throughput, prove smooth presentation, or implement M4 drift repair. Ring4,
app queues2, audio queue4/80ms and input queue4 are bounded; kernel TCP buffering
is additional. There is no end-to-end zero-buffer/zero-copy guarantee.

JSON lines report session-ready, media-progress, session-complete and final
role/ok/sessions/video_frames/audio_blocks/errors/disconnects. Counters in
consumer progress mean **verified**, not merely received. Failed partial-session
frames do not enter final completed-session totals. Physical speaker/display
alignment and Android APK/service ownership remain later milestones.

Offers use a bounded268-byte LE control envelope with counts, eight video/audio
slots and zero unused entries. M2 selection is validated against both actual
offers and acknowledged byte-for-byte before media readiness.
