# Windows 4K30 stall investigation — 2026-11-23

**Why.** Windows has the lowest steady-state arrival times of the three platforms (4K30 p50: BGRA 10.1 ms against 15.2 ms on Linux, NV12 6.2 against 8.1), yet every Windows 4K30 failure since 2026-11-22 is a single stall of 30–474 ms ([rate record](../windows-native-relay-rate-0e8da98/README.md), [A/B](../windows-4k-ab-20261122/README.md), [quiet host](../windows-4k-quiet-20261122/README.md)). This investigation asks what stops a thread for that long on an otherwise fast, mostly idle machine.

Host: the same test host (Ryzen 7 9800X3D, 8 cores / 16 threads, 64 GB; RTX 5090, driver 617.14; Windows 11 26300; Balanced power plan). All runs held the Windows test mutex. The bench binaries are the `0e8da988` ones from the formal cells (SHA-256 `a85e1bde…`).

## Summary

There are **two independent mechanisms**, and neither is in FrameLink's own code.

1. **Quiet host: a file write blocked by a Volume Shadow Copy snapshot.** The harness writes a CSV row per frame or block on its timed threads. The first write after a snapshot spent about 100 ms of kernel CPU time in NTFS. Reproduced without FrameLink.
2. **Normal desktop: a GPU-driver lock held for 50–140 ms by another component.** `flSubmit`'s `ID3D11DeviceContext4::Signal` waits in the NVIDIA user-mode driver. The driver's worker thread waits on a kernel lock, and that lock is held by csrss's display-configuration path, or a `dxgkrnl` worker, running in `nvlddmkm.sys` for the whole stall. Other GPU clients (Chrome, NVIDIA Broadcast) queue on the same lock. With Windows symbols ([section 4](#4-follow-up-the-trigger-is-display-power-management)), the lock holder is a **monitor power change**: the display turning off after the 5 min idle timeout, turning back on, or the monitor being re-detected on its connector. The driver re-sets the display timings, busy-waiting, while it holds the lock.

In every captured stall exactly one thread stopped. All other threads in the three bench processes stayed on time, so this is not a machine-wide freeze.

## 1. FrameLink-free probe

`probe/stallprobe.cpp` copies the harness's thread shapes with no FrameLink, GStreamer or GPU call:
- **video:** 30 Hz, paced with the harness's `pace()` (`Sleep`, then `SwitchToThread`), one buffered CSV row per tick;
- **pcm:** the same at 100 Hz;
- **tick:** a `Sleep(1)` loop with no I/O.

It logs every late wake-up and every slow row write with wall-clock time. On a stall of at least the trigger size it saves a WPR circular trace (CPU sampling, context switches, disk and file I/O).

**Quiet host, 60 min** (`alone-60m/`): no event over 20 ms. Worst wake-up 3.70 ms, worst write 0.61 ms, `Sleep(1)` overshoot at most 6.8 ms. No snapshot occurred during the hour.

## 2. A shadow copy reproduces the quiet-host stall

With the owner's approval, the probe ran for 300 s while two shadow copies of C: were created (at 60 s and 180 s, through `Win32_ShadowCopy.Create`, `vss.ps1`). Each was deleted 60 s later; the host has the same three shadow copies as before.

The second snapshot's commit (create returned at 13:11:27.583) was followed, at 13:11:27.87, by **one `WriteFile` on the probe's pcm thread to `pcm.csv` that took 99.2 ms**. The thread then caught up 90 → 14 ms late over ten blocks (`vss-test/events.csv`). The video and tick threads were unaffected. The first snapshot caused no stall: no thread flushed a write in its window.

In the trace (`stall-window-stallprobe.txt` in the archive), the write's `FileIoOpEnd` reports 99 156 µs. The thread was **on CPU for 96.7 ms**, not waiting. All 97 CPU samples are in the kernel write path (`NtWriteFile` → `FLTMGR.SYS` → `Ntfs.sys`, leaf in `ntoskrnl.exe`, `Ntfs.sys` or `volsnap.sys`). Just before, a fast-I/O attempt was refused (`0xC01C0004`, `STATUS_FLT_DISALLOW_FAST_IO`), so the write took the request path. Symbols were not loaded, so the function inside NTFS is not named.

This is the mechanism of both quiet-host failures on 2026-11-22. Each was a single 196 ms stall in a thread that writes CSV rows (source video, receiver PCM), during one of the day's only two snapshots (Windows Update restore points). It is a harness effect: the measured threads do their own file I/O.

## 3. Normal desktop: bench and probe side by side

The applications closed on 2026-11-22 were restarted in the owner's interactive session: LM Studio, NVIDIA Broadcast, Steam, the Epic Games launcher (with its overlay), and the Ollama service. Then a 30 min run (`noisy.ps1`) recorded a WPR file-mode trace of the whole run (21.7 GB), GPU state every 250 ms, and QPC/wall-clock pairs. In it, the probe ran for 1830 s, and the NV12 4K30 bench was restarted whenever it aborted. These bench runs are diagnostic captures, not qualification runs.

| Run | Duration | Result | Stall |
|---|---|---|---|
| bench-1 | 243 s | abort (frame lost at the relay, `fl_gst_local_native_bench.cpp:344`) | source `flSubmit` 52.2 ms, frame 7229 |
| bench-2 | 21 s | abort (same) | source `flSubmit` 141.6 ms, frame 565 |
| bench-3 | 150 s | abort (same) | source `flSubmit` 105.2 ms, frame 4439 |
| bench-4 | 1385 s | complete, exit 0 | receiver video 36.4 and 28.0 ms, receiver audio 39.6 and 44.4 ms (not analyzed in the trace) |
| probe | 1830 s | **no stall**: worst wake-up 3.54 ms, worst write 0.18 ms, `Sleep(1)` at most 7.07 ms | |

So on the busy desktop the host does not starve CPU threads in general: the probe, which touches no GPU, stayed on time throughout. The stalls are in the bench's GPU calls.

### The chain, from the trace

For each abort, the source's video thread (identified by its NVIDIA driver samples) was followed to the thread that woke it (`probe/chain.py`):

| Stall | Source video thread | Driver worker thread in the same process | Lock holder |
|---|---|---|---|
| bench-1 | waits 52.1 ms (`UserRequest`) | waits 52.1 ms on a kernel lock (`WrResource`) | **csrss.exe**, 44 samples in `nvlddmkm.sys` |
| bench-2 | waits 141.5 ms | waits 141.4 ms (`WrResource`) | **csrss.exe**, 138 samples in `nvlddmkm.sys`. Chrome and NVIDIA Broadcast threads were blocked on the same lock |
| bench-3 | waits 105.0 ms | waits 104.9 ms (`WrResource`) | **System** worker, 103 samples: `dxgkrnl.sys` → `nvlddmkm.sys` (some through `dxgmms2.sys`) |

The source thread's stack is identical in all three:

`flSubmit+0x24d` → `d3d11.dll` → `nvwgf2umx.dll` (NVIDIA user-mode driver) → `KernelBase` wait.

The call at `flSubmit+0x247` is `call *0x498(%rax)` on the device context, with the ready fence and its value as arguments. That is vtable slot 147, `ID3D11DeviceContext4::Signal`, i.e. `ch->ctx->Signal(ch->readyFence.Get(), value)` in `src/framelink_win.cpp` `flSubmit`. The return address was matched against the `framelink.dll` of these runs (SHA-256 `5d5bde8a…`) by disassembly; no PDB was used.

The lock holder in bench-1 and bench-2 is csrss's display-configuration path, on every sample:

`winsrvext.dll` → `win32u.dll` → `win32k.sys` / `win32kfull.sys` / `win32kbase.sys` → `dxgkrnl.sys` → `nvlddmkm.sys`

It ran on the CPU for the whole stall. csrss was woken just before by System threads (kernel callouts), and also by dwm, XboxService and Chrome. **What request it was serving is not established.** Naming it needs Windows symbols (Microsoft's public symbol server) for `win32k*`, `dxgkrnl` and `winsrvext`. `nvlddmkm.sys` has no public symbols.

At 250 ms resolution the GPU was in P5 at all three stalls, as it was for most of the run (P5 6319 samples, P8 1211, P0 68, P3 32). No power-state transition is visible at that resolution.

## What this means

- No stall was in the relay's forward, whose p99 stayed at or below 0.07 ms in every analyzed cell. The source's `Signal` is a required call (`flSubmit` must signal the ready fence and flush). It blocks because the NVIDIA driver serializes it behind a lock held by an unrelated display-configuration request.
- The primary profile (pool 2, queue 1, zero added latency) cannot absorb a 50–140 ms stall at 30 fps: the catch-up burst replaces a frame in the one-deep queue, and the harness fails fast. That is the design of the test, and the gate is unchanged.
- Linux and Android do not share this driver stack, which is consistent with Windows being the only platform with such stalls.

## 4. Follow-up: the trigger is display power management

With the owner's approval of all three next steps below, on the same day:

**Harness (step 1), commit `efa86dec`.** The native bench's timed threads now only format each CSV row into memory; a writer thread per file does the disk writes. A failing `CHECK` flushes the pending rows first. `--pool` and `--video-queue` select the profile (default 2 and 1, unchanged); `config.json` records it, and the checker and the rate runner fail a capture whose profile differs from the one requested. Linux: 31 checker unit tests and both `local_native_bench` ctests pass, and 5 s NV12 1080p30 captures pass at 2/1 and 4/2. Windows (bench SHA-256 `3014565c…`): NV12 4K30 and BGRA 1080p60, each preflight, trace and 30 s, pass at both 2/1 and 4/2, 12 of 12 (`build-and-short-runs.log`).

**Symbols (step 2).** The three noisy-host windows were dumped again from the same `run.etl` with Microsoft's public symbol server (`m6h-syms.ps1`; 2.5 GB symbol cache on the test host). Function-level stacks of the lock holders (`stacks/`, from `probe/stkf.py`):

| Stall | Lock holder | Path (outermost first) |
|---|---|---|
| bench-1, 52 ms | csrss thread 1536, 64 samples | `winsrvext!PowerNotificationThread` → `win32kbase!xxxUserPowerEventCalloutWorker` → **`PowerOffMonitor`** → `DrvSetMonitorPowerState` → `dxgkrnl!DxgkPowerOnOffMonitor` → `MONITOR_MGR::_EnableDisableMonitor` → `VIDPN_MGR::PowerOnOffVidPnTarget` → `SetTimingsFromVidPn` → `nvlddmkm` |
| bench-2, 142 ms | the same thread, 153 samples | the same, with **`PowerOnMonitor`** |
| bench-3, 105 ms | System worker 47572, 131 samples | `IopProcessWorkItem` → `dxgkrnl!DpiIndicateConnectorChangeWorkItem` → **`DxgkQueryConnectionChanges`** → `DmmPowerOnOffTargetsFromSourceIfNecessary` → `SetTimingsFromVidPn` → `nvlddmkm`; 29 samples also in `dxgmms2!VidSchiSuspendResumeDevicesForPowerTransition` |

In each, 25–67 samples are in `ntoskrnl!KeStallExecutionProcessor` under `nvlddmkm`: the driver busy-waits during the timing change, and holds the lock that `Signal` waits for. `nvlddmkm.sys` frames stay unnamed (no public symbols).

**The event log matches.** The host turns the display off after 300 s idle (`display-state.txt`: one LG UltraWide monitor). Kernel-Power event 566 records each display transition, reason 12 for the idle timeout and 31/32 for the display coming back on input (`kernel-power-566-today.txt`). The noisy trace began at 13:19:18.16, so:
- bench-1 (`PowerOffMonitor`) at 13:23:21.7; display off (reason 12) logged at 13:23:26.8;
- bench-2 (`PowerOnMonitor`) at 13:23:42.9; display on (reason 32) logged at 13:23:43.1;
- bench-3 (connector change) at 13:26:13.4, 2.5 min after the display came back; no 566 event. The monitor was re-detected on its connector.

So the trigger is not one of the owner's applications. No application-elimination runs were needed, and none were done.

**Normal desktop again, harness `efa86dec`, NV12 4K30 1800 s** (`m6h-noisy.ps1`, same applications running before and after, `symbols-and-desktop-runs.log`):

| Profile | Result | Notes |
|---|---|---|
| pool 2, queue 1 (default) | **FAIL** at frame 27358, 912 s: the relay lost a frame (`fl_gst_local_native_bench.cpp:377`) | source `flSubmit` 9.6, 47.3 and **178.2 ms** on frames 27353–27356, at 18:41:14.6; the source acquire also waited up to 53.8 ms. The display had come back on at 18:40:39.95 (566, reason 32), 35 s earlier. Not traced, so the lock holder is not established for this one. Every role's CSV ends at the last frame before the abort: the failure flush worked |
| pool 4, queue 2 | **pass**, zero misses | arrival p50 / p99 / max 4.54 / 6.61 / 17.95 ms; source `flSubmit` max 9.18 ms. **No driver stall happened in this run**, so it does not show that this profile absorbs one |

The default-profile failure shows the desktop stall with no file I/O on the timed threads. So it is separate from the shadow-copy mechanism, as found from the trace.

**Profile (step 3).** The pool and unread queue are already configuration in the product: the GStreamer elements' `video-pool` and `video-queue-frames` properties, and the session API's `video_pool` and `video_queue_frames`. The harness can now run any profile, and the primary profile stays the default. Two points for choosing a Windows profile:
- A deeper queue can stop the frame loss, but not the lateness. Frames behind a 50–180 ms stall at the source arrive past the 34 ms budget whatever the queue depth: in the failed run they were 213–237 ms late. A profile with headroom would turn an abort into counted misses.
- Whether pool 4 / queue 2 actually absorbs a stall is not yet measured. It needs a run in which a display power change happens. That can be forced (turning the monitor off and on during a run), but that is the owner's call, since it is their screen.

## 5. Forced display power changes: pool 4 / queue 2 does not absorb the stall

With the owner's approval, the same evening, NV12 4K30 600 s at harness `efa86dec` (`followup/forced/`):
- **Host change first:** the display idle timeout on AC was set from 300 s to 0 (never), so that only forced changes occur. To restore it: `powercfg /change monitor-timeout-ac 5`.
- **Forcing the changes:** a task in the owner's interactive session (`m6h-blink.ps1`, run by `m6h-forced.ps1`) waited for the long capture to start. Then, at 60 s and every 120 s after, it turned the monitor off (`SC_MONITORPOWER` 2, broadcast) and, 30 s later, back on (a one-pixel mouse move). It logged each step.
- **Order:** pool 4 / queue 2 first, then the default 2/1 as the control. Each had the usual 5 s preflight and trace-on runs first, and all four passed.

| Profile | Result | Stall |
|---|---|---|
| pool 4, queue 2 | **FAIL** at frame 2648, about 88 s: the relay received the wrong frame (`:377`). The first forced off, at 60 s, caused no stall | at the first forced **on** (20:45:45.395; 566 reason 32 at 20:45:45.524). Source `flSubmit` **155.1 ms**, and at the same time the relay's forward of the previous frame took **184.4 ms**. After it, the source published 3 frames within 3.9 ms; a queue of 2 cannot hold them |
| pool 2, queue 1 | **FAIL** at frame 1747, about 58 s: the same check | at the first forced **off** (20:47:06.106). Source `flSubmit` **63.6 ms**; the next frame followed 1 ms later and replaced it in the one-deep queue |

- The frame times are converted to wall time from the capture start (directory creation plus the 23-frame pre-roll). Both stalls fall within about 0.5 s of a forced change.
- Audio was unaffected in both runs: the worst block was at most 2.6 ms late at any role.
- The source's later `acquireResult==FL_OK` failure (`:246`) follows from the relay's abort.
- A forced off logs no 566 event; the forced on logged reason 32, as on input.

So:
- **On demand:** the stall reproduces on demand, in 2 of 2 runs, at the first change that hit a submit.
- **Not just the source:** at the on transition, the relay's forward was blocked in the same window as the source's submit. The whole GPU path stops, not one call.
- **Headroom doesn't fix it:** pool 4 / queue 2 does not absorb a 155 ms stall at 30 fps. A queue deep enough to hold the catch-up burst (about 5 frames or more) would stop the loss. But those frames would still arrive more than 120 ms past the 34 ms budget, so no buffering profile passes the zero-miss gate through a display power change.
- **What remains for qualification:** for the 4K30 qualification cells, the remaining question is a host condition: no display power changes during the run. The display timeout is now disabled on the test host.

## Possible next steps (decisions for Jon)

Status after section 5:
- Step 1 is done (`efa86dec`).
- Step 2 is answered by the symbols, without application elimination.
- Step 3: the profile is selectable in the harness. Forced display changes show that a deeper queue does not absorb the stall within the budget.

1. **Quiet-host stalls (harness).** Move CSV writing off the timed threads, to a writer thread with an in-memory queue. This is a test-harness change only. It removes the snapshot effect from the measurement but not from the host.
2. **Desktop stalls, identify the trigger.** Resolve the csrss stack with Microsoft public symbols, and/or remove candidate applications one at a time (NZXT CAM, NVIDIA Broadcast, Steam and Epic overlays, monitor or lighting utilities) while the same trace runs.
3. **Product question.** Real Windows deployments will see the same driver-lock stalls whenever the desktop does display-configuration work. Whether the supported Windows profile needs headroom for them (a deeper pool or queue, or added latency) is a gate decision, not a test fix.

## Files

- `probe/`: the probe source and build script, the run scripts (`probe.ps1`, `vss.ps1`, `noisy.ps1`) and the analysis scripts (`coincide.py`, `cs.py`, `tl.py`, `act.py`, `chain.py`, `stk.py`).
- `alone-60m/run.log`, `vss-test/` (events and log), `noisy/` (run log, QPC/wall pairs, GPU state, probe log and events, and per bench run its config, summaries and non-empty stderr).
- Outside the repository, SHA-256 in `archives.sha256`:
  - `captures.tar.xz` (7.5 MB): all probe and bench CSVs, process lists, and the probe-filtered window of the shadow-copy trace;
  - `trace-windows.tgz` (67 MB): the text dumps of the three 450 ms trace windows around the bench aborts.
- The full traces (`run.etl`, 21.7 GB; the `vss-test` `stall-1.etl`) and the symbol cache were deleted from the test host with the owner's approval, after the archives above were checked against their SHA-256.
- `followup/forced/` (section 5): the scripts, `forced-runs.log`, and under `runs/` each capture's analysis, config, summaries and non-empty stderr. Outside the repository: `forced-display.tgz` (1.7 MB, all captures with their CSVs), with its SHA-256 in `archives.sha256`.
- `followup/` (section 4): the scripts (`m6h-*.ps1`), the logs, `stacks/`, the event and display queries, and under `runs/` each capture's analysis, config, summaries and non-empty stderr. Outside the repository, SHA-256 in `archives.sha256`: `sym-windows.tgz` (79 MB, the three symbolized windows) and `runs-efa86de.tgz` (28 MB, all captures from section 4 with their CSVs).
