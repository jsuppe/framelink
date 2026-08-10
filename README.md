# framelink

Zero-copy GPU frames between processes. One process draws, another reads the
**same GPU memory** — no copies, no encode, no network.

Windows today. Linux (dma-buf) and Android (AHardwareBuffer) are next; the API
and wire format are shaped for them already, and nothing platform-specific
appears in the core header.

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

**The consumer owns the ring.** It calls `flCreateChannel` and allocates the
buffers; producers attach and draw into them. `flOpenProducer` never creates a
channel — it returns `FL_NOT_FOUND` if nobody is consuming.

This is not arbitrary. It is `dequeueBuffer`/`queueBuffer`, the same shape as
Android's `Surface`/BufferQueue and `AImageReader`, where buffer ownership
belongs to the consumer and is not something an API gets to choose. Designing
it the other way ports fine to Windows and Linux, then hits a wall on Android.

A consequence worth knowing: geometry is the consumer's. A producer reads
width/height/format from `flQuery` rather than requesting them, exactly as an
Android producer does not choose a Surface's size.

## Not D3D11-only

`flImage` is opaque. `framelink_d3d11.h` turns it into an `ID3D11Texture2D`,
and `flImageSharedHandle` hands out the raw shared NT handle so a consumer or
producer can import it into **D3D12, Vulkan or CUDA** instead.

If you draw with something other than the channel's D3D11 device, do **not**
use `flSubmit` — it signals from framelink's own context, which orders nothing
against your writes. Use `flSharedReadyFence` + `flBeginSubmit`/`flEndSubmit`
and signal the fence inside your own submit. A Vulkan compositor does exactly
this.

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

Working: consumer-owned rings, one producer per channel, BGRA8, version refused
at handshake, D3D12/Vulkan/CUDA interop via the shared handle, bounded
back-pressure with latest-frame-wins.

Not yet: Linux and Android backends, `framelink_vulkan.h`, NV12/RGB10A2, access
control, and fan-out to several consumers of one channel (today N consumers
means N channels, so the producer writes N times).

MIT licensed.
