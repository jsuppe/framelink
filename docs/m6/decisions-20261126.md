# M6 decisions — 2026-11-26 (Jon)

Six open M6 decisions and three smaller questions, each taken from a written statement of the problem and the options. This record is dated; current status is in [STATUS.md](../STATUS.md).

| # | Decision | Choice | What it means |
|---|---|---|---|
| 1 | D2: video overload default | **B** | Keep drop-and-count (`error-on-ring-full=false`), but mark every frame dropped for a full ring to the consumer as a discontinuity, using the existing GAP / DISCONT path. Then "a stream never silently drops" holds at the consumer. Making the producer bounded by the consumer's unread queue (option D) was not chosen now. |
| 2 | Android burst headroom | **C** | Do not accept ~2x intake at 1080p60 RGBA ([diagnostic](fault-coverage/burst-transport/android-intake/README.md)). Make Android match Linux (~4x or better). First reduce the ~4.2 ms copy (bandwidth and AHB mapping cacheability); the ~2.4 ms slot wait needs the producer bounded by the consumer's unread queue, which is to be confirmed with Jon once the copy results are known. |
| 3 | Windows `read_lease_d3d11_producer_death` failure | **A** | A test-only diagnostic build that, on failure, prints the pixels read and the ready-fence state, run under ctest repeats on the qualification host. No product change. A fix comes back to Jon with the evidence. |
| 4 | `gst_native_producer_win` time limit | **A** | Raise the CTest limit from 20 s to 60 s, and print one progress line per sub-case. |
| 5 | Delayed-GPU subset of M6.4 | **D** | The GPU-busy test is not enough on its own: the subset closes only with GPU hang (driver reset) and device-loss cases, detected through the robustness APIs, with a declared outcome. First establish whether FrameLink handles a context reset. Real resets are injected only when Jon confirms the hosts are free, because a reset affects the displays. |
| 6 | Linux 8c runs | **C** | Hold to the existing rules: remove the frame-0 miss (prime the staging path at start), establish whether the consumer memory steps are growth, then 3 runs per cell judged by the zero-miss rule and 64 kB/min. |


Three smaller questions, decided the same way:

| # | Question | Choice | What it means |
|---|---|---|---|
| 7 | Name `appsink` as holding the last frame in the reference | **B** | Add the note, measured, after checking other common sinks (`glimagesink`, `waylandsink`, `fakesink`, `autovideosink`) on Linux, and on Android where they exist, so it can say which hold the last frame. |
| 8 | Tell the consumer why the producer failed | **D** | Both: the producer sends a final reason code over the session event stream before an explicit failure (the consumer error names it; a crash still shows as peer death), and every consumer peer-failure path reports one stable message and error code. A protocol change on all three platforms, Windows included; its requalification goes back to Jon before merge. |
| 9 | `gst_av_parity` `consumer-pause` and "Could not write to resource." | **C** | Keep the test as it is, and investigate the race: whether the producer should report the session's cancellation before the GIO writer error can surface. |

### Decision 9 follow-up: the race traced (same day)

Jon chose D (trace first), then **C** and **E1**.

**What was traced.** The trace was a temporary in-memory timeline on Android; it was not committed. Logs are retained outside the repository.
- **The mechanism.** The producer's audio sink checks the session for each 10 ms block, and its internal queue writes the block about 0.3–0.6 ms later. When the consumer cancels inside that gap, the write fails on the shut socket: "Could not write to resource."
- **Not bin-specific.** With the gap widened by 2 ms (test-only), the write failed in 7 of 80 results, with element and bin producers. It was the producer's first error in 2 of them. Which error came first was a second race, between the writer thread and the audio and video threads.
- **Rate without the widened gap:** 0 in 480 traced results.
- **Side finding.** Decision 8's consumer path waited up to 100 ms for a producer reason before posting any error. For the consumer's own failure no reason ever comes, so the cancel always reached the producer 101 ms late (every trace).

**What was chosen.**
- **C:** every producer error posted after the consumer closed the session becomes "FrameLink peer closed or failed" (reason `closed`). This covers a socket write into the closed stream and a session found cancelled. The producer's own failures keep their errors.
  - The audio sink's session-ended refusals moved from `STREAM_FORMAT` to `RESOURCE_WRITE`, so they are no longer recorded as invalid input.
- **E1:** the consumer posts its own "frame replaced before it was read" at once, without the wait. Its validation errors keep the wait, because a truncated packet can come from a producer that is failing.

Windows requalification of decisions 8 and 9 goes back to Jon before merge.

Windows items (3, 4 and the Windows side of 5) are changes to test code or test settings on the frozen platform, approved here. Any Windows product change still needs Jon's approval with its evidence.

### Decision 10: the survivor's generic error (same day)

**The problem.** The decision 8 and 9 reruns at `66fa0dbf` failed 8 of 130 runs: in each, the surviving side reported GStreamer's generic "Internal data stream error." instead of "FrameLink peer closed or failed". Linux shared CPU: producer kill 2/5 and consumer kill 1/5, video without a timestamp 4/5; Android native: consumer kill 1/5. The failures stand.

**What a focused verification established** (temporary probes, not committed; evidence retained outside the repository):
- **Silent stops.** Each failure was a FrameLink element returning a flow error without posting its own error, which GStreamer requires. The shared-CPU source and sink posted no error on any of their error returns, and the native sink had six more, among them "channel lost" inside the bin.
- **The producer's end was already visible.** At the consumer's local cancel the producer's close was already pending on the event socket in 14 of 14 sampled runs, but the reader thread had recorded it in only 3. The cancel then stopped the reader, so decision 8's rewrite waited its 100 ms and let the generic error through.
- **Partly verified.** The producer sends its reason before it shuts its event and audio sockets. Whether it always does so before the video ring closes was not established.

**What was chosen (Jon).**
1. Every FrameLink error return posts a FrameLink error first. The shared-CPU sink now refuses invalid video metadata with the native sink's error, "valid supported frame metadata and TIME mapping required", and so sends the invalid-input reason; this removes a recorded shared-CPU limit.
2. Before a consumer element acts on a failure (its local cancel, or the decision 8 rewrite), it takes the records and end of stream already pending on the event socket, without waiting.
3. Decision 8's 100 ms wait stays as a fallback. Whether to remove it is decided later, once the producer's order is verified on all three platforms (with the Windows requalification).

### Decision 11: the Linux BGRA relay stalls and the Vulkan import guidance (2026-11-27)

**The problem.** At `b5c942a` the Linux native relay's BGRA 1080p60 and BGRA 4K30 cells each failed zero-loss on one stall of 19 and 37 ms in 1800 s ([record](linux-native-relay-rate-b5c942a/README.md)). At `0e8da988`, with a cheaper receiver check, all four cells passed once ([record](linux-native-relay-rate-0e8da98/README.md)); the stall's cause was not established. NV12 never stalled. In the same 30–40 s window at BGRA 1080p60 a long-lived Vulkan import of a linear DMA-BUF read zeros while a CPU read and a fresh import were correct, contradicting `framelink_vulkan.h`'s advice to import each slot once per channel generation.

**What was chosen (Jon).**
- **A3:** find the stall's cause now, before the final-revision qualification (9e).
- **B3:** first reproduce the stale import in a short test with a long-lived import, then decide between changing the header's guidance (comment only) and changing `flVkImageCurrent` (a public-header behavior change, which affects Windows and so returns to Jon).

**Outcome (same day, [record](linux-native-relay-d11/README.md)).** A3: the stall is the `b5c942a` test receiver's byte-by-byte payload check (8–16 ms per BGRA 1080p60 frame), not FrameLink, the relay or the driver; `0e8da988`'s faster check leaves margin, and the recorded failures stand. B3: the stale import reproduces at the second slot's first reuse on CPU-written linear rings (BGRA 1080p60 every run, NV12 4K30); the cause is not established. **Jon chose B1:** `framelink_vulkan.h` and `VULKAN.md` now carry the limit and advise a fresh import per frame for such rings on NVIDIA Linux. Comment only; no API or behavior change.

### Decision 12: the scope of M6.5c and M6.6a for 1.0 (2026-11-27)

**The problem.**
- **5c.** Windows native 1080p60 failed its 1800 s run on two final candidates (BGRA on `a50841a`, NV12 on `060c54e4`). Each time the test source's video thread woke 30–45 ms late, outside any FrameLink call, and the catch-up burst replaced a frame in the one-deep queue. On 2026-11-17 the diagnosis was deferred under a host-scheduling assumption ([record](windows-final-duration-060c54e4/README.md)); its four-step plan was not run. Since then `efa86dec` moved harness CSV writes off the timed threads, after shadow-copy snapshots were found to block them for about 100 ms ([investigation](windows-stall-investigation-20261123/README.md)).
- **6a.** Each source frame is to be followed through every boundary, GPU completion and the renderer's presentation submission, with PCM and the A/V offset tracked separately. Windows has renderer submission-identity evidence only ([vkrender `a6a1906`](renderer-integration-a6a1906/README.md): three 5 s NV12 1080p60 captures). Linux and Android have identities through FrameLink's boundaries and GPU completion in the harness CSVs, and no renderer evidence.

**What was chosen (Jon).**
- **A2:** run the deferred diagnosis now, before the Windows requalification: per-frame timing on the source's video thread, a rolling WPR kernel trace saved on a stall, and the standalone pacing probe at 60 Hz, with fixture experiments labelled separately. FrameLink code changes only if the trace points into it; a Windows product change returns to Jon with the evidence. The stage-split script goes into the rate runner on all platforms.
- **B1:** the full 6a scope on all three platforms: identity joined through every FrameLink boundary, GPU completion and vkrender's presentation submission on Windows, Linux and Android, with PCM and the A/V offset tracked separately.

**B1 scope, settled the same day (Jon).** vkrender's Android renderer has no A/V scheduling or audio output, and the Linux host has no desktop session (the RTX 3090 cannot present to a Vulkan headless surface).
- **Linux presentation:** each capture starts its own headless GNOME Shell (Wayland, virtual 1920x1080 monitor) and vkrender presents into it on the RTX 3090. No host change; present submission is the API return, as on Windows.
- **Android:** the desktop A/V scheduler and identity tracing are ported to the Android renderer, and PCM is received through FrameLink and counted and timed there. The A/V offset is measured at presentation against PCM arrival on the same clock. There is no audio device output, recorded as a limit.
- **vkrender:** the renderer changes go on a new vkrender branch and PR, with FrameLink pinned per vkrender's CI convention; nothing merges without Jon.

**Two Linux findings, decided the same day (Jon).**
- **One acquire per render iteration loses frames when the renderer is vsync-paced.** vkrender's A/V input takes at most one FrameLink frame per iteration (its documented acquisition budget). At FIFO into a 60 Hz monitor, two 60 Hz frames in one iteration made FrameLink's one-frame mailbox replace the older: 1 frame lost in 4 BGRA 1080p60 captures (1200 frames). Windows's evidence ran with the display off, so its loop free-ran. **Chosen: drain to the pending cap**: each iteration acquires every available frame into the existing 6-slot pending queue. No ring or memory change, but a vkrender behavior change on Windows too, so the Windows renderer evidence is rerun.
- **NV12 into vkrender on Linux is blocked by the CPU read path (M6.8):** 1.2–2.8 s per 1080p frame from a linear GBM ring in video memory. **Chosen: a GPU NV12 import** in vkrender's Linux input (two planes imported into Vulkan, converted in a shader), so NV12 gets the same evidence as BGRA.
- **Correction and follow-up (Jon, same day).** Draining alone did not stop the loss (3 of 8 runs passed): FrameLink's one-frame mailbox replaced the older frame before the render loop polled. With a 2-frame receive queue as well (test-only), the renderer acquired all 300 frames in 8 of 8 runs. In 3 of those, vkrender discarded the final frame because the session end arrived while it was still pending. **Chosen:** the A/V channel's receive queue becomes 2 and vkrender's A/V ring 13 slots (one more frame of memory per session), and vkrender presents frames still pending at a session end at their due times before ending the generation. Both apply on Windows too; its renderer evidence is rerun.
- **Android A/V input (Jon, same day):** vkrender's Android renderer was deliberately GStreamer-free, while the desktop A/V input receives the session, PCM and clock through GStreamer. **Chosen:** link the GStreamer Android SDK and FrameLink's session statically into the renderer, used only by the A/V input, so the desktop `AvInput`, scheduler and tracing are shared unchanged; the test producer runs inside the renderer APK (same app UID).

**Outcome (same day).**
- **A2, done** ([record](windows-1080p60-wake-d12/README.md)). The Windows 1080p60 failures come from two host mechanisms, neither in FrameLink. In two diagnostic batches of 1800 s runs (12 runs, 5 passed):
  - **Display power changes** stall the NVIDIA driver 50–150 ms behind `flSubmit`: 6 of the 7 failures.
  - **CPU starvation:** a Defender scan burst with foreground load held all 16 logical CPUs above normal priority for about 30 ms, 1 failure. It is also the mechanism of the earlier `060c54e4` and `a50841a` failures.
  - An MMCSS source thread tightened ordinary jitter (worst pacing lateness 0.18–1.47 ms against 2.5–5.0 ms) but met no burst, so it is untested against one.
  - The requalification's host conditions are Jon's, in decision 14.
- **B1, done on all three platforms** ([record](renderer-identity-6a/README.md); vkrender PR #25):
  - **Linux:** BGRA and NV12 1080p60, 10/10 and 5/5.
  - **Android:** NV12 720p60 3/3, BGRA 360p60 2/2. 1080p is limited by the in-APK test producer on that tablet.
  - **Windows:** NV12 1080p60 6/6 at latencies 0 and 100 ms, after a first set lost trace events to capacity (1/6).
  - Each pass covers identity through present submission, with PCM complete and the A/V offset reported.
  - The black BGRA frames, resolved the same day ([record](renderer-identity-6a/README.md#the-black-bgra-frames-resolved)), on Jon's go-ahead: not occasional but one ring slot in every frame (about one in five BGRA frames). Cause: an NVIDIA Linux driver bug, reproduced without FrameLink or vkrender: the second non-dedicated DMA-BUF image import in a Vulkan device reads zeros on the GPU. vkrender `7cf02c9` (PR #25) and FrameLink's `flVkImportImage` now import dedicated; Linux reruns BGRA 10/10 and NV12 5/5 with no black preview. This was also decision 11's stale import for whole-image imports (keep-one-import BGRA 1080p60 3/3). Per-plane NV12 chroma, bound at an offset, still reads zeros in its first frames, so the fresh-import advice stays for it. The earlier "different slots" and "fresh import did not help" statements came from mismatched previews and are corrected in the record.

### Decision 17: what 1.0 includes and how it ships (2026-11-27)

Taken before decisions 13–16, because it decides which of them block the release. Jon chose **A2, B1, C1, D1**:

| Question | Choice | What it means |
|---|---|---|
| Platforms in 1.0 | **A2** | Windows, Linux and Android are all in 1.0. Each advertises only what it has qualified. This keeps the 2026-11-18 order: M6 on Linux and Android before M7. |
| What blocks 1.0 | **B1** | Strict: every M6 item closes on every platform before release. That includes GPU reset and device loss (decision 13), fault coverage on the remaining cells (15), the Android burst headroom (16), M6.2 acceptance, M6.3 lifecycle cycles, M6.8 8c, M6.9 9d/9e and the final-revision qualification (M6.7), and the Windows requalification (14). |
| Release form | **C1** | A source release: a tag, release notes, the capability matrix and the evidence. Users build FrameLink; no prebuilt binaries in 1.0. M7.3's clean-install validation is therefore a clean build and run from source on each platform. |
| vkrender | **D1** | FrameLink alone is 1.0. vkrender stays a separate consumer that pins FrameLink; FrameLink ships its own examples (M7.2). |

Options not chosen: Windows-only 1.0 (A1); Windows and Linux with Android as a preview (A3); advertising only what is qualified with the rest as documented limits (B2); blocking on correctness but not performance extensions (B3); prebuilt binaries (C2) or packages (C3); vkrender as part of the release (D2).

### Decision 13: GPU hang and device-loss testing (2026-11-27)

Decision 5 requires GPU hang (driver reset) and device-loss cases for M6.4's delayed-GPU subset; decision 17 (B1) makes them release-blocking on all three platforms. Jon chose **A3, B2, C2**:

| Question | Choice | What it means |
|---|---|---|
| Real device loss | **A3** | A context reset (a shader that never finishes, reset by the driver's watchdog) on Windows, Linux and Android; a whole-device reset on all three where the driver allows it, and where it does not, that is recorded with evidence; and physical loss on Windows (adapter disabled and re-enabled). |
| Injection | **B2** | A small set of real resets per case on free hosts qualifies the behaviour; test-only injection inside FrameLink (a fence that reports an error, a device that reports removed) covers the same code paths deterministically in ctest. |
| What the application sees | **C2** | A new `FL_DEVICE_LOST` result, so an application can tell "recreate your GPU objects" from "the peer went away" (`FL_DISCONNECTED`). An API addition on every platform; it touches the frozen Windows API and returns to Jon with decision 14. |

Cases, each with a declared outcome (bounded return, no frame delivered as valid after a failed GPU write, a stable error, resources released, a new session working after recovery): the producer's context reset mid-stream; the consumer's context reset while it holds a read lease; a whole-device reset; physical loss (Windows); recovery afterwards. Order: review each platform's fence and device paths and build the test-only injection first, without disturbing hosts; product changes come to Jon with evidence; real resets only when Jon says the hosts are free. Host risk as stated to Jon: a Windows reset blanks the display and may crash other GPU applications; on Linux the only NVIDIA host is the workstation, and whether the driver recovers without the desktop must be established; a Mali job timeout usually recovers.

Options not chosen: context reset plus Windows-only device reset (A1); whole-device reset without physical loss (A2); real resets only (B1); injection only (B3); existing codes only (C1).
