# Linux native relay qualification (M6.9d phase 5)

Exact revision `61375e75` (clean tree), Linux, NVIDIA GeForce RTX 3090, driver 580.178.04, GStreamer 1.28.2. Runner: `tools/run_relay_qualification.py --repeat 10`; checker: `tools/check_relay_trace.py`. **40 of 40 runs passed.**

Every graph is two hops with PCM, run as three processes: source, relay and final reader. `FRAMELINK_TRACE` and `FRAMELINK_GST_TRACE` were on in every process.

| Graph | What it is | Frames relayed per run | Runs | Time per run |
|---|---|---|---|---|
| `far5_av` (`--relay-three-local-av`) | FAR4 source-time input, then FAR5 authenticated pool transfer to the final reader; no inherited native handles | 8, plus 384 PCM frames | 10/10 | 0.15–0.21 s |
| `broker_process` (`--relay-process-diagnostic`) | Broker-composed rings (one allocation pair handed to both rings), raw source writes; an empty graph, then 16 frames | 16, plus 768 PCM frames | 10/10 | 0.28–0.42 s |
| `broker_reservation` (`--relay-reservation-diagnostic`) | As above, with the source writing GstMemory reservations | 16, plus 768 PCM frames | 10/10 | 0.21–0.33 s |
| `decoder` (`--relay-decoder-diagnostic`) | Hardware H.264 decode (NVDEC) into the source's reservations; each frame's hash is carried in its PCM block | 16, plus 25600 PCM frames | 10/10 | 0.90–1.01 s |

## What each run proves

**Allocation identity by inode.** This is checked inside the graphs, against the original allocations:
- `far5_av`: after the FAR5 transfer, the relay maps upstream to downstream frames by DMA-BUF inode, never by slot.
- Broker graphs: the final reader checks each received image's inode against the two broker allocations, and requires both of them (the down ring is adopted in reverse order).
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

In all 40 runs the relay had zero payload operations and zero maps. The only CPU maps are explicit test work:
- the raw source's upload (`broker_process`, `far5_av`);
- the final reader's pixel oracle.

The reservation and decoder sources have no FrameLink maps at all.

**PCM and timing.** These are checked by the graphs themselves:
- exact PCM bytes and frame counts;
- the 37 ms mapped A/V offset;
- PTS, DTS, duration and generation of every frame;
- authenticated video END and audio EOS;
- the held final-reader lease excludes reuse until it is released.

**Hardware decode** (`decoder`). The steps:
1. `nvh264dec` decodes into GL memory.
2. GStreamer GL converts and scales on the GPU.
3. CUDA copies the result into the reservation's own DMA-BUF through an EGLImage. NVIDIA imports a linear DMA-BUF for GL sampling only, so GL cannot render into it.
4. The source reads its reservation back to hash it.
5. The final reader, two processes later, hashes the same pixels and compares them with the hash carried in the PCM.

Every frame's hash is distinct and every alpha value is 255. The relay ring and policy declare sampling, not `GPU_RENDER`: on Linux the write is a copy, and NVIDIA refuses render usage on linear buffers.

## Negative controls

Each was a temporary edit, reverted, and each failed as intended:

| Edit | Result |
|---|---|
| The relay maps each frame before forwarding (`SessionNativeRelay::forward`) | checker: `relay: 8 CPU image maps` |
| The decoder skips its CUDA copy | source: decoded alpha check fails (the reservation was never written) |
| Identity always names the first allocation (phase 4b) | `process`: both-allocations check fails |
| The same-allocation check always agrees (phase 4b) | `process`: the reversed down ring is no longer matched |
| A Linux reservation drops its pin at submit (phase 4b) | `reservation`: the slot is reacquirable after the remote drain |

`tools/test_relay_trace.py` checks the checker itself on synthetic traces in hosted CI. It covers a CPU map or payload in the relay, a lossy or missing footer, missing final reads and an empty run.

## Files

- `summary.json`: every run with its result.
- `environment.json`: revision, GPU and driver, GStreamer, and SHA-256 of the binary, the plugin and the relay sources.
- `runner.log`
- `runs/<graph>/run<n>/check.json` and `stdout.txt`.
- `traces.tar.xz` (SHA-256 in `traces.tar.xz.sha256`): all 140 raw trace files (every process of every run).

## Scope

This is **functional** relay qualification on small frames (64×32; 8 or 16 frames per graph), repeated. It covers what phase 5 of the plan names: two hops, PCM, identity by inode, and zero payload copies in the trace.

It is **not** rate, duration or resource qualification. The Windows native relay has those for its advertised cells: paced 1080p60 and 4K30 runs, 1800 s duration, and memory, handle and thread slopes. Linux has none of them yet. The trace is FrameLink's boundary instrumentation; it says nothing about driver-internal copies or physical memory traffic. Same suite and stress results for this revision: full Linux suite 186 of 186 (one known skip); the 36 shared relay tests passed 10 repeated rounds under `-j8`.
