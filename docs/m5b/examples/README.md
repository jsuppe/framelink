# M5b.7 integration examples

Locally validated, not yet milestone-merged.

## What was added

- `fl_gst_session_open_local_av`: one-call two-process A/V (or single-track)
  setup on the existing current-user rendezvous. Authenticated negotiation, an
  HMAC-authenticated clock-offset round trip installed as the consumer mapping
  (rejects above 50ms uncertainty), authenticated audio and event streams, and
  the video lease under a secret-derived channel name.
- `fl_gst_session_apply_pipeline_clock`: monotonic system clock, producer base
  time at the epoch, consumer base time 0 plus a pipeline latency.
- `fl_gst_session_local_ready` and `fl_gst_session_finish_local`: bounded
  startup gate and end-of-session drain on the retained control connection.
- `samples/gst_av_example.cpp` (`framelink_gst_av`) with producer, consumer
  `--display` and headless `--verify` modes, and the developer guide
  [GST_AV_EXAMPLE.md](../../../samples/GST_AV_EXAMPLE.md), including
  code-free audio-only `gst-launch-1.0` commands (checked on Linux).
- Tests: `gst_local_av` (both roles in one process with real pipelines) and
  `m5b_gst_av_example` (real producer and consumer processes).

## Defects found and fixed while building the examples

Each has a retained negative control that fails without the fix.

| Defect | Fix | Control |
|---|---|---|
| Consumer started streaming before the producer attached; the late attach changed the ring generation and the consumer rejected it | Consumer returns from setup only after the producer confirms the attach | `control-attach.log`: 3 of 4 video cases fail |
| Video-only coordinated sessions never ended: the consumer learned video EOS only from the audio terminal record | Video source reads the producer's END accounting from the ordered event stream without consuming it | `control-video-end.log`: video-only times out |
| Producer exit closed the lease while the consumer still held queued frames | Drain handshake before the producer tears down | `control-drain.log`: 4 of 4 video cases lose the tail |
| Display consumer took ~250ms to create its device; the producer overwrote the first 7 frames | Producer waits for the consumer's ready report before PLAYING | Observed before the fix (sequence gap of 7 at start, producer 150 submitted, 0 drops, consumer idle-deadline error); that run was not saved. Fixed runs 3/3 in `windows-display-first-three.log` |
| Nagle/delayed-ACK on the control socket skewed the clock round trip by ~21ms on Android | `TCP_NODELAY` on the control connection; skew now below 0.1ms | `android-nodelay-negative.log`: -21ms skew, test fails |

Two integration rules are documented rather than coded: a `queue` after each
FrameLink source (a synchronized sink otherwise blocks the source thread and
only the newest frame is seen), and a video pool covering frames held within
the latency (`pool >= ceil(latency * fps) + 2`). On Windows the plugin
directory must also be on `PATH` for the registry scanner.

## Results on the final source

| Platform | Result |
|---|---|
| Linux full suite, serial | 107/107 (`linux-serial.log`) |
| Linux two-process example, 6 cases x5 | 30/30 |
| Linux `gst_local_av` repeat | 20/20 |
| Windows (Caspar) affected suite | 12/12 |
| Windows two-process example, 6 cases x5 | 30/30 |
| Windows `gst_local_av` and `gst_av_timeline` repeat | 20/20 each |
| Windows display mode (interactive desktop, autovideosink + autoaudiosink) | passed, 150/150 frames submitted, 0 drops |
| Android tablet two-process example, 6 cases x5 | 30/30 |
| Android `gst_local_av` repeat | 20/20 |
| Android timeline, audio sink/source, local audio | passed |
| Android arm64/x86_64 and JNI builds | passed, no warnings |

Cases: A/V RGBA consumer-first and producer-first, A/V NV12, video-only BGRA,
audio-only, and a negative track-set mismatch that must fail on both sides.
Verify mode requires exact frame and sample counts, uniform test pixels,
contiguous audio timestamps and delivery between PTS+latency-5ms and
PTS+latency+100ms on the system clock. Observed lag stayed within 150.0 to
152.4ms at a 150ms latency.

Windows display mode ran through an interactive-only scheduled task,
`FrameLink-M5b-Display-Example` (`m5b-display-example.ps1`), because the SSH
session cannot open a window. One earlier run captured a desktop screenshot
showing the rendered test pattern; it is not retained because it included
unrelated desktop content.

## Limits

CPU-mappable rings only; native GPU memory is qualified separately in M5a.
Display mode was exercised on Windows only; Linux and Android runs are
headless. Delivery timing is checked against the system clock, not measured
at the physical display or speaker. Standalone audio-only gst-launch has no
cross-process clock mapping. Reconnect requires a new session.
