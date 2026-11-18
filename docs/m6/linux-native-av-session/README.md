# M6.9 on Linux: native DMA-BUF producer in coordinated A/V sessions — 2026-11-18

Base revision: `b11002e6` (framelink `master`). This record is dated; current status is in [STATUS.md](../../STATUS.md).

## What changed

Before this change the explicit local native session profile (`fl_gst_session_open_local_native[_epoch]` and `set-local-native-candidate` on both A/V bins) was compiled for Windows only and accepted only D3D11 candidates. On Linux a coordinated session could carry video only by copying it into the ring.

- **Session profile on Linux.** `FL_GST_LOCAL_NATIVE_SESSION` is defined on Windows and desktop Linux (not Android). The profile's candidate check uses the platform's resource (`FL_GST_LOCAL_NATIVE_RESOURCE`: D3D11 on Windows, DMA-BUF on Linux). The rules are otherwise unchanged:
  - the producer is `GPU_SAMPLE` or `GPU_SAMPLE|GPU_RENDER` with `operations=0`;
  - the receiver is native (`GPU_SAMPLE`, deferred release) or CPU readback (`CPU_READ`, `COPY|READBACK`);
  - there is no fallback.
- **Windows-only parts stay Windows-only.** These are the relay entry points (`open_local_native_relay*`, `open_local_native_pool_receiver`), `set-local-native-pool-candidate` and `set-local-d3d11-device-owner`. On Linux they return false.
- **`framelinkavsink` video pad** now advertises `video/x-raw(memory:DMABuf)` on Linux, with the same formats as `framelinksink`. Before, the pad offered native memory only for D3D11.
- **Producer actions on the bin.** `framelinkavsink` forwards `set-native-ready-fd` and `acquire-native-buffer` to its inner `framelinksink` on DMA-BUF and AHardwareBuffer builds. A DMA-BUF writer must give the sink its ready fence before publication, and without these actions a bin user could not.
  - They are not registered on Windows, so the Windows elements' interface is unchanged.
- **No change to the frozen Windows API.** The added symbols exist only in the Linux build. There is no wire, protocol or default change.

## Test: `gst_native_av_linux`

`test/fl_gst_native_av_linux.cpp` runs both bins with `set-local-native-candidate`. The producer writes into the sink's own pool, which holds the ring's DMA-BUF allocations, and signals an eventfd ready fence. Each A/V case sends 8 frames with contiguous PCM and a 37 ms video lead.
- **Native receiver:** each received buffer is DMA-BUF memory whose inode matches one of the producer's allocations. The producer saw exactly 2 distinct allocations, the ring's pool of 2.
- **CPU readback receiver:** the same pixels.
- **Both receivers:** PTS, DTS, duration, PCM content, the A/V offset and the session's video EOS count all match.

Output on the Linux host (Ubuntu 26.04, RTX 3090, NVIDIA 580.178.04, GStreamer 1.28.2):

```
native bins (CPU readback receiver): 28 configuration controls, pool-receiver rejection and both-role bounded cancellation pass
native local setup (CPU readback receiver): positive, invalid policies, foreign resource, candidate mismatch, plain-profile isolation, capacity, epoch and cancellation pass
native bins (native receiver): 28 configuration controls, pool-receiver rejection and both-role bounded cancellation pass
native local setup (native receiver): positive, invalid policies, foreign resource, candidate mismatch, plain-profile isolation, capacity, epoch and cancellation pass
RGBA native DMA-BUF A/V bins, native receiver: 8 frames, original allocations, pixels, PCM and A/V offset pass
NV12 native DMA-BUF A/V bins, native receiver: 8 frames, original allocations, pixels, PCM and A/V offset pass
RGBA native DMA-BUF A/V bins, native receiver, sample+render producer: 8 frames, original allocations, pixels, PCM and A/V offset pass
RGBA native DMA-BUF A/V bins, CPU readback receiver: 8 frames, original allocations, pixels, PCM and A/V offset pass
PASS
```

The negatives mirror the Windows native A/V controls:
- invalid version, generation and operations;
- a shared-CPU or D3D11 resource;
- `GPU_RENDER` alone, or `CPU_WRITE`;
- a distinct candidate from the peer;
- a plain A/V peer;
- a queue that exceeds capacity;
- a negative epoch;
- cancellation within the bound;
- immutability after selection.

## Regressions

| Platform | Build | Result |
|---|---|---|
| Linux (above host) | Ninja Release, GStreamer elements built | 141/142 pass, 1 skipped (`read_lease_egl_delayed_completion`, as on the base revision) |
| Windows (RTX 5090 qualification host) | `cmake --preset ci` with the same patch | 180/180 pass (the qualified revision's count) |
| Android arm64 | NDK 28.2, GStreamer 1.24.13 Android SDK | builds; not run (Android M6.9 is the next task) |

The base revision on the same Linux host gave 140/141 with the same skip. `m5_preconnect` and `m5_consumer_drain` fail when run in parallel (`ctest -j4`) and pass serially on both revisions.

## Not established here

- **9d:** real decoder or renderer producers, the relay and two-hop paths on Linux, and producer GPU writes with real fences. This test writes through the CPU and signals an already-complete eventfd.
- **9e:** endurance, performance, trace and final-revision gates.
- **Android:** AHardwareBuffer sessions.
- No claim of physical presentation timing or arbitrary decoder compatibility.
