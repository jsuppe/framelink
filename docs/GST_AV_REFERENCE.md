# GStreamer A/V element reference

The `framelink` plugin (version 0.1.0) provides six elements. Property tables
below are generated from `gst-inspect-1.0` on the built plugin (Linux build;
the Windows and Android builds register the same properties) and omit
properties inherited from GstObject, GstBin, GstBaseSink and GstBaseSrc.
For a walkthrough see the [developer guide](../samples/GST_AV_EXAMPLE.md);
for the design and acceptance rules see the [A/V contract](GSTREAMER_AV_CONTRACT.md);
for moving existing pipelines see the [migration guide](GST_AV_MIGRATION.md).

| Element | Role | Pads | Use |
|---|---|---|---|
| `framelinksink` | FrameLink video producer | `sink` (always) | Video into a consumer-owned ring; alone (legacy, video only) or as the video track of a session |
| `framelinksrc` | FrameLink video consumer | `src` (always) | Owns the ring; alone or as the video track of a session |
| `framelinkaudiosink` | Audio producer | `sink` (always) | Serialized PCM over an authenticated local stream; audio-only session (`session=`) or the audio track of a session context |
| `framelinkaudiosrc` | Audio consumer | `src` (always) | Live PCM source; the counterpart of `framelinkaudiosink` |
| `framelinkavsink` | Producer bin | `video_sink`, `audio_sink` (sometimes: enabled tracks only) | `framelinksink` + `framelinkaudiosink` with discovery, timeline and drain |
| `framelinkavsrc` | Consumer bin | `video_src`, `audio_src` (sometimes) | `framelinksrc` + `framelinkaudiosrc` with discovery, timeline, ready and drain |

Caps: video `BGRA`, `RGBA`, `NV12`, `RGB10A2_LE` (device-dependent), system
memory or the platform memory feature (`memory:DMABuf` on Linux,
`memory:D3D11Memory` on Windows, `memory:FrameLinkAHardwareBuffer` on Android).
Audio `S16LE` or `F32LE`, interleaved, 8000–192000 Hz, mono or stereo. A
session fixes one format per track at negotiation; caps must match it.

## Choosing a setup

- **Bins with `session=`** (simplest): one element per side, a private
  current-user directory as the rendezvous. The bins do everything below.
- **Individual elements with a session context**: the application calls
  `fl_gst_session_open_local_av`, installs the `framelink.session.v1` context,
  adopts the video lease (`adopt-channel`), applies the pipeline clock and
  reports ready/drain (`fl_gst_session_local_ready`, `fl_gst_session_finish_local`).
  Use this to put the tracks in different bins or to control the order of setup.
- **Bins in thin mode** (no `session=`): as the previous, with the bin's own
  `adopt-channel` action.
- **Audio only**: `framelinkaudiosink session=DIR` and `framelinkaudiosrc
  session=DIR`, or the bins with `tracks=audio`.
- **Video only, no session**: `framelinksink channel=NAME` / `framelinksrc
  channel=NAME`, unchanged from earlier releases (latest-frame delivery, no
  coordinated timing).

## Behaviour in a session

These hold for the individual elements attached to a session and for the bins.

- **Clock and timeline.** Both pipelines use the monotonic system clock. The
  producer pipeline's base time is the negotiated epoch and its start time
  NONE; the consumer's base time is 0 with pipeline latency `latency-ms`.
  Consumer PTS are the producer's running time mapped to consumer system-clock
  time; relative A/V timestamps are never rewritten. A fixed non-system clock
  fails PLAYING.
- **Pacing.** The coordinated sinks do not synchronize to the clock (their
  `sync` is off). Producer sources must deliver in real time: live sources, or
  `clocksync` after a non-live chain.
- **Video accounting.** Every frame is accounted for. A frame replaced in the
  ring before the consumer read it fails the consumer at once ("coordinated
  video frame replaced before it was read"); a stream never silently drops.
- **Audio.** PCM blocks need valid PTS and duration in a TIME segment; a block
  larger than `audio-queue-ms` fails; a timestamp gap without DISCONT, a
  segment replacement, rate changes and non-forward segments are refused.
  GAP events are carried; no silence is synthesized.
- **Seeks** on either pipeline are refused by the coordinated sinks
  (`gst_element_seek` returns FALSE) and the session continues.
- **Pause.** A producer pause is a media gap; a consumer pause fails the
  session (frames are replaced meanwhile).
- **EOS.** The producer's EOS is held until the consumer has drained every
  enabled track; the consumer reports the drain after EOS left each source.
- **Failures.** Peer death, a missing peer, track-set or format mismatch,
  authentication failure and idle deadlines post errors within bounded time on
  both sides. Reconnect requires NULL and a fresh session; there is no
  automatic in-element reconnect.
- **Latency queries.** FrameLink sources answer live with no added latency,
  so the consumer pipeline's latency is its sinks' plus `latency-ms`. The
  coordinated sinks answer not-live (they do not synchronize).

## Copy and timing contract

Boundary copies per frame for system-memory input (exact, from the
`FRAMELINK_GST_TRACE` payload records; [task 12 evidence](m5b/platform-qualification/README.md)):

| Platform | Producer (`framelinksink`) | Consumer, CPU output | Consumer, `native-memory=true` |
|---|---|---|---|
| Linux, Android | CPU copy into the ring image (`gst.cpu_copy_sink`) | CPU copy out (`gst.cpu_copy_source`) | none (DMABuf / AHardwareBuffer) |
| Windows | GPU upload (`gst.gpu_upload_issued`) | GPU readback + CPU copy (`gst.gpu_readback_issued`, `gst.cpu_copy_source`) | none (D3D11Memory) |

- Audio is serialized PCM over a local authenticated stream (GDP over GIO),
  never shared memory. Accounting is the producer's EOS statistics; copies
  inside GDP/GIO and the OS socket stack are unknown, not zero.
- Native producer allocations (`framelinksink native-memory=true`) are an M5a
  feature not yet qualified with coordinated sessions (roadmap M6.9).
- On Linux with a discrete GPU the CPU consumer readback is slow (about 34MB/s
  measured on an RTX 3090); use `native-memory=true` for large frames
  (roadmap M6.8).
- Timing figures are software timing on the consumer's pipeline clock. Display
  scanout and speaker output are not measured, and no physical lip-sync claim
  is made.

## Messages

| Message (element) | Posted by | Fields |
|---|---|---|
| `framelink-sink-stats` | `framelinksink`, at EOS | `received`, `submitted`, `dropped`, `ring-full-drops` |
| `framelink-audio-sink-stats` | `framelinkaudiosink`, at EOS | `blocks`, `pcm-bytes`, `sample-frames`, `generation`, `shared-pcm` (always false) |
| `framelink-source-sequence-gap` | `framelinksrc` with `preserve-timestamps` or `preserve-frame-metadata` outside a session (in a session the gap fails the stream instead) | `previous-sequence`, `sequence`, `missing`, `pts-ns` |

## Properties

Setup properties change only in NULL.

#### `framelinkavsink`

| Property | Type, range, default | Access | Meaning |
|---|---|---|---|
| `audio-format` | string, default "S16LE" | read/write | Offered PCM format with session= (S16LE, F32LE) |
| `audio-queue-ms` | uint 1 - 2000, default 80 | read/write | Hard audio queue budget, not target latency |
| `audio-track-id` | uint, default 0 | read-only | Negotiated audio identity, zero when absent |
| `channels` | uint 1 - 2, default 2 | read/write | Offered PCM channels with session= |
| `connect-timeout-ms` | uint 1 - 60000, default 15000 | read/write | Session setup and media hold deadline |
| `framerate` | Fraction. 1/1 - 240/1, default 30/1 | read/write | Offered video frame rate with session= |
| `generation` | uint64, default 0 | read-only | Negotiated session generation |
| `height` | uint 16 - 8192, default 720 | read/write | Offered video height with session= |
| `idle-timeout-ms` | uint 1 - 60000, default 15000 | read/write | Bounded transport idle deadline |
| `rate` | uint 8000 - 192000, default 48000 | read/write | Offered PCM rate with session= |
| `session` | string, default null | read/write | Private current-user local discovery directory (not a secret or URL); unset: use an installed framelink.session.v1 context |
| `session-id` | string, default null | read-only | Negotiated identity, not a secret |
| `tracks` | string, default "av" | read/write | Required track set: audio, video or av; only enabled pads exist |
| `video-format` | string, default "BGRA" | read/write | Offered video format with session= (BGRA, RGBA, NV12, RGB10A2_LE) |
| `video-pool` | uint 0 - 16, default 0 | read/write | Consumer ring images requested (0: consumer default) |
| `video-queue-frames` | uint 1 - 16, default 2 | read/write | Unread coordinated video frames the consumer ring keeps, oldest first; limited to pool - 2 (at least 1) |
| `video-track-id` | uint, default 0 | read-only | Negotiated video identity, zero when absent |
| `width` | uint 16 - 8192, default 1280 | read/write | Offered video width with session= |

Signals: `pad-added`, `pad-removed`, `no-more-pads`

Actions: `adopt-channel`

#### `framelinkavsrc`

| Property | Type, range, default | Access | Meaning |
|---|---|---|---|
| `audio-format` | string, default "S16LE" | read/write | Accepted PCM format (S16LE, F32LE) |
| `audio-queue-ms` | uint 1 - 2000, default 80 | read/write | Hard audio queue budget, not target latency |
| `audio-track-id` | uint, default 0 | read-only | Negotiated audio identity, zero when absent |
| `channels` | uint 1 - 2, default 2 | read/write | Accepted PCM channels |
| `connect-timeout-ms` | uint 1 - 60000, default 15000 | read/write | Session setup and ready deadline |
| `framerate` | Fraction. 1/1 - 240/1, default 30/1 | read/write | Accepted video frame rate with session= |
| `generation` | uint64, default 0 | read-only | Negotiated session generation |
| `height` | uint 16 - 8192, default 720 | read/write | Accepted video height |
| `idle-timeout-ms` | uint 1 - 60000, default 15000 | read/write | Bounded transport idle deadline |
| `latency-ms` | uint 0 - 10000, default 150 | read/write | Pipeline latency installed with session=; covers transport and sink buffering |
| `native-memory` | bool, default false | read/write | Output platform GPU memory (DMABuf, D3D11Memory, FrameLinkAHardwareBuffer) from the ring; with session= the ring defers read release |
| `rate` | uint 8000 - 192000, default 48000 | read/write | Accepted PCM rate |
| `session` | string, default null | read/write | Private current-user local discovery directory (not a secret or URL); unset: use an installed framelink.session.v1 context |
| `session-id` | string, default null | read-only | Negotiated identity, not a secret |
| `tracks` | string, default "av" | read/write | Required track set: audio, video or av; only enabled pads exist |
| `video-format` | string, default "BGRA" | read/write | Accepted video format (BGRA, RGBA, NV12, RGB10A2_LE) |
| `video-pool` | uint 0 - 16, default 8 | read/write | Ring images this consumer allocates with session= (0: 4); cover ceil(latency*fps)+2 |
| `video-queue-frames` | uint 1 - 16, default 2 | read/write | Unread coordinated video frames the consumer ring keeps, oldest first; limited to pool - 2 (at least 1) |
| `video-track-id` | uint, default 0 | read-only | Negotiated video identity, zero when absent |
| `width` | uint 16 - 8192, default 1280 | read/write | Accepted video width |

Signals: `pad-added`, `pad-removed`, `no-more-pads`

Actions: `adopt-channel`

#### `framelinksink`

| Property | Type, range, default | Access | Meaning |
|---|---|---|---|
| `buffer-wait-ms` | uint 0 - 1000, default 5 | read/write | Maximum wait for a reusable image; zero is nonblocking, not an added presentation delay |
| `channel` | string, default "framelink" | read/write | framelink channel name (or slot-set prefix when slots > 0) |
| `connect-timeout-ms` | uint 0 - 60000, default 0 | read/write | Open the producer before streaming; 0 keeps lazy connection |
| `error-on-ring-full` | bool, default false | read/write | Raise an error instead of dropping when the bounded buffer wait expires |
| `native-memory` | bool, default false | read/write | Strict native buffers acquired from this sink; no implicit copy fallback |
| `preserve-frame-metadata` | bool, default false | read/write | Require exact running-time PTS/DTS/duration and media generation; both endpoints must opt in |
| `preserve-timestamps` | bool, default false | read/write | Opt-in agreed session running-time PTS; both pipelines must share clock and base time |
| `slots` | int 0 - 64, default 0 | read/write | walk consumer slot channels <channel>.0..N-1 and take the first free one (0 = open <channel> directly) |

Actions: `apply-memory-policy`, `adopt-channel`, `set-native-ready-fd`, `acquire-native-buffer`

#### `framelinksrc`

| Property | Type, range, default | Access | Meaning |
|---|---|---|---|
| `channel` | string, default "framelink" | read/write | framelink channel to create (this element is the consumer and owns the ring) |
| `expired-frames` | uint64, default 0 | read-only | Frames discarded by receiver-local age policy |
| `format` | string, default "BGRA" | read/write | Ring pixel format (BGRA, RGBA, NV12 or RGB10A2_LE; device-dependent) |
| `fps` | int 1 - 240, default 30 | read/write | nominal frame rate advertised in the caps |
| `height` | int 16 - 8192, default 720 | read/write | ring height to allocate |
| `max-queue-age-ms` | uint 0 - 60000, default 0 | read/write | Discard frames whose local receipt/readiness wait exceeds this age; 0 disables; never derived from media PTS |
| `native-memory` | bool, default false | read/write | Opt-in native DMA-BUF, D3D11 or exact RGB AHardwareBuffer output; unsupported paths fail without CPU fallback |
| `pool-size` | uint 1 - 16, default 4 | read/write | Allocated images, not a queue-fill target or presentation delay; adopted channels retain their actual capacity |
| `preserve-frame-metadata` | bool, default false | read/write | Require exact running-time PTS/DTS/duration and media generation; both endpoints must opt in |
| `preserve-timestamps` | bool, default false | read/write | Opt-in agreed session running-time PTS; both pipelines must share clock and base time |
| `width` | int 16 - 8192, default 1280 | read/write | ring width to allocate |

Actions: `apply-memory-policy`, `adopt-channel`

#### `framelinkaudiosink`

| Property | Type, range, default | Access | Meaning |
|---|---|---|---|
| `audio-queue-ms` | uint 1 - 2000, default 80 | read/write | Hard queue budget, not target latency |
| `audio-track-id` | uint, default 0 | read-only | Negotiated audio identity |
| `channels` | uint 1 - 2, default 2 | read/write | Standalone mono or stereo |
| `connect-timeout-ms` | uint 1 - 60000, default 15000 | read/write | Whole standalone startup deadline |
| `format` | string, default "S16LE" | read/write | Standalone S16LE or F32LE |
| `generation` | uint64, default 0 | read-only | Negotiated session generation |
| `idle-timeout-ms` | uint 1 - 60000, default 15000 | read/write | Bounded transport idle deadline |
| `rate` | uint 8000 - 192000, default 48000 | read/write | Standalone PCM sample rate |
| `session` | string, default null | read/write | Private local audio-only rendezvous path, or use an authenticated context |
| `session-id` | string, default "" | read-only | Negotiated identity, not a secret |
| `tracks` | string, default "audio" | read/write | audio standalone or av with shared session context |
| `video-track-id` | uint, default 0 | read-only | Negotiated video identity, zero when absent |

#### `framelinkaudiosrc`

| Property | Type, range, default | Access | Meaning |
|---|---|---|---|
| `audio-queue-ms` | uint 1 - 2000, default 80 | read/write | Maximum negotiated PCM block/queue budget |
| `audio-track-id` | uint, default 0 | read-only | Audio identity |
| `channels` | uint 1 - 2, default 2 | read/write | Exact channel count |
| `connect-timeout-ms` | uint 1 - 60000, default 15000 | read/write | Whole startup deadline |
| `format` | string, default "S16LE" | read/write | Exact S16LE or F32LE |
| `generation` | uint64, default 0 | read-only | Negotiated generation |
| `idle-timeout-ms` | uint 1 - 60000, default 15000 | read/write | Socket timeout rounded up to seconds |
| `rate` | uint 8000 - 192000, default 48000 | read/write | Exact sample rate |
| `session` | string, default null | read/write | Protected local rendezvous path |
| `session-id` | string, default "" | read-only | Not a secret |
| `tracks` | string, default "audio" | read/write | audio standalone or av with context |
| `video-track-id` | uint, default 0 | read-only | Video identity |

## Tracing

`FRAMELINK_GST_TRACE=/abs/prefix` records the GStreamer boundary
(`prefix.PID.csv`) and `FRAMELINK_TRACE=/abs/prefix` the core ring operations;
both are written at process exit. Check the footer for
`dropped_events=0,rejected_threads=0` before trusting totals. See
[memory tracing](MEMORY_TRACING.md) and [performance tracing](PERFORMANCE_TRACING.md).

### Receive depth versus allocation capacity

Coordinated sessions reject a negotiated unread-video queue that cannot fit
the consumer pool. Depth1 supports pools of2 or more; larger depths require
`queue + 2 <= pool`. No silent reduction of the agreed depth is performed.
This is capacity validation, not permission to add a playout delay.
