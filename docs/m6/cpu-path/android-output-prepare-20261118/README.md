# M6.8c: prepared CPU output buffers, Android 4K30 rerun — 2026-11-18

Base revision: `61510c22` plus this change. TCL 8483A (MT6768), Android 15. `fl_gst_av_bench` SHA-256 prefix `3bbf414418731570`. This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Why

In the [first sustained 4K30 run](../android-cpu-receiver-20261118/README.md), the CPU receiver's first three frames arrived 68–89 ms late and the frame at 0.1 s was dropped (ring full). A 60 s control repeated this. The receiver's two reused output buffers were created and first written while frames 0 and 1 were being delivered, so those frames paid the first-write page faults: about 8,100 pages per 4K RGBA frame.

## Change

`framelinksrc` start (`srcPrepareCpuOutput`, Linux and Android ring readback only) does the following before the video track reports ready:
- Creates the bounded output cache.
- Writes zeros through both of its buffers.
- Returns them to the cache.

This follows the precedent of the [shared-CPU producer startup preparation](../shared-cpu-prefault/README.md).

The cost is bounded: two frames' worth of memory writes, once per start. A later ring reallocation still creates the cache fresh. The pool depth (2), queue settings, native receiver, shared-CPU path and Windows path are unchanged. The step is traced as `gst.cpu_output_prepare`.

## Results (same profile as the first run: pool 2, unread 1, output 1, latency 0, transport-only, RGBA 4K30, CPU-copying producer)

| Run | Complete | Frames | First 3 frames (ms) | Arrival p50 / p99 / max (ms) | Over 34 ms | Ring-full drops | CPU producer / consumer |
|---|---|---|---|---|---|---|---|
| Before, 1800 s | no | 53996/54000 | 68.2, 79.0, 88.6 | 26.62 / 32.17 / 88.64 | 283 | 4 | 83.5% / 59.8% |
| Before, 60 s | no | 1799/1800 | (max 88.1) | 26.56 / 31.87 / 88.11 | 5 | 1 | 82.5% / 58.8% |
| **After, 1800 s** | **yes** | 54000/54000 | 30.7, 42.4, 32.4 | 25.77 / 29.67 / 49.46 | 83 | 0 | 81.5% / 58.3% |
| After, 60 s | yes | 1800/1800 | 32.6, 41.4, 32.0 | 26.43 / 32.06 / 41.35 | 10 | 0 | 81.0% / 58.8% |
| After, 60 s | yes | 1800/1800 | 30.6, 41.1, 31.7 | 26.50 / 31.66 / 41.13 | 5 | 0 | 82.6% / 59.5% |

In the after 1800 s run:
- Audio was contiguous with no misses, EOS was reached and queue accounting was complete.
- RSS slopes were negative (consumer −57, producer −80 KB/min).
- Steady state (after 2 s): p99 29.65 ms, p99.9 35.82 ms, max 49.46 ms.
- The 83 late frames fall in 42 clusters. Five of them have 5–7 frames, at 62, 69, 81, 155 and 1775 s. Per 5 minutes: 37, 8, 9, 10, 5, 14.

## Findings

- **The startup drop is gone in all three runs**, and the worst startup arrival halves (about 42 ms against 89 ms). The first frames are still over budget. Preparation moves the page faults, not the first frame's other setup.
- **The 1800 s 4K30 cell now completes** with every frame and no drops.
- **83 frames are still over the 34 ms diagnostic.** The steady p99 has about 4 ms of margin.
- **The steady-state improvement is not attributed.** The first run's unattributed burst at 79–103 s did not recur here; preparation acts only at startup, so the difference is likely run variance.
- The CPU cost is unchanged. The receiver's copy and the producer's contention (see the first record) remain.

## Regressions

| Platform | Result |
|---|---|
| Linux (Ubuntu 26.04, RTX 3090, GStreamer 1.28.2), full suite `ctest -j4` | 142/142 (including the known skip, `read_lease_egl_delayed_completion`) |
| Android tablet, all 96 CTest-registered Android tests run on the device | 95 pass, 1 skipped (`read_lease_egl_delayed_completion`). `gst_av_bench_smoke` passes with an on-device `--out .`; the runner passes the host build path ([M6.9 record](../../android-native-av-session/README.md)) |
| Windows | Not run on the qualification host. The change is excluded from Windows builds (`#ifndef _WIN32`); CI builds and tests Windows |

## Not established here

- Repeatability of the 1800 s 4K30 pass: one run only.
- Linux sustained cells for the CPU receiver.
- Cause of the remaining scattered misses (thread placement, bandwidth contention or system activity were not captured).

Raw CSVs are retained outside the repository; their SHA-256 hashes are in `raw-sha256.json`.
