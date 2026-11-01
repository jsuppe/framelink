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

- Android external-YUV/native-layout negotiation; opaque YUV is not native NV12.
- Upstream decoder device/allocation/completion compatibility beyond explicit adapters.
- Authenticated remote requirements, exact device/layout/generation agreement;
  enforce all endpoint permissions and reject unsupported legacy peers.
- Full metadata opt-in is now available (see FRAME_METADATA.md); combine its
  qualified timing path with downstream native decoder/GPU qualification.
- Renegotiation and failure/reconnect tests with buffers still in flight.
- GPU completion integration and actual cross-process native pipeline evidence.

## Windows native sink: explicit reserved-buffer adapter

With D3D11-enabled GStreamer, `framelinksink native-memory=true` accepts only
`memory:D3D11Memory` issued by that sink's `acquire-native-buffer` action. The
UINT timeout argument is bounded to 0..1000ms; the result is an owned `GstBuffer*`
or NULL. Call after PAUSED/PLAYING. Stop acquisition calls before changing state.
The native sink preconnects (default finite deadline 1000ms in this mode).
Its format/geometry must match the existing consumer ring; live pool replacement
is rejected. Legacy system-memory mode remains the default on all platforms.

The returned memory wraps the original shared texture. Write through that texture's
same D3D11 device/immediate context, then push the buffer. Native submission signals
FrameLink readiness on that context; no sink-side map, pixel copy, conversion or
upload occurs. Synthetic test uploads create the source pixels; consumer readback
in the oracle verifies them and is not a claim of end-to-end GPU-only output.
Pending CPU-upload flags, truncated memory, foreign allocations and duplicate
publication reject. They never silently take the legacy upload path.

Local references keep both producer slot and channel alive after sink shutdown.
Remote completion alone cannot reuse a locally-retained slot. **Before abandoning
an unpublished buffer, finish all GPU work on it.** After publication, do not write
through retained aliases. This explicit application adapter is not yet a generic
GStreamer decoder pool: generic decoder cancellation/completion
integration, remote permission agreement and actual decoder compatibility remain
open. Do not attach arbitrary decoder-owned textures and assume shared adapter
identity makes them FrameLink allocations.

The pipeline gate currently covers RGBA. The internal producer adapter additionally
covers NV12, original texture identity, exact metadata, retained-memory reuse and
shutdown lifetime. No full-rate or cross-process pipeline claim is made here.
Linux and Android native sink modes still reject; their native source adapters
and legacy sink paths remain supported as documented above.

### Explicit-completion ALLOCATION pool

Native sinks answer ALLOCATION queries that request a pool AND set the string
field `framelink-completion-contract` to `same-device-before-abandon-v1` on Windows
or `external-sync-fd-before-submit-v1` on Linux/Android. Missing/unknown contracts,
mismatched geometry, wrong/ANY memory features and incompatible pool configuration reject. This
local API agreement is not authenticated remote memory/usage negotiation.

The offered GstBufferPool has minimum zero and maximum equal to the existing
FrameLink image capacity. It does not prefill a playback queue. Each acquisition
reserves an available original image; returning the wrapper does not cache a stale
published reservation. The native image pool persists without reallocating pixels.
DONTWAIT exhaustion returns GST_FLOW_EOS; normal acquisition uses the sink's bounded
buffer-wait-ms. The caller must treat exhaustion according to its agreed drop/error
policy. Finalizers can release references while another acquisition waits.

Sink shutdown revokes old pools (further acquisition returns FLUSHING), while
outstanding memory retains ownership. The pool advertises VideoMeta and
FrameLinkExplicitCompletionV1 options. POSIX callers must set readiness before
submission, with a fence covering all prior channel writes (or -1 only after
completion). On Windows, upstream must use the supplied device and
finish GPU work before abandoning unpublished buffers; it must not hold the device
lock across acquisition. Generic decoder integration remains unqualified until
its allocation, device, cancellation and completion behavior satisfies this contract.

## Android explicit native sink

Android `native-memory=true` now accepts sink-issued exact RGBA/RGB10 opaque AHB
buffers through `acquire-native-buffer`. Before publishing, call
`set-native-ready-fd(buffer, fd)` and check its Boolean result. The FD is borrowed
and duplicated; `-1` explicitly declares completed writes. Missing readiness,
invalid FD values and repeated readiness assignment reject. The producer forwards
that fence via FrameLink; the sink does not map/copy/upload the image. Abandoned
unpublished buffers still require completed GPU access before final release.
This is an explicit application adapter, not a stock decoder integration. The
ALLOCATION pool above also returns these same buffers. Opaque YUV-as-NV12 remains rejected.

Whole GstBuffer copies may share the same AHB GstMemory to change buffer metadata
without copying pixels. Pixel deep-copy, partial memory subviews and implicit CPU
maps remain unsupported. Removing NO_SHARE is essential: that flag forced a pixel
copy during gst_buffer_make_writable; an opaque allocator refusing that copy made
GStreamer 1.24.13's source DISCONT path dereference NULL. Both producer and source
now test same-memory whole-buffer copies. Retaining an AHB alone still does not
retain its FrameLink lease; retain the GstMemory/buffer through completion.

## Linux explicit DMA-BUF sink

Linux's native sink now mirrors the explicit Android action API with
`memory:DMABuf` buffers. It wraps original ring allocations and requires
`set-native-ready-fd` before publication (`-1` means writes are already complete).
Ready fences must cover **all prior channel writes**, not merely work from an
unrelated queue. Caller retains responsibility for correct cache ownership and
finishing access before abandoning an unpublished buffer. The sink never maps
or copies the payload, and cannot silently accept arbitrary external buffers.

The initial adapter requires LINEAR layout and one backing DMA-BUF per image
(possibly shared by NV12 planes). Unsupported modifiers or separate-plane objects
reject. RGBA/NV12 are tested with exact backing-object identity, pixel/metadata
checks, bounded original-image references, delayed readiness and missing-ready
rejection. Synthetic CPU mappings generate test data with DMA_BUF_IOCTL_SYNC;
these results do not establish direct GPU-renderability of LINEAR NVIDIA memory,
a native decoder path or actual driver-exported sync-file qualification.
Linux/Android now share the explicit-completion ALLOCATION pool above.
