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

## Phase 2 (2026-11-20)

**FAR4 on Android.** `FL_GST_LOCAL_NATIVE_RELAY` now covers every platform with a local native session, so `fl_gst_session_open_local_native_relay_input` and `fl_gst_session_open_local_native_relay` are built on Android. The FAR5 half has its own macro, `FL_GST_LOCAL_NATIVE_RELAY_POOL` (Windows and desktop Linux until phase 3). It covers:
- `open_local_native_pool_receiver` and the typed senders;
- the pool-transfer implementation;
- the `framelinkavsrc` pool-receiver profile;
- the `samples/local_native_relay_setup.h` FAR4-plus-snapshot-plus-FAR5 transaction.

Windows and Linux compile exactly what they did before.

**Shared relay tests on Android.** `fl_gst_native_relay` builds for Android with GStreamer and the plugin linked statically. `argv[1]` is a placeholder, so child command lines keep their shape. It registers four tests:
- `gst_relay_local_session_android`
- `gst_relay_local_source_control_android`
- `gst_relay_video_event_controls_android`
- `gst_relay_supervisor_controls_android`

The FAR5 modes and their helpers in `test/fl_gst_native_relay_platform.h` compile only with `FL_GST_LOCAL_NATIVE_RELAY_POOL`. The platform adapter gains the Android identity check: the AHardwareBuffer allocation ID, never a slot or pointer. `Gate` and `Child` use the POSIX path (pipes; `fork`/`exec` of `/proc/self/exe` with `PR_SET_PDEATHSIG`).

## Phase 3 (2026-11-21)

FAR5 on Android.

- **Session:** `fl_gst_session_open_local_native_relay_pool_ahb` is the sender, taking AHardwareBuffers. `fl_gst_session_open_local_native_pool_receiver` is enabled on Android with its common signature. `FL_GST_LOCAL_NATIVE_RELAY_POOL` now covers every platform with a local native session. The `framelinkavsrc` pool-receiver profile (`set-local-native-pool-candidate`) works on Android; `gst_native_av_android` now expects it accepted, like every other platform.
- **Transfer.** It shares `gst/fl_gst_native_relay_pool_linux.h` with desktop Linux:
  - The same abstract SEQPACKET socket, `SO_PEERCRED` pinning and PID-bound mutual HMAC, under Android's own domains ("FrameLink authenticated Android native relay pool v1").
  - The offer names each slot's allocation ID (`AHardwareBuffer_getId`), format and usage. The receiver accepts the offer before any buffer is sent.
  - Each buffer then travels as its own message (`AHardwareBuffer_sendHandleToUnixSocket`), on the non-blocking socket within the transfer's deadline and cancellation.
  - The receiver checks every received buffer against the offered ID, format, usage and geometry, then adopts the same allocations with `flCreateChannelFromHardwareBuffers` before acknowledging.
  - There is no render-node check: Android has one GPU.
- **Sample:** `samples/local_native_relay_setup.h` builds on Android (AHardwareBuffer snapshot plus the typed sender).
- **Core fix (the one Linux phase 3 left for Android):** `flAcquireBuffer` now uses the Windows and Linux order. A slot freed by a release that arrived just before the consumer closed is returned first, and the disconnect is reported only when no slot is free.
- **Transfer fix, shared with Linux:** when the peer closed with data unread, `recvmsg` failed with `ECONNRESET`, and the receive path still walked the control data using the unchanged input length. A zero-length header made that walk spin forever. Any `recvmsg` error now returns `FL_DISCONNECTED` first. It was found on Android: a receiver that rejects a buffer mid-transfer left the sender spinning.

**Tests on Android** (`gst_relay_<name>_android`): `local_process`, `local_pool_failures`, `local_three_process`, `local_three_process_av`, `local_replacement`, `local_recovery`, `local_bin`, `local_bin_negatives` and `setup_negatives`, alongside the phase 2 four. The adapter gains the Android pool export, identity by allocation ID, and the malformed exports:
- duplicate buffer;
- null buffer;
- a foreign 16×16 buffer;
- a ring-sized buffer in another format (refused at adoption).

AHardwareBuffer reader memory is deliberately not CPU-mappable, so the bin reader checks pixels by locking the buffer.

**Checks:**
- Tablet (API 35): the 13 shared relay tests pass. The nine FAR5 tests passed 5 repeated rounds (45 of 45), and the full set passed 5 rounds earlier (65 of 65). Full Android suite 114 of 116 pass; the others are the known EGL skip and the known `gst_av_bench_smoke` host-path failure. `gst_native_av_android` first failed here because it still expected Android to refuse the pool candidate; after the expectation was updated it passes.
- Negative controls (temporary edits, reverted):
  - a wrong role byte in the receiver's MAC check fails FAR5 setup;
  - a sender that sends the buffers in a different order from its offer is refused by the allocation-ID check, and now fails promptly on both sides. Before the `recvmsg` fix, the sender spun.
- Linux: the 36 shared relay tests passed 3 repeated rounds under `-j8`. Windows: see the pull request.

## Phase 4a (2026-11-21)

**Controls and in-process bin relays on Android.** The pause controls (`local_bin_pause`, `local_bin_paused_cancel`) and the 13 in-process GStreamer bin relays run on Android under the shared names:
- `bin`, `lease`, `bin_cancel`, `bin_events`, `bin_supervised_downstream`, `bin_supervised_upstream`;
- `pool_lifetime`, `authenticated_pool`, `local_pool`;
- `local_cancel_downstream`, `local_cancel_upstream`, `local_events`, `pool_controls`.

All 28 Android relay tests now pass.

**New Android samples**, the counterparts of the D3D11 and DMA-BUF ones, with the same rules:
- `samples/native_ahb_relay_pool.h`: the export guard over the native AHardwareBuffer producer pool. It pins every unpublished reservation and refuses busy, partial, foreign or stale pools. `createNativeAhbRelayPool` builds the in-process same-allocation ring through `flCreateChannelFromHardwareBuffers`.
- `samples/native_gst_ahb_relay.h`: the GStreamer read-lease adapter. It accepts only FrameLink AHardwareBuffer *reader* memory (never a producer reservation) and forwards by allocation ID.
- `samples/native_ahb_relay_transfer.h`: the broker-keyed transfer over the phase 3 Android FAR5 transfer.

The adapter's Android branches:
- the caps feature `memory:FrameLinkAHardwareBuffer`;
- a locked AHardwareBuffer write plus a signalled ready fd for the sink;
- reader-memory and allocation identity by ID;
- pixel checks by locking, since AHardwareBuffer reader memory is not CPU-mappable. The shared bin-relay source now uses `checkNativePixels` and `nativeFirstByte` on every platform; on Windows and Linux these are the same GStreamer map as before.

`gst/fl_gst_ahb_memory.hpp` gains a declaration-only mode (`FL_GST_AHB_MEMORY_EXTERN`). Code linked against the static plugin, such as these samples and tests, uses the plugin's allocator type instead of defining a second one.

**Checks:**
- Tablet: all 28 Android relay tests pass, and 3 repeated rounds (84 of 84). Full Android suite 128 of 131: the known EGL skip and the known `gst_av_bench_smoke` failure, plus one `gst_av_parity` failure.
- `gst_av_parity` is intermittent in its consumer-pause and burst cases: 3 of 10 runs failed on this branch, and **2 of 10 on master**, which has neither phase 3 nor phase 4a. So it is a pre-existing Android timing flake, not a regression; it is recorded as open.
- Negative controls (temporary edits, reverted):
  - matching the first reserved slot instead of the allocation ID fails `lease` (unrelated ring accepted) and `bin` (allocation identity);
  - accepting a partially available pool fails the `FL_BUSY` check in `bin_supervised_downstream`;
  - leaking one fd per pool setup fails `pool_lifetime`.
- Linux: the 36 shared relay tests passed 3 repeated rounds under `-j8`.
- Windows: build and exports unchanged (88, 47, 2); the 36 relay tests and 10 repeated rounds pass; the full suite failed only `m4_source_drift` (a timing test; it passed 3 of 3 repeats); the unchanged 060c54e4 suite against the new DLLs 180 of 180.

## Phase 4b (2026-11-22)

**Broker-composed process relays on Android.** The eight process-relay modes run on Android under the shared names: `av`, `process`, `reservation`, `death`, `downstream_death`, `cancel`, `automatic_negative` and `decoder`. Android now runs the same 36 relay tests as Windows and Linux.

**The decoder relay** (`gst_relay_decoder_android`) skips with 77. A MediaCodec decode into the source's reservation is phase 5.

The Android parts in `test/fl_gst_native_relay_platform.h`:
- **`SharedImages`:** the two broker allocations are RGBA8 AHardwareBuffers (GPU sampling plus CPU read and write).
  - AHardwareBuffers cannot be inherited like file descriptors. Each child gets its own `SOCK_SEQPACKET` pair instead. Both buffers are sent into the pair with `AHardwareBuffer_sendHandleToUnixSocket` before the child starts, and the child inherits only its end. Queued messages stay readable after the parent closes its end.
  - The child receives the buffers with `AHardwareBuffer_recvHandleFromUnixSocket` and adopts them with `flCreateChannelFromHardwareBuffers`.
  - `indexOf` names the original allocation by allocation ID.
- **`ReservationOwner`, `reservationOf` and `uploadReservation`:** they use the AHardwareBuffer producer's reservations. Identity is by allocation ID. The upload is a locked CPU write followed by a signalled ready fd.

The automatic-relay negative checks pixels with `checkNativePixels`, like the bin relays, because AHardwareBuffer reader memory is not CPU-mappable. On Windows and Linux this is the same GStreamer map as before.

**Checks** (device runs use `tools/run_android_ctest.py`, which runs the registered ctest suite on the device over adb):
- Tablet (API 35): the seven pass, and the decoder skips. The 36 Android relay tests passed 5 repeated rounds (175 of 175 plus the decoder skip). Full Android suite 137 of 139: the known EGL skip and the decoder skip. `gst_av_bench_smoke` and `gst_av_parity`, recorded as failing in phase 4a, passed in this run; `gst_av_parity` stays recorded as an open flake.
- Negative controls (temporary edits, reverted), the same as on Linux:
  - identity that always names the first allocation fails `process` (`seen==3`);
  - a same-allocation check that always agrees fails `process` (the reversed down ring's timestamps no longer match);
  - an AHardwareBuffer reservation that drops its buffer pin at submit fails `reservation` in the source (`!owner->acquire(0)`).
- Linux: full suite 187 of 187 run serially (the known EGL skip). Under `-j8`, `m4_av_recovery`, `m5_startup`, `m5_preconnect`, `m5_consumer_drain` and `broker_producer_stream` failed under load; each passed 3 of 3 serial runs. The 36 shared relay tests passed 3 repeated rounds under `-j8`.
- Windows: build and exports unchanged (88, 47, 2); the 36 relay tests pass, including `decoder`, and 10 repeated rounds; full suite 187 of 187; the unchanged 060c54e4 suite against the new DLLs 180 of 180.

## Phase 5a (2026-11-22)

**Qualification on the device.** `tools/run_relay_qualification.py --adb <device-dir>` runs the Linux qualification graphs on the tablet. Each run's traces are pulled back and checked on the host with the unchanged `tools/check_relay_trace.py`. The Android build links the plugin statically, so only the test binary is pushed and hashed.

Result at `eb950cbf`: 30 of 30 runs (three graphs, ten runs each); the decoder graph skips until phase 5b. See the [evidence](../android-native-relay-qualification-eb950cb/README.md). This is functional qualification on small frames; Android has no rate, duration or resource cells.

**Checks:**
- Negative control: a relay that maps each frame fails the trace check (`relay: 8 CPU image maps` in `far5_av`, 16 in `broker_process`).
- Linux, through the unchanged local path of the runner: 4 of 4 graphs, including `decoder`; `tools/test_relay_trace.py` passes.

## Phase 5b (2026-11-22)

**Decoder relay on Android.** `gst_relay_decoder_android` now runs instead of skipping:
1. GStreamer produces the H.264 stream (`videotestsrc ! x264enc ! h264parse`), as on the other platforms.
2. The platform's hardware decoder (NDK `AMediaCodec`, here `c2.mtk.avc.decoder`) decodes into an `AImageReader`. Each decoded frame is the decoder's own AHardwareBuffer.
3. GLES samples that buffer through an EGLImage and renders it, converted and scaled to 64×32 RGBA, into the source reservation's own AHardwareBuffer as a framebuffer.
4. The render's EGL native fence is the reservation's ready fd, so FrameLink waits on the GPU's own completion. The decoded image goes back to the decoder once that fence signals.

No CPU copy. GStreamer's `androidmedia` plugin is not used: it enumerates codecs and creates its output surface through Java, and a native test process has no Java VM.

Android renders, like Windows, so the decoder graph's ring and policy declare `GPU_RENDER`. The broker allocations now carry `GPU_COLOR_OUTPUT`. The test requires a hardware decoder: if the default H.264 decoder is a software codec (`c2.android.*`, `OMX.google.*`), or the fixture plugins are missing, it skips with 77 and says why.

**Binder threads.** MediaCodec delivers its callbacks on binder threads. An app process has them, but a native executable does not until it starts them. Without them the codec never returns a frame. The fixture starts the pool (`ABinderProcess_startThreadPool`, resolved at run time from `libbinder_ndk.so`).

**Timestamps.** MediaCodec carries microseconds, so decoded timestamps are the 1/30 s steps rounded down to whole microseconds. MediaCodec reports no duration, so only the timestamps are checked against the decoder. The frame metadata is the same as on the other platforms.

The build links `videotestsrc`, `x264` and `videoparsersbad` statically when the SDK has them (`FL_RELAY_AMC_DECODER`), plus `mediandk`, `EGL` and `GLESv3`.

**Checks:**
- Tablet: `gst_relay_decoder_android` passes: 16 hardware-decoded frames rendered into the reservations, each frame's hash distinct and carried in its PCM, 25600 PCM frames, 37 ms offset, both reversed slots. Full Android suite 138 of 139 (the known EGL skip). The 36 Android relay tests passed 5 repeated rounds (180 of 180, decoder included).
- Negative control (temporary edit, reverted): a decoder that skips its render fails the source's alpha check, and the graph fails.
- Linux: full suite 187 of 187 run serially (the known EGL skip); the 36 shared relay tests passed 3 repeated rounds under `-j8`.
- Windows: build and exports unchanged (88, 47, 2); the 36 relay tests pass, including `decoder`, and 10 repeated rounds; full suite 187 of 187; the unchanged 060c54e4 suite against the new DLLs 180 of 180.
