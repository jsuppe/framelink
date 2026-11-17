# Option B gate run, cycles 100–199 (2026-11-17 00:59–01:17 UTC)

Criterion approved by Jon on 2026-11-16: 200 cycles per case, gated on the slope over cycles 100–199, with limits 16 KiB/cycle (memory), 0.1 (handles) and 0.02 (threads). The runner is `remote.py`, Evelyn's matched-100 runner with only the window, cycle count and timeouts changed. Binary pins are identical before and after, and match the matched-100 and extended-200 runs.

| Case | Memory KiB/cycle | Handles | Threads | Result |
|---|---:|---:|---:|---|
| restart-producer (BGRA1080p60 native, 5 s cycles) | 3.13 | −0.009 | −0.005 | pass |
| held leases (`fl_read_leases --cycles 200 --device-owner`) | 1.33 | 0 | 0 | pass |
| restart-consumer (from [extended-200](../extended-200/README.md)) | 2.97 | −0.011 | −0.005 | pass |

For reference, restart-producer over the original cycles 5–104 window is −1.58.
