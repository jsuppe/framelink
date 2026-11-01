# Read-only same-allocation relay (M5a.9)

`samples/native_relay.h` is a bounded, single-owner building block using existing
FrameLink APIs. Trusted setup supplies two immutable views of the same native
allocation pool and an exact allocation-identity matcher. Only the original
producer writes payloads. The relay reserves output slots but never writes/maps
pixels; it matches actual allocations, not slot numbers, forwards exact metadata,
and holds an upstream read lease until the downstream slot becomes reusable.

Reacquisition follows the downstream consumed-fence contract. Therefore downstream
GPU completion must be supplied by the final reader; merely destroying a borrowed
handle is not completion. Pool capacity is not a queue-fill target. `collect` and
`forward` are nonblocking; application polling/processing time still costs latency.

Initialize AFTER both producer handshakes and memory agreement: a first attachment
can change the native generation. Initialization requires an idle, exclusively
owned downstream ring; upstream must support deferred leases and frame metadata.
No implicit copy or pixel-format transformation is performed. Wrong identities
reject before consuming the input frame. Changed generations/disconnect poison
this relay, retaining outstanding ownership. `finish` refuses unfinished readers.
The application must stop writers and drain GPU work before resetting a poisoned
pipeline; this helper does not invent a safe automatic restart from peer death.

## Evidence

- Linux native NV12: three processes,16/16 frames, actual DMA-BUF identity/layout,
  reversed downstream slot order, all payload pixels and exact PTS/DTS/duration/
  generation, bounded final drain. FD inheritance is same-user test provisioning,
  not a production cross-UID broker qualification. Producer/consumer map pixels
  for test generation/verification; relay performs no payload mapping/copy.
- Linux/Windows/Android local ownership fixtures: wrong-identity rejection leaves
  input held; two final-reader leases exhaust upstream capacity; retiring one
  releases only the matching allocation. Windows uses canonical NT-object
  identity (NV12); Android uses actual AHB IDs (RGBA, API31+).
- Downstream death must keep the upstream allocation pinned, not implicitly
  declare read completion. This test stops the entire fixture before cleanup;
  it does not qualify recovery from a real GPU context loss.
- Android arm64/x86_64 builds and physical arm64 device execution are recorded.

GPU transform output is demonstrated separately by the Windows decoder example:
NV12→BGRA writes a new shared output allocation as required processing, rather
than a hidden relay copy. Its explicit CPU alternative reports download/conversion/
upload. The relay itself only passes an unchanged allocation.

## Still outside these results

No GPU relay throughput/latency or physical presentation claim. Windows/Android
three-process native relay, real downstream GPU-fence delay/recovery and production
broker integration remain platform/multi-hop qualification gates (M5a.12–13).
Neither matching a handle nor passing CPU pixel tests proves those gates.
