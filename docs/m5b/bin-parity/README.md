# M5b.11 bin parity and examples

Locally validated, not yet milestone-merged.

## What was added

- **Parity test** `test/fl_gst_av_parity.cpp` (ctest `gst_av_parity`, label
  `m5b_av`, Android matrix). Every scenario runs with the producer and the
  consumer each as individual elements (the task 7 application path) or as
  the bins, in all four combinations, and the observable outcome must be
  identical.
- **Two-process example** `framelink_gst_av --bin` on either side. The runner
  `test/m5b_gst_av_example.py` runs every case element/element and bin/bin,
  A/V cases also bin/element and element/bin, and requires the delivery lag
  of the element and bin runs to agree within 20ms.
- **Developer guide** (`samples/GST_AV_EXAMPLE.md`): `--bin`, gst-launch
  pipelines with no code, bin property and pad reference, audio-only and
  video-only use, a lifecycle and failure table, real-time delivery and CPU
  readback limits.

## Parity scenarios

| Scenario | Identical outcome in all four combinations |
|---|---|
| A/V consumer-first, producer-first; video-only; audio-only | EOS both sides, 30 frames / 24000 samples exact, lag latency+0–100ms per track |
| Native memory | Same, platform memory negotiated on both consumer paths |
| Early producer EOS | Everything pushed arrives; producer EOS after the drain |
| Producer delivers a burst (starts 300ms behind its timeline) | Consumer fails at once: "coordinated video frame replaced before it was read" |
| Consumer pause/resume | Same immediate failure (frames replaced while paused) |
| Flushing seek on the producer or consumer pipeline | Refused; stream intact, exact counts |
| Producer death, consumer death | Peer error within milliseconds |
| Missing audio track, missing video track | Both sides fail at negotiation, bounded |
| Audio block above `audio-queue-ms` | Producer error, consumer sees the peer close |
| Reconnect (two sessions on one path) | Both complete exactly |
| Cleanup: 8 bin/bin sessions | Handle count flat; a 4-socket-per-round leak control is detected |

## Defects found and fixed

- **Consumer bin drained after a failure.** GstBaseSrc pushes EOS after a
  fatal flow error, and `framelinkavsrc` counted that EOS and reported the
  drain, so the producer finished "successfully". The bin now records inner
  errors and reports no drain, exactly as an application that stops on the
  error.
- **Refused seek still broke the session.** The pipeline sends a seek to
  every sink: the audio socket refused it, but the video sink forwarded it
  upstream, the sources flushed, and the coordinated video sink then failed
  on the new segment. Both coordinated sinks now refuse seeks themselves.
- **Lost video frames surfaced late and misleadingly.** The ring keeps only
  the newest unread frame. Any frame replaced before the consumer read it (a
  producer burst, a paused consumer, a slow CPU readback) made the session
  wait for a frame that could never arrive and fail at the idle deadline with
  "coordinated video idle deadline, cancellation or stale generation". The
  source now fails on the next read, naming the cause. Decision (Jon,
  2026-11-02): fail fast and document; no ring queue change.

## Findings documented, not changed

- **Real-time delivery.** The coordinated video sink does not pace, by
  contract. Non-live sources need `clocksync`, and a clock-paced producer
  that starts behind its timeline bursts. With `framelinkavsink` such a
  producer is effectively live and starts before the session exists, so it
  bursts by the setup time; live sources do not.
- **CPU readback on discrete GPUs.** On Linux with an RTX 3090, ring images
  are linear GBM buffers in video memory and the CPU consumer path reads them
  at about 34MB/s: 640x360 RGBA took 27ms median, 78ms p90 per frame, so the
  task 7 example at its old 640x360 default lost frames in about half the
  runs with elements and bins alike (task 7 qualified at 160x90). The example
  default is now 320x180 (8/8 element, 8/8 bin); `native-memory=true` passed
  640x360 5/5. Follow-up: roadmap M6.8. Decision (Jon, 2026-11-02): document,
  keep scope.
- **Producer-bin preroll experiment, reverted.** Holding a non-live producer
  in preroll until the session is ready would remove the setup backlog, but a
  clock-paced pipeline is live to GStreamer (`clocksync` returns NO_PREROLL),
  so it never applies; reverted to the validated task 9 design.

## Results

| Gate | Linux x86_64 | Windows (Caspar) | Android arm64 tablet |
|---|---|---|---|
| `gst_av_parity` (16 scenarios x 4 combinations + churn), single run | pass, 77s | pass | pass |
| Native memory in parity | DMABuf | D3D11Memory | FrameLinkAHardwareBuffer |
| Validation matrix x10, `m5b_av` | 13/13 | 13/13 | 13/13 (fresh private TMPDIR per run) |
| Validation matrix x10, `gst_video_regression` | 12/12 | 13/13 | 7/7 |
| Two-process example (23 cases incl. 5 timing-parity checks) | 23/23; element vs bin lag within 0.1ms | in matrix | in matrix (adb) |
| Full serial suite | 111/111 | — | — |
| Builds | no warnings | Release, no warnings | arm64/x86_64 + JNI, no warnings |

Bin churn handle counts stayed flat (Linux 11, Windows 1531–1533, Android 21)
while the leak control grew by 4 per round on every platform.

Per-run output: `linux-parity-run.log`, `windows-parity-run.log`,
`android-parity-run.log`, `linux-example-run.log`; matrices `*-matrix-x10.*`;
`linux-serial.log`.

## Limits

Physical display and speaker timing are not measured. The parity test runs
both sides in one process; the example runner covers two processes. Android
native memory (`FrameLinkAHardwareBuffer`) is not CPU-mappable, so its pixel
content is checked by the native-source tests.
