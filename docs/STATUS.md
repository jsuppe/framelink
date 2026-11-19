# FrameLink project status — 2026-11-19

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
