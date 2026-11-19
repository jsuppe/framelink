# Android BufferQueue baseline for the FrameLink receivers — 2026-11-18

Same TCL 8483A (MT6768), Android 15. Source: `bq_bench.cpp` (NDK r28, API 29, `-O2`). Raw output for all 8 runs is in `output.txt`. This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## What it measures

The question is how FrameLink's Android receivers compare with Android's own frame queue (BufferQueue, through `AImageReader` and its `ANativeWindow`) on the same device, sizes and rates.

- **Producer thread.** It is paced at the target rate. For each frame it calls `ANativeWindow_lock`, copies a prepared full RGBA frame into the buffer, writes the frame index, and calls `ANativeWindow_unlockAndPost`.
- **Consumer thread.** It is woken by the image listener and acquires every image in order (`acquireNextImage`; no frames are skipped).
  - `cpu`: the reader uses `CPU_READ_OFTEN`. The consumer locks the `AHardwareBuffer`, copies it into a reused, pre-touched buffer and checks the frame index.
  - `gpu`: the reader uses `GPU_SAMPLED_IMAGE`. The consumer takes the buffer without copying.
- **Settings.** `maxImages=2`, matching FrameLink's pool of 2. Lateness is measured from the consumer finishing to the frame's scheduled time, which is the definition `fl_gst_av_bench` uses for arrival.

**Differences from the FrameLink cells, which matter for reading the numbers:**
- **One process, two threads.** FrameLink's cells use separate producer and consumer processes with GStreamer pipelines, audio and measurement sinks.
- **The producer copies a prepared frame.** FrameLink's test producer generates each frame with `videotestsrc` and then copies it into the ring. CPU percentages are per thread here and per process there, so they are **not comparable** as they stand.
- **60 s runs**, two alternating rounds, not 1800 s.
- `post_to_done` takes the producer's timestamp after `unlockAndPost` returns, so it can be slightly negative when the consumer finishes first.

## Results (steady state after 2 s, p50 / p99 / max in ms; both rounds)

| Path | 1080p60 arrival | 4K30 arrival | Post to consumer done, p50 (1080p / 4K) | Misses (1080p / 4K) |
|---|---|---|---|---|
| BufferQueue, CPU copy | 7.63 / 11.32 / 17.09; 7.68 / 11.46 / 15.63 | 19.90 / 23.82 / 30.66; 19.91 / 24.32 / 29.87 | 3.14–3.19 / 9.26–9.34 | 4, 4 of 3600 / 2, 2 of 1800 |
| BufferQueue, GPU, no copy | 4.80 / 8.71 / 11.30; 4.80 / 8.73 / 12.16 | 11.17 / 14.50 / 16.04; 11.16 / 14.87 / 16.68 | 0.28–0.29 / 0.24–0.25 | 2, 2 / 1, 1 |

Every run delivered every frame, and the CPU variant had no frame-index errors. Over all frames, each run's maximum (40–65 ms) is its first frames. The producer's own full-frame write takes 3.4–3.5 ms at 1080p and 9.7–10.0 ms at 4K.

FrameLink on the same device, from the linked records. These are p50 arrival times, and they include the test producer's generation and copy:

| Path | 1080p60 | 4K30 |
|---|---|---|
| FrameLink native receiver (60 s control) | 6.13 ms | 11.34 ms |
| FrameLink CPU receiver (1800 s) | 10.01 ms | 25.77 ms (prepared) |

## What this shows and what it does not

- **No-copy paths.** At 4K30, the FrameLink native receiver and BufferQueue without a copy arrive at the same time (11.3 against 11.2 ms p50). At 1080p60, FrameLink is about 1.3 ms later. Both figures are dominated by the producer's write. The cross-process GStreamer pipelines, which BufferQueue doesn't have here, are the likely source of FrameLink's extra 1.3 ms. That is not isolated.
- **CPU copy paths.** On BufferQueue, handoff plus copy costs about 9.3 ms at 4K. That is close to FrameLink's push-to-pull stage cost of 11.2 ms in the [output-reuse record](../cpu-output-reuse-20261118/README.md). End to end, the FrameLink CPU receiver arrives about 6 ms later than BufferQueue at 4K30 (25.8 against 19.9 ms) and about 2.4 ms later at 1080p60. That difference includes `videotestsrc` frame generation, the second process and GStreamer, which this baseline doesn't have.
- **CPU use.** The 4K BufferQueue threads use about 31% (producer) and 27% (consumer). The FrameLink processes use 82% and 58%. Most of that gap is plausibly the test producer's frame generation and the A/V pipelines, but this baseline cannot separate it. A fair CPU comparison needs the same producer on both paths.
- **Not covered:** cross-process BufferQueue (Surface over Binder), a real GPU producer or consumer, sustained runs, and audio.
