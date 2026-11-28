# GPU-staged receiver: frames lost at session start (2026-11-28)

The Linux GPU-staged growth runs lost frames in clean cycles: 22 of 50 in restart-consumer-kill at `6c45029e`, and 5 in the first campaign ([rerun](../README.md#rerun-at-6c45029e-after-jons-two-follow-ups-2026-11-28)). Jon asked for the test producer to be fixed. This record finds the cause and shows that it is in the receiver, not in the test producer. This record is dated; current status is in [STATUS.md](../../../../STATUS.md).

## Bench change (test only)

- `fl_gst_av_bench` now times each slot request from the GPU-staged feed thread: `native.acquire_wait.ms`, and `cycle.N.acquire_wait.ms` in growth runs.
- Growth runs now record the feed's slot drops for every cycle: `cycle.N.slot_drops`, `cycle.N.slot_drop_frames`, and in the summary `cycle.slot_drops.series` and `cycle.slot_drops.total`.
- `?` marks a killed fresh producer, which writes nothing.
- The gate is unchanged: a clean cycle must still deliver all 300 frames.

## Findings

1. **The lost frames are the first frames of a session.** They are frames 2 and 3 in the two rerun audio-stall failures, and up to 6 in the early cycles of the rerun. In the 30-cycle runs here, every clean cycle had 0 drops except one, which lost frame 2 (`c30-*`, `p2-*`). Faulted cycles drop about 124 frames after the kill, which is expected because the consumer is gone.
2. **The producer behaves as the sink documents.** A frame whose slot does not free within `buffer-wait-ms` (5 ms) is dropped and counted, as the CPU-copy sink drops on a full ring. Slot waits were under 0.1 ms at p99, with rare peaks of 1–5.3 ms (`cycle.N.acquire_wait.ms`).
3. **The receiver's first readback takes about 31 ms. Later ones take about 1–3 ms.**
   - Six traced 5 s runs gave 30.6–32.6 ms for the first `gst.egl_readback` (`first-readback-trace.txt`).
   - Steps were timed temporarily inside `FrameLinkEglReadback::read` (not committed; `readback-steps.txt`). The first call spent 29–33 ms in `bind()`. Import, draw, readback and finish together took under 2 ms, the same as on later calls.
4. **Why `prepare()` does not cover it.** The element opens the staging context in `srcStart` with `prepare()`, on a GLib pool thread. Frames are read on the streaming thread, and NVIDIA pays about 30 ms the first time a new thread makes that context current.
   - Two pool slots give a reader 2 periods (33 ms) plus the 5 ms wait at 1080p60, so a 30 ms first bind sits right at the edge.
   - A cold GPU (the early cycles of a run) pushes it over, and the producer drops frames 2–6.
5. **The two remedies already tried were both rejected** ([readback rate record](../../../cpu-path/linux-gpu-readback-rate/README.md)):
   - preparing on the streaming thread's first `srcCreate` lost every frame;
   - leaving the context on the start thread caused a 40–50 ms stall 15.5 s later, when GLib reaped that thread.
6. **A third pool buffer does not help** (`p3-ckill-p3.summary.txt`). With pool 3, the producer used the extra slot to overwrite the unread frame during the first bind. The consumer failed in cycle 0 ("ring sequence 3 after 1 with a receive queue of 1"), and the run failed 0/30.

## Options (product changes, for Jon)

- **A. A dedicated reader thread per GPU-staged receiver.** It is created and warmed at `srcStart` and owns the EGL context for the element's life; `srcCreate` hands each read to it and waits. This removes both the first-bind cost and the thread-reap stall. Cost: one thread per receiver and a synchronous handoff on each frame. Linux only.
- **B. A deeper unread queue for the GPU-staged receiver** (pool 4, queue 2). It absorbs the first bind at the cost of one more frame of latency.
- **C. Document it.** Up to about 5 counted, visible frames may be lost at session start at pool 2, and the growth gate would accept startup drops that are counted and shown by the content check.

## Option A at `30c64032` (Jon, 2026-11-28)

Jon chose A. Each Linux GPU-staged receiver now owns a reader thread (`FrameLinkEglReader`, `gst/fl_gst_egl_readback.hpp`). The thread opens and binds the staging context at `srcStart`, keeps it for the element's life, and releases it on its own thread at stop. `srcCreate` hands each read to it and waits for the GPU to finish, so the slot is released at the same point as before. Readback code is unchanged. Evidence: [`../reader-30c64032/`](../reader-30c64032/) (bench `24499157edc721b4`, plugin `3380a01caf1a718b`; `raw-sha256.json` covers the 628 raw CSVs).

- **Test.** `egl_readback` now requires the first read on the reader thread to take under 15 ms, on a different thread from the caller, with exact content on 8 reads. With preparation skipped, the first read took 32.6 ms and the test failed; with it, 2.0 ms. Linux suite: 203 passed, 1 skipped.
- **First readback in the pipeline:** 1.9–2.0 ms in six traced 1080p60 runs (before: 30.6–32.6 ms).
- **1800 s readback-rate cells** (`rate/`): 4K30 54000/54000 and 1080p60 108000/108000, every frame content-checked, no slot drops, 0 arrival misses (before: 1 each, at frame 0). Arrival max 21.3 ms at 4K30 and 15.0 ms at 1080p60 (before: 35.5 and 31.9 ms). There is no stall at 15.5 s: the start thread no longer holds the context.
- **Decision 15 cells:** GPU-staged 1080p60 70/70 (rerun at `6c45029e`: 68/70) and 4K30 70/70. Copy-out bursts: 1080p60 3 replacement failures and 2 counted drops, 4K30 2 and 3.
- **Growth, clean cycles:** every clean cycle in all three runs delivered all 300 frames with 0 slot drops (`cycle.slot_drops.series`). restart-consumer-kill 100/100 PASS (before: 78/100).
- **Growth, memory gate: 2 of 3 fail.** restart-producer-kill 27.46 KB/cycle and restart-producer-malformed 26.59 KB/cycle against 16.0. The failures stand.
  - The long-lived consumer gains about 13 MB over its first 20 or so cycles, then levels off. A least-squares fit over cycles 25–99 gives 2.15 and 2.39 KB/cycle; the rerun at `6c45029e` gave 1.95 and 4.72. The gate skips only 5 warm-up cycles.
  - **Cause: glibc gives each new reader thread its own malloc arena.** In 40-cycle restart-producer-kill runs (`arena-diag/`), the control gained 107416 → 120232 kB (140.44 KB/cycle by the gate). With `MALLOC_ARENA_MAX=1` it stayed at 108–110.6 MB (−2.28 KB/cycle). Before A, reads ran on GStreamer's reused streaming thread.
  - Whether to keep the gate's 5-cycle warm-up, or change the receiver so it does not create a thread per session, is for Jon.

## Steady consumer growth found during the check (not caused by A)

The 1800 s cells show consumer RSS rising steadily: 202 kB/min at 4K30 and 358 kB/min at 1080p60, fitted over 60–1790 s. The 2026-11-25 record at `4f40c517` showed 55 and 110 kB/min, mostly from a few steps, with about 0.1 MB of gradual growth per run.

- **A/B, 600 s at 1080p60, same bench** (`ab-rss/`): the pre-fix plugin (`92a75664`, `a56cb363a4731f3e`) grew 328 kB/min in both runs; the fixed plugin grew 329 kB/min in both runs; the fixed plugin with `MALLOC_ARENA_MAX=1` grew 325 kB/min. The growth is the same without the reader thread, so A did not cause it.
- Pre-fix runs: 2 arrival misses each; fixed runs: 0. All five complete with content checked and no drops.
- **Where it is:** all of a 220 s delta (60 → 280 s) is anonymous memory in one malloc heap (`ab-rss/consumer-smaps-60-280.txt`), at about 5 kB/s at 1080p60. That is roughly 70 bytes per video frame on a two-cell fit, not driver mappings.
- **Not yet attributed.** It appeared between `4f40c517` and `92a75664` (62 commits, bench and plugin both changed). One known contributor is the harness's per-frame `bufferLife` record (16 bytes per video frame, reserved but not touched in advance), which accounts for about a quarter. No root or heap profiler is available on this host. A bisect, or an allocation-tracing shim, is the next step.
