# M6.4 on Linux and Android: fault reruns after decisions 8–10 — 2026-11-27

This record is dated; current status is in [STATUS.md](../../../STATUS.md). It reruns the [endpoint-death and stall cells](../at-rate/README.md) and the [malformed-metadata cells](../malformed-at-rate/README.md), unchanged (same scripts, profile, hosts and 5 repeats), after the consumer was told the producer's reason ([decisions 8 and 9](../../decisions-20261126.md)). It covers two revisions:

| Revision | What it adds | Result |
|---|---|---|
| `66fa0dbf` | decisions 8 and 9, the avsink setup-order fix | **122/130**: 8 failures, kept below |
| `263c32e4` | decision 10: every FrameLink flow error posts its own error; the consumer takes a pending producer end before it fails | **130/130** |

Windows is not rerun here. Windows stays frozen at `060c54e4`, and its requalification of decisions 8–10 goes back to Jon before merge.

## Declared outcomes

The same as the two original records, with the survivor's error now required to be stable:

- **Producer killed:** the consumer's error is "FrameLink peer closed or failed".
- **Consumer killed:** the producer's error is "FrameLink peer closed or failed".
- **Malformed input:** the producer fails with its specific error, and the consumer's error is "FrameLink producer failed: invalid input".
- **200 ms stall under error-on-ring-full:** the consumer's error is "FrameLink producer failed: overload".
- **Other stalls:** EOS, as before.
- **Bound:** every survivor ends within 2000 ms. A survivor error of "Internal data stream error." fails the run.

## `66fa0dbf`: 122/130

| Cell | Case | Pass | Survivor error in the failures |
|---|---|---|---|
| Linux shared CPU | video no PTS | 1/5 | "Internal data stream error." (4) |
| Linux shared CPU | producer kill | 3/5 | "Internal data stream error." (2), at 100–105 ms |
| Linux shared CPU | consumer kill | 4/5 | "Internal data stream error." (1) |
| Android native | consumer kill | 4/5 | "Internal data stream error." (1) |
| All other cells | | 5/5 | |

All 8 are the same fault: a FrameLink element returned a flow error without posting its own error, so GStreamer posted its generic one. Two paths were silent:

- **The shared-CPU sink and source** returned errors from about 25 places without a message. This includes the shared-CPU sink's invalid-metadata refusal, recorded as a limit in the malformed record.
- **The native sink's "channel lost" return** (`gst/gstframelink.cpp`, then line 677): the Android consumer-kill failure.

Also, on shared CPU the consumer often failed before its reader thread had taken the producer's ABORT or close from the socket. It then waited decision 8's full 100 ms for it, which is the 100–105 ms detection above.

These 8 failures stand. Rerunning did not replace them; the fix below did.

## The fix (`263c32e4`, decision 10)

- Every FrameLink flow error now posts its own error first: shared-CPU sink (9 returns), shared-CPU source (17), and native sink and source (8).
- The shared-CPU sink refuses invalid video metadata with the native sink's message, "valid supported frame metadata and TIME mapping required", so it also sends the invalid-input reason.
- Before a consumer cancels locally, or starts decision 8's wait, it takes any producer ABORT or close already pending on the event socket, with no wait. Decision 8's 100 ms wait remains as a fallback.
- The harness records the first error's element and debug text (`*.result.source`, `*.result.debug`).

The silent paths were found with temporary probes (line tags at each return), since removed. The probes changed the timing: with full printing, every run passed. The final evidence is the unprobed reruns.

## `263c32e4`: 130/130

Survivor end after the fault (min–max over 5 runs):

| Case | Linux native NV12 | Linux shared CPU NV12 | Android native RGBA |
|---|---|---|---|
| Producer kill | 5/5, 5.4–5.9 ms | 5/5, 5.2–5.5 ms | 5/5, 17.7–36.1 ms |
| Consumer kill | 5/5, 4.2–9.8 ms | 5/5, 6.0–11.8 ms | 5/5, 25.6–30.4 ms |
| Stall 5 ms | 5/5 | 5/5 | 5/5 |
| Stall 200 ms | 5/5 | 5/5 | 5/5 |
| Stall 200 ms, error on ring full | 5/5 | not run (shared CPU never drops) | 5/5 |
| Video no PTS | 5/5, 0.2–0.9 ms | 5/5, 0.2–0.6 ms | 5/5, 2.0–2.7 ms |
| Video stale metadata | 5/5, 0.8–1.0 ms | 5/5, 0.2–0.6 ms | 5/5, 2.4–2.9 ms |
| Audio no PTS | 5/5, 0.8–0.9 ms | 5/5, 0.4–0.6 ms | 5/5, 1.6–3.3 ms |
| Audio misaligned | 5/5, 0.8–1.0 ms | 5/5, 0.5–0.6 ms | 5/5, 2.7–3.7 ms |

Survivor errors, all 130 runs:

| Error | Runs |
|---|---|
| "FrameLink producer failed: invalid input" | 60 (every malformed run) |
| "FrameLink peer closed or failed" | 30 (every kill) |
| "FrameLink producer failed: overload" | 10 (every strict stall) |
| EOS | 30 (other stalls) |
| "Internal data stream error." | 0 |

- **Shared-CPU detection:** the malformed cases end in 0.2–0.6 ms and producer kill in 5.2–5.5 ms, against 100–105 ms in the failing runs.
- **The Android channel-lost path ran:** in `android-native-consumer-kill-1`, the producer's first error came from that return ("FrameLink video channel lost", `*.result.debug`). It now ends with the stable error.

Suites on the same code: Linux 200/200 (1 skipped, `read_lease_egl_delayed_completion`), serial `ctest`; Android 150/151 (1 skipped).

A first Linux suite run with `ctest -j6` failed `m5_startup` after 7.2 s. Its output was overwritten by the next run, so the cause is unknown. It passed when run alone (30.5 s) and in the serial suite. It is recorded here, not replaced.

## Binaries

`263c32e4` SHA-256: `linux-binaries-263c32e4.sha256`, `android-binaries-263c32e4.sha256`. Prefixes: Linux `fl_gst_av_bench` `d62c7f6e62872662`, `libgstframelink.so` `7023582d635bd31a`, `libframelink_gst_session.so` `7df404ee7c5c1728`; Android arm64 `fl_gst_av_bench` `bfe6a8560be63df3`.

The `66fa0dbf` binary hashes were not kept. That rerun is identified by its revision only.

## Not established here

- **Whether the producer always sends its reason before its video ring closes.** If it does, decision 8's 100 ms fallback wait can go. This is decided later, after the send order is verified on all three platforms with the Windows requalification.
- **Windows**, as above.
- Other cells, wire malformations and growth across repeats, as in the original records.

## Files

- `summaries-66fa0dbf/` and `summaries-263c32e4/`: 130 run summaries each. Local paths are removed from the debug strings.
- `*-263c32e4.sha256`: binary hashes.
- `raw-sha256.json`: hashes of the raw outputs (stdout, per-run CSVs, driver logs), which are kept outside the repository.

To reproduce, run `../at-rate/run_faults.sh` and `../malformed-at-rate/run_malformed_faults.sh` for each cell with 5 repeats.
