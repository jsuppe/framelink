# Android native relay rate cells (M6.9d)

The paced authenticated FAR4 → FAR5 relay harness (`fl_gst_local_native_bench_android`), run on the tablet as three processes: source, relay, final reader. Video is autonomous at 30/60 fps and PCM at 100 Hz. The profile and analyzer (`tools/check_local_native_bench.py`) are the same as the Windows and Linux cells: two slots, one unread frame. Each cell follows the rate plan's order (`tools/run_local_native_rate_cells.py --adb`):
1. a 5 s trace-off preflight;
2. a matched 5 s trace-on run;
3. 1800 s trace-off, only if both earlier gates passed.

Budgets are the whole-path arrival diagnostics: 17 ms at 60 fps and 34 ms at 30 fps, with **zero** misses and zero lost frames. No repeat replaces a failure.

Device: Android 15 (API 35), MediaTek MT8786V/N, ARM Mali-G52 MC2 (OpenGL ES 3.2, r49p1), GStreamer 1.24.13 linked statically.

**The Android harness** uploads through a mapped AHardwareBuffer write. It verifies through FrameLink's CPU map into packed host memory, then completes the read lease on the CPU. On this unified-memory SoC, the `CPU_READ_OFTEN` mapping is cached. Linux needed a Vulkan GPU copy only because CPU reads of a discrete GPU's mapping are uncached. Both the upload and the verification copy are explicit test work, not zero-copy claims; the relay does neither.

**Formats.** AHardwareBuffer has no public BGRA storage: FrameLink stores BGRA8 as RGBA8 and swizzles it in CPU maps. So the Android cells use RGBA, the native layout, and NV12. NV12 CPU maps on this tablet are converting copies, because the buffer's YUV layout is not packed NV12. The trace shows them as `image.cpu_yuv_read` and `image.cpu_yuv_write` in the source and the receiver, never in the relay. They are part of the upload and verification times below.

## Revision `a50f6555` (clean tree)

| Cell | Preflight | Trace on | 1800 s | Arrival p50 / p99 / max (ms) | Relay forward p99 | Resource slopes (max over roles) |
|---|---|---|---|---|---|---|
| RGBA 1080p30 | pass | pass | **pass** | 7.58 / 9.93 / 15.77 | 0.42 ms | memory 1.11 KB/min, fds 0.001/min, threads 0 |
| NV12 1080p30 | pass | pass | **pass** | 8.30 / 10.72 / 17.29 | 0.49 ms | memory 0.94 KB/min, fds 0.001/min, threads 0 |
| RGBA 1080p60 | **FAIL** (frame 1 replaced) | **FAIL** (frames 1–2 late) | not run | | | |
| NV12 1080p60 | **FAIL** (frame 1 replaced) | **FAIL** (frame 1 replaced) | not run | | | |

The resource gates are memory ≤ 64 KB/min, handles ≤ 1/min and threads ≤ 0.5/min, on the steady second half. They pass by wide margins in both 30 fps cells.

CPU per role over the 1800 s runs:

| Cell | Source | Relay | Receiver |
|---|---|---|---|
| RGBA 1080p30 | 35 % | 20 % | 72 % |
| NV12 1080p30 | 38 % | 21 % | 59 % |

Upload and verification per frame, p50 / p99 / max:

| Cell | Source upload | Receiver verification |
|---|---|---|
| RGBA 1080p30 | 3.3 / 4.7 / 9.1 ms | 2.6 / 3.9 / 8.4 ms |
| NV12 1080p30 | 4.9 / 5.8 / 13.2 ms | 5.4 / 7.8 / 17.6 ms |

The relay's forward call is 0.1 ms at p50 and under 0.5 ms at p99.

**Both 60 fps cells fail at startup, not in steady state.** Every 60 fps failure is the same event:
- The receiver takes 28–39 ms between its first two acquisitions.
- The one-frame queue replaces frame 1, and the zero-loss rule fails the run.

The cause is the first use of each of the two pool buffers in each process: the first map and touch of a 1080p buffer costs 9–15 ms. After it, 60 fps fits.

The trace-on RGBA 1080p60 run is the only 60 fps capture that ran to the end:
- Frames 1 and 2 arrived late (20.8 and 17.6 ms).
- Every later frame was within budget: p50 7.48, p99 11.31, max 14.06 ms.

An earlier 5 s NV12 1080p60 run, before the formal cells, showed the same pattern: one late frame at startup, steady p99 11.0 ms. Neither replaces these failures.

## Open, for Jon

- **The 60 fps cells are not qualified on Android.** They fail on the first-touch cost of the pool buffers at startup. A pre-roll that maps each pool buffer once before the paced epoch would very likely let them pass, but it changes the frozen profile. It would apply on every platform and needs a decision. Moving the receiver verification to the GPU (a Vulkan copy, possible for RGBA but not for this tablet's external-format NV12) would also lower the receiver's CPU load.
- **Linux harness startup flake, pre-existing.** NV12 1080p60 on Linux sometimes replaces frame 0 at startup: 2 of 7 runs with this change, and 1 of 8 with the unchanged harness, each time the first run after a rebuild. It is unrelated to Android. It is recorded here because it showed up while checking that the shared harness still works on Linux.

## Files

- `cells-a50f655/summary.json` and `environment.json` (revision, Android release and device, build fingerprint, SoC and GLES renderer, static GStreamer version, harness SHA-256).
- `cells-a50f655/<cell>-<arm>.analysis.json`: the full analyzer output.
- Per capture: `config.json`, the role summaries and any non-empty stderr.
- The raw captures (per-role video, audio and resource CSVs, and the trace-on traces) are 19 MB compressed and kept outside the repository: `captures.tar.xz`, SHA-256 in `captures.tar.xz.sha256`.
