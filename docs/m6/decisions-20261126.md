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

Windows items (3, 4 and the Windows side of 5) are changes to test code or test settings on the frozen platform, approved here. Any Windows product change still needs Jon's approval with its evidence.
