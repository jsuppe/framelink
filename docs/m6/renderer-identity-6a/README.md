# Frame identity through presentation on Linux and Android (6a, decision 12 B1) — 2026-11-27

This record is dated; current status is in [STATUS.md](../../STATUS.md). Jon chose B1 in [decision 12](../decisions-20261126.md): the full 6a scope on all three platforms. Each source frame is followed through every FrameLink boundary, GPU completion and the renderer's presentation submission, with PCM and the A/V offset tracked separately.

**What a pass means** (`tools/check_renderer_identity.py`, 11 negative controls in ctest):
- Every trace file is complete (no lost events).
- The producer submitted exactly N frames and the renderer acquired exactly those, keyed by (channel, fence, PTS).
- Each of adopt, render submission, GPU completion and present submission (`VK_SUCCESS` or `VK_SUBOPTIMAL_KHR`) covers all N. Render, completion and present carry the same (frame, submission) pairs.
- Causal order holds per frame, and there are no renderer drops.
- **PCM:** every block and sample was played, none late.
- **A/V offset:** each frame's first present submission minus the audio clock's time for its PTS (`playback_base + latency + PTS`, from the renderer's playback-clock record). This is software submission timing, not scanout or acoustic timing.

The checker reproduces the three Windows `a6a1906` passes, including their 11,025 matched submission serials.

## Renderer changes this needed (vkrender PR #25, each decided by Jon)

- **Linux identity tracing:** the Windows service's input events in the Linux compositor, with sampled identities handed to the shared scene host.
- **A/V input, all platforms:**
  - **Frame loss at a vsync-paced loop.** At FIFO into a 60 Hz display, the loop took one frame per iteration, and FrameLink's one-frame mailbox replaced the older one (5 of 8 Linux captures lost 1–17 frames). Draining alone did not help: 3 of 8 captures passed. Now each poll drains into the 6-slot pending queue, and the A/V channel's receive queue is 2 frames, so the ring is 13 slots.
  - **End of stream.** Frames still pending at a session end are now presented at their due times instead of discarded.
- **Linux NV12:** imported on the GPU as a two-plane DMA-BUF. The CPU path read a linear GBM ring in video memory at 1.2–2.8 s per 1080p frame (M6.8).
- **Android:** the desktop A/V input, linked with the static GStreamer SDK on Jon's choice. PCM goes to `fakesink` (received, counted and timed; no audio output). FrameLink's A/V producer runs in the APK's own `:avproducer` process (same app UID). The debug manifest gains INTERNET, because the session's PCM uses loopback TCP.

## Linux (RTX 3090, driver 580.178.04)

`capture_linux.py`: `framelink_av_producer` (`videotestsrc pattern=ball`, silent audio) into `vkrenderer --wayland`. vkrender presents on the RTX 3090 into a private headless GNOME Shell (virtual 1920x1080@60, FIFO) that the script starts and stops. The shell has its own empty profile: user extensions once stalled frame callbacks for 16.6 s. Turning off both start-in-overview and the welcome dialog throttled the renderer to 10 Hz in 3 of 4 runs, so the shell keeps those defaults. FrameLink at vkrender's pin `0d72a94b`; vkrender at PR #25.

| Cell | Latency | Result | A/V offset at present submission, p50 (max) |
|---|---|---|---|
| BGRA 1080p60, 5 s | 0 ms | **10 of 10 pass** | 30.1–34.1 ms (max 31.5–43.0) |
| NV12 1080p60, 5 s | 0 ms | **5 of 5 pass** | 18.8–34.1 ms (max 25.3–44.1) |

Each pass: 300 of 300 frames at every stage, 300 PCM blocks and 240,000 samples, no late block.

## Android (tablet 8483A, Mali-G52 MC2, Android 15)

`capture_android.py`: the renderer Activity with the A/V session extras, then the in-APK producer service. The renderer finishes at the session end; logs and traces are pulled with `run-as`. Latency 100 ms (at 0 ms the PCM missed its playout budget on this device).

| Cell, 5 s | Result | A/V offset p50 (max) |
|---|---|---|
| NV12 1280x720 60 fps | **3 of 3 pass** | 19.4–22.7 ms (max 32.2–38.0) |
| BGRA 640x360 60 fps | **2 of 2 pass** | 20.0–21.3 ms (max 32.2–33.1) |
| NV12 640x360 30 fps, 640x360 60 fps, 1280x720 30 fps | 1 of 1 each, pass | 9.4–11.8 ms |
| NV12 1920x1080 30 fps | **fail**, 2 of 2 | |
| BGRA 1280x720 60 fps | **fail**, 2 of 2 | |

**The failures are the test producer's throughput on this device.** At 1080p30 the in-APK synthetic producer delivered a frame every 35.7 ms against 33.3 ms. Its PCM fell 169 ms behind, and the playback renegotiated ("audio playout budget exhausted"). FrameLink's own producer calls were about 5 ms per frame of that; the rest is the GStreamer test source filling frames on the CPU. The renderer side adopted and presented every frame it received. 1080p on this tablet needs a faster producer (a decoder or GPU source), not a FrameLink or vkrender change.

## Windows

Pending: the `a6a1906` case (native NV12 1080p60 A/V into `vkrenderer --serve`, 5 s, latency 0 and 100 ms, three each), rerun with the PR #25 renderer (`windows/d12-vkr.ps1`).

## Open finding: black video frames in vkrender on Linux (BGRA)

In BGRA captures with a colour pattern, a few scene previews show black cube faces: 1–3 of about 20 per capture. NV12 showed none in about 200 previews. Identity passes regardless; this concerns pixels. Established so far:
- **Not one stale slot.** Decision 11's stale import would black out every use of that slot. Here the black frames came from different ring slots (4, 4 and 1 in one capture; none in the next). They were mostly early in the stream, but mid-stream in other captures.
- **A fresh import per frame**, as `framelink_vulkan.h` now advises, did not change the rate (1–3 of about 20), so it was not kept.
- **CPU writes are synchronized.** FrameLink brackets them with `DMA_BUF_IOCTL_SYNC` start and end.
- The cause is not established.

## Files

- `capture_linux.py`, `capture_android.py`, `windows/d12-vkr.ps1`: the capture drivers.
- `results/linux/`, `results/android/`: the checker's result for every capture above (trace footers omitted).
- `raw-sha256.json`: hashes of the raw traces and logs, which are kept outside the repository.
