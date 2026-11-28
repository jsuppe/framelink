# M6.4 on Linux and Android: growth across repeated faults — 2026-11-26

Revision `be4b6435`. `fl_gst_av_bench` SHA-256 prefixes: Linux `ffb6ec46c4e26a67`, Android arm64 `56858a3ef4d500b0`; `libgstframelink.so` `f9d0d96cc0ea1f4b`. Hosts as in the [endpoint-death record](../at-rate/README.md). This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Question

The earlier M6.4 records ran each fault in a fresh pair of processes. This record asks whether a long-lived process that sees the same fault repeatedly recovers every time and does not grow.

## Harness (`fl_gst_av_bench --cycles 100 --cycle-fault`, commit `be4b6435`)

The M6.3 lifecycle harness runs one session per cycle in a single long-lived process, starts a fresh process for the other side each cycle, and records the long-lived process's memory, handles and threads after every cycle. Here every **odd cycle is faulted**:
- `kill`: the fresh peer is killed (SIGKILL) 3000 ms after it starts, restarting either the producer or the consumer;
- `malformed` (restart producer only): the fresh producer sends frame 180 with stale FrameLink metadata and is refused.

Every even cycle is clean.

**Gate, declared before the runs:**
- a clean cycle delivers exactly 300 frames and 240 000 samples with EOS on both sides;
- a faulted cycle ends in an error on the long-lived side, never EOS, within 2000 ms of the fault, and the fresh peer was killed or failed with "stale producer video metadata generation";
- **growth: the frozen M6.3 limits and window.** Least-squares slopes of the long-lived process over cycles 5–99 must be at most 16 KB, 0.1 handles and 0.02 threads per cycle. They are applied by `tools/m6_bench_matrix.py`'s `lifecycle_gate` through `check_cycle_faults.py`.

Profile: the primary profile (pool 2, unread queue 1, output queue 1, latency 0, transport-only, no output preroll), 1080p60, 5 s cycles. Android ran with `TMPDIR` on the device and `G_DEBUG=fatal-warnings`, as the M6.3 runner does.

## Results: 9/9 pass, 900/900 cycles

| Cell | Restarted side, fault | Cycles | Memory slope (KB/cycle) | Resident, cycle 5 → 99 (KB) | Handles, threads | Long-lived end after the fault (ms, median / max) |
|---|---|---|---|---|---|---|
| Linux native NV12 | producer, kill | 100/100 | 1.88 | 16 088 → 16 308 | flat (8, 7) | 5.5 / 5.9 |
| | consumer, kill | 100/100 | 1.65 | 33 884 → 34 172 | flat (7, 6) | 7.1 / 12.3 |
| | producer, malformed | 100/100 | 2.08 | 16 176 → 16 456 | flat (8, 7) | 0.7 / 0.8 |
| Linux shared CPU NV12 | producer, kill | 100/100 | 1.61 | 15 928 → 16 120 | flat (7, 7) | 5.1 / 5.4 |
| | consumer, kill | 100/100 | 2.11 | 24 196 → 24 420 | flat (7, 6) | 6.2 / 9.4 |
| | producer, malformed | 100/100 | 1.98 | 15 972 → 16 176 | flat (7, 7) | 0.5 / 0.6 |
| Android native RGBA | producer, kill | 100/100 | 0.28 | 45 208 → 45 340 | flat (10, 6) | 19.9 / 34.2 |
| | consumer, kill | 100/100 | 1.82 | 45 972 → 46 312 | flat (15, 5) | 25.6 / 31.5 |
| | producer, malformed | 100/100 | 4.01 | 45 532 → 46 068 | flat (10, 6) | 2.1 / 2.9 |

The handle and thread slopes are 0.000 in every run. Every clean cycle after a fault delivered exact media. The memory slopes, 0.3–4.0 KB/cycle, are in the range of the clean M6.3 lifecycle cells (for example 1.6–3.1 KB/cycle at 1080p60 in [linux-android-051c070](../../lifecycle/linux-android-051c070/README.md), buffered profile). `gate.txt` holds the gate output.

## Errors the long-lived side reported on faulted cycles

All 450 faulted cycles ended in an error, never EOS. The messages vary with the moment the fault lands:
- **long-lived consumer, producer killed:** "framelink acquire failed" or "audio peer closed before GDP EOS", and on shared CPU also "Internal data stream error.";
- **long-lived producer, consumer killed:** "coordinated session not ready or cancelled", "required session tracks not ready or cancelled", "metadata frame publication failed" (Linux native, 3) and "Internal data stream error.";
- **long-lived consumer, malformed producer:** "audio peer closed before GDP EOS", or "Internal data stream error." on shared CPU.

Two messages occurred once each, both on Android:
- **"GDP payload exceeds negotiated bounds or invalid wire header"** (long-lived consumer, malformed case). The consumer's audio reader rejected the wire data left as the refusing producer shut down: a truncated packet was refused by validation before use, not accepted. Which bytes it saw was not captured.
- **"Could not write to resource."** (long-lived producer, consumer killed): a write to the dead peer. This is the message in the open `gst_av_parity` question; here it is an expected outcome of peer death.

## Pilots (kept, not part of the 9)

One 6-cycle run of each case on Linux native, before the commit (`pilot/`): all passed. With 6 cycles only one cycle is past warm-up, so their slopes are 0 and carry no growth evidence.

## Not established here

- Other cells (4K30, BGRA, the GPU-staged receiver, Windows), and the stall, burst and underrun faults under repetition.
- Growth beyond 100 cycles, or under faults landing at other moments (for example during connection setup).
- What produced the truncated wire packet in the one Android cycle.

Files: `run_cycle_faults.sh`, `check_cycle_faults.py`, `gate.txt`, `summaries/` (9 runs with the full per-cycle series, plus per-cell `run.log`), `pilot/`, `*binaries.sha256`. Per-cycle fresh-peer results are retained outside the repository.
