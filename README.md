# framelink

Native shared GPU frames between processes. A native producer and consumer can
use the **same GPU allocation** without a payload copy. CPU maps, current
GStreamer system-memory elements and GDP audio can copy; see the capability
matrix below rather than assuming every pipeline is zero-copy.

**Windows** (shared NT-handle textures), **Linux** (dma-buf over a Unix socket),
and **Android** (AHardwareBuffer) share the core C API. Platform headers expose
interop handles; allocation/format/fence support still depends on the driver.

## Design and progress — November 1, 2026

**Shipped on `master`: M1–M5 are complete within the documented platform scope.**
This includes A/V session contracts, producer/consumer examples, synchronized
playback, signed Android cross-app integration and targeted platform qualification.
See the [M5 capability matrix](docs/m5/CAPABILITY_MATRIX.md) for exact supported
paths, evidence and exclusions; passing tests do not imply universal driver support.

**M5a: native-memory pipeline boundaries — in development, not merged.**
The current development branch is
[`feature/m5a-native-memory`](https://github.com/jsuppe/framelink/tree/feature/m5a-native-memory).

- **M5a.1 locally validated and pushed:** versioned required/preferred memory-usage
  contract and strict copy/readback/upload/conversion policy, commit
  [`3fc6f67`](https://github.com/jsuppe/framelink/commit/3fc6f67).
- **M5a.2 locally validated and pushed:** actual native-resource queries for
  device identity, format/layout, usage, mapping costs and supported interop,
  commit [`4aab05f`](https://github.com/jsuppe/framelink/commit/4aab05f).
  Linux, Windows/Caspar and Android device checks passed **6/6 each**, with no
  skips; Android arm64/x86_64 builds passed. These test resource/mapping/pixel/PTS
  behavior—not GPU throughput, arbitrary driver imports or physical output.
- **M5a.3 next incomplete task:** external allocation ownership and safe buffer
  retirement. Inspection has begun; no implementation/validation result is yet
  published. A task marked in progress is not evidence of a running executor.
- **M5a.4–15 remain pending:** transport/presentation separation, bounded small
  pools, strict/fallback negotiation, native GStreamer sink/source integration,
  Windows decoder example, relay, instrumentation and multi-hop qualification.
- **M6 not started:** reliability/performance qualification, including 30-minute
  A/V soaks, at least 100 reconnect cycles per supported target and measured
  tracing overhead. **M7 not started:** release packaging and developer handoff.

The intended M5a design shares allocations at process boundaries, with explicit
CPU/GPU usage and import compatibility negotiation. Consumer-provided and
producer-owned pools must retain storage until every downstream reader's actual
CPU/GPU work completes. Strict zero-copy paths must reject incompatible resources
rather than silently copy. Permitted staging or conversion must be explicit.

**Pool capacity is not intentional queue latency.** A 12-buffer allocation must
not imply a 12-frame playback reservoir at every hop. Pass-through boundaries
preserve allocation identity and the common A/V media timeline; final outputs own
presentation scheduling. Zero-copy does not mean zero synchronization or elapsed
transfer time. Audio transport costs are assessed separately from video sharing.

Start here:
- [Current detailed task checklist](https://github.com/jsuppe/framelink/blob/feature/m5a-native-memory/docs/ROADMAP_TASKS.md)
- [Pipeline-boundary design and acceptance criteria](docs/M5A_PIPELINE_BOUNDARIES.md)
- [Development memory contract](https://github.com/jsuppe/framelink/blob/feature/m5a-native-memory/docs/MEMORY_CONTRACT.md)
- [Adapter validation evidence](https://github.com/jsuppe/framelink/tree/4aab05f/docs/m5a/adapters-20261101)
- [Remaining milestone roadmap](docs/REMAINING_ROADMAP.md)

This is a dated source/evidence snapshot, not a live executor-status dashboard.
Development-branch results are not merged release features.

```c
// consumer — owns the buffers
flChannel* ch;
flCreateChannel("myapp.video", 1280, 720, FL_FORMAT_BGRA8, 4, &ch);
for (;;) {
    flFrame f;
    if (flAcquireFrame(ch, &f, 100) != FL_OK) continue;
    use(flImageD3D11(f.image));      // the producer's actual texture
    flRelease(ch, &f);
}
```

```c
// producer — attaches and draws
flChannel* ch;
flOpenProducer("myapp.video", &ch);  // FL_NOT_FOUND if nobody is consuming
for (;;) {
    flBuffer b;
    if (flAcquireBuffer(ch, &b, 100) != FL_OK) continue;
    draw_into(flImageD3D11(b.image));
    flSubmit(ch, &b, -1);
}
```

## The one thing to understand

The sketches above omit error/cleanup handling and assume work is complete at
submit/release. Asynchronous GPU work must use the platform ready/consumed fence
contract; releasing a slot before its GPU reads finish is not safe.

**The consumer owns the ring.** It calls `flCreateChannel` and allocates the
buffers; producers attach and draw into them. `flOpenProducer` never creates a
channel — it returns `FL_NOT_FOUND` if nobody is consuming.

This is the current FrameLink channel model, similar to Android Surface/BufferQueue
usage. It is not a universal restriction on sharing AHardwareBuffer allocations.
M5a adds explicit external-allocation ownership and lifetime negotiation; that
new path is not implemented by the examples above.

Geometry remains consumer-owned. Producers query the accepted configuration
with `flQuery`; `flRequestGeometry` is an advisory resize request, not permission
to assume that dimensions or allocation generation have changed.

## Not D3D11-only

`flImage` is opaque. `framelink_d3d11.h` turns it into an `ID3D11Texture2D`,
and `flImageSharedHandle` hands out the raw shared NT handle so a consumer or
producer can import it into **D3D12, Vulkan or CUDA** instead.

If you draw with something other than the channel's D3D11 device, do **not**
use `flSubmit` — it signals from framelink's own context, which orders nothing
against your writes. Use `flSharedReadyFence` + `flBeginSubmit`/`flEndSubmit`
and signal the fence inside your own submit. A Vulkan compositor does exactly
this.

## NV12

Linux, Windows and supported Android allocations expose `FL_FORMAT_NV12`: full-resolution Y plus half-resolution
interleaved UV, with even width and height. See [NV12 support](docs/nv12.md)
for plane mapping, GStreamer examples, compatibility and tested limits.

## Building

```
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

The test is two processes: one owns the ring and verifies pixels, the other
attaches and draws. Each frame's colour is derived from its frame number and
checked against the timestamp that travelled with it, so slot aliasing or a
missing fence wait fails — not merely "no pixels arrived".

Set `FRAMELINK_DEBUG=1` for connection diagnostics. Errors always print.

## Status

Consumer-owned rings, one producer per channel, bounded backpressure, version
refusal, BGRA/RGBA/NV12/RGB10A2 and native interop are implemented with documented
driver-dependent format limits. Windows uses shared timeline fences. Linux and
Android expose explicit ready/consumed sync-FDs through `framelink_sync_fd.h`;
external GPU producers must supply actual completion, not submit unfinished work.
The Windows shared-fence API is not silently emulated on Linux/Android.

**Linux allocation note:** on the tested NVIDIA GBM path, `RENDERING|LINEAR`
was refused and legacy CPU-mappable allocation could fall back to LINEAR-only.
That does not guarantee GPU renderability. Do not infer actual usage from requested
flags; M5a capability queries expose the allocation facts, and strict negotiation
must reject a path that cannot establish required rendering support.

A/V samples, signed Android cross-app ownership and qualified platform behavior
are described in the [M5 capability matrix](docs/m5/CAPABILITY_MATRIX.md).
For integration, start with the [Windows decoded-pipeline guide](docs/M5_WINDOWS_PIPELINE.md)
or [Android cross-app sample](android/crossapp/README.md).
Native GStreamer allocation/zero-copy negotiation remains pending within M5a; fan-out to
multiple consumers of one channel is not implemented (use separate channels).
Physical output timing and seamless device/suspend behavior are not blanket claims.

MIT licensed.

## Configurable validation samples

Build `framelink_producer` and `framelink_consumer` to exercise BGRA/NV12,
geometry, pacing, ring sizes, reconnects, pixel verification and Windows GPU
fences. See [sample commands and options](samples/README.md).

For opt-in per-frame timing and bounded capture, see [performance tracing](docs/PERFORMANCE_TRACING.md).

### A/V session control contract

The optional [`framelink_session.h`](include/framelink_session.h) helpers provide
bounded format negotiation, a versioned configuration codec, generation checks,
and checked clock/sample timestamp mapping. Audio remains separate from the video
frame API. See [the A/V session contract](docs/AV_SESSION_CONTRACT.md) for lifecycle,
authentication, queue policy, and the boundary between these helpers and the
application's transport implementation. `session_contract` is hardware-independent.
