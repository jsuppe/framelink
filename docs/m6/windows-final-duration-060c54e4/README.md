# Final-revision duration requalification — 6 pass, 1 FAILED

Revision `060c54e41bcbfe7ca8dbdef89e6e74fb2930935c`, run 2026-11-17 04:09–07:32 UTC on the Windows test host in [M6_WINDOWS_READINESS.md](../M6_WINDOWS_READINESS.md). Binaries are the ones built by the [final integrated run](../windows-integrated-final/README.md); the three DLL hashes were pinned against that run and all five binary hashes were identical before and after the batch. One job at a time under the validation mutex, no retries. Seven 5-second preflights passed before any 1800 s cell started.

## Results
| Cell (1800 s) | Result | Video / PCM | Deadline misses | Worst memory slope (KB/min, limit 64) |
|---|---|---|---|---|
| Shared CPU NV12 1080p60 | pass | 108000 / 86400000 | 0 | 7.33 |
| Shared CPU BGRA 1080p60 | pass | 108000 / 86400000 | 0 | 6.46 |
| Shared CPU NV12 4K30 | pass | 54000 / 86400000 | 0 | 7.96 |
| Shared CPU BGRA 4K30 | pass | 54000 / 86400000 | 0 | 7.62 |
| Native NV12 1080p60 | **FAILED** at about 1298.7 s | 77924 of 108000 submitted | run aborted | not evaluated |
| Native NV12 4K30 | pass | 54000 / 86400000 | 0 | 42.51 (source) |
| Native BGRA 4K30 | pass | 54000 / 86400000 | 0 | 44.33 (source) |

Handle and thread slopes are within ±0.05 per minute in every passing cell (limits 1 and 0.5). Native scheduled-to-final-arrival maximum: 9.94 ms (NV12 4K30) and 16.53 ms (BGRA 4K30) against the 34 ms budget.

## The failure: native NV12 1080p60
No retry and no clean-repeat waiver: this cell is failed on the final revision.

What the capture shows (tails in `cells/windows-native-nv12-1920x1080-60-1800/`):
- The test source's video thread reached `flAcquireBuffer` for frame 77922 **43.53 ms late** and for 77923 27.13 ms late. Both acquire calls then took under 1 µs. Frame 77921 was on time.
- The source then submitted 77922 and 77923 0.24 ms apart. With the default unread queue of 1, the relay received 77923 where it expected 77922 and stopped on its strict identity check (`relay.stderr.log`, fixture line 90). The source's next acquire then failed because the relay was gone (`source.stderr.log`, line 68).
- The source's PCM thread kept its 10 ms cadence through the gap (lateness under 0.01 ms), so the process and host were not suspended as a whole.
- Before the stall the receiver had 77902 complete rows with scheduled-to-arrival p50 4.18 ms, p99 5.37 ms, max 8.93 ms and no miss. Memory, handles and threads were flat in all three processes for the last several samples.
- Earlier in the same run the source woke late three other times (2.45, 6.36 and 2.61 ms), all inside the 17 ms budget. Neither 4K30 native cell shows any wake-up later than 1 ms.

This is the same signature as the [BGRA 1080p60 failure on `a50841a`](../windows-native-final-duration-bgra-1080-failure-a50841a/README.md) (323 ms late wake-up, catch-up burst, queue-1 replacement), which predates the device-owner code. The [stall diagnostic](../windows-native-stall-diagnostic-a50841a/README.md) reproduced the replacement mechanism but not the cause of the delay.

**Not established:** where the 43 ms went. The delay sits between the previous frame's CSV row and the return of the fixture's pacing sleep, outside any FrameLink call. Candidates remain fixture file I/O, the sleep, and thread scheduling.

**Host correlation (suggestive only):** the only Defender scan start in the batch window was logged at 06:32:14 UTC, with Defender status updates until 06:32:18; the stall happened at about 06:32:29. Hourly Defender health reports at 04:32, 05:32 and 07:32 coincided with passing cells. See `host-events-0400-0740utc.txt`.

## What this means
- Shared-CPU duration is now measured on the final revision: 4/4 pass.
- Native 4K30 duration (NV12 and BGRA) is measured on the final revision: 2/2 pass.
- Native 1080p60 has now failed its 1800 s run in two of three final-candidate attempts (BGRA on `a50841a`, NV12 here; NV12 passed on `a50841a`). Whether native NV12 1080p60 stays advertised is Jon's decision.

## Contents
- `runner/`: the batch runner, the analyzers it used and the DLL pins.
- `status.json`, `remote-sha256.json`: batch status and hashes of every file produced on the host.
- `cells/<label>/` and `cells/<label>.meta/`: command, logs, summaries and analysis per cell. The large per-frame CSVs are kept outside the repository; their hashes are in `remote-sha256.json`.
