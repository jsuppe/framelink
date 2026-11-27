# Windows native 1080p60 late wake-up: the diagnosis (decision 12, A2) — 2026-11-27

This record is dated; current status is in [STATUS.md](../../STATUS.md). Jon chose A2 in [decision 12](../decisions-20261126.md): run the diagnosis deferred on 2026-11-17 ([`060c54e4` record](../windows-final-duration-060c54e4/README.md)) before the Windows requalification.

Host: the Windows test host (Ryzen 7 9800X3D, 8 cores / 16 threads; RTX 5090; Windows 11 26300), launched over ssh like the `060c54e4` batch, under the validation mutex. **Diagnostic, not qualification:** the binaries are the PR #48 branch at `5dbd8aa4` (bench SHA-256 `f53fe296…`), which carries decisions 8–10's unqualified Windows product changes.

## Method

- **Pacing record** (`FL_BENCH_PACE_DIAG=1`, test-only): per measured frame, the source video thread's sleeps (actual against requested), yields, its own CPU cycles across the wait, the system timer resolution, and the previous loop tail. The bench's CSV rows are written off the timed threads (since `efa86dec`).
- **Rolling WPR trace** (CPU sampling, context switches, ready threads, disk and file I/O) for the whole batch. A watcher saves it as soon as any role reports a failed check.
- **FrameLink-free probe** (`probe/stallprobe.cpp`, 60 Hz with a 3 MB fill, a 100 Hz thread and a `Sleep(1)` tick) alongside, on the same QPC clock.
- **Stage split** (`tools/attribute_native_stages.py`) on every capture; the rate runner prints a failure's worst stage.
- Batch: NV12 then BGRA 1080p60, 5 s preflight, 5 s traced, then 1800 s, three repeats (`d12-runs.ps1`).

## Display events first: the earlier 1080p60 failures had no display change

The host's Kernel-Power 566 log shows the display off and steady through both earlier failures: from 11-16 14:03 to 11-17 12:30 UTC (the `060c54e4` NV12 failure was at about 06:32), and from 11-11 14:14 to 11-13 13:55 (the `a50841a` BGRA failure, 11-12). So the 1080p60 late wake-up is not the display-power driver stall found for 4K30 on 2026-11-23.

## Results so far (batch 2, in progress)

| Run | Result | Worst stage | Display (566) | Mechanism |
|---|---|---|---|---|
| batch 1, NV12 1 | FAIL at about 628 s | `flSubmit` 151.6 ms | display on (input), 14:03:06.45 | display-power driver stall (known) |
| NV12 1 | FAIL at about 248 s | `flSubmit` 65.5 ms | display off (idle timeout), logged 14:34:14.68, about 5 s after | display-power driver stall (known) |
| BGRA 1 | FAIL at about 577 s | `flSubmit` 146.5 ms | display on (input), 14:46:23.25 | display-power driver stall (known) |
| NV12 2 | **pass**, 1800 s | | none, display off and steady | |
| BGRA 2 | FAIL at about 332 s | **`source.late_wake` 21.5 ms** (frame 19920) | none, display off and steady | **the late wake-up** |

Batch 1 was stopped after its first run: its watcher was stopped while it was still saving the trace, so no trace was kept. The script was fixed (the batch now waits for a save in progress) and restarted as batch 2. Someone was using the host during the batch (display woken by input twice).

### The late wake-up, BGRA 2 (15:25:43 UTC)

**Pacing record, frame 19920:** the wait began 10.0 ms before the due time; one `Sleep(8)` returned after 8.98 ms (normal); then one `SwitchToThread()`, and the thread did not run again for about 22 ms. Across the 31.5 ms wait it used 130,879 cycles (about 0.03 ms of CPU). So it was **off the CPU while ready**: not oversleeping (the timer resolution was 1 ms throughout), not file I/O, not FrameLink.

**Probe, same moment:** the probe's `Sleep(1)` tick thread took 29.3 ms, ending 15:25:43.048 UTC. A separate process with no FrameLink, GStreamer or GPU call stalled at the same time.

**Trace** (window 2183.65–2183.90 s of `bgra1080p60-2.etl`, no lost events):
- In the same 30 ms, threads of the probe and of all three bench processes were **ready but not running** for 15–28 ms each. They were made ready on time, from timer DPCs on CPU 14, and were not dispatched.
- **All 16 logical CPUs were occupied by higher-priority threads** for that time: about twelve Windows Defender scan threads (`MsMpEng.exe`, priority 9–14) and an interactive chat client's threads (priority 9–11); a game was also running. Of the 299 CPU samples in 2183.81–2183.84 s, 139 are in `MsMpEng.exe` (97 in its scan engine, `mpengine.dll`), 110 in the chat client, 15 in an NVIDIA service and the rest scattered; none is idle. The window also holds 1,833 Defender MetaStore actions and heavy file I/O.
- The bench threads run at normal priority (8), and so do the probe's.
- No long DPC or ISR in the window.

**So the 1080p60 late wake-up is host CPU starvation:** a Defender scan burst, together with other foreground load, saturated every logical CPU with threads above normal priority for about 30 ms, and every normal-priority thread on the host waited, the bench's and the probe's alike. FrameLink's calls are not involved. This is consistent with the `060c54e4` failure, 15 s after the only Defender scan start in that batch's window.

## Not established yet

- Whether MMCSS registration (the labelled experiment `FL_BENCH_MMCSS=1`), or another fixture change, keeps the source on time through such a burst.
- What triggered this Defender burst.

## Files

- `d12-build.ps1`, `d12-runs.ps1`: build and batch scripts.
- `probe/stallprobe.cpp`: the probe, with `--video-hz` and `--fill-mb`.
