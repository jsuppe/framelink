# Option B lifecycle: advertised native configurations (2026-11-17 01:42–03:32 UTC)

Jon's 2026-11-16 criterion: 200 × 5 s cycles per case, gated on cycles 100–199, with limits 16 KiB/cycle (memory), 0.1 (handles) and 0.02 (threads). The binaries are the same as matched-100, extended-200 and gate-200, with pins identical before and after. Each case had a 2-cycle smoke check first. All cases delivered exact media counts on every cycle. The runner is `remote.py`, and the settings match the shared-CPU lifecycle cells (pool 2, video queue 1, latency 0, arrival budget 17 ms at 60 fps and 34 ms at 30 fps).

| Case | Memory KiB/cycle | Handles | Threads | Original window 5–104 (reference) | Result |
|---|---:|---:|---:|---:|---|
| nv12-1080p60-restart-producer | 3.85 | -0.012 | -0.006 | 0.22 | pass |
| nv12-1080p60-restart-consumer | 7.07 | -0.012 | -0.006 | 17.35 | pass |
| nv12-4k30-restart-producer | 3.70 | -0.011 | -0.006 | -0.71 | pass |
| nv12-4k30-restart-consumer | 11.24 | -0.011 | -0.005 | 8.02 | pass |
| bgra-4k30-restart-producer | 3.16 | -0.011 | -0.005 | -1.00 | pass |
| bgra-4k30-restart-consumer | 13.97 | -0.014 | -0.007 | 12.58 | pass |

All six pass. Restart-consumer cases sit closest to the limit (BGRA 4K30: 13.97). Those are the CPU-written persistent-producer cases with the copy-pool warm-up described in [owner attribution](../../owner-attribution-20261115/README.md).
