# M6.4 on Linux and Android: producer bursts and audio underruns at rate — 2026-11-25

Revision `ca99ed0c`. `fl_gst_av_bench` SHA-256 prefixes: Linux `86d6c98fda3ba69e`, Android arm64 `f849e82a7998172a`; `libgstframelink.so` `f9d0d96cc0ea1f4b`. Hosts:
- Linux: Ubuntu 26.04, RTX 3090 (NVIDIA 580.178.04), GStreamer 1.28.2.
- Android: TCL 8483A (Mali-G52), Android 15.

This record is dated; current status is in [STATUS.md](../../../STATUS.md). It follows [endpoint death and consumer stalls](../at-rate/README.md) with the same profile and cells.

## Harness (`fl_gst_av_bench --fault`, commit `ca99ed0c`)

Each run is a separate producer and consumer process at 1080p60 with the primary profile (pool 2, unread queue 1, output queue 1, latency 0, transport-only, no output preroll, arrival budget 17 ms, 12 s, raw capture). Faults are injected in the producer at the test source's push, before FrameLink sees the buffer. Every case declares its outcome before the run, and the driver gates the run against it:

| Case | Injection | Pass requires |
|---|---|---|
| `producer-burst`, 5 / 200 ms, `absorbed` | the live video source blocks on frame 360, then catches up | 720/720 frames and 576000 samples, both EOS, audio contiguous, queue accounting complete; video arrival back under 17 ms within ceil(M/16)+2 frames of frame 360 (3 / 15) |
| `audio-stall`, 50 / 200 ms, `absorbed` | the live audio source blocks on 10 ms block 600, then catches up | the same completeness; audio arrival back under 17 ms within ceil(M/10)+2 blocks (7 / 22) |
| `audio-drop`, 50 ms, `explicit-failure` | blocks 600–604 never reach the sink: a timestamp gap without DISCONT | the producer fails with "PCM sample duration/continuity mismatch" (documented: such a gap is refused); the consumer does not report EOS and ends within 2000 ms of the first missing block; both exit normally |
| `audio-gap`, 50 ms, `gap-visible` | the same blocks, signalled as GStreamer does: a GAP event for the 50 ms, then DISCONT on the next block | both EOS, 720/720 frames; exactly the 2400 dropped samples missing; the consumer sees one PTS jump and one GAP event, each 50 ms within 2 ns (documented: GAP events are carried, no silence is synthesized) |

CTest adds `gst_av_bench_fault_audio_drop` and `gst_av_bench_fault_audio_gap` on the 160x90 cell; they pass on Linux and Android (Android `gst_av_bench_fault` group 5/5), and the full Linux suite under `-j4` passed 194/194 with the known skip `read_lease_egl_delayed_completion`. The timing-bound cases are not in CTest. `run_producer_faults.sh` reproduces the cells.

## Results: 85/90; Android 200 ms burst failed 5/5

| Cell | Case | Runs | Result |
|---|---|---|---|
| Linux native (NV12, DMA-BUF receiver) | burst 5 ms | 5/5 | complete; recovery 0 frames |
| | burst 200 ms | 5/5 | complete; recovery 15 frames in every run (limit 15) |
| | audio stall 50 / 200 ms | 5/5, 5/5 | complete; recovery 4 / 19 blocks (limits 7 / 22) |
| | audio drop | 5/5 | the producer refused the gap; the consumer failed 50.7 ms after the first missing block |
| | audio gap | 5/5 | 2400 samples missing, one 50 ms jump, one 50 ms GAP event |
| Linux shared-CPU ref (NV12) | burst 5 / 200 ms | 5/5, 5/5 | complete; recovery 0 / 12–13 frames |
| | audio stall 50 / 200 ms | 5/5, 5/5 | complete; recovery 4 / 19 blocks |
| | audio drop | 5/5 | consumer failed in 50.2–50.5 ms |
| | audio gap | 5/5 | as on native |
| Android native (RGBA, AHardwareBuffer receiver) | burst 5 ms | 5/5 | complete; recovery 0 |
| | **burst 200 ms** | **0/5** | complete, no drops (720/720, 0 producer drops, audio contiguous), but **recovery 18–19 frames against the declared 15** |
| | audio stall 50 / 200 ms | 5/5, 5/5 | complete; recovery 4 / 19–20 blocks |
| | audio drop | 5/5 | consumer failed in 52.3–53.0 ms |
| | audio gap | 5/5 | as on Linux |

Stalls executed for 5.1–5.4, 50.1–50.5 and 200.1–200.6 ms. Across the 15 audio-drop runs the consumer's error was "audio peer closed before GDP EOS" (12), "Internal data stream error." (2) or "coordinated session not ready or cancelled" (1), after 361–363 frames. No run lost a frame or sample without the loss being refused or signalled.

## The Android 200 ms burst failure

> **Correction (2026-11-25, later the same day):** the attribution below to the test source is wrong. Push lateness here was measured at the source's output, and a push returns only once FrameLink has taken the frame in, so the catch-up rate is largely FrameLink's intake rate (about 8 ms per 1080p RGBA frame on this device). This run also did not create a real burst at FrameLink's input. See the [queued-burst record](../burst-transport/README.md). The failed runs stand; the text below is kept as it was written.

The failed runs stand. The arrival lag comes from the test source, not from the transport. The producer's own push lateness, measured before FrameLink, stays over 17 ms for 19 frames: after the stall the live `videotestsrc` gains only about 10 ms per frame, which implies about 6 ms to produce each 1080p RGBA frame on this device (inferred from the push times; the source's cost was not measured separately). Consumer arrival follows push lateness plus the usual 4–5 ms (run 1: push 200.7 → 2.0 ms over frames 360–379, arrival 205.3 → 7.8 ms). On Linux the source gains about 12.5 ms per frame, which is why it recovered at exactly the limit.

The declared limit assumed the source catches up with back-to-back frames. That assumption was wrong for this source, not checked before the runs, and the Linux pass at the limit rests on the same source behaviour. What the runs do show on every cell is that a 200 ms producer burst is delivered completely, with no drop and no transport delay beyond the source's own lateness. A transport-only recovery measure (arrival minus push lateness) would need a new declared expectation and new runs; that was not done.

## Pilot runs (kept, not part of the 90)

One run of each case on Linux native, before the commit, from the same bench source (`pilot/`). All six matched their declared outcomes; the 200 ms burst recovered in 15 frames, at the limit.

## Not established here

- **Transport-only burst recovery.** See above.
- **Bursts that overrun the ring.** The source's catch-up is slower than the consumer, so no burst here forced a producer drop; `counted-drop` exists for `producer-burst` but was not run.
- **Audio underrun at the consumer's audio output.** The runs are transport-only (no audio sink clock); speaker-side underrun is not measured.
- **Malformed metadata.** M6.4b also lists it; not injected here.
- **Growth across repeats, other cells** (4K30, BGRA, element mode, GPU-staged receiver, Windows) and **the D2 overload default**, as in the [earlier record](../at-rate/README.md#not-established-here).

Files: `run_producer_faults.sh`, `summaries/` (90 runs plus per-cell `run.log`), `pilot/`, `*binaries.sha256`, `raw-sha256.json` (raw CSVs retained outside the repository).
