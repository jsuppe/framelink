# M6 qualification matrix and numeric budgets

M6.1 defines what M6 qualifies, how it is measured, and the numeric limits that
M6.2–M6.7 must meet. The limits are derived mechanically from measured
baselines (rule below); they are not targets chosen in advance. Anything outside
this matrix is not qualified by M6, whatever earlier milestones showed.

## Supported targets

| Key | Machine | GPU / memory path | OS, GStreamer |
|---|---|---|---|
| `linux` | Intel i9-10900K | NVIDIA RTX 3090 (driver 580.178.04), GBM linear rings in video memory; native = DMABuf | Ubuntu, kernel 7.0, GStreamer 1.24.2 |
| `windows` | AMD Ryzen 7 9800X3D | NVIDIA RTX 5090, D3D11 shared textures; native = D3D11Memory | Windows 11 Home, GStreamer 1.28.0 (MSVC) |
| `android` | TCL 8483A tablet, MediaTek MT8786 (arm64-v8a) | AHardwareBuffer rings; native = FrameLinkAHardwareBuffer | Android 15, static GStreamer (NDK 28.2) |

Android x86_64 is build-only (no device). Pi/Cuttlefish runtime, other GPUs and
integrated-GPU Linux are not in the matrix.

## Target and workload cells

FrameLink's target is low-overhead frame transport between processes at
**1080p60 and 4K30**. Those sizes are the qualification cells
("requirement" set). The smaller cells ("baseline" set) are functional and
reference cells: they cover integration forms, formats and paths that cannot
yet reach the target sizes, and they are where the tracing and element-form
comparisons are made.

Every cell is one live A/V session through the public elements: producer
`videotestsrc is-live=true` + `audiotestsrc is-live=true` (S16LE stereo 48 kHz,
10 ms buffers) into the producer bin or the individual sinks; consumer bin or
individual sources into `queue ! appsink sync=true` per track; session latency
150 ms; producer and consumer in separate processes. The consumer ring holds
ceil(latency × fps) + 2 images (at least 8: 11 at 60 fps), and the queue in
front of each appsink has no byte limit (a 4K frame is 33 MB) and a 1 s time
bound.

### Requirement cells

| Cell | Video | Consumer memory | Platforms |
|---|---|---|---|
| `1080p60-bgra-native`, `1080p60-nv12-native` | 1920x1080 @ 60, BGRA / NV12 | native | Linux, Windows (Android native output supports RGBA layouts only) |
| `1080p60-bgra-cpu`, `1080p60-nv12-cpu` | 1920x1080 @ 60, BGRA / NV12 | CPU | all |
| `4k30-bgra-native`, `4k30-nv12-native` | 3840x2160 @ 30, BGRA / NV12 | native | Linux, Windows |
| `4k30-bgra-cpu`, `4k30-nv12-cpu` | 3840x2160 @ 30, BGRA / NV12 | CPU | all |
| `1080p60-rgba-native`, `4k30-rgba-native` | RGBA, as above | native | Android |
| `src-*` | the producer's test sources alone into fakesinks | — | all (reference: source cost, not budgeted) |

All requirement cells use the bins. The producer copies every frame into the
ring in every cell (native producer allocations in sessions are M6.9), so the
producer CPU in these cells is transport copy plus test-source cost; the
`src-*` cells give the source cost alone.

### Functional and reference cells (30 fps)

| Cell | Video | Integration | Consumer memory | Purpose |
|---|---|---|---|---|
| `ref` | RGBA 320x180 | bins | CPU (system memory) | reference cell for comparisons |
| `element` | RGBA 320x180 | individual elements + session context | CPU | second integration form |
| `nv12` | NV12 320x180 | bins | CPU | planar format |
| `native` | RGBA 320x180 | bins | native (`native-memory=true`) | zero-copy consumer |
| `cpu-640` | RGBA 640x360 | bins | CPU | CPU readback envelope |
| `native-720p` | RGBA 1280x720 | bins | native | resolution envelope |
| `cpu-720p` | RGBA 1280x720 | bins | CPU | CPU readback envelope |
| `ref-traced` | as `ref` | bins | CPU | tracing overhead (both recorders, 1M events) |
| `ref-160` | RGBA 160x90 | bins | CPU | Android documented production size (Android only) |

Not in the matrix: other frame rates and sizes, RGB10A2 sessions, native
producer allocations in sessions (M6.9), multiple concurrent sessions, decoded
file sources (`clocksync` pacing), physical display/speaker output.

## Measurements

`fl_gst_av_bench` ([source](../../test/fl_gst_av_bench.cpp)) runs one cell;
`tools/m6_bench_matrix.py` runs a set of cells (desktop or `--adb`), interleaving
repeats; `tools/m6_bench_report.py` aggregates and checks budgets. The first 2 s
of each run are warm-up and excluded from distributions and growth.

| Metric | Definition |
|---|---|
| Arrival lateness | Consumer pipeline running time when a buffer enters the queue after the FrameLink source, minus its PTS. PTS is the producer's capture running time on the shared session timeline, so this is capture-to-consumer transport latency (copies, IPC, scheduling). |
| Deadline misses | Buffers whose arrival lateness exceeds the session latency (150 ms): they could not be presented on time. Counted over the whole run including warm-up. |
| Render lateness / jitter | Running time at the synchronizing appsink's `new-sample` minus PTS (nominally the session latency). Jitter is its p99 − p1. |
| A/V offset | Per one-second window, median video render lateness minus median audio render lateness; reported as the largest absolute window value. The session preserves relative PTS exactly (M5b timeline tests), so this is sink scheduling skew between tracks. |
| CPU | Process CPU time per wall time (100 % = one core), producer and consumer separately, steady state. |
| Memory growth | Least-squares slope of resident set size over the second half of steady state, per process (KB/min). |
| Handles / threads | Spread (max − min) of open handles/fds and threads in steady state, per process. |
| Completion | Both processes reach EOS without error and every pushed frame and audio sample is delivered. |
| GPU busy | Linux: device-wide NVIDIA SM load above an 8 s idle reference taken before the cell (GBM/DMABuf rings are not attributed to a process by the driver). Windows: per process, the busiest GPU engine type (Task Manager convention) from the `GPU Engine` performance counters, the larger of producer and consumer. Android: GPU active time of the benchmark's uid (shell, 2000) from `dumpsys gpu`, per wall time. |
| GPU device counters | Linux and Windows (`nvidia-smi dmon`): board power, PCIe receive + transmit throughput and framebuffer use, each above the idle reference, and core clock. Device-wide, so other GPU users on the machine are included; power is approximate because the idle reference can follow a warm GPU. |
| GPU memory per process | Windows only: dedicated GPU memory per process (`GPU Process Memory` counters). Shared ring textures appear in both processes. Not available on Linux (driver attribution) or Android (`dumpsys gpu` does not attribute AHardwareBuffer rings to the benchmark processes). |

These are software measurements on the pipeline clock. Display scanout,
compositor and speaker latency come after the appsinks and are not measured.

## Budget rule

For each platform and cell, from the baseline (3 interleaved rounds of 60 s):

    limit = min(cap, max(scale x baseline, baseline + floor))

with the worst of the baseline rounds as the baseline for every metric (the
Linux CPU path is bimodal between rounds, so a median round would put the slow
mode over budget).

| Metric | Scale | Floor | Cap |
|---|---|---|---|
| Video/audio arrival p99 | 2 | +5 ms | 75 ms (half the session latency) |
| Video/audio arrival max | 2 | +10 ms | 75 ms |
| Video/audio render jitter (p1–p99) | 2 | +1 ms | — |
| A/V offset, max absolute window | 2 | +1 ms | 5 ms |
| Producer/consumer CPU | 1.5 | +2 points | — |
| GPU busy | 1.5 | +2 points | — |
| Handle / thread spread per process | 2 | +8 / +4 | — |

Fixed for every cell: completion in every round and 0 deadline misses. Growth
limits per process, checked only on runs of at least 10 minutes (a one-minute
run cannot separate warm-up from growth): memory ≤ 64 KB/min (traced cells
exempt: the recorder fills its preallocated capacity), handles ≤ 1/min,
threads ≤ 0.5/min. GPU device counters (power, PCIe, framebuffer, clock) are
reported, not budgeted: they are device-wide.

The derived limits are in [budgets.json](budgets.json); regenerate with

    tools/m6_bench_report.py docs/m6/baseline/{linux,windows,android}{,-requirement}.json --derive docs/m6/budgets.json \
      --unsupported "linux:cpu-640=..." --unsupported "linux:cpu-720p=..."

(the exact reasons are recorded in budgets.json).

A cell whose baseline did not complete every round is either classified
unsupported (with the reason) or rerun; the derivation refuses incomplete cells.

## Requirement baseline: 1080p60 and 4K30 (2026-11-03)

3 interleaved rounds of 60 s per cell; raw results in
[baseline/](baseline/) (`*-requirement.json`). Columns as in the functional
baseline below. `src-*` rows give the test sources' CPU alone.

### linux-i9-10900k-rtx3090

| Cell | Config | OK | V arr p50 | V arr p99 | V arr max | A arr p99 | V jit | A jit | |A/V| max | Late | C CPU% | P CPU% | Mem KB/min | dH | dT | GPU busy% | GPU +W | PCIe MB/s | GPU +MB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1080p60-bgra-native | BGRA 1920x1080@60 bin native 60s | 3/3 | 6.16 | 12.22 | 14.59 | 0.42 | 0.16 | 0.14 | 0.08 | 0 | 1.6 | 37.3 | 6 | 1 | 1 | 0.0 | 4.1 | 624 | 88 |
| 1080p60-bgra-cpu | BGRA 1920x1080@60 bin cpu 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: Internal data stream error.; error: coordinated video frame replaced before it was read: the producer delivered faster than real time; error: metadata fr |
| 1080p60-nv12-native | NV12 1920x1080@60 bin native 60s | 3/3 | 5.46 | 7.56 | 9.68 | 0.52 | 0.16 | 0.16 | 0.04 | 0 | 3.5 | 27.5 | 6 | 1 | 1 | 0.0 | 1.8 | 248 | 44 |
| 1080p60-nv12-cpu | NV12 1920x1080@60 bin cpu 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: Internal data stream error.; error: coordinated video frame replaced before it was read: the producer delivered faster than real time; error: metadata fr |
| 4k30-bgra-native | BGRA 3840x2160@30 bin native 60s | 3/3 | 16.86 | 26.32 | 40.58 | 0.37 | 0.09 | 0.17 | 0.04 | 0 | 1.0 | 57.4 | 8 | 1 | 1 | 0.0 | 5.3 | 1218 | 256 |
| 4k30-bgra-cpu | BGRA 3840x2160@30 bin cpu 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: Internal data stream error.; error: audio peer closed before GDP EOS |
| 4k30-nv12-native | NV12 3840x2160@30 bin native 60s | 3/3 | 10.70 | 19.80 | 20.78 | 0.47 | 0.16 | 0.16 | 0.07 | 0 | 2.1 | 38.5 | 9 | 1 | 2 | 0.0 | 3.0 | 470 | 96 |
| 4k30-nv12-cpu | NV12 3840x2160@30 bin cpu 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: Internal data stream error.; error: coordinated video frame replaced before it was read: the producer delivered faster than real time |
| src-1080p60-bgra | BGRA 1920x1080@60 source only 60s | 3/3 | source alone: 11.5% CPU | | | | | | | | | | | | | | | | |
| src-1080p60-nv12 | NV12 1920x1080@60 source only 60s | 3/3 | source alone: 4.9% CPU | | | | | | | | | | | | | | | | |
| src-4k30-bgra | BGRA 3840x2160@30 source only 60s | 3/3 | source alone: 22.8% CPU | | | | | | | | | | | | | | | | |
| src-4k30-nv12 | NV12 3840x2160@30 source only 60s | 3/3 | source alone: 10.4% CPU | | | | | | | | | | | | | | | | |

### windows-9800x3d-rtx5090

| Cell | Config | OK | V arr p50 | V arr p99 | V arr max | A arr p99 | V jit | A jit | |A/V| max | Late | C CPU% | P CPU% | Mem KB/min | dH | dT | GPU busy% | GPU +W | PCIe MB/s | GPU +MB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1080p60-bgra-native | BGRA 1920x1080@60 bin native 60s | 3/3 | 4.44 | 6.31 | 9.31 | 2.03 | 1.91 | 1.94 | 0.43 | 0 | 6.8 | 9.6 | 150 | 48 | 6 | 13.7 | 14.4 | 571 | 125 |
| 1080p60-bgra-cpu | BGRA 1920x1080@60 bin cpu 60s | 3/3 | 5.62 | 9.07 | 10.99 | 2.03 | 1.94 | 1.95 | 0.43 | 0 | 20.8 | 10.7 | 506 | 46 | 6 | 7.8 | 22.5 | 1149 | 40 |
| 1080p60-nv12-native | NV12 1920x1080@60 bin native 60s | 3/3 | 3.37 | 5.14 | 7.33 | 2.02 | 1.90 | 1.93 | 0.35 | 0 | 6.7 | 8.9 | 85 | 48 | 6 | 6.2 | 13.0 | 279 | 65 |
| 1080p60-nv12-cpu | NV12 1920x1080@60 bin cpu 60s | 3/3 | 4.94 | 6.75 | 8.15 | 2.02 | 1.93 | 1.93 | 0.36 | 0 | 16.4 | 10.1 | 2276 | 46 | 6 | 6.7 | 20.9 | 513 | 35 |
| 4k30-bgra-native | BGRA 3840x2160@30 bin native 60s | 3/3 | 12.84 | 15.05 | 17.10 | 2.04 | 1.82 | 1.94 | 0.44 | 0 | 7.2 | 14.3 | 39 | 48 | 6 | 27.1 | 9.9 | 1173 | 223 |
| 4k30-bgra-cpu | BGRA 3840x2160@30 bin cpu 60s | 3/3 | 16.61 | 20.39 | 21.16 | 2.02 | 2.24 | 1.94 | 1.97 | 0 | 34.3 | 14.0 | 959 | 48 | 6 | 14.0 | 17.5 | 2271 | 64 |
| 4k30-nv12-native | NV12 3840x2160@30 bin native 60s | 3/3 | 8.34 | 10.25 | 13.41 | 2.03 | 1.84 | 1.95 | 0.43 | 0 | 6.0 | 12.9 | 4256 | 48 | 6 | 12.1 | 13.2 | 546 | 104 |
| 4k30-nv12-cpu | NV12 3840x2160@30 bin cpu 60s | 3/3 | 10.24 | 15.43 | 16.80 | 2.04 | 1.96 | 1.95 | 0.54 | 0 | 19.6 | 13.7 | 9040 | 50 | 27 | 7.2 | 20.7 | 1098 | 44 |
| src-1080p60-bgra | BGRA 1920x1080@60 source only 60s | 3/3 | source alone: 6.0% CPU | | | | | | | | | | | | | | | | |
| src-1080p60-nv12 | NV12 1920x1080@60 source only 60s | 3/3 | source alone: 6.1% CPU | | | | | | | | | | | | | | | | |
| src-4k30-bgra | BGRA 3840x2160@30 source only 60s | 3/3 | source alone: 8.3% CPU | | | | | | | | | | | | | | | | |
| src-4k30-nv12 | NV12 3840x2160@30 source only 60s | 3/3 | source alone: 5.6% CPU | | | | | | | | | | | | | | | | |

### android-tcl8483a-mt8786

| Cell | Config | OK | V arr p50 | V arr p99 | V arr max | A arr p99 | V jit | A jit | |A/V| max | Late | C CPU% | P CPU% | Mem KB/min | dH | dT | GPU busy% | GPU +W | PCIe MB/s | GPU +MB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1080p60-bgra-native | BGRA 1920x1080@60 bin native 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: consumer did not report ready before the deadline; error: native AHB output currently requires exact RGBA/RGB10 layout; opaque YUV is not NV12 bytes |
| 1080p60-bgra-cpu | BGRA 1920x1080@60 bin cpu 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: audio peer closed before GDP EOS; error: required video EOS missing or cancelled |
| 1080p60-nv12-native | NV12 1920x1080@60 bin native 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: consumer did not report ready before the deadline; error: native AHB output currently requires exact RGBA/RGB10 layout; opaque YUV is not NV12 bytes |
| 1080p60-nv12-cpu | NV12 1920x1080@60 bin cpu 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: audio peer closed before GDP EOS; error: required video EOS missing or cancelled |
| 4k30-bgra-native | BGRA 3840x2160@30 bin native 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: consumer did not report ready before the deadline; error: native AHB output currently requires exact RGBA/RGB10 layout; opaque YUV is not NV12 bytes |
| 4k30-bgra-cpu | BGRA 3840x2160@30 bin cpu 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: Could not write to resource.; error: coordinated video idle deadline, cancellation or stale generation |
| 4k30-nv12-native | NV12 3840x2160@30 bin native 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: consumer did not report ready before the deadline; error: native AHB output currently requires exact RGBA/RGB10 layout; opaque YUV is not NV12 bytes |
| 4k30-nv12-cpu | NV12 3840x2160@30 bin cpu 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: audio peer closed before GDP EOS; error: required video EOS missing or cancelled |
| src-1080p60-bgra | BGRA 1920x1080@60 source only 60s | 3/3 | source alone: 40.4% CPU | | | | | | | | | | | | | | | | |
| src-1080p60-nv12 | NV12 1920x1080@60 source only 60s | 3/3 | source alone: 21.4% CPU | | | | | | | | | | | | | | | | |
| src-4k30-bgra | BGRA 3840x2160@30 source only 60s | 3/3 | source alone: 36.5% CPU | | | | | | | | | | | | | | | | |
| src-4k30-nv12 | NV12 3840x2160@30 source only 60s | 3/3 | source alone: 27.6% CPU | | | | | | | | | | | | | | | | |
| 1080p60-rgba-native | RGBA 1920x1080@60 bin native 60s | 3/3 | 6.16 | 8.90 | 9.94 | 2.61 | 0.59 | 0.74 | 0.16 | 0 | 12.3 | 55.2 | -0 | 1 | 1 | 0.0 | - | - | - |
| 4k30-rgba-native | RGBA 3840x2160@30 bin native 60s | 3/3 | 10.65 | 14.16 | 17.29 | 3.45 | 0.62 | 0.93 | 0.22 | 0 | 12.7 | 57.6 | -0 | 2 | 1 | 0.0 | - | - | - |
| src-1080p60-rgba | RGBA 1920x1080@60 source only 60s | 3/3 | source alone: 40.3% CPU | | | | | | | | | | | | | | | | |
| src-4k30-rgba | RGBA 3840x2160@30 source only 60s | 3/3 | source alone: 36.8% CPU | | | | | | | | | | | | | | | | |


### Supported envelope at the target sizes

| Platform | 1080p60 | 4K30 | Not supported (reason) |
|---|---|---|---|
| Linux | BGRA and NV12, native consumer | BGRA and NV12, native consumer | CPU consumer, all four cells: discrete-GPU CPU readback cannot sustain the rate (M6.8) |
| Windows | BGRA and NV12, native and CPU consumer | BGRA and NV12, native and CPU consumer | — |
| Android | RGBA, native consumer | RGBA, native consumer | BGRA/NV12 native (native output supports RGBA layouts only); CPU consumer, all four cells (both processes saturate at this size and rate) |

### Transport cost at the target sizes

The producer copies every frame into the ring (M6.9). Producer CPU minus the
source-only CPU is the transport's producer cost:

| Platform, cell | Producer CPU | Source alone | Transport | Consumer CPU | Video arrival p50 / p99 |
|---|---|---|---|---|---|
| Linux 1080p60 BGRA native | 37.3 % | 11.5 % | ≈ 26 % | 1.6 % | 6.2 / 12.2 ms |
| Linux 4K30 BGRA native | 57.4 % | 22.8 % | ≈ 35 % | 1.0 % | 16.9 / 26.3 ms |
| Linux 4K30 NV12 native | 38.5 % | 10.4 % | ≈ 28 % | 2.1 % | 10.7 / 19.8 ms |
| Windows 1080p60 BGRA native | 9.6 % | 6.0 % | ≈ 4 % (+13.7 % GPU 3D engine) | 6.8 % | 4.4 / 6.3 ms |
| Windows 4K30 BGRA native | 14.3 % | 8.3 % | ≈ 6 % (+27.1 % GPU 3D engine) | 7.2 % | 12.8 / 15.1 ms |
| Windows 4K30 BGRA CPU | 14.0 % | 8.3 % | ≈ 6 % | 34.3 % | 16.6 / 20.4 ms |
| Android 1080p60 RGBA native | 55.2 % | 40.3 % | ≈ 15 % | 12.3 % | 6.2 / 8.9 ms |
| Android 4K30 RGBA native | 57.6 % | 36.8 % | ≈ 21 % | 12.7 % | 10.7 / 14.2 ms |

Every supported requirement cell completed every round with 0 late frames and
a max A/V offset of 0.44 ms or less (Windows 4K30 BGRA CPU: 1.97 ms).
Linux moves 1.2 GB/s over PCIe at 4K30 BGRA; Windows up to 2.3 GB/s
(4K30 BGRA CPU, upload and readback).

Windows memory growth in some 60 s rounds (up to 9 MB/min, 4K30 NV12 CPU) and
a thread spread of 27 in that cell are not budgeted at 60 s; the M6.2 soaks
decide whether they are warm-up or growth.

## Functional baseline (2026-11-03)

3 interleaved rounds of 60 s per cell, one benchmark version on all platforms;
raw results in [baseline/](baseline/). Typical values are the median round,
tails the worst round. Columns: arrival p50/p99/max (video, ms), audio arrival
p99, render jitter, max A/V offset, deadline misses, median CPU per process,
memory slope (not budgeted at 60 s), handle/thread spread, GPU busy, GPU power
above idle, PCIe MB/s and GPU memory added (Linux: framebuffer delta; Windows:
dedicated memory per process).

### linux-i9-10900k-rtx3090

| Cell | Config | OK | V arr p50 | V arr p99 | V arr max | A arr p99 | V jit | A jit | |A/V| max | Late | C CPU% | P CPU% | Mem KB/min | dH | dT | GPU busy% | GPU +W | PCIe MB/s | GPU +MB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ref | RGBA 320x180@30 bin cpu 60s | 3/3 | 7.81 | 22.95 | 24.76 | 0.51 | 0.14 | 0.17 | 0.05 | 0 | 24.4 | 3.8 | 6 | 0 | 1 | 0.0 | 2.2 | 23 | 2 |
| element | RGBA 320x180@30 element cpu 60s | 3/3 | 20.31 | 22.94 | 23.93 | 0.54 | 0.15 | 0.15 | 0.06 | 0 | 48.2 | 2.2 | 6 | 0 | 1 | 0.0 | 5.1 | 33 | 2 |
| nv12 | NV12 320x180@30 bin cpu 60s | 3/3 | 11.46 | 14.22 | 16.09 | 0.51 | 0.14 | 0.14 | 0.06 | 0 | 28.1 | 3.2 | 6 | 1 | 1 | 0.0 | 2.5 | 10 | 1 |
| native | RGBA 320x180@30 bin native 60s | 3/3 | 1.77 | 3.03 | 3.23 | 0.52 | 0.15 | 0.14 | 0.04 | 0 | 3.2 | 4.1 | 17 | 2 | 1 | 0.0 | -0.5 | 7 | 2 |
| cpu-640 | RGBA 640x360@30 bin cpu 60s | 3/3 | 30.65 | 35.64 | 37.43 | 0.46 | 0.09 | 0.14 | 0.03 | 0 | 86.9 | 2.7 | 6 | 0 | 1 | 0.0 | 10.4 | 104 | 7 |
| native-720p | RGBA 1280x720@30 bin native 60s | 3/3 | 6.78 | 7.05 | 11.11 | 0.49 | 0.14 | 0.17 | 0.08 | 0 | 2.9 | 17.2 | 6 | 2 | 2 | 0.0 | 1.1 | 153 | 32 |
| cpu-720p | RGBA 1280x720@30 bin cpu 60s | 0/3 | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - |
|  | failed: error: Internal data stream error.; error: coordinated video frame replaced before it was read: the producer delivered faster than real time |
| ref-traced | RGBA 320x180@30 bin cpu traced 60s | 3/3 | 8.57 | 23.03 | 25.81 | 0.48 | 0.14 | 0.16 | 0.05 | 0 | 36.2 | 2.9 | 3849 | 0 | 1 | 0.0 | 3.7 | 29 | 2 |

### windows-9800x3d-rtx5090

| Cell | Config | OK | V arr p50 | V arr p99 | V arr max | A arr p99 | V jit | A jit | |A/V| max | Late | C CPU% | P CPU% | Mem KB/min | dH | dT | GPU busy% | GPU +W | PCIe MB/s | GPU +MB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ref | RGBA 320x180@30 bin cpu 60s | 3/3 | 2.48 | 4.05 | 4.54 | 2.02 | 1.84 | 1.94 | 0.47 | 0 | 5.8 | 4.7 | 41 | 46 | 6 | 0.4 | 8.2 | 15 | 32 |
| element | RGBA 320x180@30 element cpu 60s | 3/3 | 2.47 | 4.00 | 18.19 | 2.02 | 1.84 | 1.94 | 0.60 | 0 | 6.0 | 4.9 | 5 | 49 | 6 | 0.4 | 7.9 | 25 | 32 |
| nv12 | NV12 320x180@30 bin cpu 60s | 3/3 | 2.41 | 4.06 | 4.75 | 2.01 | 1.85 | 1.93 | 0.39 | 0 | 5.8 | 4.8 | 102 | 49 | 6 | 0.3 | 7.2 | 16 | 32 |
| native | RGBA 320x180@30 bin native 60s | 3/3 | 2.28 | 3.79 | 4.22 | 2.02 | 1.87 | 1.94 | 0.40 | 0 | 5.1 | 6.1 | -22 | 48 | 3 | 0.3 | 5.1 | 5 | 33 |
| cpu-640 | RGBA 640x360@30 bin cpu 60s | 3/3 | 2.72 | 4.29 | 5.05 | 2.02 | 1.80 | 1.95 | 0.40 | 0 | 7.3 | 4.8 | 29 | 50 | 6 | 1.0 | 8.2 | 67 | 33 |
| native-720p | RGBA 1280x720@30 bin native 60s | 3/3 | 3.16 | 5.02 | 5.90 | 2.02 | 1.87 | 1.93 | 0.42 | 0 | 5.6 | 5.8 | -67 | 48 | 6 | 3.1 | 7.9 | 144 | 54 |
| cpu-720p | RGBA 1280x720@30 bin cpu 60s | 3/3 | 4.69 | 6.52 | 7.14 | 2.01 | 1.89 | 1.93 | 0.49 | 0 | 10.6 | 7.0 | 324 | 46 | 6 | 3.3 | 10.9 | 252 | 35 |
| ref-traced | RGBA 320x180@30 bin cpu traced 60s | 3/3 | 2.47 | 4.04 | 5.76 | 2.03 | 1.85 | 1.94 | 0.53 | 0 | 6.9 | 5.4 | 144 | 49 | 6 | 0.4 | 6.8 | 22 | 32 |

### android-tcl8483a-mt8786

| Cell | Config | OK | V arr p50 | V arr p99 | V arr max | A arr p99 | V jit | A jit | |A/V| max | Late | C CPU% | P CPU% | Mem KB/min | dH | dT | GPU busy% | GPU +W | PCIe MB/s | GPU +MB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ref | RGBA 320x180@30 bin cpu 60s | 3/3 | 6.69 | 8.22 | 10.58 | 2.40 | 0.82 | 0.92 | 0.42 | 0 | 22.3 | 13.9 | -0 | 2 | 1 | 0.0 | - | - | - |
| element | RGBA 320x180@30 element cpu 60s | 3/3 | 6.77 | 8.34 | 10.06 | 2.60 | 0.73 | 0.81 | 0.45 | 0 | 22.1 | 13.8 | -0 | 1 | 1 | 0.0 | - | - | - |
| nv12 | NV12 320x180@30 bin cpu 60s | 3/3 | 32.67 | 38.22 | 40.11 | 2.18 | 0.63 | 0.53 | 0.24 | 0 | 62.4 | 42.5 | 106 | 1 | 1 | 0.0 | - | - | - |
| native | RGBA 320x180@30 bin native 60s | 3/3 | 2.91 | 4.34 | 4.91 | 2.42 | 0.46 | 0.69 | 0.27 | 0 | 10.7 | 13.8 | 0 | 1 | 1 | 0.0 | - | - | - |
| cpu-640 | RGBA 640x360@30 bin cpu 60s | 3/3 | 17.65 | 19.68 | 21.30 | 2.62 | 0.97 | 0.89 | 0.94 | 0 | 52.5 | 17.6 | 0 | 3 | 2 | 0.0 | - | - | - |
| native-720p | RGBA 1280x720@30 bin native 60s | 3/3 | 5.44 | 8.16 | 9.81 | 3.04 | 0.60 | 0.86 | 0.27 | 0 | 12.2 | 30.3 | -0 | 3 | 2 | 0.0 | - | - | - |
| cpu-720p | RGBA 1280x720@30 bin cpu 60s | 3/3 | 26.30 | 39.93 | 43.92 | 2.94 | 3.33 | 0.90 | 2.61 | 0 | 77.5 | 24.2 | -0 | 2 | 1 | 0.0 | - | - | - |
| ref-traced | RGBA 320x180@30 bin cpu traced 60s | 3/3 | 6.83 | 8.20 | 9.86 | 2.43 | 0.69 | 0.75 | 0.50 | 0 | 22.5 | 14.2 | 3872 | 2 | 1 | 0.0 | - | - | - |
| ref-160 | RGBA 160x90@30 bin cpu 60s | 3/3 | 3.45 | 4.95 | 5.63 | 2.27 | 0.44 | 0.65 | 0.16 | 0 | 12.4 | 12.6 | -0 | 1 | 1 | 0.0 | - | - | - |


### Supported envelope and classification

| Platform | Supported cells | Unsupported (reason) |
|---|---|---|
| Linux | `ref`, `element`, `nv12`, `native`, `native-720p`, `ref-traced` | `cpu-640` (5 of 9 rounds across three baseline runs), `cpu-720p` (0 of 9): CPU readback from discrete-GPU memory cannot hold 30 fps; M6.8 |
| Windows | all eight | — |
| Android | all nine | — |

### Findings that carry into later tasks

- **Linux CPU path is bimodal.** At 320x180 a run sits near 7.7 ms or near
  20.4 ms median arrival (23–49 % consumer CPU across rounds) and can switch between rounds
  and within a run; GPU core and memory clocks stay at idle (210/405 MHz) in
  both modes, so it is not the GPU clock state. Cause open; M6.8.
- **Linux sessions do no GPU work.** SM and memory-controller load stay at 0;
  CPU readback and producer copies cross PCIe (about 23 MB/s at 320x180 CPU,
  153 MB/s at 720p native) and raise board power by up to 10 W.
- **Windows GPU cost is small.** Busiest engine ≤ 3.3 % (3D engine: uploads
  and readbacks), PCIe up to 252 MB/s at 720p CPU, about 32–54 MB dedicated
  GPU memory per process.
- **Android NV12 is slow.** 32.7 ms median arrival and 62 %/43 % consumer/
  producer CPU against 6.7 ms and 22 %/14 % for RGBA at the same size; no GPU
  work on Android (AHardwareBuffer CPU locks). Investigate in M6.5.
- **Producer copies every frame** (native cells included): producer CPU grows
  with resolution; M6.9.
- **Tracing** does not change latency; CPU changes by about a point or less on
  Windows and Android (the bimodal Linux CPU path hides any difference there). Recorder capacity is memory:
  at 1M events per recorder Windows commits about 575 MB (producer) and 745 MB
  (consumer) of private memory up front; Linux/Android grow toward it (about 3.8 MB/min). M6.6
  documents capacity sizing.
- **Windows handle counts drift** by about 46–50 per process without a leak;
  thread counts by 3–6.


## How M6.2–M6.6 use the budgets

| Task | Cells | Gate |
|---|---|---|
| M6.2 sustained A/V | every supported requirement cell, 30 min per target | every budget of the cell, with growth over the whole soak |
| M6.3 lifecycle endurance | the supported 1080p60 native cell per target (Android: RGBA) | ≥ 100 restart/reconnect cycles per target; each cycle complete; handles/threads/memory return to the warm level |
| M6.4 faults | the same cell, plus `ref` for CPU-path faults | fault-specific expectations; recovery within the documented bound; no growth across repeats |
| M6.5 performance/resources | all supported cells | distributions published; budgets hold |
| M6.6 tracing | `ref` vs `ref-traced` | traced cell within its budgets; lossless recorders; frame/audio accounting reproducible |

Check results with

    tools/m6_bench_report.py results.json --budgets docs/m6/budgets.json
