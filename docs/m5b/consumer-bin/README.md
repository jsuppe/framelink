# M5b.10 consumer A/V bin (`framelinkavsrc`)

Locally validated, not yet milestone-merged. Same design decision as the
producer bin (Jon, 2026-11-02): self-contained with `session=`, a thin
composition over an application-installed context without it.

## What was added

- `gst/gstframelinkavsrc.cpp`, registered as `framelinkavsrc`. It composes
  `framelinksrc` and `framelinkaudiosrc` and contains no display or audio
  device.
  - **Pads and properties.**
    - `video_src`/`audio_src` sometimes-pads, created only for the enabled
      `tracks=audio|video|av`. The video pad template allows any caps
      features, so native memory is negotiated by the inner `framelinksrc`.
    - Setup properties, changeable only in NULL: `session`, `tracks`,
      `video-format`, `width`, `height`, `framerate`, `audio-format`, `rate`,
      `channels`, `connect-timeout-ms`, `idle-timeout-ms`, `audio-queue-ms`,
      `video-queue-frames`, `video-pool` (default 8), `latency-ms` (default
      150) and `native-memory`.
    - Read-only `session-id`, `generation`, `audio-track-id` and
      `video-track-id`; `adopt-channel` action for thin mode.
  - **`session=` mode.**
    - **Live source:** READY→PAUSED and PLAYING→PAUSED return NO_PREROLL, so
      the pipeline reaches PLAYING while discovery runs.
    - **Setup:** a worker thread runs consumer discovery, negotiation, the
      clock mapping, the streams and the ring (with deferred read release
      when `native-memory=true`). No state change blocks on the peer.
    - **Release:** once set up and PLAYING, the bin adopts the lease, gives
      both sources the session context and starts them, then reports ready,
      which releases the producer's held media.
    - **Drain:** when every enabled track has delivered EOS downstream, all
      media has left the transport and the bin reports the drain, which
      releases the producer's EOS.
  - **Timeline.** Identical to
    `fl_gst_session_apply_pipeline_clock(consumer)`, applied to the top-level
    pipeline at NULL→READY: system clock, start time NONE, base time 0,
    pipeline latency `latency-ms`. A clock the application fixed is respected
    and refused at PAUSED→PLAYING unless it is the monotonic system clock.
  - **Reconnect:** NULL ends the session; the next NULL→PLAYING starts a fresh
    one. No automatic in-element reconnect.
- `framelinkavsink` now applies the same clock rule: it selects the system
  clock unless the application fixed one. Before, a pipeline element that
  provides a clock (a capture device) would have been selected and refused.
- `test/fl_gst_av_src_bin.cpp` (ctest `gst_av_src_bin`, label `m5b_av`, also in
  the Android matrix list). Producers are the validated `framelinkavsink` and
  the task 7 standalone-element path.

## Cases

| Case | Requirement |
|---|---|
| av consumer-first; av producer-first with 800ms lead | 30 frames uniform / 24000 samples contiguous, exact; every buffer renders latency+0–100ms after capture, per track; producer EOS only after drain; consumer pipeline on system clock, latency 150ms, start NONE, base 0; identity reported |
| video-only, audio-only | only enabled pads and children; same bounds |
| clock-providing element in both pipelines | both bins still run on the system clock; same bounds. Control: with the selection disabled the pipeline refuses PLAYING |
| `native-memory=true` | negotiated `memory:DMABuf` (Linux), `memory:D3D11Memory` (Windows), `memory:FrameLinkAHardwareBuffer` (Android); same counts and bounds; pixels mapped and checked on Linux and Windows |
| thin mode (application context, consumer setup, ready/drain calls) | same bounds |
| standalone-element producer (task 7 path) | same bounds; the bin's drain completes the producer's `finish_local` |
| reconnect through NULL, one consumer pipeline | two complete sessions, distinct session IDs |
| producer death mid-stream | consumer error, measured 1–12ms |
| teardown during setup | NULL within 1s (measured 3.2–6.1ms) |
| no producer, track mismatch, caps mismatch, fixed test clock, outside a pipeline | bounded error with the reason; producer fails too where present |

## Results

| Gate | Linux x86_64 | Windows (Caspar) | Android arm64 tablet |
|---|---|---|---|
| `gst_av_src_bin`, x20 | 20/20 | 20/20 | 20/20 (fresh private TMPDIR per run) |
| `gst_av_bin` (clock change), x20 | 20/20 | 20/20 | 20/20 |
| Render latency, every buffer, both tracks (150ms setting) | 150.0–150.3ms | 150.0–152.4ms | 150.2–151.0ms |
| Native memory | DMABuf | D3D11Memory | FrameLinkAHardwareBuffer |
| Full serial suite | 110/110 | — | — |
| Builds | no warnings | Release, no warnings | arm64/x86_64 + JNI, no warnings |

The Linux full suite ran before the standalone-producer case was added to
`gst_av_src_bin`; the x20 results above are for the final test.

Per-case output: `linux-run.log`, `windows-run.log`, `android-run1.log`.

## Limits

- The drain is reported when every track delivered EOS downstream of the bin,
  not when downstream sinks finished rendering; the consumer owns the ring, so
  frames held downstream stay valid after the producer leaves.
- `FrameLinkAHardwareBuffer` memory is not CPU-mappable through
  `gst_buffer_map`; on Android the native case checks negotiation, counts and
  timing, and pixel content is covered by the native-source tests.
- Verify-mode presentation is measured against the system clock, not physical
  scanout or speaker output.
- Pause/flush parity, the bin examples and queue-bound qualification are
  task 11.
