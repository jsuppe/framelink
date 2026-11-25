# Android drain-report timeout — 2026-11-25

**Question.** In the Android shared NV12 4K30 restart-producer lifecycle cell at `e1284833`, cycle 82 of 100 failed: the long-lived consumer received every frame and sample and reached EOS, but the fresh producer's sink reported `consumer did not report drain before the deadline` ([record](../primary-e128483/README.md#android-shared-nv12-4k30-restart-producer-a-drain-report-timeout)). Why was the report late?

## Answer

**A race in `framelinkavsrc` between its drain report and the application's teardown at EOS.** The producer may close only after the consumer bin's drain report, so it holds its EOS until then (deadline: connect + idle timeouts, 8 s in the bench).
- **Report:** when every track has reached EOS at the bin's source pads, a separate thread writes the report, one byte on the local control connection.
- **Teardown:** the application stops the consumer when EOS reaches its bus, as the bench does and as applications normally do. Leaving PAUSED cancelled the session.
- **The race:** nothing made the report come before the teardown. If the report thread had not written by then, the write was cancelled, and a cancelled report posted no error. The consumer ended with a clean EOS, and the producer waited its full deadline and failed.

That matches cycle 82 exactly. The race is platform-independent; it is tight on the Android device.

## Evidence

**On the device: how close the race runs** (`run_drain_logged.sh`, `drain_margin.py`). The cell exactly as `m6_bench_matrix.py` runs it, 100 cycles, with the drain handshake logged (INFO, new in this change) and the per-cycle files kept. Same device as the recorded failure. Bench without the fix: `640751a9…`.
- All 100 cycles completed this time (the failure was 1 in 100 at `e1284833`).
- The consumer wrote its report 1.0–3.1 ms after EOS (median 1.6).
- **Margin between the written report and the teardown that would cancel it: 0.05–1.65 ms, median 0.89. Under 1 ms in 68 cycles, under 0.5 ms in 13.** A delay of more than that on the report thread loses the race.

**Deterministic reproduction** (`test/fl_gst_av_src_bin.cpp`, case "consumer teardown at EOS, drain report delayed"). A test-only fault, `FRAMELINK_TEST_DRAIN_REPORT_DELAY_MS` (at most 1 s, after the bin's existing `FRAMELINK_TEST_PRODUCER_OPEN_DELAY_MS`), delays the report by 100 ms. The consumer goes to NULL the moment EOS reaches its bus.
- **Without the fix:** 0/3 rounds. Each fails exactly as cycle 82 did: consumer EOS, producer `consumer did not report drain before the deadline`.
- **With the fix:** 3/3.
- Without the delay, the same case passes 20/20 on the Linux desktop even without the fix: the race does not show there unaided.

## Fix

`gst/gstframelinkavsrc.cpp`: leaving PAUSED, once every track has reached EOS and ready was reported, the bin first lets the pending drain report finish, then stops the session. The report is one byte on the local control connection, bounded by `connect-timeout-ms`. A failed or never-ready session, and a teardown before EOS, are cancelled as before.

Also new: INFO logs of the drain handshake on both bins, and a warning when a drain report is cancelled or not sent.

## Results with the fix

- **Device cell** (bench `33e60671…`, same cell, 100 cycles, logged): 100/100, memory slope 0.25 KB/cycle, all 100 drain reports received, none cancelled. This shows no regression. It cannot show the fix itself, because the race was not lost in either 100-cycle run. The deterministic case is the proof.
- **Regression case on the device:** `gst_av_src_bin` passes on the Android device (`tools/run_android_ctest.py`), with the new case.
- **Linux:** 187/187 (`read_lease_egl_delayed_completion` skipped, as before), built on top of the Linux lifecycle-memory change (PR #45), which touches only glibc builds.
- **Android suite** (`tools/run_android_ctest.py`, fresh push): **137 pass, 1 fail, 1 skip of 139.** The failure is `gst_av_parity`, case `consumer-pause`, bin producer with bin consumer. The consumer failed as the case intends (`coordinated video frame replaced before it was read`). The producer reported `Could not write to resource.`, which the case does not accept: it accepts `required session tracks not ready or cancelled` or `coordinated session not ready or cancelled`.
  - **Most likely unrelated to this change.** The message is the producer's GIO audio writer failing when the consumer's audio connection closes, which the [M5b fatal-propagation record](../../../m5b/fatal-propagation/README.md) documents as how a producer sees a consumer stop. In this case the consumer has failed, so the bin's `failed` flag is set and the fix does not wait; no drain report is involved. The case's own comment says which side reports the cancellation first is a race. This is a third way it can surface. Not shown: that it also happens without the fix.
  - Reruns of `gst_av_parity` on the same build: 8/8 pass. They do not replace the failure.
  - Whether the case should accept this outcome is a test-gate decision for Jon.
- **Windows:** the change is in shared code, so it applies there too. The Windows suite has not been run on it here.
- **Not established:** the cell at the final revision (M6.3). The recorded `e1284833` failure stands.

## Files

- `run_drain_logged.sh`: the logged device run. `drain_margin.py`: per-cycle timing from its log.
- `logged-1.margin.txt`, `fixed-1.margin.txt`: `drain_margin.py` on both device runs.
- Outside the repository: both runs' full logs and pulled per-cycle files (`runs.tar.xz`, SHA-256 in `runs.tar.xz.sha256`).
