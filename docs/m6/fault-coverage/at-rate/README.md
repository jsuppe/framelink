# M6.4 on Linux and Android: endpoint death and consumer stalls at rate — 2026-11-25

Revision `4f8cb2d6`. `fl_gst_av_bench` SHA-256 prefixes: Linux `bc912b7ae034481a`, Android arm64 `65bdb6ae43974083`; `libgstframelink.so` `f9d0d96cc0ea1f4b`. Hosts:
- Linux: Ubuntu 26.04, RTX 3090 (NVIDIA 580.178.04), GStreamer 1.28.2.
- Android: TCL 8483A (Mali-G52), Android 15.

This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Question

M6.4b asks for consumer/GPU stalls, producer bursts, endpoint death, audio underruns and malformed metadata to be injected, and each selected policy validated. M6.4c then asks for bounded recovery, explicit loss and no unsafe reuse. On the supported cell the gate is "fault-specific expectations; recovery within the documented bound" ([matrix](../../QUALIFICATION_MATRIX.md)). Coverage before this record:
- [existing-policy regressions](../README.md) on every platform;
- relay death and cancellation tests (`gst_relay_*_death|cancel_*`) on Linux and Android;
- [delayed GPU completion](../egl-gpu-busy/README.md).

None of these injected a fault into a live A/V session at the target rate on Linux or Android.

## Harness (`fl_gst_av_bench --fault`, commit `4f8cb2d6`)

Each run is a separate producer and consumer process at 1080p60 with the primary profile (pool 2, unread queue 1, output queue 1, latency 0, transport-only, no output preroll, 12 s, raw capture). Every case declares its outcome before the run, and the driver gates the run against it:

| Case | Injection | Pass requires |
|---|---|---|
| `producer-kill` / `consumer-kill` | the driver kills that process 6 s after the producer starts (SIGKILL) | the victim dies by the kill; the survivor exits normally with an **error result (never EOS)** within **2000 ms** of the kill (bound chosen before the runs, as in the [Windows relay death controls](../../windows-relay-death/README.md)) |
| `consumer-stall`, 5 ms, `absorbed` | the consumer's video appsink blocks 5 ms on frame 360 | every frame and sample delivered, both EOS, audio contiguous, queue accounting complete, arrival back under 17 ms within ceil(M/period)+2 frames |
| `consumer-stall`, 200 ms, `counted-drop` (native cells; default `error-on-ring-full=false`) | 200 ms (12 frame periods) | both EOS; the producer sink dropped ≥ 1 frame, all as ring-full; **delivered + dropped == pushed exactly**; audio complete |
| `consumer-stall`, 200 ms, `absorbed` (shared-CPU ref) | 200 ms | as for 5 ms |
| `consumer-stall`, 200 ms, `explicit-failure` with `--error-on-ring-full` (native cells) | 200 ms | the producer fails with "FrameLink pool exhausted within configured wait budget"; the consumer does not report EOS; both exit normally |

In every stall case, a loss not matched by the producer's drop count fails the run. A fourth outcome, `replacement-failure`, covers receivers that release slots as they copy out: the consumer must fail with "coordinated video frame replaced before it was read". It is used by the small CTest stall case.

CTest: `gst_av_bench_fault_producer_kill`, `gst_av_bench_fault_consumer_kill` and `gst_av_bench_fault_consumer_stall` (160x90 CPU cell, replacement-failure), on all platforms. `run_faults.sh` reproduces the cells.

## Results: 70/70 pass

| Cell | Case | Runs | Result |
|---|---|---|---|
| Linux native (NV12, DMA-BUF receiver) | producer-kill | 5/5 | consumer failed in 3.4–5.9 ms ("framelink acquire failed" / "audio peer closed before GDP EOS") |
| | consumer-kill | 5/5 | producer failed in 3.7–9.0 ms ("required session tracks not ready or cancelled" / "coordinated session not ready or cancelled") |
| | stall 5 ms | 5/5 | 720/720, no drops, recovery 0 frames |
| | stall 200 ms | 5/5 | 709 delivered + 11 counted ring-full drops = 720 in every run; recovery 0 frames |
| | stall 200 ms, strict | 5/5 | the producer failed (pool exhausted) after 361 pushed; the consumer failed after 359 |
| Linux shared-CPU ref (NV12) | producer-kill | 5/5 | 1.7–5.2 ms |
| | consumer-kill | 5/5 | 4.5–8.3 ms |
| | stall 5 ms | 5/5 | 720/720, recovery 0 |
| | stall 200 ms | 5/5 | 720/720, no loss; 12–13 frames arrived over 17 ms after the stall (limit 15) |
| Android native (RGBA, AHardwareBuffer receiver) | producer-kill | 5/5 | 17.9–34.5 ms |
| | consumer-kill | 5/5 | 23.2–27.4 ms |
| | stall 5 ms | 5/5 | 720/720, recovery 0 |
| | stall 200 ms | 5/5 | 708–709 delivered + 11–12 counted drops = 720 in every run; recovery 0–2 |
| | stall 200 ms, strict | 5/5 | the producer failed (pool exhausted) after 361 pushed; the consumer failed after 359 |

The stalls executed for 200.1–200.7 ms (5.1–5.3 ms for the short case). No run reported EOS after an endpoint died, and no stall run lost a frame that was not counted.

## Findings

- **Endpoint death is detected fast and reported as an error** on every cell: at most 9 ms on Linux and 35 ms on Android, against the 2 s bound. Neither side ever reported a clean end.
- **Under the default policy the loss is explicit only at the producer.** With a native receiver, the consumer holds the slot while downstream holds the buffer. A stalled consumer therefore leaves the producer with no slot, and its sink drops and counts the frame (`framelink-sink-stats`). The consumer sees a contiguous sequence with no DISCONT, gap message or arrival miss, and ends with EOS on 709 of 720 frames. This is the documented default (`error-on-ring-full=false`: drop and count; [reference](../../../GST_AV_REFERENCE.md)), not a defect. An application that needs the consumer to know about the loss must read the producer's statistics, or choose `error-on-ring-full=true`, which turns the same stall into an explicit failure on both sides. Whether this default is right is the deferred D2 decision (Jon).
- **The outcome depends on the receiver.** A receiver that releases slots as it copies out (CPU at small sizes) turns the same stall into the consumer's fatal replacement error. Shared CPU absorbs 200 ms by delivering late, with no loss.

## Pilot runs (kept, not part of the 70)

The first pilots declared outcomes that were wrong (`pilot/`). Those runs stand as recorded:
- 200 ms on the native cell was declared `explicit-failure` (the replacement error) but produced counted drops.
- On shared CPU it was declared `explicit-failure` but was absorbed.

The outcomes above were then derived from the documented overload policy and the receiver mechanism. The shared-CPU 200 ms expectation (`absorbed`) was set after that one pilot observation.

The CPU receiver at 1080p60 (`pilot-control-cpu`, `pilot-stall200-cpu`) delivered 18 of 720 frames with or without the stall: 702 counted ring-full drops, and the consumer reported EOS. Linux CPU readback at the target cells is already unsupported (M6.8), so that cell was not run.

## Regression found and fixed in this change

The new `read_lease_egl_gpu_busy_completion` ([record](../egl-gpu-busy/README.md)) saturates the GPU for about 0.7 s. In a parallel `ctest -j4` run, `gst_local_native_bench_smoke_linux` failed once (output not captured). Running the four GPU tests together reproduced failures 6 of 6:
- `gst_av_bench_gpu_readback` had two startup slot drops;
- the local native bench skipped a pre-roll frame.

The test is now `RUN_SERIAL`. The same group then passed 6 of 6, and the full Linux suite under `-j4` passed 192/192 twice, with the known skip `read_lease_egl_delayed_completion`. The Android `gst_av_bench` group passes 4 of 4.

## Not established here

- **Producer bursts and audio underruns at rate.** The bench's `--video-stall-stage` producer stall exists, but it was not run under declared outcomes here. Audio underrun remains covered only by the functional `gst_audio_*` tests.
- **Consumer GPU stalls in a live session.** Only the lease-level [GPU-busy test](../egl-gpu-busy/README.md) covers them.
- **Growth across repeats.** Each run is a fresh pair of processes; lifecycle (M6.3) covers repeated sessions in a long-lived process.
- **Other cells.** Not covered: 4K30, BGRA, element mode, the GPU-staged receiver, or the Windows cells, although the harness also builds on Windows.
- **The D2 overload default.**

Files: `run_faults.sh`, `summaries/` (70 runs plus per-cell `run.log`), `pilot/`, `*binaries.sha256`, `raw-sha256.json` (raw CSVs retained outside the repository).
