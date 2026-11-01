# Native GStreamer memory integration (M5a.7 — partial)

## Linux source: opt-in linear DMA-BUF

`framelinksrc native-memory=true` advertises `video/x-raw(memory:DMABuf)`.
It wraps a duplicate descriptor of the **original ring allocation** with
GstDmaBufMemory and attaches GstVideoMeta containing actual offsets/strides.
The source performs no pixel map, readback or payload copy on this path.
Producer readiness currently uses FrameLink's normal readiness wait; this is
not an asynchronous all-GPU scheduling claim.

Supported initial scope: Linux, linear modifier, one backing DMA-BUF object per
image (including two-plane NV12 in that object). Other layouts fail explicitly;
there is no fallback to CPU pixels. Native mode rejects sysmem-only downstream
caps. Android/Windows native-source adapters and externally adopted native
channels are not yet wired; those cases reject this option. Legacy defaults
remain the CPU-copy path on all platforms.

The source transfers each acquired frame to a bounded FrameLink reader lease.
The GstMemory finalizer completes the reader; shared memory subviews retain the
parent and therefore the lease. The channel remains alive after source shutdown
until the last exported memory reader finishes. Downstream must retain the
GstMemory until all asynchronous GPU access has completed—dropping its final
reference before GPU completion violates this contract. Native fence handoff
and platform allocator integration for general GPU consumers remain subsequent
M5a.7 work. This is not permission to reuse a frame while a GPU reads it.

Validation checks RGBA/NV12 allocation identity via DMA-BUF fstat, exact pixels,
PTS, two-slot saturation, retained memory-subview lifetime after shutdown, and
sysmem-only negotiation rejection. CPU mapping in the test verifies pixels; it
is not performed by the native source. No decoder, native sink, cross-process
GStreamer session, frame-rate, multi-hop or physical-output claim follows.

## Remaining task 7 work

- Windows D3D11 and Android native-memory source/sink adapters.
- Native sink allocation/pool negotiation and upstream decoder compatibility.
- Authenticated remote requirements, exact device/layout/generation agreement;
  enforce all endpoint permissions and reject unsupported legacy peers.
- Preserve variable duration, DTS, discontinuity and stream-generation metadata.
- Renegotiation and failure/reconnect tests with buffers still in flight.
- GPU completion integration and actual cross-process native pipeline evidence.
