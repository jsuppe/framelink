# Linux native relay rate cells (M6.9d)

The paced authenticated FAR4 → FAR5 relay harness, run as three processes: source, relay, final reader. Video is autonomous at 30/60 fps and PCM at 100 Hz. Two slots, one unread frame, the same profile and analyzer (`tools/check_local_native_bench.py`) as the Windows cells. Each cell follows the rate plan's order (`tools/run_local_native_rate_cells.py`): a 5 s trace-off preflight, then a matched 5 s trace-on run, then 1800 s trace-off. Host: Linux, NVIDIA GeForce RTX 3090, driver 580.178.04, GStreamer 1.28.2. Budgets are the whole-path arrival diagnostics, 17 ms at 60 fps and 34 ms at 30 fps, with **zero** misses and zero lost frames.

The Linux harness uploads through the mapped DMA-BUF. It verifies through a Vulkan GPU copy into host-cached memory (`framelink_vulkan.h`), whose real SYNC_FD completion it hands to FrameLink: the counterpart of the Windows staging copy and shared fence. CPU reads of the GPU mapping are uncached (about 9 MB/s measured), so a CPU verification read could not keep up.

## Final revision `b5c942a` (clean tree)

| Cell | Preflight | Trace on | 1800 s | Arrival p50 / p99 / max (ms) | Relay forward p99 | Resource slopes (max over roles) |
|---|---|---|---|---|---|---|
| NV12 1080p60 | pass | pass | **pass** | 3.84 / 5.70 / 6.75 | 0.050 ms | memory 0.03 KB/min, fds 0.002/min, threads 0 |
| NV12 4K30 | pass | pass | **pass** | 7.36 / 9.30 / 14.47 | 0.034 ms | memory 1.04 KB/min, fds 0.001/min, threads 0 |
| BGRA 1080p60 | pass | pass | **FAIL** | (frame 2337 at 39 s replaced: 19.1 ms between receiver acquisitions) | | |
| BGRA 4K30 | pass | pass | **FAIL** | (frame 3732 at 124 s replaced: 36.7 ms between receiver acquisitions) | | |

The resource gates (memory ≤ 64 KB/min, handles ≤ 1/min, threads ≤ 0.5/min, on the steady second half) pass by wide margins in both passing cells. CPU per role at 1080p60 NV12: source 26 %, relay 7 %, receiver 44 %. The relay's forward call is 0.01–0.06 ms at p99 in every cell.

**Both failures are a single stall.** In each failing run, exactly one gap between the receiver's acquisitions exceeds one frame interval, so the one-frame queue replaced a frame. The zero-loss rule then fails the cell. Before the stall every arrival was within budget (BGRA 1080p60 max under 11 ms), and no cell missed an arrival deadline. NV12 never stalled in either revision. BGRA, with 2.7 times NV12's upload size, stalled in both BGRA cells at `b5c942a`, and in BGRA 1080p60 at the first revision too.

## First revision `b945c5f` (clean tree; superseded)

| Cell | Preflight | Trace on | 1800 s |
|---|---|---|---|
| BGRA 1080p60 | pass | pass | FAIL: content (frame 2269) |
| NV12 1080p60 | pass | pass | pass (p99 5.85 ms; slopes ≤ 0.18 KB/min) |
| BGRA 4K30 | pass | pass | pass (p99 16.97 ms, max 33.42 ms; slopes ≤ 0.57 KB/min) |
| NV12 4K30 | FAIL: frame replaced | pass | not run |

**The content failure led to a fix in the harness.** With `FL_BENCH_DIAG=1` the receiver settles its copy while still holding the lease, and on a mismatch reads the same held storage on the CPU:
- The CPU read showed the correct frame.
- Copies through the long-lived Vulkan import read **zeros**, and kept doing so.
- A fresh import of the same storage read the frame correctly.

So on NVIDIA, in this three-process graph, a Vulkan import of a linear DMA-BUF went stale after about 30–38 s, at BGRA 1080p60 only. A single-process reproduction with CPU writes every frame did not go stale in 90 s. `b5c942a` imports afresh for every verification copy; the cost is not measurable. No content mismatch occurred at `b5c942a`.

The stale imports and the stalls cluster in the same 30–40 s window at BGRA 1080p60, which suggests one driver-side memory event behind both. Its cause is not established.

## Open, for Jon

- **BGRA 1080p60 and BGRA 4K30 are not qualified on Linux.** Each fails zero-loss on one isolated stall of 19–37 ms in 1800 s. This parallels the Windows BGRA 1080p60 duration failure, which is supported by decision under a host-scheduling assumption. No cause is established here, and no repeat replaces either failure.
- **`framelink_vulkan.h` guidance.** The header says to import each slot once per channel generation. The stale import above contradicts that on NVIDIA for linear (`FL_MAP_CPU`) rings in this graph. Until the trigger is understood, the guidance should carry this limit, or recommend re-importing such rings.

## Files

- `cells-<rev>/summary.json`, `environment.json` (revision, binary and plugin SHA-256), `<cell>-<arm>.analysis.json` (full analyzer output), and per capture `config.json`, the role summaries and any non-empty stderr.
- The raw captures (per-role video, audio and resource CSVs, and the trace-on traces) are 39 MB compressed, kept outside the repository: `captures.tar.xz`, SHA-256 in `captures.tar.xz.sha256`.
