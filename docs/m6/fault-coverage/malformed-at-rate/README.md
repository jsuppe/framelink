# M6.4 on Linux and Android: malformed metadata at rate — 2026-11-26

Revision `1a87145d`. `fl_gst_av_bench` SHA-256 prefixes: Linux `cd2184e4e372e854`, Android arm64 `492e15d96f89e7da`; `libgstframelink.so` `f9d0d96cc0ea1f4b`. Hosts, profile and cells as in the [endpoint-death record](../at-rate/README.md). This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Question

M6.4b lists malformed metadata among the faults to inject. Before this record it was covered only below the live session: `frame_metadata`/`contract` (validation and stale rejection) and `av_memory_adversarial` (malformed negotiation), in the [existing-policy regressions](../README.md). This record injects malformed input into a live A/V session at 1080p60.

## Harness (`fl_gst_av_bench --fault`, commit `1a87145d`)

One producer buffer is corrupted at the test source's push, before FrameLink sees it (video frame 360 or audio block 600, both 6 s in):

| Case | Corruption | Expected producer error (from the sink code) |
|---|---|---|
| `video-no-pts` | PTS invalid | "valid supported frame metadata and TIME mapping required"; on the shared-CPU sink, which refuses without its own message, the generic "Internal data stream error" |
| `video-stale-meta` | FrameLink frame metadata of another stream generation | "stale producer video metadata generation" |
| `audio-no-pts` | PTS invalid | "PCM requires valid PTS, duration and TIME segment" |
| `audio-misaligned` | PCM block one byte short | "PCM block is misaligned, empty or exceeds bounded queue budget" |

**Declared outcome (`explicit-failure`) for every case:** the producer fails with that message, the consumer does not report EOS, both exit normally, and the consumer ends within 2000 ms of the malformed buffer's push.

A producer process alone never registers FrameLink's frame-metadata type (only FrameLink sources add the meta). For `video-stale-meta` the test registers it through the plugin's header, as a relay's upstream FrameLink consumer would. CTest: `gst_av_bench_fault_{video-no-pts,video-stale-meta,audio-no-pts,audio-misaligned}` on the 160x90 cell, passing on Linux and Android. `run_malformed_faults.sh` reproduces the cells.

## Results: 60/60 pass

Consumer end after the malformed push (min–max over 5 runs):

| Cell | Video no PTS | Video stale metadata | Audio no PTS | Audio misaligned |
|---|---|---|---|---|
| Linux native NV12 | 5/5, 0.3–0.8 ms | 5/5, 0.7–0.8 ms | 5/5, 0.6–0.8 ms | 5/5, 0.3–0.8 ms |
| Linux shared CPU NV12 | 5/5, 0.2–0.4 ms | 5/5, 0.1–0.5 ms | 5/5, 0.4–0.5 ms | 5/5, 0.4–0.6 ms |
| Android native RGBA | 5/5, 2.2–2.8 ms | 5/5, 2.1–2.9 ms | 5/5, 2.1–3.5 ms | 5/5, 2.5–2.9 ms |

- Every producer failed with its declared message.
- **The malformed buffer was never delivered.** The consumer received 359 video frames on the video cases (frames 1–359, before the bad frame) and 357–360 on the audio cases.
- **The consumer is not told why.** Its error was "audio peer closed before GDP EOS" (47 runs), "Internal data stream error." (12, all shared CPU) or "coordinated session not ready or cancelled" (1). These are peer-failure errors; the producer's reason is not carried to the consumer.

## Pilots (kept, not part of the 60)

One run of each case on Linux native and shared CPU before the commit (`pilot/`). Three cases passed on both. `video-stale-meta` failed on both (`*-vstale`): the producer aborted in the test's own CHECK, because the meta type was not registered in the producer process. After the harness registered it as described above, both passed (`*-vstale-2`).

## Not established here

- **Malformed metadata on the wire** (a corrupted frame record or GDP packet between processes): covered only by the contract and adversarial unit tests.
- **Other malformations:** non-monotonic or overlapping PTS, an invalid DTS or duration overflow on video, and caps or segment changes mid-stream. The first has no documented outcome to declare.
- **Carrying the producer's reason to the consumer**, which would let an application tell malformed input from a crash. A product question, not a defect.
- Other cells, growth across repeats, D2, as before.

Files: `run_malformed_faults.sh`, `summaries/` (60 runs plus per-cell `run.log`), `pilot/`, `*binaries.sha256`, `raw-sha256.json` (raw CSVs retained outside the repository).
