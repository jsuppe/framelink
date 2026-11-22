# Android native relay rate cells with pre-roll (M6.9d)

The same harness, device, cells and order as the [first Android record](../android-native-relay-rate-a50f655/README.md), at revision `0e8da988` (clean tree). The change since then is the cross-platform **pre-roll** in the rate plan's frozen requirements ([amendment](../WINDOWS_NATIVE_RELAY_RATE_PLAN.md#frozen-observable-requirements)):
- Before the measured stream, the source publishes `8 + fps/2` marked frames (23 at 30 fps, 38 at 60 fps): 8 at 100 ms spacing that cycle through every pool slot, then half a second at the stream rate, straight into the epoch.
- The relay forwards every pre-roll frame, and END and EOS count them. The reader must have read every pool slot before measured frame 0.
- Pre-roll frames are not in the CSVs. The analyzer checks their trace counts separately.

The receiver's payload check is now an exact vectorized comparison, checking the same bytes as before. The per-byte check had cost about a frame interval per 1080p RGBA frame on the tablet.

Device: Android 15 (API 35), TCL 8483A, MediaTek MT8786V/N, ARM Mali-G52 MC2 (OpenGL ES 3.2, r49p1), GStreamer 1.24.13 linked statically. Budgets: 17 ms at 60 fps and 34 ms at 30 fps, with **zero** misses and zero lost frames, video and audio. No repeat replaces a failure.

## Revision `0e8da988` (clean tree)

| Cell | Preflight | Trace on | 1800 s | Video arrival p50 / p99 / max (ms) | Audio arrival p99 / max (ms) | Relay forward p99 | Resource slopes (max over roles) |
|---|---|---|---|---|---|---|---|
| RGBA 1080p30 | pass | pass | **pass** | 8.66 / 11.75 / 17.32 | 6.42 / 17.15 | 0.54 ms | memory 0.33 KB/min, fds 0.002/min, threads 0 |
| NV12 1080p30 | pass | pass | **pass** | 9.82 / 13.24 / 17.02 | 6.69 / 17.71 | 0.52 ms | memory 0.66 KB/min, fds 0.002/min, threads 0 |
| RGBA 1080p60 | pass | pass | **FAIL** (one audio block, 17.80 ms) | 7.78 / 10.52 / 16.02 | 6.31 / **17.80** | | |
| NV12 1080p60 | pass | pass | **pass** | 8.72 / 11.03 / 15.87 | 6.35 / 15.34 | 0.54 ms | memory 0.74 KB/min, fds 0.003/min, threads 0 |

The RGBA 1080p60 row is computed from the raw captures. The analyzer stops at the first failed requirement, so its file has only the error. Rerunning the analyzer on the raw capture with only the arrival budget lifted, every other requirement passes: 108 000 frames and 180 000 PCM blocks, exact identities, terminal accounting, and resource slopes (memory ≤ 1.61 KB/min, fds and threads ≤ 0). CPU was 56 % / 26 % / 56 % (source / relay / receiver), and relay forward p99 0.55 ms. That rerun is a diagnostic, not a verdict.

**The pre-roll removed the startup failure.** All four 60 fps short runs pass, and frame 0 and frame 1 arrive within budget in every run. At `a50f6555` the same four runs all failed at frame 1. NV12 1080p60 now passes 1800 s.

**RGBA 1080p60 fails on one audio block.** PCM block 51 848 (at 518.5 s) arrived 17.80 ms after its schedule, against 17 ms:
- The source sent it on time: 0.15 ms after its schedule.
- The relay received it 14.4 ms after its schedule. Its usual time is 1.5–2.5 ms.
- The next block was 8.4 ms late, then 7.4 ms. The stream had caught up within 30 ms.
- Video at the same moment was normal: frames 31 106–31 112 arrived at 5.8–10.7 ms.

So the relay process's PCM path stalled for about 12 ms once in 30 minutes, while its video path was unaffected. Every 1800 s run shows the same kind of audio tail, with a maximum of 15.3–17.8 ms. Only this run crossed 17 ms, and the 30 fps cells have twice the budget. The cause of the stall is not established. No repeat replaces the failure.

CPU per role over the 1800 s runs (source / relay / receiver):

| Cell | Source | Relay | Receiver |
|---|---|---|---|
| RGBA 1080p30 | 40 % | 26 % | 39 % |
| NV12 1080p30 | 44 % | 26 % | 37 % |
| NV12 1080p60 | 60 % | 27 % | 53 % |

Upload and verification per frame, p50 / p99 / max:

| Cell | Source upload | Receiver verification |
|---|---|---|
| RGBA 1080p30 | 3.6 / 5.4 / 10.5 ms | 5.3 / 5.9 / 14.9 ms |
| NV12 1080p30 | 5.5 / 7.9 / 11.7 ms | 7.2 / 8.3 / 12.2 ms |
| NV12 1080p60 | 5.2 / 6.4 / 10.9 ms | 5.6 / 8.0 / 11.9 ms |

The receiver's CPU at RGBA 1080p30 fell from 72 % to 39 % with the vectorized check. But its verification time per frame rose from 2.6 to 5.3 ms at p50, and arrival p99 from 9.93 to 11.75 ms. Lower CPU frequencies under the lighter load would explain this, but that is not measured. It does not affect any verdict.

## Open, for Jon

- **RGBA 1080p60 is not qualified on Android.** It fails zero-loss on one late audio block (17.80 ms against 17 ms) in 1800 s. The stall was in the relay's PCM path; its cause is not established. NV12 1080p60 and both 1080p30 cells pass.
- **The audio budget at 60 fps.** The audio arrival diagnostic uses the video frame budget, 17 ms at 60 fps, although audio blocks are 10 ms apart and do not depend on the video rate. This is how the frozen profile is defined. Changing it is a profile decision, and it would not apply retroactively to this run.

## Files

- `cells-0e8da98/summary.json` and `environment.json` (revision, Android release and device, build fingerprint, SoC and GLES renderer, static GStreamer version, harness SHA-256).
- `cells-0e8da98/<cell>-<arm>.analysis.json`: the full analyzer output.
- Per capture: `config.json` (with `preroll`), the role summaries and any non-empty stderr.
- The raw captures (per-role video, audio and resource CSVs, and the trace-on traces) are 47 MB compressed and kept outside the repository: `captures.tar.xz`, SHA-256 in `captures.tar.xz.sha256`.
