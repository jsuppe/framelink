# M6.8b: CPU-consumer paths for GPU producers on Linux and Android — 2026-11-18

Base revision: `2075e089` (framelink `master`, after M6.9 9a–9c on Linux and Android). This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Where M6.8 stood

M6.8a ([CPU-path investigation](../README.md)) found that a CPU consumer reading a Linux ring directly is slow, because the ring lives in discrete-GPU video memory. Since then, two CPU-efficient paths were built and measured separately:
- **Shared CPU** (`shared-cpu=true`) for CPU producers ([benchmark](../SHARED_CPU_BENCHMARK.md)).
- **Explicit EGL staging** (`framelinksrc gpu-readback=true`) for GPU allocations ([adapter](../EGL_READBACK.md)).

Two gaps remained for a **GPU producer feeding a CPU consumer** in a coordinated A/V session (the M6.9 local native profile with a readback receiver: `CPU_READ`, `COPY|READBACK`):
1. **Linux.** The A/V bin had no way to select the EGL staging adapter, so the readback receiver always read the ring directly.
2. **Android.** The readback receiver had not been measured. It turns out to read uncached memory.

## What changed

- **`framelinkavsrc gpu-readback`** (Linux builds with EGL only; the property is not registered elsewhere, so the Windows and Android element interfaces are unchanged).
  - Default false; there is no automatic selection.
  - It must be set before `set-local-native-candidate`, which freezes it.
  - It is accepted only with the readback candidate. It is refused with a native receiver candidate, with `shared-cpu`, and without a candidate (the state change fails).
  - It forwards to the inner `framelinksrc`. That element's existing checks still apply: the negotiated `COPY|READBACK`, exact BGRA/RGBA, and no fallback.
- **Android rings with `FL_MAP_CPU` prefer `AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN`** (`cpuRingUsage` in `src/framelink_android.cpp`).
  - They used `CPU_READ_RARELY`. `FL_MAP_CPU` is documented as the flag for anything that reads pixels, so the cached read hint matches its contract.
  - If a device reports the cached hint unsupported for a description, the ring keeps the previous hint. This is an allocation hint, not a copy path.
  - Lock usage, pixel layout and the lock/unlock coherency contract are unchanged.
  - This applies to **every** CPU-mappable Android ring, including native receivers' rings. Measured effect below.
- **The comment on `wcCopy`** said Android locks return cached mappings. On this device that is only true when the allocation asks for frequent CPU reads. The comment is corrected.
- **Reference:** `GST_AV_REFERENCE.md` documents `gpu-readback` on both sources and `shared-cpu` on the thin elements. It also corrects two stale notes: M6.9 on Linux/Android, and the old Linux readback advice.

## Test changes (`test/fl_gst_native_av_session.cpp`)

- **Positional pixel pattern.** The pattern is per byte, per plane, per row and per frame. It replaces a uniform per-frame value, so a stale, shifted or channel-swapped byte fails. It is used on both platforms.
- **Linux EGL builds add four checks:**
  - `gpu-readback` controls: native receiver, shared CPU, frozen property, and a missing candidate with and without `shared-cpu`.
  - BGRA through the direct readback receiver.
  - RGBA and BGRA through the GPU-staged readback receiver. Each run is 8 frames with pixels, PCM, PTS mapping and A/V offset checked.
- **Android:** the producer's ring buffers must carry `CPU_READ_OFTEN` wherever the device supports it. Forcing the old hint makes the test fail at that check, which was verified.
- **`--bench WIDTH HEIGHT FORMAT FRAMES cpu|gpu|native`** times **push to pull** for one frame at a time through real bins in a real session. It writes and checks only the first 4 rows of each frame, outside the timed interval. This is a stage cost, not paced throughput or added-hop latency. The `native` mode skips readback and gives the session's own baseline.

Confirmed by trace that the GPU-staged receiver uses the adapter: 6 frames produced 6 `gst.egl_readback`, 6 `gst.gpu_copy_issued` and 6 `gst.gpu_readback_issued` events, with no `gst.cpu_copy_source`.

## Results

### Linux: RTX 3090, NVIDIA 580.178.04, BGRA, 120 frames per run, ABBA order

| Receiver | 1920x1080 p50 / p99 / max (ms) | 3840x2160 p50 / p99 / max (ms) |
|---|---|---|
| Direct readback (existing) | 688.9 / 765.2 / 772.1; 686.8 / 714.6 / 719.6 | **No frame** within the bench's 1 s idle deadline (both runs) |
| GPU-staged (`gpu-readback=true`) | 3.19 / 5.40 / 5.79; 3.09 / 5.00 / 5.73 | 11.45 / 12.86 / 15.59; 11.18 / 12.68 / 16.50 |
| Native receiver (baseline) | 1.71 / 2.01 / 2.02; 1.87 / 2.18 / 2.23 | 1.94 / 2.32 / 2.32; 1.97 / 2.34 / 2.35 |

The first GPU-staged frame costs 72–114 ms (context creation). It is reported and not discarded. The direct path is now slower than the 8a figure, about 12 MB/s here against 34 MB/s then; the cause is not investigated. The 4K direct failure output is in `raw/`. Its line number refers to an earlier revision of the test.

### Android: TCL 8483A (MT6768), Android 15, RGBA, 120 frames per run

| Receiver | 1920x1080 p50 / p99 (ms) | 3840x2160 p50 / p99 (ms) |
|---|---|---|
| Readback, `CPU_READ_RARELY` ring (before) | 33.8 / 41.7; 34.1 / 45.9 | 104.0 / 120.7; 104.1 / 118.3 |
| Readback, `CPU_READ_OFTEN` ring (after) | 20.9 / 25.3; 20.4 / 27.1 | 43.8 / 60.0; 43.0 / 58.7 |
| Native receiver, before | 2.06 / 4.18; 2.24 / 5.02 | 3.04 / 5.03; 2.88 / 4.12 |
| Native receiver, after | 1.70 / 4.18; 1.69 / 4.16 | 1.62 / 7.47; 1.50 / 5.13 |

Probes on the same device (sources and output in `android-probe/`):

| Probe | Result |
|---|---|
| `read_usage`: lock + copy of a 1080p RGBA AHB | `READ_RARELY` 12.6 ms (about 655 MB/s); `READ_OFTEN` 2.05 ms (about 4,040 MB/s). 4K: 51.4 against 7.97 ms. Pixels correct in all cases |
| `alloc_vs_lock`: which usage matters | Only the **allocation** usage matters; lock flags make no difference (12.7 ms against 2.1 ms whatever the lock asks for). Copying into a freshly mapped output adds 5–6.5 ms of page faults |
| `format_support` | RGBA8, RGB10A2 and YUV420 support and allocate with `READ_OFTEN`, with and without `GPU_COLOR_OUTPUT` |

A trace of the readback receiver after the change (1080p, 40 frames) shows the map and copy step at 13.5 ms p50, against about 7.3 ms in the fresh-output probe. That remaining difference is not explained here. Candidates are per-frame output allocation (page faults) and thread placement on this big.LITTLE SoC.

## Regressions

| Platform | Result |
|---|---|
| Linux (Ubuntu 26.04, RTX 3090, GStreamer 1.28.2), full suite `ctest -j4` | 141/142 pass, 1 skipped (`read_lease_egl_delayed_completion`, unsupported control) |
| Android tablet, all 96 CTest-registered Android tests run on the device | 95 pass, 1 skipped (`read_lease_egl_delayed_completion`). `gst_av_bench_smoke` passes when given an on-device `--out .`; the runner passes the host build path ([M6.9 record](../../android-native-av-session/README.md)) |
| Windows (RTX 5090 qualification host), `cmake --preset ci`, full suite | 180/180 pass. The Windows element interface is unchanged: `gpu-readback` is registered only in Linux EGL builds |

**Unexplained intermittent failures, retained.** During development, two of the seven full Linux `ctest -j4` runs of this change each had one failure, in different tests:
- `gst_native_av_linux`: output not captured.
- `gst_av_parity`, `audio-queue-bound` case: one of four element/bin variants reported "coordinated session not ready or cancelled" where the others reported "audio peer closed before GDP EOS".

Neither reproduced afterwards:
- 4 later full `-j4` runs.
- 44 stressed runs of `gst_native_av_linux` (4 and 8 at a time).
- 8 parallel runs of `gst_av_parity`.

Master `2075e089` passed 3/3 full `-j4` runs and 8/8 parallel `gst_av_parity` runs. This does not show that the change causes them or that it does not.

## Not established here

- **8c rate qualification.** The Android readback receiver is still above the frame budgets at both sizes (16.7 ms at 1080p60, 33.3 ms at 4K30). The Linux GPU-staged figures are stage costs; no paced 1800 s A/V run was made.
- Real GPU producers writing cached Android rings. The test producer writes through a CPU lock. Coherency after GPU writes relies on the gralloc lock contract and is untested here.
- Effect of the cached hint on other Android GPUs or gralloc implementations, and on the Android cells qualified earlier. Those will be repeated in the final-revision qualification.
- NV12 GPU staging, which the adapter rejects, and BGRA on Android, which is unsupported.
