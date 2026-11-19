# M6.8c: Android CPU receiver, sustained — 2026-11-18

Runtime revision `61510c22` (framelink `master`, after the [cached-ring](../cpu-consumer-20261118/README.md) and [output-reuse](../cpu-output-reuse-20261118/README.md) changes). TCL 8483A (MT6768), Android 15, arm64 build. `fl_gst_av_bench` SHA-256 prefix `5c2265e3c5fae0a8`. This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## What was run

These runs use the same profile as the earlier Android native cells ([1080p60](../../zero-delay-platform-controls/android-1080p60-current/README.md), [4K30](../../zero-delay-platform-controls/android-4k30-soak/)), except that the receiver is the CPU receiver (`memory:SystemMemory`; no `--native`):
- RGBA, bin mode, pool 2, unread queue 1, output queue 1, latency 0, transport-only, no output preroll.
- Raw capture on, trace off.
- Arrival diagnostic 17 ms at 1080p60 and 34 ms at 4K30.

The producer is the bench's CPU-copying test producer. Producer and consumer are separate processes. Nothing else was running for qualification. The tablet was on USB power, battery 74–76%, battery temperature 24–29 °C. No thermal or system-load capture was made during the runs.

```
fl_gst_av_bench x --seconds 1800 --fps 60 --size 1920x1080 --arrival-budget-ms 17 \
  --pool 2 --video-queue 1 --output-queue-buffers 1 --latency-ms 0 --transport-only \
  --no-output-preroll --raw --format RGBA --out . --label android-1080p60-cpu1800
```

The 4K30 run uses `--fps 30 --size 3840x2160 --arrival-budget-ms 34`. The 60 s controls use the same arguments with `--seconds 60`, and `--native` for the native controls.

## Results

| Cell | Complete | Frames | Video arrival p50 / p95 / p99 / max (ms) | Over budget | Ring-full drops | CPU producer / consumer |
|---|---|---|---|---|---|---|
| 1080p60 CPU receiver, 1800 s | **yes** | 108000/108000 | 10.01 / 12.70 / 14.12 / 23.89 | 21 / 108000 | 0 | 54.3% / 37.3% |
| 4K30 CPU receiver, 1800 s | **no** | 53996/54000 | 26.62 / 29.33 / 32.17 / 88.64 | 283 / 53996 | 4 | 83.5% / 59.8% |
| 1080p60 CPU receiver, 60 s | yes | 3600/3600 | 9.99 / 12.60 / 14.00 / 25.16 | 2 / 3600 | 0 | 53.9% / 36.9% |
| 1080p60 native, 60 s (control) | yes | 3600/3600 | 6.13 / 9.27 / 10.51 / 12.88 | 0 | 0 | 53.0% / 14.6% |
| 4K30 CPU receiver, 60 s | no | 1799/1800 | 26.56 / 29.26 / 31.87 / 88.11 | 5 / 1799 | 1 | 82.5% / 58.8% |
| 4K30 native, 60 s (control) | yes | 1800/1800 | 11.34 / 13.08 / 15.52 / 19.92 | 0 | 0 | 65.7% / 15.7% |

Earlier native 1800 s cells, at older revisions, for reference: 1080p60 p50 6.19 / max 10.81 ms, producer 55.5%; 4K30 p50 10.70 / max 23.48 ms, producer 58.1%. Neither had a miss or a drop.

**In both 1800 s runs:**
- Audio was contiguous (180,000 blocks) with no audio arrival miss.
- EOS was reached and queue accounting was complete.
- Consumer RSS, handle and thread slopes were zero.
- The 1080p producer's RSS rose by 128 KB over 30 minutes (73,168 to 73,296 KB; slope 6.33 KB/min). This is recorded, not interpreted.

### Where the 4K30 misses and drops are (`analysis.txt`)

- **Startup.** The first three frames arrived 68–89 ms late, and the frame at 0.1 s was dropped (ring full). The 60 s control repeated this: 1 startup drop and the same first-frame maximum.
- **79–103 s.** A burst: 163 late frames in 7 clusters of 5 or more, up to 87 ms, including the other 3 drops. The largest cluster is 97 frames, from 93.9 to 98.2 s. It is unattributed, because system load was not captured.
- **Rest of the run.** 19 frames in three clusters of 5 to 8 (121, 189 and 744 s), and 98 frames in 49 clusters of fewer than 5.
- **Steady state** (after the first 2 s): p99 32.2 ms and p99.9 44.4 ms, against a 34 ms budget.

At 1080p60 the 21 misses are spread out: 20 clusters, at most 2 frames each, steady max 20.3 ms.

## Findings

- **1080p60 CPU receiver: transport gate passes.** The run completed with every frame and sample, no drops and flat resources. It is not equal to the native receiver: arrival is about 3.8 ms later at p50, there are 21 diagnostic misses (native: 0), and it uses about 23 more percentage points of consumer CPU.
- **4K30 CPU receiver: fails on this device.**
  - Frames are lost: 4 ring-full drops, one of them at startup in both runs.
  - Steady arrival has about 2 ms of margin at p99, so this is not only a startup defect.
- **The cost is not only the receiver's copy.** With the CPU receiver, the producer's CPU rises from 65.7% to 82.5% at 4K30 on the same build, while its own work is unchanged. That points to contention for memory bandwidth or cores, which is not investigated further here. The producer's CPU in the native control also rose against the older native cell (58% to 66%), so the cached `CPU_READ_OFTEN` hint may cost CPU on the write side. That is not isolated here.

## Not established here

- A real GPU producer (M6.9 9d) instead of the CPU-copying test producer. The arrival times here include the producer's own 4K copy.
- Whether pre-touched output buffers at caps negotiation would remove the 4K startup drop, or whether a third pool buffer would absorb it. The pool depth is part of the zero-delay profile and is not changed here.
- Where the threads run (big.LITTLE placement) and the bandwidth contention at 4K.
- A comparison with Android BufferQueue (`AImageReader`/`ANativeWindow`) on the same device. None exists in this repository.

Raw CSVs (65 MB) are retained outside the repository; their SHA-256 hashes are in `raw-sha256.json`. Summaries for all six runs are alongside this file.
