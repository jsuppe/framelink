# framelink

Native shared GPU frames between processes. A native producer and consumer can
use the **same GPU allocation** without a payload copy. CPU maps, current
GStreamer system-memory elements and GDP audio can copy; see the capability
matrix below rather than assuming every pipeline is zero-copy.

**Windows** (shared NT-handle textures), **Linux** (dma-buf over a Unix socket),
and **Android** (AHardwareBuffer) share the core C API. Platform headers expose
interop handles; allocation/format/fence support still depends on the driver.

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

This is not arbitrary. It is `dequeueBuffer`/`queueBuffer`, the same shape as
Android's `Surface`/BufferQueue and `AImageReader`, where buffer ownership
belongs to the consumer and is not something an API gets to choose. Designing
it the other way ports fine to Windows and Linux, then hits a wall on Android.

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

**NVIDIA note:** its GBM backend refuses `RENDERING|LINEAR` together, so a
CPU-mappable buffer is not GPU-renderable. framelink falls back automatically
and logs it; a GPU producer on NVIDIA should leave `FL_MAP_CPU` off and import
the dma-buf.

A/V samples, signed Android cross-app ownership and qualified platform behavior
are described in the [M5 capability matrix](docs/m5/CAPABILITY_MATRIX.md).
For integration, start with the [Windows decoded-pipeline guide](docs/M5_WINDOWS_PIPELINE.md)
or [Android cross-app sample](android/crossapp/README.md).
Native GStreamer allocation/zero-copy negotiation is planned in M5a; fan-out to
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
