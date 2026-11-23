# Linux and Android lifecycle and shared CPU, primary profile — 2026-11-22

Revision `e1284833` (clean tree). Its library, plugin and test code are identical to `051c0701` and `0e8da988`; `e1284833` only changes the runner (`tools/m6_bench_matrix.py`). The binaries are byte-identical to the [buffered-defaults run](../linux-android-051c070/README.md): Linux `fl_gst_av_bench` `63695530…`, `libgstframelink.so` `f181428c…`, `fl_read_leases` `ea679f81…`; Android `fl_gst_av_bench` `64f3e9f2…`, `fl_read_leases` `81977dd5…`.

**Profile.** Every cell runs the primary profile: pool 2, unread queue 1, output queue 1, latency 0, unsynchronized sinks (`--pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only --no-output-preroll`). The runner fails a cell whose summary reports another profile. None did.

**Commands**, in this order, per platform (`--adb` on Android):
1. `m6_bench_matrix.py --set lifecycle`: native lifecycle. Linux runs BGRA and NV12; Android runs RGBA only, since its bench refuses NV12 native output ([why](../linux-android-051c070/README.md)).
2. `--set lifecycle-shared`: the same cells on the shared CPU path (`--shared-cpu`), BGRA and NV12 on both platforms.
3. `fl_read_leases --cycles 100`: held leases.
4. `--set shared-primary`: shared-CPU 1800 s rate cells.

Each lifecycle cell runs 100 cycles of 5 s at 1080p60 and 4K30, in two directions: restarting the producer, or restarting the consumer, while the other side is long-lived and measured.

**Gates** (frozen, applied by the runner):
- **Lifecycle:** every cycle delivers exactly 300 or 150 frames and 240 000 PCM samples, and reaches EOS. The long-lived peer's least-squares slopes over cycles 5–99 must be within 16 KB, 0.1 handles and 0.02 threads per cycle.
- **Rate:** 17 ms at 60 fps and 34 ms at 30 fps, with zero video and zero audio misses, and the run must complete.

## Summary

| Set | Linux | Android |
|---|---|---|
| Native lifecycle | 7 of 8 | **4 of 4** |
| Shared-CPU lifecycle | 6 of 8 | 7 of 8 |
| Held leases | pass (0.01 KB/cycle) | pass (−0.03 KB/cycle) |
| Shared-CPU 1800 s | **4 of 4** | **4 of 4** |

Handles and threads were flat (slope 0.000) in every lifecycle cell on both platforms. All four failures are at 4K30. No repeat replaces a failure.

## Lifecycle

Memory slope (KB/cycle) of the long-lived peer, cycles 5–99. "Steps" counts the cycles whose resident memory is more than 2 MB away from the cell's median. "Without the steps" is the slope over the remaining cycles. That column is a diagnostic, not a verdict.

| Platform | Path | Cell | Cycles | Slope | Steps (size) | Without the steps | Result |
|---|---|---|---|---|---|---|---|
| Linux | native | BGRA 1080p60, restart producer | 100/100 | 1.88 | | | pass |
| Linux | native | BGRA 1080p60, restart consumer | 100/100 | 2.25 | | | pass |
| Linux | native | NV12 1080p60, restart producer | 100/100 | 2.99 | | | pass |
| Linux | native | NV12 1080p60, restart consumer | 100/100 | 2.33 | 5 (up to 12 MB) | 2.30 | pass |
| Linux | native | BGRA 4K30, restart producer | 100/100 | 1.69 | | | pass |
| Linux | native | BGRA 4K30, restart consumer | 100/100 | **176.66** | 12 (63 MB), including cycles 94–99 | 1.80 | **FAIL** (memory) |
| Linux | native | NV12 4K30, restart producer | 100/100 | 1.81 | | | pass |
| Linux | native | NV12 4K30, restart consumer | 100/100 | −16.78 | 4 (36 MB) | 1.54 | pass |
| Linux | shared | BGRA 1080p60, restart producer | 100/100 | 12.05 | 2 low, at cycles 5–6 (8 MB) | 1.48 | pass |
| Linux | shared | BGRA 1080p60, restart consumer | 100/100 | −20.28 | 7 (16 MB) | 1.77 | pass |
| Linux | shared | NV12 1080p60, restart producer | 100/100 | −16.15 | 24 (6 MB) | 1.31 | pass |
| Linux | shared | NV12 1080p60, restart consumer | 100/100 | 1.67 | | | pass |
| Linux | shared | BGRA 4K30, restart producer | 100/100 | 1.75 | | | pass |
| Linux | shared | BGRA 4K30, restart consumer | 100/100 | −164.04 | 14 (63 MB) | 2.67 | pass |
| Linux | shared | NV12 4K30, restart producer | 100/100 | **25.32** | 3 low, at cycles 5–7 (12 MB) | 1.84 | **FAIL** (memory) |
| Linux | shared | NV12 4K30, restart consumer | 100/100 | **75.11** | 12 (48 MB) | 1.07 | **FAIL** (memory) |
| Android | native | RGBA 1080p60, restart producer | 100/100 | 2.53 | | | pass |
| Android | native | RGBA 1080p60, restart consumer | 100/100 | 2.83 | | | pass |
| Android | native | RGBA 4K30, restart producer | 100/100 | 2.28 | | | pass |
| Android | native | RGBA 4K30, restart consumer | 100/100 | 1.28 | | | pass |
| Android | shared | BGRA 1080p60, restart producer | 100/100 | 2.45 | | | pass |
| Android | shared | BGRA 1080p60, restart consumer | 100/100 | 0.10 | | | pass |
| Android | shared | NV12 1080p60, restart producer | 100/100 | 3.46 | | | pass |
| Android | shared | NV12 1080p60, restart consumer | 100/100 | −36.03 | 6 low, at cycles 94–99 (10 MB) | 1.40 | pass |
| Android | shared | BGRA 4K30, restart producer | 100/100 | 0.99 | | | pass |
| Android | shared | BGRA 4K30, restart consumer | 100/100 | −1.81 | | | pass |
| Android | shared | NV12 4K30, restart producer | **99/100** | −1.54 | | | **FAIL** (cycle 82) |
| Android | shared | NV12 4K30, restart consumer | 100/100 | 3.24 | | | pass |

### The Linux memory failures are storage-sized steps

All three Linux failures, and the large slopes that happen to pass, come from resident-memory steps of about the size of the frame storage. At 4K they are 63 MB for BGRA (two 33 MB frames) and 36–48 MB for NV12 (three to four 12 MB frames). Each step either appears and is released, or is a one-time change at the start of the window. Underneath, every Linux cell grows by 1.0–3.0 KB/cycle. Seven of the 16 Linux cells have no steps at all. A cell passes or fails depending on where in cycles 5–99 the steps fall:
- BGRA 4K30 restart-consumer fails because its last six cycles are all stepped up;
- shared BGRA 4K30 restart-consumer passes at −164 because its steps are early.

This is the same signature as the [NV12 4K30 failure at `051c0701`](../linux-android-051c070/README.md#the-failure-linux-nv12-4k30-restart-consumer), now seen in 9 of 16 Linux cells, in both directions and on both paths. Android shows one late drop of 10 MB and nothing else. What allocates and releases these blocks is not established: the producer when a consumer attaches, the consumer's output buffers, or the allocator returning memory late. The gate counts resident memory, and the cells fail as defined. Whether the gate should measure something else, or the steps be eliminated, is open.

### Android shared NV12 4K30, restart producer: a drain-report timeout

In cycle 82, the long-lived consumer received the full 150 frames and 240 000 samples, then EOS. Its resident memory was in line with the cycles around it. The fresh producer then failed: its sink's drain handshake (`fl_gst_session_finish_local_cancellable`, deadline = connect + idle timeouts, `gst/gstframelinkavsink.cpp:340`) did not get the consumer's drain report in time. Its process exited 0 with that error. So no media was lost, but the cycle did not end cleanly, and the cell fails the completeness gate. Why the report was late in one cycle of 100 is not established. Per-cycle logs on the device are removed by the runner, so only the two sides' results are kept.

## Held leases

`fl_read_leases --cycles 100` covers retained readers blocking reuse, out-of-order retirement, resize, and reconnect while a reader is outstanding. It passes on both platforms. Slopes over cycles 5–99: Linux 0.01 KB/cycle (handles 3, threads 1 throughout), Android −0.03 KB/cycle (handles 6, threads 1).

## Shared-CPU 1800 s

`shared-{size}-{format}`: `--shared-cpu --raw` plus the primary profile and the arrival budget. Arrival statistics are as reported by the bench.

| Platform | Cell | Video misses | Video arrival p50 / p99 / max (ms) | Audio misses | Audio arrival p99 / max (ms) |
|---|---|---|---|---|---|
| Linux | BGRA 1080p60 | 0/108 000 | 5.07 / 5.66 / 7.31 | 0/180 000 | 0.51 / 1.79 |
| Linux | NV12 1080p60 | 0/108 000 | 1.99 / 2.27 / 3.22 | 0/180 000 | 0.46 / 1.23 |
| Linux | BGRA 4K30 | 0/54 000 | 17.81 / 21.39 / 22.62 | 0/180 000 | 0.54 / 1.00 |
| Linux | NV12 4K30 | 0/54 000 | 8.05 / 8.76 / 11.88 | 0/180 000 | 0.49 / 1.01 |
| Android | BGRA 1080p60 | 0/108 000 | 8.05 / 10.33 / 14.38 | 0/180 000 | 2.71 / 6.65 |
| Android | NV12 1080p60 | 0/108 000 | 7.16 / 8.17 / 12.96 | 0/180 000 | 3.06 / 10.36 |
| Android | BGRA 4K30 | 0/54 000 | 21.78 / 23.81 / **33.03** | 0/180 000 | 3.32 / 11.67 |
| Android | NV12 4K30 | 0/54 000 | 11.77 / 14.06 / 16.50 | 0/180 000 | 3.16 / 7.58 |

All eight pass. Android BGRA 4K30 passes with a maximum of 33.03 ms, 0.97 ms under its budget; at an older revision it failed with 15 misses. Android BGRA is stored as RGBA and swizzled by the CPU map, which may explain why its arrival times are higher than NV12's; that is not measured here.

Before this campaign, a 10 s pilot of the Android NV12 1080p60 cell had one video miss at 18.4 ms; that pilot is not a qualification run, and the 1800 s cell here is a separate run, not a replacement.

## Open, for Jon

- **Linux lifecycle memory gate:** three 4K30 cells fail on storage-sized resident-memory steps, over 1–3 KB/cycle of underlying growth. Decide between finding and removing the steps, or a gate that separates them from steady growth. Any new gate would be a gate change.
- **Android drain-report timeout:** one cycle of 100 in the shared NV12 4K30 restart-producer cell. A diagnosis needs per-cycle logs kept on the device.
- **Android BGRA 4K30 shared-CPU** passes with 0.97 ms of headroom.

## Files

- `linux/` and `android/`: each set's `results.json` (named `<set>.json`) and log, and the held-lease log. Linux also has each cell's `.summary.txt`, with the long-lived peer's full per-cycle series (on Android the series is in the JSON).
- The raw Linux files (per-cycle fresh-peer results, and the 1800 s cells' per-frame and per-block CSVs) are 11 MB compressed and kept outside the repository: `linux-raw.tar.xz`, SHA-256 in `linux-raw.tar.xz.sha256`.
