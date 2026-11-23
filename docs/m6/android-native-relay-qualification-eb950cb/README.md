# Android native relay qualification (M6.9d phase 5a)

Exact revision `eb950cbf` (clean tree). Tablet: Android 15 (API 35), arm64-v8a, MediaTek MT8786V/N, ARM Mali-G52 MC2 (OpenGL ES 3.2, r49p1), GStreamer 1.24.13 linked statically. Runner: `tools/run_relay_qualification.py --adb <device-dir> --repeat 10`; checker: `tools/check_relay_trace.py`, run on the host over the pulled traces. **30 of 30 runs passed; the decoder graph skips** (phase 5b).

These are the Linux qualification graphs ([61375e7](../linux-native-relay-qualification-61375e7/README.md)), from the same shared relay test binary, run on the device. Every graph is two hops with PCM, run as three processes: source, relay and final reader. `FRAMELINK_TRACE` and `FRAMELINK_GST_TRACE` were on in every process.

| Graph | What it is | Frames relayed per run | Runs | Time per run |
|---|---|---|---|---|
| `far5_av` (`--relay-three-local-av`) | FAR4 source-time input, then FAR5 authenticated pool transfer to the final reader; no inherited native handles | 8, plus 384 PCM frames | 10/10 | 0.93–1.10 s |
| `broker_process` (`--relay-process-diagnostic`) | Broker-composed rings (one allocation pair handed to both rings), raw source writes; an empty graph, then 16 frames | 16, plus 768 PCM frames | 10/10 | 1.70–1.91 s |
| `broker_reservation` (`--relay-reservation-diagnostic`) | As above, with the source writing GstMemory reservations | 16, plus 768 PCM frames | 10/10 | 1.12–1.20 s |
| `decoder` (`--relay-decoder-diagnostic`) | Hardware decode into the source's reservations | — | skipped (77) | — |

Times include the adb round trip for each run.

## What each run proves

**Allocation identity by allocation ID** (`AHardwareBuffer_getId`). It is checked inside the graphs, against the original allocations:
- `far5_av`: after the FAR5 transfer, the relay maps upstream to downstream frames by allocation ID, never by slot. The receiver also checks every received buffer against the ID in the sender's offer.
- Broker graphs: the final reader checks each received image's allocation ID against the two broker AHardwareBuffers, and requires both of them (the down ring is adopted in reverse order).
- The source checks that each reacquired reservation is the same allocation.

**No payload work in the relay.** The trace checker classifies each process by what it did:
- a process that only publishes is the source;
- one that acquires and publishes is the relay;
- one that only acquires is the final reader.

It then requires:
- every trace has a lossless recorder footer and rows;
- the relay forwarded exactly the frames it acquired;
- the relay performed **no payload operation** (details bit 1) and **no CPU map** of any image;
- the final readers acquired, completed and retired every frame.

In all 30 runs the relay had zero payload operations and zero maps. The only CPU maps are explicit test work:
- the raw source's upload (`broker_process`, `far5_av`);
- the final reader's pixel oracle (`flImageMapPlanes`).

The reservation source writes by locking its AHardwareBuffer reservation, so it has no FrameLink maps.

**PCM and timing.** These are checked by the graphs themselves:
- exact PCM bytes and frame counts;
- the 37 ms mapped A/V offset;
- PTS, DTS, duration and generation of every frame;
- authenticated video END and audio EOS;
- the held final-reader lease excludes reuse until it is released.

## Negative controls

Each was a temporary edit, reverted, and each failed as intended:

| Edit | Result |
|---|---|
| The relay maps each frame before forwarding (`SessionNativeRelay::forward`) | checker: `relay: 8 CPU image maps` (`far5_av`), `16 CPU image maps` (`broker_process`) |
| Identity always names the first allocation (phase 4b) | `process`: both-allocations check fails |
| The same-allocation check always agrees (phase 4b) | `process`: the reversed down ring is no longer matched |
| An AHardwareBuffer reservation drops its pin at submit (phase 4b) | `reservation`: the slot is reacquirable after the remote drain |

## Files

- `summary.json`: every run with its result.
- `environment.json`: revision; Android release, device, build fingerprint, SoC and GLES renderer; the static GStreamer version; SHA-256 of the binary and the relay sources.
- `runner.log`
- `runs/<graph>/run<n>/check.json` and `stdout.txt`; `runs/decoder/run1/stderr.txt` holds the skip reason.
- `traces.tar.xz` (SHA-256 in `traces.tar.xz.sha256`): all 110 raw trace files (every process of every run).

## Scope

This is **functional** relay qualification on small frames (64×32; 8 or 16 frames per graph), repeated. It covers phase 5 of the plan except the decoder: two hops, PCM, identity by allocation ID, and zero payload copies in the trace. The hardware-decoder relay (MediaCodec into the source's reservations) is phase 5b.

It is **not** rate, duration or resource qualification. Windows and Linux have paced rate cells; Android has none. The trace is FrameLink's boundary instrumentation; it says nothing about driver-internal copies or physical memory traffic. Same suite and stress results for the phase 4b code: full Android suite 137 of 139 (the EGL and decoder skips); the 36 Android relay tests passed 5 repeated rounds.
