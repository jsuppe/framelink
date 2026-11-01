# Same-allocation read-only relay

`native_relay.h` demonstrates how to join two FrameLink rings without copying
pixels or adding a playback queue. It borrows channels; the application owns
session authentication, handle delivery, native-resource verification and cleanup.

1. Provision immutable pools naming the same allocations. There is exactly one
   payload writer, the original producer. Do not attach independent writers to
   those pools. A transformed output uses different storage instead.
2. Complete both peer handshakes and authenticated memory agreement first.
3. Create `flsample::NativeRelay(upstreamConsumer, downstreamProducer, matcher)`.
   The matcher must verify actual allocation identity and compatible layout/device,
   never only sizes, claimed IDs or raw OS handle values.
4. `initialize()` reserves the idle downstream pool. Call `collect()` frequently.
5. Acquire an upstream frame and call `forward(frame)`. It transfers the held
   frame into an upstream lease, publishes the SAME allocation with exact timing,
   and retains the lease until downstream completion permits slot reacquisition.
6. Stop intake, then call `finish()` until drained. `FL_BUSY` means final readers
   still own frames. No destructor silently cancels or completes those reads.

Disconnect/generation errors retain ownership and require whole-pipeline recovery.
Keep channels and allocations alive until actual GPU work is drained; do not
recycle storage just because a process disconnected. The final consumer must use
its normal GPU completion/lease APIs correctly. Copy/move of the relay is disabled.

`fl_native_relay_linux` provides the three-process qualification fixture and local
slow-reader/death negatives. Windows and Android local fixtures use the same helper.
See [evidence and explicit limits](../docs/m5a/native-relay-20261101/README.md).
