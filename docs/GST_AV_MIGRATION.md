# Migrating to the public GStreamer A/V elements

M5b adds public audio elements and coordinated A/V sessions to the
`framelink` plugin. Existing video pipelines keep working unchanged; this
guide covers moving to the new elements. Element details are in the
[element reference](GST_AV_REFERENCE.md), a walkthrough in the
[developer guide](../samples/GST_AV_EXAMPLE.md).

## Nothing changes unless you opt in

`framelinksink` and `framelinksrc` without a session context behave as
before: latest-frame delivery, `channel=` naming, legacy timestamps unless
`preserve-timestamps`/`preserve-frame-metadata` are set, and no audio. The
video regression suite (`gst_video_regression` label) runs this behaviour on
Linux, Windows and Android. What is new for existing users is additive: the
four new elements, and session behaviour on the video elements that applies
only when a session context is attached.

## From video-only FrameLink to A/V

| Before | After |
|---|---|
| `... ! framelinksink channel=NAME` | `... ! s.video_sink` with `framelinkavsink name=s session=DIR tracks=av video-format=… width=… height=… framerate=…`, and the audio chain into `s.audio_sink` |
| `framelinksrc channel=NAME width=… height=… format=… ! ...` | `framelinkavsrc name=c session=DIR tracks=av video-format=… width=… height=…` with `c.video_src ! queue ! ...` and `c.audio_src ! queue ! ...` |
| `framelinksrc pool-size=N` | `framelinkavsrc video-pool=N` (default 8; size it as `ceil(latency × fps) + 2`) |
| `framelinksrc native-memory=true` | `framelinkavsrc native-memory=true` |
| Channel name agreed out of band | A private current-user directory (`session=`); the ring name is derived from the session secret |

Keep the individual elements instead of the bins when the tracks live in
different bins or the application must control setup order; see
"Individual elements with a session context" in the reference.

## From hand-built A/V with `preserve-timestamps`

Earlier samples ([AUDIO_VIDEO.md](AUDIO_VIDEO.md),
[TRANSPORT_TIMING.md](TRANSPORT_TIMING.md)) carried video with
`preserve-timestamps=true` and PCM over application-built GDP/appsrc/appsink
chains, with the application agreeing clocks and base times. The session
replaces all of that:

| Hand-built | Session |
|---|---|
| Both pipelines share clock and base time by arrangement | Negotiated epoch, measured clock mapping (rejected above 50ms uncertainty), system clock on both sides |
| `preserve-timestamps=true` on both video elements | Implied; running time is session time on both tracks |
| GDP over your own socket/appsrc | `framelinkaudiosink`/`framelinkaudiosrc` over an authenticated local stream, bounded by `audio-queue-ms` |
| Producer `sync=true` paces video | The coordinated sinks never synchronize; pace with live sources or `clocksync` |
| Consumer latency by your sinks only | `latency-ms` (consumer bin) or the latency passed to `fl_gst_session_apply_pipeline_clock` |
| Gaps and drops reported by `framelink-source-sequence-gap` | A replaced frame fails the session at once; every frame is accounted for |
| EOS at the producer ends its pipeline | The producer's EOS waits until the consumer drained every track |

## From the standalone A/V applications

`framelink_av_producer` and `framelink_av_consumer`
([M3_AV_APPS.md](M3_AV_APPS.md)) and the M4 playback path remain available
as verification tools. To put the same transport inside your own GStreamer
application, use the bins with the same session directory semantics
(owner-only, current user). Their finite `--frames` declaration does not
exist in sessions: a session ends at EOS, which is drained and acknowledged.

## Behaviour to check when migrating

- **Real-time delivery.** A producer that delivers faster than real time, or a
  consumer that pauses, ends the session ("coordinated video frame replaced
  before it was read"). Non-live chains need `clocksync`; prefer live sources
  with `framelinkavsink`, which may start before the consumer exists.
- **Seeks are refused** on both pipelines; there is no in-session seek.
- **Clock.** A pipeline clock fixed by the application to something other
  than the system clock fails PLAYING. Audio sinks that provide a clock do not
  replace it on the consumer.
- **Reconnect** needs NULL and a fresh session; elements do not reconnect on
  their own after a peer failure.
- **Formats** are fixed at negotiation from the bin properties (or the
  capabilities passed to the helper); upstream caps must match exactly.
- **Copies.** Without `native-memory=true`, the consumer copies each frame
  out of the ring (slow from discrete-GPU memory on Linux; see M6.8). The
  producer always copies its system-memory input into the ring; native
  producer allocations are not yet qualified with sessions (M6.9).
- **Latency queries.** The producer pipeline answers not-live (its FrameLink
  sinks do not synchronize); the consumer pipeline's latency is its own sinks'
  plus `latency-ms`.
- **Timing claims.** Measured timing is software timing on the pipeline
  clock. Display and speaker latency come after your sinks and are not
  measured.
