# M6.8c on Linux: paced GPU-staged readback receiver — 2026-11-25

Base revision: `4f40c517`. Ubuntu 26.04, NVIDIA GeForce RTX 3090, driver 580.178.04, GStreamer 1.28.2. SHA-256 prefixes: `fl_gst_av_bench` `57973f875c52c4cd`, `libgstframelink.so` `f9d0d96cc0ea1f4b`. This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Question

The [M6.8b record](../cpu-consumer-20261118/README.md) measured the Linux GPU-staged receiver (`framelinkavsrc gpu-readback=true`) only as stage costs: "no paced 1800 s A/V run was made". The Linux shared-CPU path already has its 1800 s cells. The open case is a GPU producer writing DMA-BUF ring slots, read by a CPU receiver through GPU staging, at 4K30 and 1080p60 for 1800 s with audio.

## Harness

`fl_gst_av_bench --gpu-readback`, added in `4f40c517`. It applies to bin mode, BGRA/RGBA only, and is not combined with shared-CPU, native, cycles or stall injection.

- **Producer.** An appsrc with `video/x-raw(memory:DMABuf)` feeds `framelinkavsink native-memory=true`. Both bins select the M6.9 local native candidate with a readback policy: path 0 is GPU sample, path 1 is CPU read. The producer thread:
  - takes the sink's own pool through the allocation query (`external-sync-fd-before-submit-v1`);
  - paces on the pipeline clock;
  - writes the frame index and four content rows through the DMA-BUF (`DMA_BUF_IOCTL_SYNC`);
  - signals an eventfd with `set-native-ready-fd`.

  If the pool acquire fails within the sink's `buffer-wait-ms`, the producer counts a *slot drop* and continues. It stands in for a GPU renderer: it writes only four rows, so it does not load the GPU.
- **Consumer.** `framelinkavsrc gpu-readback=true` delivers `memory:SystemMemory` buffers. A probe checks each frame's index and rows in order, and records mismatches, gaps and missing frames.
- **Completion** additionally requires every frame checked, no content mismatch, and system memory on the output.
- **CI:** `gst_av_bench_gpu_readback`, 6 s at 160x90, label `gpu`. It skips (77) where EGL staging is not built.

The profile is the primary zero-delay one, as in the [Android CPU receiver cells](../android-cpu-receiver-20261118/README.md): pool 2, video queue 1, output queue 1, latency 0, transport-only, no output preroll. `run_cells.sh` reproduces the cells.

## Two startup defects and their fix

**1. A slot drop at start.** The receiver created its EGL staging context lazily, on the first frame. This took about 100 ms (frame 0 arrived 88–106 ms late). Meanwhile the producer filled both ring slots and its 5 ms acquire wait expired.

| Unfixed build, 4K30, 22 s | Frame 0 / 1 (ms) | Slot drops (frames) | Complete |
|---|---|---|---|
| run A | 106.0 / 76.6 | 2 (2, 3) | no |
| run B | 88.0 / 58.5 | 1 (2) | no |

**2. The first fix exposed a one-time stall at 15.50 s.** The first fix opened the context at `srcStart` with `FrameLinkEglReadback::prepare()`. The startup drop went away. But every run then had one 40–50 ms readback stall at 15.50 s after the first readback. At 1080p60 that stall cost a slot drop (frame 932).

| Prepare at start, no release | Over budget | Slot drops |
|---|---|---|
| 4K30, 22 s | f465 @ 15.50 s, 52.0 ms | 0 |
| 4K30, 120 s | f0 36.3 ms; f465 @ 15.50 s, 53.2 ms | 0 |
| 1080p60, 60 s | f0, f1; f930–931 @ 15.50 s, 49.7 / 37.4 ms | 1 (932) |

Diagnosis:
- **The trigger is time.** The stall comes at 15.50 s after the first readback at 20, 30 and 60 fps, once per process; the 120 s run had none after it. The single-process stage bench (`fl_gst_native_av_linux --bench`) shows it at 15.498 s.
- **Not the shader cache or threaded optimization.** `__GL_SHADER_DISK_CACHE=0` and `__GL_THREADED_OPTIMIZATIONS=0` had no effect.
- **Not the driver alone.** A driver-only probe (`egl_stall_probe.cpp`: GBM linear slots, EGL dma-buf import, draw, `glReadPixels` + `glFinish`, with or without producer writes and context toggling) did not reproduce it.
- **The thread exit matches.** Under strace, the only thread exit mid-run comes 15.47 s after the first readback. It is the GLib pool thread that ran `srcStart` and opened the render node. With the lazy build, the stall is absent.

Conclusion: GLib reaps an idle pool thread after about 15 s, and the NVIDIA driver then tears down that thread's EGL state, stalling the reader. The fix calls `eglReleaseThread()` straight after `prepare()` when the calling thread had no current context.

A second attempt moved `prepare()` to the streaming thread's first `srcCreate`. It failed fatally: "coordinated video frame replaced before it was read" (every frame dropped, 656/656). It was reverted.

| Final build, 22 s | Over budget | Slot drops | Complete |
|---|---|---|---|
| 4K30 run A | f0 34.3 ms | 0 | yes |
| 4K30 run B | f0 35.8 ms | 0 | yes |
| 1080p60 | f0 32.9, f1 17.8 ms | 0 | yes |

## Results: 1800 s cells (final build)

| Cell | Complete | Frames checked | Content mismatches / gaps | Slot / ring drops | Over budget | Arrival p50 / p95 / p99 (ms) | Max all / after warmup (ms) | CPU producer / consumer |
|---|---|---|---|---|---|---|---|---|
| BGRA 4K30, budget 34 ms | **yes** | 54000/54000 | 0 / 0 | 0 / 0 | 1 (frame 0, 35.48) | 15.43 / 17.08 / 17.42 | 35.48 / 22.13 | 3.3% / 43.7% |
| BGRA 1080p60, budget 17 ms | **yes** | 108000/108000 | 0 / 0 | 0 / 0 | 1 (frame 0, 31.91) | 7.84 / 9.90 / 10.41 | 31.91 / 14.20 | 4.6% / 40.1% |

In both cells:
- **Audio:** contiguous, with no misses (180000 blocks each).
- **Shutdown:** EOS was reached and queue accounting was complete.
- **Delivery:** the sink received and submitted every frame.
- **The only miss is frame 0.** At 1080p60 frame 1 is 16.71 ms, just under budget. After it, the worst frames are six scattered ones at 13.6–14.2 ms (47 s to 1583 s). At 4K30 no frame after frame 0 exceeds 80% of the budget.
- **Handles and threads are flat.** Consumer handles: 44–46 at 4K30, 44–45 at 1080p60. Threads: 10 in steady state.
- **Consumer RSS grows in steps, not steadily.** The fitted slope over 60–1790 s is 55 kB/min at 4K30 (range 183984–186116 kB) and 110 kB/min at 1080p60 (136992–141708 kB). Most of that comes from a few 1–3.5 MB steps (4K30: 1067 s; 1080p60: 534 s and 1627 s); between steps the change is a few kB per two minutes. The summary's whole-run slopes (65 and 227 kB/min) include the teardown sample. Whether the steps keep coming over longer runs is not established.
- **Producer RSS slope:** 3 kB/min.

Arrival p50 is about 15 ms at 4K and is set by the readback. Under this light, paced load the GPU stays at P8 (405 MHz memory). A 4K readback then takes about 13 ms; back-to-back it takes about 5.5 ms. The consumer's 40–44% CPU is that readback and the copy out.

## Regressions

| Platform | Result |
|---|---|
| Linux, full suite on `4f40c517` | 188/188 (the known skip `read_lease_egl_delayed_completion` included); the readback tests 9/9; the new `gst_av_bench_gpu_readback` passes |
| Windows | Not run on the qualification host. The changes are in the Linux EGL staging path and the bench's `FL_BENCH_GPU_READBACK` build (neither is built on Windows); CI builds and tests Windows |

`4f40c517` also fixes the stage bench `fl_gst_native_av_session --bench`: its PCM check compared a `memset` byte against an int, which failed any run longer than 224 frames.

## Not established here

- **Repeatability:** one run per cell.
- **NV12:** the staging adapter handles BGRA/RGBA only, so NV12 CPU-receiver cells on Linux remain shared-CPU only.
- **A real GPU producer:** the producer writes four rows from the CPU and adds no render load; a loaded GPU changes clocks and contention.
- **Other GPUs and drivers:** one host only. The 15.5 s stall is attributed to NVIDIA teardown of a reaped thread's EGL state; other drivers are not checked.
- **Frame 0:** it still misses (32–36 ms) after preparation; what remains of its cost was not traced.
- **The cause of the RSS steps.**

## Files

- `run_cells.sh`: the two cells.
- `*-1800.summary.txt`: the cell summaries.
- `diag-*.summary.txt`: the diagnostic runs above:
  - `lazyA`/`lazyB`: unfixed;
  - `control`/`p120`/`pilot-1080p60`: prepare without release;
  - `strA`: prepare on the streaming thread;
  - `relA`/`relB`/`rel1080`: final.
- `egl_stall_probe.cpp`: the driver-only probe.
- `raw-sha256.json`: SHA-256 of the raw CSVs, logs and summaries, which are retained outside the repository.
