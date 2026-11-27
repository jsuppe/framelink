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
