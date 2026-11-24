# FrameLink project status — 2026-11-24

This is the authoritative current-status page. Other documents link here instead of repeating status. Dated evidence folders (`docs/m*/…/README.md`) are records of their own date and are not updated.

## Summary
| Milestone | Status |
|---|---|
| M1–M5, M5a, M5b | **Complete** within documented scope (merged; see [ROADMAP_TASKS.md](ROADMAP_TASKS.md)) |
| M6 — Windows scope | **Ready**, recorded 2026-11-17 by Jon. [M6_WINDOWS_READINESS.md](m6/M6_WINDOWS_READINESS.md) |
| M6 — Linux and Android | **Open, next** (Jon's decision, 2026-11-18). Primary-profile measurements exist; acceptance and the remaining M6 work have not started (below) |
| M7 — Release and handoff | **Held** until M6 Linux and Android. 7.1 (API freeze) is done for Windows only; 7.2–7.4 not started |

**Decision (Jon, 2026-11-18):** extend M6 to Linux and Android next; M7 stays held until then.

**Order of work.** Implementation first, with focused checks; broad campaigns afterwards on the final revision (the 2026-11-12 execution-order rule):
1. M6.9 on Linux (DMA-BUF): native producer allocations in coordinated sessions. 9a–9c done with focused checks on 2026-11-18 ([evidence](m6/linux-native-av-session/README.md)); 9d/9e follow with the final-revision qualification.
2. M6.9 on Android (AHardwareBuffer). 9a–9c done with focused checks on 2026-11-18 ([evidence](m6/android-native-av-session/README.md)).
3. M6.8b/8c: CPU-consumer ring memory on Linux and Android. 8b in progress: on 2026-11-18, GPU staging was added to the Linux A/V source and Android CPU-mappable rings were made cached ([evidence](m6/cpu-path/cpu-consumer-20261118/README.md)), and ring readback on Linux and Android reuses its CPU output buffers ([evidence](m6/cpu-path/cpu-output-reuse-20261118/README.md)). Push-to-pull stage costs are now within the 1080p60 and 4K30 frame budgets on both platforms. 8c started on Android ([evidence](m6/cpu-path/android-cpu-receiver-20261118/README.md)): the CPU receiver's 1800 s 1080p60 cell completes (21 arrival misses over 17 ms, no drops); the 4K30 cell failed (4 ring-full drops, 283 misses over 34 ms), then completed after the receiver prepared its output buffers at start (54000/54000, no drops, 83 misses over 34 ms; one run) ([evidence](m6/cpu-path/android-output-prepare-20261118/README.md)). A same-device BufferQueue baseline is recorded ([evidence](m6/cpu-path/android-bufferqueue-20261118/README.md)). Linux 8c has not started.
4. Check the Linux/Android integration against the frozen Windows API `060c54e4`; any breaking change goes to Jon first. Done on 2026-11-19 at `4a944ab9`: no breaking change; the Windows exports and element surface are identical, and the frozen revision's 180 tests pass against the new DLLs. Jon decided all five recorded observations on 2026-11-19 ([evidence](m6/api-check-linux-android-20261119/README.md)): full native relay parity on Linux, then Android, as part of 9d (in progress, [plan](m6/linux-native-relay/README.md)); Linux exports only API symbols; public epoch and cancellable session calls; `flReconfigure` removed; `gpu-readback` registered on `framelinkavsrc` everywhere.
5. Final-revision qualification: budget acceptance (M6.2b/2e), lifecycle (M6.3), faults (M6.4), 9d/9e and the acceptance review (M6.7c).

**Latest measurements (2026-11-22, not yet the final-revision campaign):**
- Native relay rate cells with the pre-roll at `0e8da988`: Linux 4/4, Android all cells, Windows 1080p60 2/2 ([plan](m6/WINDOWS_NATIVE_RELAY_RATE_PLAN.md#pre-roll-cells-on-every-platform)).
- Windows 4K30 failed on the noisy host; the frozen binaries failed there too ([A/B](m6/windows-4k-ab-20261122/README.md)). On a quieted host both 4K30 cells passed 1800 s, while two A/B runs just before failed on 196 ms stalls coinciding with Windows Update restore points ([record](m6/windows-4k-quiet-20261122/README.md)). Investigated on 2026-11-23 ([record](m6/windows-stall-investigation-20261123/README.md)): the quiet-host stalls are harness CSV writes blocked by shadow-copy snapshots; the desktop stalls are `flSubmit`'s D3D11 `Signal` waiting 50–140 ms on an NVIDIA driver lock held during a monitor power change (display idle-off after 5 min, wake, or connector re-detection), named with Windows symbols. Since `efa86dec` the harness writes its CSVs off the timed threads and takes `--pool`/`--video-queue` (default 2/1, unchanged). On the desktop, NV12 4K30 failed again at 2/1 on a 178 ms `flSubmit` stall after a display wake; pool 4 / queue 2 passed 1800 s, but no stall occurred in that run. With the monitor turned off and on during the run, both 4/2 and 2/1 failed at the first change that hit a submit (155 ms and 64 ms stalls; the relay's forward stalled too). No buffering profile keeps those frames within the 34 ms budget, so 4K30 qualification needs a host with no display power changes during the run. Since `357d82a5` the Windows relay holds the display awake, as vkrender does while presenting; Windows honours that only from the interactive session. With the display timeout back at 5 min, both formal 4K30 cells passed 1800 s on the normal desktop with zero misses ([record](m6/windows-native-relay-rate-357d82a/README.md); arrival max 31.63 ms BGRA, 16.95 ms NV12). The display had gone idle-off just before the first measured frame, so it stayed off throughout. A re-run with the display lit throughout also passed both cells with zero misses (arrival max 20.77 ms BGRA, 16.93 ms NV12). **Decision (Jon, 2026-11-24): Windows native 4K30 is qualified** on the `357d82a5` cells, with the stream's display held awake as a presenting renderer does. Display power changes during a stream are a recorded limit (below).
- Primary-profile lifecycle at `e1284833` ([record](m6/lifecycle/primary-e128483/README.md)): native Linux 7/8, Android 4/4; shared CPU Linux 6/8, Android 7/8; held leases and shared-CPU 1800 s cells pass on both. Open: Linux storage-sized memory steps against the lifecycle gate, and one Android drain-report timeout.
- Still to do in M6: faults (M6.4) on Linux and Android, budget acceptance (M6.2b/2e), 9e on the final revision, and the acceptance review (M6.7c).

## Current revisions
| Repository | Revision | Notes |
|---|---|---|
| framelink `master` | current head | `c6d7db7` (merge of PR #10, M6 Windows) plus the tooling change (PR #11), documentation (PR #12 and later) and M6.9 on Linux and Android (PRs #17, #18) and M6.8b/8c (PRs #19–#22): code for those platforms only, Windows API and behavior unchanged (180/180 on the qualification host; [API check](m6/api-check-linux-android-20261119/README.md)) |
| Frozen Windows API | `060c54e41bcbfe7ca8dbdef89e6e74fb2930935c` | Tag `m6-windows-api-freeze`. `FL_VERSION` 6. [API freeze](m6/API_FREEZE_CANDIDATE.md) |
| vkrender `master` | current head | Code as of `f18c97f` (PR #15 merge; later commits are docs and test scripts). Pins framelink `c6d7db7` (code identical to the frozen revision); includes the renderer repair `86853d6`, the scene-output fix (PR #20) and fullscreen across displays (PR #15) |
| vkrender-airplay `main` | `ac4e29f` | Pins framelink `c6d7db7` |
| framecam `master` | `4a87bb3` | Virtual camera; not pinned, builds against the framelink checkout beside it (vkrender builds it with its own pin) |

vkrender and vkrender-airplay stay on the same framelink revision because peers with a different `FL_VERSION` are refused at handshake.

## Windows: supported configurations
Host: Windows 11 build 26300, NVIDIA RTX 5090, driver 32.0.16.1714, same adapter. No inference to other GPUs, drivers, OS builds or cross-adapter use.

| Path | Configurations | Evidence on the final revision |
|---|---|---|
| Negotiated shared CPU | BGRA and NV12 at 1920x1080p60 and 3840x2160p30 | 1800 s 4/4 pass; lifecycle 8/8 by accepted reviewed equivalence |
| Native D3D11 two-hop, retained device owner (option B) | NV12 1080p60, NV12 4K30, BGRA 4K30, BGRA 1080p60 | 4K30 1800 s 2/2 pass; lifecycle 6/6 plus BGRA 1080p60 and held leases (cycles 100–199 rule) |
| Native 1080p60 (NV12 and BGRA) | Supported by Jon's decision | **No clean 1800 s pass on the final revision.** Both 1800 s failures are recorded; working assumption: host scheduling stall. [Details](m6/windows-final-duration-060c54e4/README.md) |

Integrated: 180/180 tests, 137 exports (128 retained + 9 owner), C11 headers, 3/3 unchanged old clients, hosted CI. [Evidence review](m6/EVIDENCE_REVIEW_20261117.md).

## Windows: excluded or limited
| Item | Status |
|---|---|
| Decoded relay 3840x2160p30 | **Not qualified**, excluded (two 34 ms misses) |
| Decoded relay 1920x1080p60 | Short functional pair only; no endurance claim |
| Renderer final submission (NV12 1080p60) | Submission proof only (three 5 s captures, ring 12 / pending 6, explicit CPU conversion) |
| Legacy fresh-device native reconnect | **Fails** the 100-cycle limit; use the device owner |
| Owner device-removal path | Implemented, **not tested** |
| Physical scanout, acoustic sync, cold display wake, suspend | Not established by any test |
| Display power change during a stream (display wake, connector re-detection) | **Limit.** It stalls the NVIDIA driver for 50–180 ms, so frames arrive late or the run fails, at any pool or queue depth ([record](m6/windows-stall-investigation-20261123/README.md#5-forced-display-power-changes-pool-4--queue-2-does-not-absorb-the-stall)). Holding the display awake while presenting prevents idle display-off, but not these |

## Deferred decisions and investigations
- **D2/D3** (overload default and copy fallback for future convenience callers): deferred until a convenience entry point is proposed. Existing defaults and explicit profiles are unchanged.
- **Native 1080p60 late wake-up**: diagnosis deferred; [plan](m6/windows-final-duration-060c54e4/README.md#decision--jon-2026-11-17).
- **Decoder 4K misses** and the historical intermittent native NV12 4K30 159 ms event: causes not established.
- **Default (non-owner) native lifecycle growth**: no remedy; the owner profile is the supported path.

## Linux and Android
- **Measured:** primary zero-added-delay 1800 s cells pass their transport gates: Linux native BGRA/NV12 at 1080p60 and 4K30, Android native RGBA at 1080p60 and 4K30 ([PRIMARY_RESULTS.md](m6/PRIMARY_RESULTS.md)). Integrated regressions passed earlier (Linux 114/114, Android 72/72).
- **Not done:** primary-profile budget acceptance (M6.2b/2e), lifecycle (M6.3), fault qualification (M6.4), CPU-consumer memory path (M6.8b in progress, 8c not started; Linux and Android CPU target cells unsupported), native producer in coordinated sessions (M6.9: 9a–9c done on Linux and Android; 9d/9e not started on either), and final-revision qualification.
- The Windows API freeze does not establish that Linux/Android integration will need no breaking change; that check is part of the deferred work.

## Tooling (all repositories)
Builds use the same tooling on every platform: Ninja, Release, `CMakePresets.json` (`cmake --preset ci`, `cmake --build --preset ci`, `ctest --preset ci`), one CI job definition per platform, and pinned runners (`ubuntu-26.04`, `windows-2025`). Only dependency installation and the MSVC environment differ.

## Related projects
- **vkrender renderer repair:** the Windows readiness record cites renderer `a6a1906` on top of `9f74b9d`. It accepts `--av-latency-ms 0`, caps the option at 2000 and leaves the 100 ms default unchanged. It is merged to vkrender `master` as `86853d6` (vkrender PR #19).
- **vkrender scene output:** from FrameLink `12326e17` (2026-11-01) until 2026-11-17, the renderer's Windows scene output delivered no frames, the virtual camera included. FrameLink requires the exact acquired buffer at submit, and the renderer rebuilt it without its image, so every `flEndSubmit` returned `FL_INVALID`. FrameLink behaved as its API documents; the renderer was fixed in vkrender PR #20. Its end-to-end check now runs in vkrender's `hardware` test preset.
- **vkrender:** PR #15 (fullscreen across displays, on-screen controls) is merged. It was tested with multiple windows on the single-monitor test host. The per-monitor fullscreen path and the display on/off checkboxes are untested on real multiple displays.
- **vkrender-airplay:** the GPU self-test passes since PR #6 (2026-11-18): 8 of 8 runs delivered 58 of 60 frames. The cause of the earlier 54–56 was the GPU pipeline's `appsink max-buffers=1`; the decoder emits frames in bursts and the older of two was dropped before reaching FrameLink. The sink now holds 4, as the CPU path does.
