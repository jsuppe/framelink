# M6.8b: CPU output reuse for ring readback on Linux and Android — 2026-11-18

Base revision: `0f4715a1` (framelink `master`, after the [CPU-consumer record](../cpu-consumer-20261118/README.md)). This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Where this started

After the cached-ring change, the Android readback receiver (the M6.9 local native profile's `CPU_READ`, `COPY|READBACK` receiver) still took 20.6 ms at 1080p and 43 ms at 4K, push to pull. The budgets are 16.7 ms (1080p60) and 33.3 ms (4K30). A trace put about 13 ms in the copy step against 7.3 ms in a probe that copied into a freshly mapped output buffer, and 2.05 ms in a probe whose output was already mapped.

## Cause

`framelinksrc` reads a ring into a new `gst_buffer_new_allocate` buffer for every frame on Linux and Android. Windows already used the element's bounded output cache (`srcCpuOutputBuffer`: at most two buffers, never waits; a fresh buffer is used only when downstream still holds both). The shared-CPU path already used it on all platforms.

A fresh buffer is not touched until the copy writes it, so every page faults during the copy. At 1080p RGBA that is about 2,000 4 KiB pages per frame. The allocation trace shows 0.2 ms because the cost lands inside the copy.

## Change

- The ring-readback path uses `srcCpuOutputBuffer` on every platform. That covers the direct copy, the NV12 plane copy and the Linux EGL staging destination.
- On ring reallocation, Linux and Android drop the cache, as Windows drops its staging and cache. A size change already dropped it.
- The RGBA copy gains a `gst.cpu_copy_source_duration` trace scope, which the NV12 copy already had.

Nothing else changes. Buffers come back to the cache only when downstream releases them, so a reused buffer is never written while downstream holds it. Every byte of a reused buffer is rewritten: the full copy for RGBA/BGRA, and `memset` plus the planes for NV12. The Windows code path is unchanged.

## Results (push to pull, one frame at a time; a stage cost, not paced throughput)

### Android: TCL 8483A (MT6768), Android 15, RGBA, 120 frames per run, before/after/after/before

| Receiver | 1920x1080 p50 / p99 (ms) | 3840x2160 p50 / p99 (ms) |
|---|---|---|
| Readback, before (`0f4715a1`) | 21.6 / 26.2; 21.0 / 26.3 | 49.7 / 59.0; 48.8 / 69.5 |
| Readback, after | 5.16 / 8.89; 4.89 / 7.46 | 11.3 / 17.5; 11.1 / 17.5 |
| Readback, after (two more runs) | 4.92 / 7.68; 5.03 / 9.27 | 11.4 / 19.3; 11.2 / 14.7 |
| Native receiver, after | 1.76 / 3.90 | 2.89 / 4.35 |

The 4K "before" here (49 ms) is higher than the 43 ms in the previous record. Both are the same code, so this is run-to-run spread on this device.

The first frame (17–64 ms) and the maximum (15–39 ms) include the first two output buffers, which are fresh. They are reported and not discarded.

A 1080p trace after the change, 40 frames, p50 (`raw/android-1080p-trace-summary.txt`):

| Step | Before | After |
|---|---|---|
| `gst.srcCreate` | 21.8 ms | 6.07 ms |
| `gst.cpu_copy_source_duration` | not traced (about 13 ms, by remainder) | 3.59 ms |
| `flAcquireFrame` | 8.64 ms | 2.18 ms |

The acquire wait also dropped. This bench pushes one frame and then pulls it, so the acquire wait overlaps work on the producer side, which runs in the same process. It is not investigated here. The copy is still about 1.5 ms above the warm-output probe (2.05 ms). Thread placement on this big.LITTLE SoC is the remaining candidate and is not investigated.

### Linux: RTX 3090, NVIDIA 580.178.04, BGRA, before/after/after/before

| Receiver | 1920x1080 p50 / p99 (ms) | 3840x2160 p50 / p99 (ms) |
|---|---|---|
| GPU-staged (`gpu-readback=true`), before | 3.09 / 4.89; 3.09 / 4.73 | 11.7 / 12.7; 11.4 / 12.5 |
| GPU-staged, after | 3.09 / 5.45; 3.05 / 3.72 | 5.48 / 6.09; 5.49 / 6.21 |
| Direct readback, 30 frames, before / after | 674 / 698; 705 / 752 | not run (no frame within 1 s in the previous record) |

The direct path is still bound by reads from video memory, so output reuse does not help it.

## Regressions

| Platform | Result |
|---|---|
| Linux (Ubuntu 26.04, RTX 3090, GStreamer 1.28.2), full suite `ctest -j4` | Two of three runs: 142/142 (including the known skip, `read_lease_egl_delayed_completion`). The first run had one failure, described below |
| Android tablet, all 96 CTest-registered Android tests run on the device | 95 pass, 1 skipped (`read_lease_egl_delayed_completion`). `gst_av_bench_smoke` passes with an on-device `--out .`; the runner passes the host build path ([M6.9 record](../../android-native-av-session/README.md)) |
| Android `fl_gst_native_av_android` on its own | Pass, including 8 frames through the readback receiver with every pixel checked against a per-frame positional pattern, so stale data in a reused buffer would fail |
| Windows | Not run on the qualification host. The Windows code path is unchanged: `srcCpuOutputBuffer` was already used there, and the other edits are in Linux/Android branches. CI builds and tests Windows |

**One unexplained failure, retained.** The first full Linux `-j4` run failed `m5b_gst_av_example` in 1 of 23 cases (`av-rgba-consumer-first`, bin producer to element consumer). The consumer reported `ring sequence 31 after 29 with a receive queue of 2`: it fell behind real-time pacing. The producer's audio was then refused because the session was cancelled. It did not recur afterwards:
- 6 serial runs of that test on each build (this change and `0f4715a1`).
- Two more full `-j4` runs on each build, alternating, all 142/142.

The failure output is in `raw/linux-j4-m5b-example-failure.txt`. This change makes the consumer's per-frame work smaller, not larger. That does not show that it is unrelated.

## Not established here

- **8c rate qualification.** All figures are stage costs from a one-frame-at-a-time bench, not paced 1800 s A/V runs.
- Downstream that holds both cached buffers falls back to a fresh buffer per frame and pays the page-fault cost again. This bench releases each buffer before the next pull.
- Real GPU producers writing cached Android rings (carried over from the previous record).
