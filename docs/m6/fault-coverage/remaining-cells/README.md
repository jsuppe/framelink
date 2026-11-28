# M6.4 decision 15: fault coverage on the remaining cells — 2026-11-28

Decision 15 (A1, B2, C2, D1; [decisions-20261126.md](../../decisions-20261126.md)) asked for the declared fault case set on every supported requirement cell not yet covered, 100-cycle growth on one cell per platform and memory path, a Windows driver smoked on one cell, and element mode on one cell per platform. This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Binaries

| Platform | Revision | `fl_gst_av_bench` SHA-256 | Plugin SHA-256 |
|---|---|---|---|
| Linux, fault cells | `7f732e45` tree, bench built from `1d470a43` + `0d3eb2ca` | `07a644d60ef2431c` | `libgstframelink.so` `6840e3da0cb33364` |
| Linux, cycles | as above | `4cd2a748` (prefix) | as above |
| Android arm64 | `7f732e45` | `bbf8ea1aa77fc495` (plugin linked in) | — |
| Windows smoke | `f3ae593d` | `fdf11cd20570ae0d` | `gstframelink.dll` `d1e3b41c0cdf9693` |

`linux-binaries.sha256` holds the full Linux hashes. `raw-sha256.json` holds the SHA-256 of every raw CSV (2908 files); the CSVs themselves stay out of the repository.

## Method

- **Drivers:** `run_cells.sh` (Linux, Android) and `run_cells.ps1` (Windows) run the 15-case set on a cell named `PLATFORM-MEMORY-FORMAT-SIZE[-element]`, 5 repeats each. Copy-out cells have no strict case, so they run 14 cases, 70 runs.
- **Profile:** the primary zero-delay profile: pool 2, video queue 1, output queue 1, latency 0, transport-only, no output preroll, 12 s, raw. 1080p60: fault frame 360, arrival budget 17 ms. 4K30: fault frame 180, budget 34 ms.
- **Declared outcomes, by memory path, fixed before each campaign:**

  | Memory | stall200 | burst200 | strict |
  |---|---|---|---|
  | native | counted-drop | absorbed | explicit-failure |
  | gpu-readback (Linux GPU-staged) | replacement-failure | replacement-failure | — |
  | cpu (Android CPU) | replacement-failure | counted-drop | — |
  | shared-cpu (Windows) | absorbed | absorbed | — |

  The GPU-staged stall200 and Android CPU burst200 declarations were changed after the pilots (below) and before the campaigns. The pilot runs that disagreed stand as failures in `pilot/`.
- **GPU-staged content check:** every GPU-staged run checks each delivered frame's index and rows. Drops must match the counted slot drops exactly.
- **Growth:** `run_cycle_cells.sh` runs 100 × 5 s cycles with odd cycles faulted. The gate is `cycle-faults/check_cycle_faults.py`, applying the frozen M6.3 limits.

## Results

| Cell | Pass | Kill detection (ms, min–max) | stall200 counted drops |
|---|---|---|---|
| Linux native BGRA 1080p60 | 75/75 | 2.6–11.3 | 11 |
| Linux native BGRA 4K30 | 75/75 | 1.6–9.8 | 6 |
| Linux native NV12 4K30 | 75/75 | 4.3–9.1 | 5 |
| Linux native NV12 1080p60, element | 75/75 | 1.8–12.1 | 11 |
| Linux GPU-staged BGRA 1080p60 | **58/70** | 0.9–14.1 | replacement failure |
| Linux GPU-staged BGRA 4K30 | **55/70** | 1.2–33.1 | replacement failure |
| Linux GPU-staged consumer-kill rerun, `0d3eb2ca` | 10/10 | 10.1–25.7 | — |
| Android native RGBA 4K30 | 75/75 | 24.9–59.5 | 6 |
| Android native RGBA 1080p60, element | **73/75** | 19.0–36.3 | 11–12 |
| Android CPU RGBA 1080p60 | **64/70** | 18.2–38.0 | replacement failure |
| Android CPU RGBA 4K30 | **62/70** | 24.8–62.0 | replacement failure |
| Windows native NV12 1080p60 (smoke, 1 repeat) | 15/15 | 15.9–21.1 | 11 |

All kills were detected well inside the 2000 ms bound. No run delivered a stale or wrong frame.

### Growth (100 cycles)

| Cell | Fault | Cycles | Memory (KB/cycle) | Handles, threads | Result |
|---|---|---|---|---|---|
| Linux GPU-staged BGRA 1080p60 | restart producer, kill | 100/100 | 3.46 | 0, 0 | PASS |
| | restart consumer, kill | **95/100** | 1.76 | 0, 0 | **FAIL** |
| | restart producer, malformed | 100/100 | 3.17 | 0, 0 | PASS |
| Android CPU RGBA 1080p60 (`G_DEBUG=fatal-warnings`) | restart producer, kill | 100/100 | 1.69 | 0, 0 | PASS |
| | restart consumer, kill | 100/100 | 2.50 | 0, 0 | PASS |
| | restart producer, malformed | 100/100 | −7.89 | 0, 0 | PASS |

Growth is flat everywhere, including the failed run.

## Failures (all stand; none was replaced by a repeat)

1. **GPU-staged consumer-kill, 10/10 failed (both sizes), gate bug.** A killed consumer leaves no content counters, and the gate required them. Fixed in `0d3eb2ca` (the content check applies only on producer kill). The rerun passed 10/10. The original 10 failures stand.
2. **GPU-staged video-no-pts, 10/10 failed (both sizes), product diagnostic.** The consumer fails correctly with "invalid input" within 0.9 ms. But the producer reports the native sink's catch-all, "native sink requires its own reserved allocation and valid metadata; no copy fallback" (`gst/gstframelink.cpp:604-616`), not the specific "valid supported frame metadata and TIME mapping required". Android native gives the specific message. The fix is in code shared with Windows, so it needs Jon's approval.
3. **GPU-staged burst200, 7/10 failed, declaration wrong.** At 1080p60, 3/5 were the declared replacement failure. The other 2/5 completed with 11 counted app slot drops. At 4K30, 5/5 completed with 5 counted slot drops, EOS, and exact content. The outcome is racy at 1080p60. The declared outcome is Jon's to decide.
4. **GPU-staged restart-consumer-kill growth, 95/100.**
   - Clean cycles 20, 40, 42, 58 and 74 each delivered 299 of 300 frames. In each, one app slot drop was counted, the content check showed the missing frame, and there was no mismatch.
   - Faulted cycle 23 ended with the generic native-sink error instead of "FrameLink peer closed or failed" (decision 10's stable error).
5. **Android CPU burst5, 9/10 failed (4/5 at 1080p60, 5/5 at 4K30), and stall5 at 4K30, 2/5 failed: recovery after a 5 ms fault.**
   - Delivery was complete and loss was accounted in these runs. They fail the recovery limit (3 frames): 97–359 frames needed to return under the arrival budget.
   - The cause is that the CPU copy-out path's steady transport share sits at the budget. At 1080p60, the share after the fault has p99 around 17 ms against a 17 ms budget. At 4K30 the p50 is around 33 ms against 34 ms.
   - So any small delay keeps later frames over budget. This is a capacity margin on this path, not a fault-handling error.
6. **Android CPU burst200 at 1080p60, 2/5 failed: racy, as on GPU-staged.** In 3/5 runs the declared outcome held (3 counted drops, EOS). In the other 2/5, the consumer failed with "coordinated video frame replaced before it was read", and the producer reported "FrameLink peer closed or failed". At 4K30, 5/5 gave counted drops. Both copy-out paths therefore split between the two outcomes at 1080p60. Item 3 and this item are one decision for Jon.
7. **Android CPU astall50 at 4K30, 1/5 failed.** The consumer received 359 of 360 frames, with 124 deadline misses. This is the same margin as item 5.
8. **Android native element burst200 at 1080p60, 2/5 failed.** In those 2 runs, the burst was not observed as back-to-back (`fault.burst=no`). The drain took 22–23 frames against the 15-frame recovery limit. The 3 passing runs drained in 20–21 frames with the burst observed.

## Pilots and incidents

- **Pilots** (`pilot/`, `pilot/android/`): two declarations were wrong, and the pilot results stand.
  - GPU-staged stall200 was declared counted-drop; the result was a replacement failure.
  - Android CPU burst200 was declared replacement-failure; the result was counted drops (1 at 1080p60, 5 at 4K30).
  - The first Android pilots failed because their output directory did not exist. They were run again after it was created.
- **Driver edited mid-run:** `run_cells.sh` was edited while the Linux campaign was running it. The first cell printed `run_cells.sh: 65: ideo-stale-meta: Permission denied` once. All 75 runs of that cell exist, 5 of each case, and all passed. The edit did not change any case.

## Not covered here

- **Android native RGBA 1080p60 (non-element)** is the primary cell already covered by the earlier M6.4 records.
- **The full Windows campaign** runs once, inside decision 14's requalification (C2).

## Rerun at `6c45029e` after Jon's two follow-ups (2026-11-28)

Jon approved both recommendations ([decision record](../../decisions-20261126.md#decision-15-fault-coverage-on-the-remaining-cells-2026-11-27)):
- **Native sink error, one per cause** (`gst/gstframelink.cpp`):
  - invalid metadata posts "valid supported frame metadata and TIME mapping required";
  - a closed channel is a resource write error, which the producer's peer-failure mapping turns into "FrameLink peer closed or failed";
  - device loss posts decision 13's "FrameLink GPU device lost";
  - foreign storage keeps the existing message.
- **Copy-out 200 ms burst:** `--fault-expect replaced-or-counted-drop`. Each outcome is held to its full check, and `fault.outcome` records which one happened.

The whole campaign was run again on every cell, against the outcomes declared before the runs. Evidence: `rerun-6c45029e/` (summaries, `cycles-gate.txt`, `raw-sha256.json` for 2976 CSVs, `windows.log`). The first campaign's failures above still stand.

**Binaries:**

| Platform | Binary | SHA-256 prefix |
|---|---|---|
| Linux | `fl_gst_av_bench` | `709ac5794f9e3eb7` |
| Linux | plugin | `c2ebae11f3d0dd18` |
| Android | `fl_gst_av_bench` | `3d3bfcfb410865d6` |
| Windows | `fl_gst_av_bench.exe` | `6c360359a5a2043a` |
| Windows | `gstframelink.dll` | `ac00db85824d3058` |

**Suites at `6c45029e`:**
- Linux: 203 passed, 1 skipped (`read_lease_egl_delayed_completion`), 0 failed.
- Android: 152 passed, 1 skipped, 0 failed.
- Windows: 198/198.

| Cell | First campaign | Rerun |
|---|---|---|
| Linux native, 4 cells | 300/300 | 300/300 |
| Linux GPU-staged BGRA 1080p60 | 58/70 | **68/70** |
| Linux GPU-staged BGRA 4K30 | 55/70 | 70/70 |
| Android native RGBA 4K30 | 75/75 | 75/75 |
| Android native RGBA 1080p60, element | 73/75 | **72/75** |
| Android CPU RGBA 1080p60 | 64/70 | **65/70** |
| Android CPU RGBA 4K30 | 62/70 | **60/70** |
| Windows native NV12 1080p60, smoke | 15/15 | 15/15 |

**What the fixes changed:**
- **video-no-pts:** every GPU-staged producer now reports the specific metadata error (10/10), as Android and Windows do.
- **Faulted growth cycles:** every faulted cycle ended with "FrameLink peer closed or failed" (100/100 across the two kill runs).
- **Copy-out 200 ms bursts:** all 20 passed.
  - Linux GPU-staged: 6 replacement failures and 4 counted drops.
  - Android CPU: 10 counted drops.

**Rerun failures (they stand):**
1. **Linux GPU-staged audio stall, 1080p60, 2/10:**
   - astall200-1 delivered 719/720 frames, and astall50-3 delivered 718/720.
   - The app feed thread counted 1 and 2 slot drops. The content check showed the gaps, with no mismatch.
   - The runs fail on completeness.
2. **Linux GPU-staged growth:**
   - restart-consumer-kill: 78/100. 22 clean cycles delivered 294–299 of 300 frames, mostly in cycles 0–24.
   - restart-producer-kill: 97/100, with 3 short clean cycles.
   - In every short cycle the consumer's content check showed the gap, with no mismatch. The run never delivered a wrong frame.
   - Cycle mode does not record the producer's per-cycle slot-drop count, so the producer side of these losses is not matched.
   - restart-producer-malformed passed 100/100. Growth stayed flat in all three runs: 1.85–4.66 KB/cycle; handles and threads 0.
   - In the first campaign the same run lost 5 clean cycles, against 22 now.
   - The losses are in the bench's GPU-staged feed thread: `feedNative` takes a buffer from the sink's two-buffer pool. The FrameLink transport is not involved. This is the same receiver-harness kind of issue as decision 11.
3. **Android CPU, 5 ms faults:** burst5 failed 10/10, and stall5 at 4K30 failed 5/5. This is the same capacity margin as in the first campaign: the copy-out transport sits at the arrival budget.
4. **Android native element burst200, 3/5 failed:** the burst was not observed as back-to-back, and the drain exceeded the 15-frame limit, as in the first campaign.

**Growth on Android CPU:** 3/3 runs passed again, with every cycle passing.
