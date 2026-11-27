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
- **Dedicated DMA-BUF imports (`7cf02c9`):** every DMA-BUF import is now a dedicated allocation. Without it, one BGRA ring slot rendered black in every frame (see [the black BGRA frames](#the-black-bgra-frames-resolved)).
- **Android:** the desktop A/V input, linked with the static GStreamer SDK on Jon's choice. PCM goes to `fakesink` (received, counted and timed; no audio output). FrameLink's A/V producer runs in the APK's own `:avproducer` process (same app UID). The debug manifest gains INTERNET, because the session's PCM uses loopback TCP.

## Linux (RTX 3090, driver 580.178.04)

`capture_linux.py`: `framelink_av_producer` (`videotestsrc pattern=ball`, silent audio) into `vkrenderer --wayland`. vkrender presents on the RTX 3090 into a private headless GNOME Shell (virtual 1920x1080@60, FIFO) that the script starts and stops. The shell has its own empty profile: user extensions once stalled frame callbacks for 16.6 s. Turning off both start-in-overview and the welcome dialog throttled the renderer to 10 Hz in 3 of 4 runs, so the shell keeps those defaults. FrameLink at vkrender's pin `0d72a94b`; vkrender at PR #25.

| Cell | Latency | Result | A/V offset at present submission, p50 (max) |
|---|---|---|---|
| BGRA 1080p60, 5 s | 0 ms | **10 of 10 pass** | 30.1–34.1 ms (max 31.5–43.0) |
| NV12 1080p60, 5 s | 0 ms | **5 of 5 pass** | 18.8–34.1 ms (max 25.3–44.1) |

Each pass: 300 of 300 frames at every stage, 300 PCM blocks and 240,000 samples, no late block.

**Rerun with dedicated imports** (vkrender `7cf02c9`, SMPTE pattern; `results/linux-dedicated/`):

| Cell | Latency | Result | A/V offset p50 (max) |
|---|---|---|---|
| BGRA 1080p60, 5 s | 0 ms | **10 of 10 pass** | 33.8–35.7 ms (max 37.6–44.0) |
| NV12 1080p60, 5 s | 0 ms | **5 of 5 pass** | 29.8–34.8 ms (max 33.1–41.0) |

Pixels, in 15 separate captures with a scene preview every 150 ms: no black faces in 111 distinct BGRA previews and 57 distinct NV12 previews. Before the fix, about one in five BGRA frames was black. Those 15 captures fail identity: the renderer acquired 290–299 of 300 frames. Each preview read waits for the GPU inside the render loop; the same build without previews passed 15 of 15 above, so the loss comes with the previews, not the fix. Exactly where those frames are lost was not traced. The identity cells take no previews, as in the first set.

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

## Windows (RTX 5090)

The `a6a1906` case rerun with the PR #25 renderer (`f76d1c6`, FrameLink `0d72a94b`, built on the test host; `windows/d12-vkr.ps1`): native NV12 1080p60 A/V into `vkrenderer --serve`, 5 s, latency 0 and 100 ms, three captures each, the same `probe.py`. Launched over ssh; the display was off, so FIFO presentation did not pace the loop (about 48 present submissions per frame).

| Set | Trace capacity per thread | Result |
|---|---|---|
| 1 | renderer 524,288, transport 262,144 (the `a6a1906` probe's) | **1 of 6 pass.** The other five lost 492–33,682 trace events to capacity, so their evidence is incomplete and they fail. In each, the renderer decided 300 of 300 frames due with no late drop, and PCM was complete. |
| 2 | 4,194,304 requested | **0 of 6, void.** Above the recorder's 1,048,576 limit, so it fell back to 4,096 events. My error. |
| 3 | 1,048,576 (the limit) for both | **6 of 6 pass** |

Set 3, every frame matched through present submission, PCM complete:

| Latency | Submissions matched | A/V offset at present submission, p50 (max) |
|---|---|---|
| 0 ms | 14,307–14,415 | 5.08–5.16 ms (7.79–8.42) |
| 100 ms | 14,167–14,400 | 3.69–3.84 ms (5.45–6.21) |

The earlier `a6a1906` captures are in the same range, per-frame minimum to maximum: 4.2–7.6 ms at 0 ms, 3.4–6.2 ms at 100 ms. The set 1 pass gave 5.29 ms p50. This renderer's loop ran at about 48 present submissions per frame, against 36–37 in `a6a1906`; its trace no longer fits the old capacity over 5 s. Which events grew is not broken down here.

## The black BGRA frames (resolved)

**Cause: an NVIDIA Linux driver bug in non-dedicated DMA-BUF imports.** On driver 580.178.04 (RTX 3090), the second DMA-BUF image imported into a Vulkan device without `VkMemoryDedicatedAllocateInfo` reads zeros on the GPU for its whole life, while the buffer holds the frame. The driver reports no dedicated-allocation preference for these linear images, so vkrender (and `framelink_vulkan.h`) did not ask for one. In vkrender's normal import order the second import is ring slot 1, so every frame on that slot rendered black: about one in five BGRA frames, not "occasional".

How it was found (a diagnostic build, not committed; the patch is hashed in `results/raw-sha256.json`):
- **The CPU always saw the frame.** At each adoption the renderer read a 16×9 pixel grid from the buffer's own mapping: correct in every frame and slot.
- **The GPU read zeros through one import.** A GPU copy of the imported image at adoption was black for the affected import in 11 of 11 frames; for every other slot it matched the CPU grid exactly (41 of 41 frames). A per-frame read of the rendered scene agreed: in normal order, slot 1 was black in every frame drawn from it (62 of 62, 71 of 71, 44 of 44 in three captures), every other slot never.
- **It follows the import, not the buffer.** Importing the ring in reverse order moved the black to whichever buffer was imported second; an extra, never-sampled import of the first buffer, made second, left every real slot correct.
- **Not visible in the API.** The validation layer reported nothing; the second import's Vulkan calls (API dump) and system calls (strace) are identical to the others' apart from handles and descriptor numbers.
- **Standalone reproduction** (`import-repro/import_repro.cpp`, no FrameLink or vkrender code): GBM linear ARGB8888 buffers written on the CPU, imported as vkrender does, read back on the GPU. The second import reads zeros in every round, for 2, 7 or 12 buffers, either write order and one or separate GBM devices. A dedicated import of every buffer reads correctly (`import-repro/repro-runs.log`).

**Fix.** vkrender `7cf02c9` imports every DMA-BUF dedicated (above). FrameLink's `flVkImportImage` now does the same whenever valid (`framelink_vulkan.h`, [VULKAN.md](../../VULKAN.md)).

**Decision 11's stale import was the same bug** for whole-image imports. In the relay harness's keep-one-import mode (`FL_BENCH_KEEP_IMPORT=1`), which went stale in every decision 11 run, BGRA 1080p60 passed 3 of 3 runs (5 s, 5 s traced, 60 s) with no wrong read; a control built with the old rule read zeros at slot 1 again and failed its 60 s run. **Not resolved for per-plane NV12:** there the chroma image binds at an offset and cannot be dedicated. At NV12 4K30 both runs passed, but the chroma read zeros past its first 64 KiB in the first one to three frames of each capture, then read correctly; a fresh import read it correctly each time. The per-plane fresh-import advice stays. (`results/linux-dedicated/d11-keep-import.json`.)

**Corrections to this record's earlier text.** It said the black frames "came from different ring slots (4, 4 and 1)", that they were "1–3 of about 20 per capture", and that a fresh import per frame "did not change the rate". The scene preview endpoint returns the previous render and refreshes at most every 400 ms, so each preview had been matched to the wrong frame. Matched correctly, every black preview was one slot. The black-preview counts were only how often a preview happened to land on that slot. The fresh-import result was judged by the same previews and is not re-established.

**Not established:** why the driver does this; whether other drivers or GPUs do; why per-plane chroma recovers after the first frames.

## Files

- `capture_linux.py`, `capture_android.py`, `windows/d12-vkr.ps1` and `windows/d12-vkr2.ps1` (set 3): the capture drivers; `windows/build-and-first-set.log`.
- `results/linux/`, `results/android/`, `results/windows/`, `results/linux-dedicated/`: the checker's result for every capture above (trace footers omitted); `results/linux-dedicated/pixels.json` (preview pixel check) and `d11-keep-import.json`.
- `import-repro/`: the standalone driver reproduction and its runs.
- `results/raw-sha256.json`: hashes of the raw traces and logs (including the diagnostic runs and the vkrender diagnostic patch), which are kept outside the repository.
