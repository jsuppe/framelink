# Extended 200-cycle restart-consumer (2026-11-17 00:32–00:50 UTC)

This run is a diagnostic follow-up to [owner attribution](../../owner-attribution-20261115/README.md). It uses the same binaries as matched-100 (pins identical before and after) and the same command except `--cycles 200`. The gate math (warmup 5, limits 16 / 0.1 / 0.02) is unchanged, and per-window memory slopes were added. `remote.py` is the runner. 200/200 cycles completed with exact media counts.

| Window (after warmup) | Memory KiB/cycle |
|---|---:|
| cycles 5–104 (same window as matched-100) | 22.13 |
| cycles 50–149 | 11.34 |
| cycles 100–199 | **2.97** |
| cycles 5–199 (all) | 11.63 |

Memory over 20-cycle windows: +20.6, +23.7, +29.6, +11.0 over cycles 20–99, then 7.9, 1.0, 2.8, 2.7, 1.9 over cycles 100–199. Total rise is about 2 MiB (86348 → 88376 KiB). Handles fall 1634 → 1614 and threads 36 → 29.

**Reading:**
- The real pipeline levels off like the D3D11 probe. The first-100-cycle window reproduces the 11-13 failure (22.1 against 24.9) and measures the driver copy-pool warm-up, while cycles 100–199 are well inside the limit.
- A small residual slope (about 2–3 KiB/cycle) remains in cycles 100–199. This run can't show whether it is zero over longer horizons.
- The 200-cycle whole-run fit falls under 16, but that's a different window, not the frozen matched-100 criterion. **The matched-100 result is still recorded as FAIL**, and accepting a warm-up or window change is Jon's decision.
