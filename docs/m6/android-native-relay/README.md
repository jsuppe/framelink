# Android native relay parity (M6.9d)

**Decision (Jon, 2026-11-19):** full parity with the Windows local native relay, Linux first, then Android. The Linux work is [linux-native-relay](../linux-native-relay/README.md). Android follows the same phases on AHardwareBuffer. The same two decisions apply:
- **Typed API per platform.** The FAR5 sender gets an AHardwareBuffer variant, `fl_gst_session_open_local_native_relay_pool_ahb`. `fl_gst_session_open_local_native_pool_receiver` has no platform types and keeps its name and signature.
- **Shared test sources.** The relay tests run from the shared sources under the same names, with the `_android` suffix.

## Starting point

| Layer | Linux | Android before this work |
|---|---|---|
| Same-allocation relay, `samples/native_relay.h` | yes | yes: `native_relay_android`, `native_relay_process_android` (identity by allocation ID) |
| Import of an external pool | `flCreateChannelFromDmabufs` | `flCreateChannelFromHardwareBuffers` (API 31+) |
| Consumer-pool snapshot | `flSnapshotDmabufConsumerPool` | none |
| FAR4 `open_local_native_relay_input`, `open_local_native_relay` | yes | none (compiled out by `FL_GST_LOCAL_NATIVE_RELAY`) |
| FAR5 sender and `open_local_native_pool_receiver` | DMA-BUF over an authenticated Unix socket | none |
| Relay samples and shared relay tests | yes | none |

## Phases

Each phase is one pull request.

1. **Core consumer-pool snapshot.** In `framelink_ahb.h`:
   - `flSnapshotAHardwareBufferConsumerPool`, `flQueryAHardwareBufferPoolSnapshot`, `flAHardwareBufferPoolSnapshotBuffer` and `flReleaseAHardwareBufferPoolSnapshot`.
   - The Windows and Linux snapshot rules apply: owned references to the exact original allocations; while any snapshot lives, a newly attaching producer gets `FL_BUSY` and geometry requests are answered with the current ring; snapshots outlive the source channel.
   - Test: `ahb_pool_snapshot`.
2. **FAR4 relay entry points** on Android, with setup, cancellation and source-control tests.
3. **FAR5 on Android:**
   - AHardwareBuffers are passed with `AHardwareBuffer_sendHandleToUnixSocket` over the same SEQPACKET socket.
   - The peer is pinned with `SO_PEERCRED`, and the transfer is authenticated under its own domain.
   - The receiver adopts the buffers with `flCreateChannelFromHardwareBuffers`. It checks geometry, pool size and distinct allocation IDs before acknowledging.
   - Adds `fl_gst_session_open_local_native_relay_pool_ahb` and enables `open_local_native_pool_receiver` and `set-local-native-pool-candidate`, plus the pool-export and relay-adapter samples.
4. **Shared relay tests** on the device: the controls, the in-process bin relays, and the broker-composed process relays.
5. **Qualification:** two hops with PCM, allocation identity by allocation ID and zero payload operations in the trace, with an evidence record. Also a hardware-decoder relay, if MediaCodec can decode into the source's reservations.

## Phase 1 (2026-11-20)

The snapshot holds one `AHardwareBuffer_acquire` reference per slot and shares the channel's pin counter. Pin checks are added at producer attach (`Busy`) and on geometry requests (the current ring is handed back), at the same points as Linux.
