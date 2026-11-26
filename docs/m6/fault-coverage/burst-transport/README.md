# M6.4 on Linux and Android: queued producer bursts, FrameLink's share — 2026-11-25

Revision `063d6234`. `fl_gst_av_bench` SHA-256 prefixes: Linux `918d15cf4d8f2845`, Android arm64 `6cf8ebcd33682f2f`; `libgstframelink.so` `f9d0d96cc0ea1f4b`. Same hosts, profile and cells as the [first producer-fault record](../producer-at-rate/README.md). This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Why a second run

In the first record the Android 200 ms burst failed 5/5: arrival took 18–19 frames to come back on time, against a declared limit of 15. That record put the lag down to the test source's slow catch-up. **That attribution was wrong.** The push time was taken at the source's output, and a push there returns only after everything downstream, including FrameLink taking the frame in, has finished. The catch-up rate it measured was therefore largely FrameLink's intake rate. That run also never produced a real burst at FrameLink's input.

## Harness change (`--burst-queue`, commit `063d6234`)

- A queue follows each test source. The stall is injected on the queue's output, so the source keeps producing in real time and the backlog then reaches FrameLink back to back. The push time is taken there, at FrameLink's input.
- **Gate (`absorbed`)**, declared before the runs:
  - complete delivery, as before (every frame and sample, both EOS, audio contiguous, queue accounting complete);
  - **the burst happened:** the first frame after the stalled one entered FrameLink at least M − 2 periods late, and the backlog drained (push lateness back under 17 ms) at least twice as fast as real time (mean gap ≤ half a period: 8 ms video, 5 ms audio);
  - **FrameLink's share of the delay** per buffer (consumer arrival lateness minus push lateness at FrameLink's input) is over 17 ms for no more than ceil(M/period)+2 buffers after the fault.
- Reported, not gated: total recovery (arrival lateness alone) and the drain rate. The drain rate is FrameLink's intake rate under the burst: each push returns once the previous buffer has been taken in.

## Results: 57/60; Android 200 ms burst failed the drain-rate check 3/5

Columns: FrameLink-share recovery (buffers over budget) / total recovery / drain mean gap.

| Cell | Burst 5 ms | Burst 200 ms | Audio stall 50 ms | Audio stall 200 ms |
|---|---|---|---|---|
| Linux native NV12 | 5/5: 0 / 0 | 5/5: 0 / 15 / 3.5–3.8 ms | 5/5: 0 / 4 | 5/5: 0 / 19 |
| Linux shared CPU NV12 | 5/5: 0 / 0 | 5/5: 0 / 13 / 2.0–2.1 ms | 5/5: 0 / 4 | 5/5: 0 / 19 |
| Android native RGBA | 5/5: 0 / 0 | **2/5**: 0 / 22–24 / **7.6–8.6 ms** | 5/5: 0 / 4 | 5/5: 0 / 20 |

- In all 60 runs FrameLink's share of the delay never went over 17 ms. On the Android 200 ms bursts it was at most 14.7 ms (median 6.9–7.2 ms).
- Delivery was complete in every run: 720/720 frames, 576000 samples, no producer drops.
- **The 3 Android failures stand.** Runs 1, 2 and 4 drained at 8.6, 8.3 and 8.1 ms per frame against the declared 8 ms (runs 3 and 5: 7.6 and 7.8 ms). Everything else in those runs passed.

## Finding: FrameLink's producer intake on Android 1080p RGBA is about 2x real time

During a burst on the Android native cell, FrameLink takes a 1080p RGBA frame (about 8.3 MB) in every 7.6–8.6 ms, about twice the frame rate. A 200 ms producer burst therefore takes 22–24 frames (about 370–400 ms) to drain, and the frames stay late until it has. On Linux the same intake is 3.5–3.8 ms per frame (native) and 2.0–2.1 ms (shared CPU), about 4–8x real time.

What makes up the 8 ms (the producer's copy into the slot, waiting for a free slot with pool 2, or both) was not measured. A trace would separate them. Whether about 2x burst headroom is acceptable on the Android cell is a budget question for Jon.

## Pilots (kept, not part of the 60)

- `pilot/linux-*`: 2 runs on Linux native, from an earlier version of the burst check that counted back-to-back pushes. The 200 ms burst failed that check (9 against 10), although its FrameLink-share recovery was 0. Each gap includes FrameLink's intake of the previous frame, so the count was the wrong test, and it was replaced before the commit.
- `pilot/*-1`: 1 run of each case on all three cells with the committed check; all 12 passed.

## Not established here

- What makes up the Android intake cost (see above).
- `counted-drop` under a burst: no burst here overran the ring.
- Everything listed in the [first record](../producer-at-rate/README.md#not-established-here).

Files: `run_burst_faults.sh`, `summaries/` (60 runs plus per-cell `run.log`), `pilot/`, `*binaries.sha256`, `raw-sha256.json` (raw CSVs retained outside the repository).
