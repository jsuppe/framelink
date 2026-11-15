# Option B retained-device owner — first matched 100-cycle result (2026-11-13)

**Result: NOT QUALIFIED.** The restart-producer case passes, and the restart-consumer case fails the original 16 KiB/cycle private-commit limit.

The run is the exact source of this commit's code changes: every file in `overlay.tar` was byte-compared with the committed tree (sha256 in `preflight/overlay.tar.sha256`). Host <win-host>, RTX 5090. Binaries are pinned in `matched-100/binary-pins.json`. The raw run files match `matched-100/remote-sha256.json`. The preflight logs were converted from UTF-16 to UTF-8 for review, so their hashes are not covered.

## Preflight (2026-11-13 01:48–01:50 UTC)
- The first build exited with code 1 (`preflight/status.json`). It was followed by include and lifecycle-propagation fixes, both of which built and passed.
- `fl_device_owner_win`: retained identity, fresh channels and pixels, concurrent BUSY, snapshot retirement, cancellation rollback and caller release all passed.
- `fl_read_leases` (6 cycles): retained readers block reuse; ordered retirement, resize and reconnect pass.

## Matched 100 cycles (11:48–12:06 UTC)
Command: `fl_gst_av_bench --device-owner --native --cycles 100 --seconds 5`, BGRA 1920x1080p60, pool 2, video queue 1, latency 0, transport-only. The limits are the original ones (memory 16 KiB/cycle, handles 0.1, threads 0.02, warmup 5).

| Restarted role (long-lived role measured) | Fresh-device baseline | Retained device owner | Result |
|---|---:|---:|---|
| producer (long-lived consumer) | 21.10 | **−0.65** | pass |
| consumer (long-lived producer) | 84.09 | **24.93** | **FAIL** |

- Handle and thread slopes are flat or negative in both cases, and all 100 cycles delivered exact media counts.
- In the failing case the long-lived producer's private commit rises from 86100 to 88016 KiB. Most cycles step +36 KiB, and cycles 89–99 are nearly flat.
- The owner removes most of the producer-side growth, but the remainder is still above the limit. Attribution is a separate diagnostic.
- Nothing has been waived. Option B still requires held-lease, fault, compatibility and final changed-runtime qualification.
