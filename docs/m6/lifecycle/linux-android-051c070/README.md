# Linux and Android native lifecycle (M6.3a) — 2026-11-22

Revision `051c0701` (clean tree). Its library, plugin and test code are identical to `0e8da988`, the revision of the [rate cells](../../linux-native-relay-rate-0e8da98/README.md); `051c0701` only widens the runner. The Linux plugin is byte-identical to the one in the rate cells (SHA-256 `f181428c…`).

**Command:** `tools/m6_bench_matrix.py --set lifecycle`, with `--adb` on Android. It runs every native target cell at 1080p60 and 4K30, in each native format: BGRA and NV12 on Linux, RGBA on Android. Each cell runs 100 cycles of 5 s, in two directions:
- restarting the producer process, while a long-lived consumer is measured;
- restarting the consumer process, while a long-lived producer is measured.

The profile is the primary one: pool 2, unread 1, output 1, latency 0, sync and async off.

**Gates**, frozen and now applied by the runner. A cell passes only if:
- every cycle delivers exactly the expected media (300 or 150 video frames and 240 000 PCM samples) and reaches EOS;
- the long-lived peer's least-squares slopes over cycles 5–99 are within 16 KB, 0.1 handles and 0.02 threads per cycle.

These are the original limits and window. The Windows option B 100–199 window is not used here.

Android native output in this session bench is RGBA only: its NV12 buffers have an opaque YUV layout, not NV12 bytes, and the bench refuses them. A 2-cycle probe confirmed the refusal. So Android has no NV12 lifecycle cells.

## Results

| Platform | Cell | Cycles | Memory slope (KB/cycle) | Handles, threads | Result |
|---|---|---|---|---|---|
| Linux | BGRA 1080p60, restart producer | 100/100 | 1.95 | flat | pass |
| Linux | BGRA 1080p60, restart consumer | 100/100 | 1.90 | flat | pass |
| Linux | NV12 1080p60, restart producer | 100/100 | 1.58 | flat | pass |
| Linux | NV12 1080p60, restart consumer | 100/100 | −5.10 | flat | pass |
| Linux | BGRA 4K30, restart producer | 100/100 | 1.87 | flat | pass |
| Linux | BGRA 4K30, restart consumer | 100/100 | −215.94 | flat | pass |
| Linux | NV12 4K30, restart producer | 100/100 | 2.27 | flat | pass |
| Linux | NV12 4K30, restart consumer | 100/100 | **24.78** | flat | **FAIL** (memory gate) |
| Android | RGBA 1080p60, restart producer | 100/100 | 3.11 | flat | pass |
| Android | RGBA 1080p60, restart consumer | 100/100 | −10.82 | flat | pass |
| Android | RGBA 4K30, restart producer | 100/100 | 1.55 | flat | pass |
| Android | RGBA 4K30, restart consumer | 100/100 | 1.39 | flat | pass |

**Held leases** (`fl_read_leases --cycles 100`, same-process, with retained readers, out-of-order retirement, resize and reconnect while a reader is outstanding): pass on both. Slopes over cycles 5–99 are 0.01 KB/cycle on Linux and 1.99 KB/cycle on Android, with handles and threads flat.

Every cycle of every cell delivered exact media; the only failure is the memory gate.

## The failure: Linux NV12 4K30, restart consumer

The long-lived producer's resident memory does not grow steadily. It has short pulses of one storage-sized block that appear and are released:

| Cell | Baseline after warm-up | Pulses | Slope without the pulses' effect |
|---|---|---|---|
| NV12 4K30, restart consumer | 66 736 → 66 932 KB | +36 MB for one sample, at cycles 65 and 84 | cycles 5–49: 0.81, cycles 50–99: 0.96 KB/cycle |
| BGRA 4K30, restart consumer | 82 856 → 83 160 KB | +65 MB at cycles 6–9 and 30–32 | cycles 50–99: 1.16 KB/cycle |
| NV12 1080p60, restart consumer | 18 348 → 18 596 KB | +9 MB for one sample, at cycles 10, 29 and 58 | |

The pulses are about the size of the 4K or 1080p frame storage. They occur in three of the four Linux restart-consumer cells (not BGRA 1080p60), never in a restart-producer cell, and never on Android. Two late pulses give the NV12 4K30 fit its 24.78; early pulses give BGRA 4K30 its −215.94. Both 4K restart-consumer cells have the same underlying growth, about 1 KB/cycle. Their verdicts differ only because of where the pulses fall in the window.

This is an observation, not a waiver: the cell fails the gate as defined. What allocates and releases the block on a consumer restart is not established. The pulse size points to storage the producer maps or allocates while a new consumer attaches.

## Files

- `linux/` and `android/`: the runner's `results.json` and `run.log`, the held-lease logs, and on Linux each cell's `.summary.txt` with the full per-cycle series of the long-lived peer.
- Linux per-cycle fresh-peer results are archived outside the repository: `linux-cycles.tar.xz`, SHA-256 in `linux-cycles.tar.xz.sha256`. The Android runner removes per-cycle files on the device; the long-lived series is in `results.json`.
- Binary SHA-256: Linux `fl_gst_av_bench` `63695530…`, `libgstframelink.so` `f181428c…`, `fl_read_leases` `ea679f81…`. Android `fl_gst_av_bench` `64f3e9f2…`, `fl_read_leases` `81977dd5…` (statically linked).
