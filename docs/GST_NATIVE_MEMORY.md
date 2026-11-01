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
caps. Native channels can also be transferred using `adopt-channel` subject to the
local ownership checks below. Legacy defaults
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

## Windows source: opt-in D3D11Memory

With the GStreamer D3D11 development library available (API 1.22+), the same
`native-memory=true` option wraps the original texture in GstD3D11Memory and
advertises `memory:D3D11Memory`. No payload readback/copy is performed by this
source path. GStreamer's wrapped allocator probes an **empty** staging resource
for CPU-accessible pitch/size; that setup cost is not a frame-payload copy and is
not yet performance-qualified. A downstream explicit CPU map is a readback, not
zero-copy; tests use one only to verify pixels. Builds without the D3D11 library
retain legacy support and reject native mode.

The wrapped device is the channel's device. The same retained-memory reader
lifetime applies. Generic asynchronous/cross-device GPU consumers still need a
proven completion handoff; these source tests do not qualify every GPU plugin.
RGBA/NV12 source tests verify pixels, PTS, bounded reuse and retained memory after
shutdown. GStreamer 1.24 D3D11 memory does not implement memory subviews, so the
Windows test retains a memory reference rather than claiming Linux subview parity.

NT object equality is insufficient to establish GPU allocation equality across
independent shared-handle exports. The test uses a controlled, synchronized
**test-only** alias mutation with no concurrent reader; the consumer observes the
changed original allocation without another publication. Production writers must
never bypass reservations this way. A deliberate GPU-copy control is successfully
created/read first, then rejected by that alias oracle. Setup failures fail the
control rather than count as success.

## Android source: explicit opaque AHardwareBuffer memory

`native-memory=true` supports exact RGBA/RGB10 native layouts through
`video/x-raw(memory:FrameLinkAHardwareBuffer)`. This is a FrameLink adapter, **not**
a claim that stock GStreamer GPU elements understand AHB memory. A custom consumer
uses `gst_framelink_memory_get_hardware_buffer` from installed
`framelink/framelink_native.h` and retains the GstMemory until GPU completion.
An independent AHB reference keeps storage alive but does not retain the reader
lease; it is not a substitute for retaining the memory.

The custom allocator refuses implicit CPU mapping, copy and subview operations.
It retains the original AHB and reader lease without copying pixels. Explicit
application-side CPU mapping is outside that zero-copy source boundary. Opaque
Android YUV420 cannot be advertised as NV12 byte layout, and BGRA logical channels
use RGBA native storage; those native-source requests currently fail instead of
silently normalizing/converting them. Their legacy CPU paths remain available.
External-YUV format negotiation/import remains an open task7 integration item.

Device tests verify RGBA/RGB10 AHB allocation IDs, PTS, pixels, two-slot ownership,
reuse and retained memory after source shutdown; implicit CPU maps and opaque
YUV-as-NV12 requests reject. API29 builds pass for arm64/x86_64; runtime evidence
is the API35 tablet (allocation ID oracle requires API31+), not all Android GPUs.

## Adopted channels

Set native-memory before invoking `adopt-channel`. The channel must be a consumer
with FL_DEFER_RELEASE and without FL_GPU_SYNC; native source uses normal producer
readiness waits. An enabled arrival-age limit additionally requires
FL_TRACK_ARRIVAL. Rejection leaves ownership with the caller; success transfers
ownership exactly once, including failure before PLAYING. Source shutdown keeps
outstanding native GstMemory readers alive as above. `flQueryConsumerFlags`
reports local flags only; it is not remote negotiation or import compatibility.
Broker authorization, fresh generation/lease binding and revocation remain the
SessionProvider's responsibilities. No named fallback is introduced.

## Remaining task 7 work

- Android external-YUV/native-layout negotiation and platform native sinks.
- Native sink allocation/pool negotiation and upstream decoder compatibility.
- Authenticated remote requirements, exact device/layout/generation agreement;
  enforce all endpoint permissions and reject unsupported legacy peers.
- Preserve variable duration, DTS, discontinuity and stream-generation metadata.
- Renegotiation and failure/reconnect tests with buffers still in flight.
- GPU completion integration and actual cross-process native pipeline evidence.
