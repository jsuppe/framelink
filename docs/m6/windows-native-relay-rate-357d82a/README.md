# Windows native 4K30 rate cells with the display held awake — 2026-11-24

**Result:** both formal 4K30 cells pass 1800 s on the normal desktop at `357d82a5`, twice. The first run had the display off throughout; the second had it lit throughout. All four runs had zero misses.

**Why.** The [stall investigation](../windows-stall-investigation-20261123/README.md) traced the Windows desktop 4K30 failures to monitor power changes. A change stalls the NVIDIA driver for 50–180 ms, and forced changes failed both pool 4 / queue 2 and the primary profile. A presenting renderer such as vkrender holds the display awake while it presents (`renderer/win_service.cpp`: `ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED`). So, at the owner's request, the bench now does the same: commit `357d82a5`, where the relay process holds it for the whole run and `config.json` records `display_hold`. These are the two formal 4K30 cells with that harness, on the normal desktop.

This is a new harness revision and a new host condition. It does not replace any earlier failure, which all stand as recorded.

## Host

- **Machine:** the same test host and driver (RTX 5090, 617.14; Windows 11 26300).
- **Applications:** the owner's applications were running, as on the noisy desktop of 2026-11-23: Chrome, Discord, the Claude app, LM Studio, NVIDIA Broadcast, Steam, the Epic Games launcher and Ollama (`host-apps.txt`).
- **Display idle timeout:** restored to its normal 300 s before the run. It had been disabled for the forced-display test of 2026-11-23.
- **Run:**
  - **Binaries:** bench SHA-256 `6c284df6…`, plugin `fcb9e0d0…`.
  - **Command:** `run_local_native_rate_cells.py --cells bgra4k30 nv12-4k30 --duration 1800`, primary profile (pool 2, queue 1), clean tree.

**The hold needs the interactive session.**
- **Over ssh, not honoured** (`hold-check.log`, `m6i-hold.ps1`): when the bench ran from an ssh session, `powercfg /requests` listed it under SYSTEM but **not under DISPLAY**. Windows does not honour a display request from a non-interactive session.
- **Cells in the desktop session:** so the cells ran as a scheduled task in the owner's desktop session (`m6i-task.ps1`, started and watched by `m6i-formal.ps1`), as a presenting app runs.
- **Checked during the cells:** every 5 min, `powercfg /requests` listed the bench under DISPLAY, from 04:59 to 05:54 (`run.log`).
- **vkrender is not affected:** it is a windowed application in the interactive session.

## Results

| Cell | Result | Video arrival p50 / p99 / max (ms) | Audio arrival p99 / max (ms) | Source `flSubmit` p99 / max (ms) | CPU source / relay / receiver |
|---|---|---|---|---|---|
| `bgra4k30` 1800 s (preflight and trace pass) | **pass**, zero video and audio misses | 9.89 / 11.63 / 31.63 | 0.19 / 20.54 | 0.09 / 0.61 | 32 % / 5 % / 9 % |
| `nv12-4k30` 1800 s (preflight and trace pass) | **pass**, zero video and audio misses | 6.21 / 9.03 / 16.95 | 0.19 / 24.92 | 0.09 / 0.23 | 23 % / 4 % / 7 % |

- **Relay forward:** p99 ≤ 0.06 ms in both cells, max ≤ 0.50 ms.
- **Resource slopes:** relay and receiver memory within 0.3 KB/min; handles and threads ≤ 0.012/min. The source memory slope is 40.7 KB/min (BGRA) and 41.7 KB/min (NV12). That is under the 64 KB/min gate, and the same as on the quieted host of 2026-11-22 (42.0 and 43.9). Its cause is still not established.
- **Margin:** BGRA's worst frame, at 31.63 ms, is within 2.4 ms of the 34 ms budget. The p99 is 11.63 ms, so this is one outlier, not a shifted distribution.

## Caveat: the display was off during the cells

- **What happened:** the task woke the display (a zero-delta mouse move, as vkrender does at presentation start) and then waited 300 s to let it settle. That wait equals the idle timeout, so the display turned off again from idle (Kernel-Power 566, reason 12) at **04:54:27.975**. That was 0.15 s after the cells began, during the setup of the first 5 s preflight, before any measured frame and before the bench's hold was in place. The preflight passed.
- **During the cells:** no further display event occurred between then and the end of the cells at 05:55:10. The hold held, but it held a display that was already off: `ES_DISPLAY_REQUIRED` prevents sleep and does not wake a display.
- **So these cells show:** no display power change during 2 × 1800 s on a normal desktop, and 2 of 2 passes. They do not show 4K30 with a lit, composited display throughout. That needs the same run with the cells starting while the display is on, for example a wake 30 s before the cells instead of 300 s.

## Re-run with the display lit throughout

At the owner's request, the same two cells ran again the same morning, with the same revision, binaries, host and applications. Scripts: `m6j-formal.ps1` and `m6j-task.ps1`; log: `run-lit.log`.

**How the display was kept on:**
- **The wake:** the task woke the display at 06:46:30.5, logged as 566 reason 32 (display on). It started the cells 30 s later.
- **Why a helper was needed:** the task also meant to hold the display for its whole session, so that no idle timeout could fall in the few seconds between captures. That call failed: PowerShell parsed `0x80000003` as a negative Int32, so the call was never made, and the log shows an empty return value.
- **The helper:** at 06:47:38.4, during the first capture, a separate desktop-session helper (`m6j-hold.ps1`, `hold-helper-lit.log`) set the same hold correctly. It held it until the run's end at 07:47:44. That was 68 s after the wake, well within the 300 s idle timeout, and the bench's own hold covered each capture.
- **Checked:** every 5-min `powercfg /requests` check from 06:51 to 07:46 listed both the helper and the bench under DISPLAY.
- **Display events:** the wake is the **only** Kernel-Power 566 event between 06:46 and the end of the run. The display was on from before the first capture to after the last.

| Cell | Result | Video arrival p50 / p99 / max (ms) | Audio arrival p99 / max (ms) | Source `flSubmit` p99 / max (ms) | CPU source / relay / receiver |
|---|---|---|---|---|---|
| `bgra4k30` 1800 s (preflight and trace pass) | **pass**, zero video and audio misses | 9.92 / 11.50 / 20.77 | 0.20 / 17.54 | 0.08 / 2.91 | 29 % / 4 % / 10 % |
| `nv12-4k30` 1800 s (preflight and trace pass) | **pass**, zero video and audio misses | 6.02 / 7.19 / 16.93 | 0.20 / 16.44 | 0.09 / 0.96 | 22 % / 7 % / 8 % |

- **Relay forward:** p99 ≤ 0.06 ms, max ≤ 1.23 ms.
- **Resource slopes:** relay and receiver memory within 0.6 KB/min; handles and threads within ±0.004/min. Source memory is 40.7 and 43.5 KB/min, the same as every 4K30 source and under the 64 KB/min gate.
- **No capture wrote to stderr.**

## Verdict, for Jon

- **Both formal Windows 4K30 cells pass at `357d82a5`** on the normal desktop, with the bench holding the display awake as a presenting renderer does. They passed **twice**:
  - first with the display off for the whole measurement (arrival max 31.63 ms BGRA, 16.95 ms NV12);
  - then **with the display lit throughout** (arrival max 20.77 ms BGRA, 16.93 ms NV12).

  All four runs had zero video and audio misses against the 34 ms budget.
- **Not covered by a display hold:**
  - the wake at presentation start (a `PowerOnMonitor` transition, which caused the 142 and 155 ms stalls in the investigation);
  - a connector re-detection, which followed a wake by 2.5 min once in the investigation.

  Both come from the display driver, not FrameLink code.
- Whether Windows 4K30 counts as qualified on these cells is Jon's call.

## Files

- `m6i-hold.ps1` and `hold-check.log`: the build at `357d82a5`, the ssh-session hold check, and the restoring of the idle timeout to 300 s.
- `m6i-formal.ps1`, `m6i-task.ps1` and `run.log`: the formal run, its display-request checks and the 566 events.
- `cells-357d82a/`: environment, summary, and each capture's analysis, config and summaries. No capture wrote to stderr.
- `host-apps.txt`: the applications running.
- Lit re-run: `m6j-formal.ps1`, `m6j-task.ps1`, `m6j-hold.ps1`, `run-lit.log`, `hold-helper-lit.log` and `cells-357d82a-lit/`, laid out like the first run.
- Outside the repository, with SHA-256 in `captures.sha256`:
  - `captures.tgz` (36 MB): all captures of the first run with their CSVs, the ssh-session hold check and the task log;
  - `captures-lit.tgz` (34 MB): the same for the lit re-run, with its task and helper logs.
