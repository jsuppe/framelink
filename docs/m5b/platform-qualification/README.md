# M5b.12 platform qualification

Locally validated, not yet milestone-merged.

## What was added

- **Qualification test** `test/fl_gst_av_qualification.cpp` (ctest
  `gst_av_qualification`, label `m5b_av`, Android matrix). The shared harness
  of the parity test moved to `test/fl_gst_av_harness.h`. Four configurations
  run in all four producer/consumer combinations (individual elements or
  bins), each in a fresh child process with `FRAMELINK_GST_TRACE` and
  `FRAMELINK_TRACE` enabled:

  | Configuration | Tracks | Consumer video memory |
  |---|---|---|
  | `av-cpu` | video + audio | CPU-mapped (`memory:SystemMemory`) |
  | `av-native` | video + audio | platform native (`native-memory=true`) |
  | `video-only` | video | CPU-mapped |
  | `audio-only` | audio | — |

  The driver process also checks the six element factories and runs element
  and bin churn (CPU and native consumers) with a leak control.

## What each run must show

| Area | Check |
|---|---|
| Delivery | EOS on both sides; 30/30 frames, 24000/24000 samples (per enabled track); uniform pixels (CPU path); contiguous audio timestamps |
| Timing | Render lag on the pipeline clock within latency-ms −5ms…+100ms per track |
| Memory | Negotiated feature: `memory:SystemMemory`, or the platform's native feature |
| Latency queries | Consumer pipeline live, configured latency 150ms ≥ reported minimum and ≤ reported maximum; each consumer source pad (element or bin ghost pad) answers live, min 0; producer pipeline answers not-live, min 0, max none |
| Queue bounds | Video ring `pool-size` 8 and native retained-frame occupancy ≤ 8 (core trace); producer audio queue limit 80ms and sampled high-water within it |
| EOS statistics | `framelink-sink-stats`: submitted 30, dropped 0, ring-full 0; `framelink-audio-sink-stats`: 50 blocks, 96000 PCM bytes, 24000 sample frames, serialized (not shared) PCM |
| Copy accounting | Traces present with lossless footers; exactly the platform's expected payload operations, each 30 times with exactly 30 x 8192 bytes; core `flEndSubmit` and `flAcquireFrame` succeed exactly 30 times; audio-only runs start no recorder |
| Parity | Accounting, statistics, memory and queue limits identical across the four combinations |
| Cleanup | Handles (and threads on POSIX) flat over 8 sessions each: element/bin x CPU/native; a 16-socket-per-round leak is detected |
| Controls | A 16-event recorder is reported lossy; the CPU-path trace fails the native expectations |

Expected boundary payload operations for 64x32 RGBA system-memory input:

| Platform | Producer (`framelinksink`) | Consumer, CPU path (`framelinksrc`) | Consumer, native path |
|---|---|---|---|
| Linux, Android | `gst.cpu_copy_sink` | `gst.cpu_copy_source` | none |
| Windows | `gst.gpu_upload_issued` | `gst.gpu_readback_issued` + `gst.cpu_copy_source` | none |

## Findings

- **Producer latency is not live.** The coordinated sinks do not synchronize
  to the clock (by contract), so GstBaseSink answers the producer pipeline's
  latency query not-live and adds no latency. Pacing comes from live sources
  (or `clocksync`); the consumer's `latency-ms` carries the session latency.
- **Consumer latency query reports the application's sinks.** The reported
  minimum (20ms) is appsink's processing deadline and the maximum (1000ms)
  the application queue's limit; the FrameLink sources add none. The
  configured pipeline latency (`latency-ms`, 150ms) lies between them.
- **Audio is not traced.** PCM accounting is the producer's EOS statistics
  (blocks, bytes, sample frames); serialized bytes inside GDP/GIO and copies
  inside the OS socket stack remain unknown, not zero.
- **Native video is consumer-side.** `av-native` negotiates DMABuf, D3D11Memory
  or FrameLinkAHardwareBuffer with no consumer-boundary copy, but the producer
  elements copy their system-memory input into the ring. Native producer
  allocations (`framelinksink native-memory=true` with M5a memory-policy
  binding and readiness fences) are not qualified with coordinated sessions;
  that combination stays with M6.

## Results

| Gate | Linux x86_64 (RTX 3090) | Windows (Caspar) | Android arm64 tablet |
|---|---|---|---|
| `gst_av_qualification`, single verbose run | 16/16 runs pass, controls pass | 16/16, controls pass | 16/16, controls pass |
| Native consumer memory | DMABuf | D3D11Memory | FrameLinkAHardwareBuffer |
| Render lag, worst run (latency-ms 150) | video 150.3ms, audio 152.9ms | 152.4ms, 152.1ms | 150.9ms, 150.8ms |
| Cleanup churn handles (threads), 8 sessions each | 7 (10) flat | 1553–1567 flat after warm-up | 15 (9) flat |
| Leak control (16 sockets/round) | +16/round detected | detected | detected |
| Validation matrix x10, `m5b_av` (now 14 tests) | 14/14 | 14/14 | 14/14 (fresh private TMPDIR per run) |
| Validation matrix x10, `gst_video_regression` | 12/12 | 13/13 | 7/7 |
| `gst_av_qualification` x10 | 467s total | 641s | 544s |
| Full serial suite | 112/112 | — | — |
| `gst-inspect-1.0`: six elements | present | present | static registration (checked by the test) |
| Builds | no warnings | Release, no warnings | arm64 and x86_64 plugin/tests and JNI apps, no warnings |

Android x86_64 is build-qualified only; runtime evidence is on the arm64
tablet. On Windows, `gst-inspect-1.0` finds the plugin only with the build
directory on `PATH` as well as `GST_PLUGIN_PATH` (`gstframelink.dll` loads
`framelink.dll`), as the guide states.

Per-run output: `*-qualification-run.log`; matrices `*-matrix-x10.*`;
`linux-serial.log`; `*-gst-inspect.txt`.

## Software timing versus physical alignment

Every timing figure in M5b is software timing on the consumer's pipeline
clock (the system monotonic clock): the time a buffer reaches the
application's sink relative to its running time, and the exact media offset
between tracks. None of it measures when light leaves the display or sound
leaves the speaker. Display scanout, compositor latency, audio device
buffering and DAC latency come after the sinks and differ per device and
configuration; they are not measured and no physical lip-sync claim is made.
A physical measurement needs an external capture (camera and microphone, or
photodiode and audio probe) and is outside M5b.
