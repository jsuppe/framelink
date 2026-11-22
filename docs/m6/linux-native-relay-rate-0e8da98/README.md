# Linux native relay rate cells with pre-roll (M6.9d)

The same harness, host, cells and order as the [`b5c942a` record](../linux-native-relay-rate-b5c942a/README.md), at revision `0e8da988` (clean tree). Two things changed since then, the same on every platform:
- **The pre-roll** in the rate plan's frozen requirements ([amendment](../WINDOWS_NATIVE_RELAY_RATE_PLAN.md#frozen-observable-requirements)). Before the measured stream, the source publishes `8 + fps/2` marked frames: 8 at 100 ms spacing that cycle through every pool slot, then half a second at the stream rate, straight into the epoch. The relay forwards every one, END and EOS count them, and the analyzer checks their trace counts separately.
- **The receiver's payload check** is an exact vectorized comparison of the same bytes, instead of a per-byte check.

Host: Linux 7.0, NVIDIA GeForce RTX 3090, driver 580.178.04, GStreamer 1.28.2. Budgets: 17 ms at 60 fps and 34 ms at 30 fps, with **zero** misses and zero lost frames, video and audio. No repeat replaces a failure.

## Revision `0e8da988` (clean tree)

| Cell | Preflight | Trace on | 1800 s | Video arrival p50 / p99 / max (ms) | Audio arrival p99 / max (ms) | Relay forward p99 | Resource slopes (max over roles) |
|---|---|---|---|---|---|---|---|
| BGRA 1080p60 | pass | pass | **pass** | 6.17 / 8.18 / 9.08 | 1.57 / 3.95 | 0.06 ms | memory 0, fds 0.0006/min, threads 0 |
| NV12 1080p60 | pass | pass | **pass** | 3.92 / 5.82 / 6.59 | 1.71 / 2.99 | 0.07 ms | memory 0, fds 0.004/min, threads 0 |
| BGRA 4K30 | pass | pass | **pass** | 15.24 / 17.89 / 20.22 | 1.08 / 2.57 | 0.04 ms | memory 0, fds 0.0006/min, threads 0 |
| NV12 4K30 | pass | pass | **pass** | 8.08 / 10.18 / 10.54 | 1.24 / 3.39 | 0.07 ms | memory 0, fds 0, threads 0 |

**All four cells pass**, including both BGRA cells that failed at `b5c942a`. That revision's two failures were single stalls of 19 and 37 ms between receiver acquisitions, at 39 s and 124 s, well after any startup effect. So the pre-roll is not what changed them. The receiver's check is the likely difference: it now costs far less CPU, so the receiver has more slack per frame. But one passing run per cell does not show that the stall is gone, and its cause is still not established.

CPU per role over the 1800 s runs:

| Cell | Source | Relay | Receiver |
|---|---|---|---|
| BGRA 1080p60 | 41 % | 7 % | 31 % |
| NV12 1080p60 | 27 % | 8 % | 23 % |
| BGRA 4K30 | 52 % | 2 % | 17 % |
| NV12 4K30 | 32 % | 7 % | 23 % |

At `b5c942a` the NV12 1080p60 receiver used 44 % CPU; it now uses 23 %.

Upload and verification per frame, p50 / p99 / max (the verification is the Vulkan copy plus the check):

| Cell | Source upload | Receiver verification |
|---|---|---|
| BGRA 1080p60 | 3.1 / 3.2 / 3.5 ms | 5.8 / 6.5 / 9.0 ms |
| NV12 1080p60 | 1.3 / 1.3 / 1.5 ms | 4.6 / 5.1 / 7.3 ms |
| BGRA 4K30 | 11.8 / 12.3 / 13.3 ms | 12.2 / 14.5 / 16.7 ms |
| NV12 4K30 | 4.6 / 4.7 / 5.4 ms | 7.2 / 8.0 / 10.3 ms |

No content mismatch occurred. The harness still imports afresh for every verification copy, as at `b5c942a`.

## Open, for Jon

- **The `b5c942a` BGRA failures stand as recorded.** These passes qualify the new revision, not the old one.
- **`framelink_vulkan.h` guidance** (stale long-lived imports of linear rings on NVIDIA) is still open, as recorded at `b5c942a`.

## Files

- `cells-0e8da98/summary.json`, `environment.json` (revision, binary and plugin SHA-256, driver, GStreamer), `<cell>-<arm>.analysis.json` (full analyzer output), and per capture `config.json` (with `preroll`) and the role summaries. No stderr was written.
- The raw captures (per-role video, audio and resource CSVs, and the trace-on traces) are 43 MB compressed and kept outside the repository: `captures.tar.xz`, SHA-256 in `captures.tar.xz.sha256`.
