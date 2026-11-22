# Windows native relay rate cells with pre-roll (M6.9d)

The synthetic paced FAR4 → FAR5 harness (`fl_gst_local_native_bench_win`), run as three processes (source, relay, final reader) at revision `0e8da988` (clean tree). The test host has a checkout of exactly that revision, built with the `ci` preset. Cells and order follow the rate plan (`tools/run_local_native_rate_cells.py`):
1. a 5 s trace-off preflight;
2. a matched 5 s trace-on run;
3. 1800 s trace-off.

Since the earlier Windows records, two things changed, the same on every platform:
- **The pre-roll** in the plan's frozen requirements ([amendment](../WINDOWS_NATIVE_RELAY_RATE_PLAN.md#frozen-observable-requirements)). Before the measured stream, the source publishes `8 + fps/2` marked frames. The relay forwards every one, END and EOS count them, and the analyzer checks their trace counts separately.
- **The receiver's payload check** is an exact vectorized comparison of the same bytes, instead of a per-byte check.

Host: Windows 11 (10.0.26300), NVIDIA GeForce RTX 5090, driver 617.14. Budgets: 17 ms at 60 fps and 34 ms at 30 fps, with **zero** misses and zero lost frames, video and audio. No repeat replaces a failure.

Before the cells, the same tree passed the full Windows suite (187/187), the 36 relay tests moved and under stress, and the unchanged `060c54e4` suite against the new DLLs (180/180). Exports were unchanged (88, 47, 2).

## Revision `0e8da988` (clean tree)

| Cell | Preflight | Trace on | 1800 s | Video arrival p50 / p99 / max (ms) | Audio arrival p99 / max (ms) | Relay forward p99 | Resource slopes (max over roles) |
|---|---|---|---|---|---|---|---|
| BGRA 1080p60 | pass | pass | **pass** | 4.83 / 6.51 / 8.07 | 0.24 / 5.49 | 0.06 ms | memory, handles and threads ≤ 0 |
| NV12 1080p60 | pass | pass | **pass** | 4.29 / 5.54 / 6.40 | 0.24 / 3.09 | 0.07 ms | memory 0.12 KB/min, handles 0.013/min, threads 0.006/min |
| BGRA 4K30 | pass | pass | **FAIL** (frame 53 437 and two audio blocks late) | 10.16 / 11.67 / **35.38** | 0.24 / **49.41** | | |
| NV12 4K30 | pass | pass | **FAIL** (aborted at 530 s; audio late before that) | 6.19 / 9.07 / 9.87 | 0.23 / **54.23** | | |

The two failed rows are computed from the raw captures. NV12 4K30 covers only the 15 916 frames recorded before the abort.

CPU per role over the passing 1800 s runs (source / relay / receiver): BGRA 1080p60 33 % / 5 % / 9 %, NV12 1080p60 24 % / 5 % / 8 %.

### BGRA 4K30: one receiver stall

At 1555.5 s the receiver stalled for about 50 ms:
- The source and relay were on time. The relay forwarded video frame 53 437 at 7.7 ms after its schedule, and received PCM block 155 550 at 0.1 ms.
- The receiver acquired that frame at 35.4 ms, against the 34 ms budget, and received the audio block at 49.4 ms.
- The relay's later audio arrivals were 39, 29, 19 and 9 ms late, the backlog draining.

Rerunning the analyzer on the raw capture with only the arrival budget lifted, every other requirement passes: 108 000 frames, 180 000 PCM blocks, exact identities, terminal accounting and resource slopes. The source's memory slope was 42.9 KB/min, under the 64 KB/min gate but far above the other cells. That rerun is a diagnostic, not a verdict.

The stall was at about 17:06:55 local time. The System event log shows background service activity in the same seconds: BITS switching to auto start at 17:06:49, and the UPnP host adding and removing URLs at 17:06:54 and 17:06:58. That is a correlation, not an established cause.

### NV12 4K30: receiver audio stalls, then a source stall

Before the abort, the receiver's PCM path stalled three times, at 154 s, 157 s and 221 s:
- Blocks arrived 54.2, 38.5 and 30.6 ms late. The relay had received each of them on time, at 0.1 ms.
- The first two exceed the 34 ms budget, so the cell would have failed even without the abort.

At 530.6 s the source's video thread woke 474 ms late:
- Frame 15 916 was submitted 1.1 ms after its schedule.
- The source started acquiring frame 15 917 474 ms late. Between those points the thread does only a running-time lookup, a CSV row write and the pacing sleep.
- The source then sent frames back to back. The one-frame queue replaced one at the relay's input, and the relay's check that every frame arrives in order failed (`fl_gst_local_native_bench.cpp:344`). The source's next acquire then failed (`:213`).

That fail-fast is the design: a lost frame ends the run. No event in the System or Application log falls within 30 s of the abort. This is the same signature as the native NV12 1080p60 failure on `060c54e4` (a late wake-up outside any FrameLink call, a catch-up burst, then a queue-1 replacement). Jon's working assumption for that failure is a host scheduling issue ([decision](../windows-final-duration-060c54e4/README.md#decision--jon-2026-11-17)); this one is far longer, 474 ms against 43 ms.

### The receiver audio stalls are 4K-only

Receiver PCM arrivals more than 15 ms late while the relay was on time:

| Cell | Windows | Linux |
|---|---|---|
| BGRA 1080p60 | 0 | |
| NV12 1080p60 | 0 | |
| BGRA 4K30 | 3 | 0 |
| NV12 4K30 | 8 (in 530 s) | 0 |

Linux 1080p cells were not counted here. Only the Windows 4K receiver shows these stalls. They are new: the `060c54e4` 4K30 cells had no arrival above 16.53 ms. Since then the receiver gained the pre-roll and the vectorized check; the host may also have changed. Its main thread maps a 4K staging texture every frame (12 MB NV12, 33 MB BGRA), while the stalled thread is its separate PCM thread. No mechanism is established.

## Open, for Jon

- **Windows 4K30 is not qualified for duration.** Both 4K cells fail. BGRA fails on one receiver stall of about 50 ms. NV12 fails on receiver PCM stalls of up to 54 ms, and was then aborted by a 474 ms source stall. The 1080p60 cells pass. This is a regression against the frozen revision: on `060c54e4` both native 4K30 cells passed 1800 s with zero misses, arrival max 9.94 ms (NV12) and 16.53 ms (BGRA) ([record](../windows-final-duration-060c54e4/README.md)).
- **Follow-up A/B (same day):** the frozen `060c54e4` binaries fail the same NV12 4K30 cell on the same host now, with a 120 ms stall in the source's submit at 986 s; the current binaries failed within 2 s. The host's behavior changed since 2026-11-17, so these failures are not attributable to the harness ([A/B](../windows-4k-ab-20261122/README.md)).
- **The receiver PCM stall at 4K needs a mechanism before another 4K attempt**, as the plan requires. A next step would be a bounded 4K diagnostic with per-phase stamps on the receiver's PCM thread (pull, map, verify, running-time lookup), to see whether it waits on the appsink, a session lock, or the scheduler.

## Files

- `cells-0e8da98/summary.json`, `environment.json` (revision, binary and plugin SHA-256, GPU and driver), `<cell>-<arm>.analysis.json` (full analyzer output), and per capture `config.json` (with `preroll`), the role summaries and any non-empty stderr (NV12 4K30 relay and source).
- The raw captures (per-role video, audio and resource CSVs, and the trace-on traces) are 27 MB compressed and kept outside the repository: `captures.tar.xz`, SHA-256 in `captures.tar.xz.sha256`.
