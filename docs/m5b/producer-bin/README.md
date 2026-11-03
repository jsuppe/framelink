# M5b.9 producer A/V bin (`framelinkavsink`)

Locally validated, not yet milestone-merged. Design decision (Jon,
2026-11-02): the bin is self-contained. `session=` performs discovery itself;
an application-installed context keeps it a thin composition.

## What was added

- `gst/gstframelinkavsink.cpp`, registered as `framelinkavsink`. It composes
  `framelinksink` and `framelinkaudiosink`.
  - **Pads and properties.**
    - `video_sink`/`audio_sink` sometimes-pads, created only for the enabled
      `tracks=audio|video|av`.
    - Setup properties, changeable only in NULL: `session`, `tracks`,
      `video-format`, `width`, `height`, `framerate`, `audio-format`, `rate`,
      `channels`, `connect-timeout-ms`, `idle-timeout-ms`, `audio-queue-ms`,
      `video-queue-frames` and `video-pool`.
    - Read-only `session-id`, `generation`, `audio-track-id` and
      `video-track-id`.
  - **`session=` mode.**
    - **Setup:** a worker thread runs discovery, negotiation, the
      authenticated clock exchange, the streams, the video lease and the ready
      gate. No state change blocks on the peer.
    - **Hold:** the inner sinks stay locked in NULL, and media waits at the
      ghost pads. The wait is bounded and woken by a flush or state change, and
      nothing is queued in the bin.
    - **Release:** once set up and PLAYING, the bin adopts the lease, gives
      both sinks the session context and starts them.
    - **EOS:** child EOS is held until the consumer confirms its drain.
  - **Timeline.** It is identical to
    `fl_gst_session_apply_pipeline_clock(producer)`. At NULL→READY the bin sets
    the top-level pipeline's start time to NONE and its base time to the
    current system-clock time, and proposes that time as the epoch. A pipeline
    that is not on the monotonic system clock is refused at PAUSED→PLAYING,
    before anything can influence the epoch.
  - **Thin mode** (no `session=`): the application provides the context and
    clock, and adopts the lease with the bin's `adopt-channel` action.
- Internal transport seams in `fl_gst_session_internal.h`:
  - `fl_gst_session_open_local_av_epoch`: an epoch provider for the producer
    proposal. The public function keeps now+200ms.
  - Cancellable `local_ready` and `finish_local`, so a state change ends the
    waits.
- `test/fl_gst_av_bin.cpp` (ctest `gst_av_bin`, label `m5b_av`, also in the
  Android matrix list). It pairs the bin with the validated task 7 consumer
  path.

## Cases

| Case | Requirement |
|---|---|
| av, producer PLAYING 800ms before the consumer | media held then released; 30 frames / 24000 samples exact; every buffer renders latency+0–100ms after capture on both tracks; drain before EOS; IDs/generation/session-id reported |
| av, consumer first | same |
| video-only, audio-only | only enabled pads; same bounds |
| thin mode (application context) | same bounds as `session=` |
| pause/resume, bin vs thin | identical outcomes. Video-only: latency unchanged across a 300ms pause, END accounting exact, at most 2 frames discarded by the live source. A/V: both refuse live `audiotestsrc`'s unannounced resume gap with the same error |
| teardown while media is held | NULL within 1s (measured 1ms) |
| consumer death mid-stream | producer error within 6s (measured 5ms) |
| track mismatch, no consumer, non-system clock, caps mismatch | bounded error on the producer; consumer fails |

## Defects found while building it

- **Test-clock epoch.** The first version recorded the base time from any
  pipeline clock. Under a test clock it proposed epoch 0, which the consumer
  rejected as a mapping. The bin now refuses non-system clocks at
  PAUSED→PLAYING.
- **Segment-offset design.** The first timeline design shifted segments by
  base−epoch. On a producer pause/resume, that either delayed every later frame
  by the pause, or needed a segment replacement, which the coordinated video
  sink correctly refuses ("segment replacement requires a fresh session
  generation"). It was replaced by the apply-pipeline-clock semantics above:
  no offset, exact parity with the standalone path.
- **Sticky events.** The failure path dropped sticky events, which tripped
  GStreamer's "data flow before stream-start" check under fatal warnings.
  Events now go on to the locked sink and fail there.
- **Windows paths.** A Windows session path embedded in `gst_parse_launch`
  text loses its backslashes. The test, and the guide, set `session` with
  `g_object_set`.

## Results

| Gate | Linux x86_64 | Windows (Caspar) | Android arm64 tablet |
|---|---|---|---|
| `gst_av_bin`, x20 | 20/20 | 20/20 | 20/20 (fresh private TMPDIR per run) |
| Render latency, every buffer, both tracks (150ms setting) | 150.1–150.2ms | 150.1–152.3ms | 150.2–150.9ms |
| Full serial suite | 109/109 | — | — |
| Affected tests (`gst_local_av`, `gst_av_validation`, `m5b_gst_av_example`, `gst_av_timeline`) | in full suite | 4/4 | — |
| Builds | no warnings | Release, no warnings | arm64/x86_64 + JNI, no warnings |

Per-case output: `linux-run.log`, `android-run1.log`.

## Limits

CPU-mappable rings only, as in task 7; native memory stays M5a-qualified. A
producer-only pause is a media gap, by the same semantics as the standalone
helper. Live sources that skip samples on resume without DISCONT are refused.
The consumer bin is task 10.
