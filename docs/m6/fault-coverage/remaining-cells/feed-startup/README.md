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
