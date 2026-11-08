# GStreamer elements

Video elements and a serialized PCM audio sink connect FrameLink to GStreamer. They build
automatically when GStreamer development files are found (`FRAMELINK_BUILD_GST`,
default ON) and produce one plugin, `gstframelink`.

The names read inverted on purpose, because the roles genuinely invert:

| element | GStreamer role | framelink role |
|---|---|---|
| `framelinksink` | sink (consumes buffers) | **producer** - dials into a consumer-owned channel |
| `framelinksrc` | source (produces buffers) | **consumer** - creates and owns the channel |

## framelinksink - any pipeline becomes a producer

```sh
# take a free slot on a compositor's slot set (vkrmod.in.0 .. vkrmod.in.11)
gst-launch-1.0 videotestsrc is-live=true \
  ! video/x-raw,format=BGRA,width=1280,height=720,framerate=30/1 \
  ! framelinksink channel=vkrmod.in slots=12

# or open one exact channel
gst-launch-1.0 ... ! framelinksink channel=mychannel
```

- `slots=N` walks `<channel>.0..N-1` and takes the first free one; `slots=0`
  (default) opens `channel` verbatim.
- The connection is lazy and retried once a second: pipelines may start before
  the consumer exists, and survive the consumer restarting.
- A full ring drops the frame - latest-wins backpressure is framelink's
  contract, so a slow consumer can never stall the pipeline.
- Caps accept BGRA and RGBA (Linux and Windows also NV12); if the ring's byte order differs the element
  swizzles (Android rings are RGBA - AHardwareBuffer has no BGRA).
- Windows writes through the channel's D3D11 device (`UpdateSubresource`);
  POSIX writes through `flImageMap`, which requires the consumer to have
  allocated the ring with `FL_MAP_CPU`.

## framelinksrc - the ring's owner as an element

```sh
# record whatever a producer submits
gst-launch-1.0 framelinksrc channel=rec.scene \
  ! videoconvert ! x264enc ! mp4mux ! filesink location=out.mp4

# Linux: an instant virtual camera
gst-launch-1.0 framelinksrc channel=cam.scene ! videoconvert \
  ! v4l2sink device=/dev/video10
```

- `width`/`height`/`fps` size the ring this element allocates (defaults
  1280x720@30). A producer's `flRequestGeometry` may reallocate it; the
  element renegotiates caps downstream when that happens.
- Live push source; buffers are timestamped on arrival.
- No producer connected just means no frames yet - the element waits, and a
  departing producer does not end the stream (the ring belongs to us).

## NV12 on Linux and Windows

Start the receiver first (it owns the format and buffer allocation):

```sh
gst-launch-1.0 framelinksrc channel=nv12.demo format=NV12 width=1280 height=720 fps=30 \
  ! videoconvert ! autovideosink
```

Then start a producer:

```sh
gst-launch-1.0 videotestsrc is-live=true \
  ! video/x-raw,format=NV12,width=1280,height=720,framerate=30/1 \
  ! framelinksink channel=nv12.demo
```

`format` defaults to BGRA and must be selected before starting the source.
NV12 requires even dimensions and matching channel/input formats and sizes.
There is no implicit RGB/YUV conversion: use `videoconvert` upstream if needed.
Both planes are copied row-by-row, honoring their individual strides and offsets.
This is still system-memory GStreamer I/O, not zero-copy DMA-BUF negotiation.
Colorimetry/chroma-site metadata is not transported by FrameLink; applications
must agree on it separately. See [the support matrix and tests](../docs/nv12.md).

## Notes

- Stage 1 is deliberately the CPU path (sysmem caps): one copy per frame each
  way, portable everywhere framelink runs. Zero-copy caps (`memory:DmaBuf` on
  Linux, `memory:D3D11Memory` on Windows) are the obvious stage 2.
- **Linux + NVIDIA: `framelinksrc` is slow today.** The GBM `FL_MAP_CPU`
  mapping is truly uncached there - measured 10 MB/s reads (≈3 fps at 720p)
  on an RTX 3090, and SSE4.1 streaming loads do not help (11 MB/s: UC, not
  WC - the element still uses them since Intel iGPU maps usually are WC).
  Writes are unaffected (2.1 GB/s measured), so `framelinksink` - the
  producer direction - runs at full speed everywhere. The fix is for
  framelink's Linux backend to allocate CPU-consumer rings from cached
  memory (dma-buf system heap) instead of GBM; until then, prefer a GPU
  consumer on NVIDIA or run CPU consumers on Intel/AMD.
- `gst-launch-1.0` on Windows treats backslashes in property values as
  escapes - use forward slashes in `location=` paths.
- Point `GST_PLUGIN_PATH` at the build directory (the plugin needs
  `framelink.dll`/`libframelink.so` findable too - same directory works).

## Audio/video sessions

The opt-in `preserve-timestamps` property on both elements retains agreed
session running time; defaults retain legacy arrival/submit timestamps.
Both pipelines must agree on clock/base and use a dedicated channel. See
[the working separate-PCM example and timing contract](../docs/AUDIO_VIDEO.md).

## Bounded pool/overload controls

See [pool policy](../docs/POOL_POLICY.md) for pool-size, buffer-wait-ms,
error-on-ring-full and max-queue-age-ms. Defaults preserve existing behavior.
[Transport timing](../docs/TRANSPORT_TIMING.md) separates these bounds from final
A/V presentation scheduling. Native zero-copy Gst integration remains pending.

## Opt-in native source memory

See [native source adapters and limits](../docs/GST_NATIVE_MEMORY.md). Linux
DMA-BUF, Windows D3D11 and exact Android RGB AHB wrappers are implemented; this
is not yet a complete native decoder/sink/remote-negotiation pipeline.


## Public audio elements — M5b development branch

`framelinkaudiosink` carries interleaved S16LE/F32LE mono/stereo PCM at8–192kHz.
This is serialized GDP/GIO audio, **not shared-memory or zero-copy PCM**. It does
not play sound or independently pace the media timeline. The public source is `framelinkaudiosrc`; coordinated A/V bins and full
lifecycle qualification remain later M5b tasks. This development branch is not
a complete shipped A/V integration. See [public contract](../docs/GSTREAMER_AV_CONTRACT.md).

Standalone audio-only mode uses `session=/private/path`, `format=S16LE|F32LE`,
`rate=48000`, `channels=2`, `audio-queue-ms=80`, `connect-timeout-ms=15000`,
`idle-timeout-ms=15000`, `tracks=audio` (defaults shown). Configure in NULL.
The producer publishes a protected current-user loopback rendezvous; startup
runs on a cancellable worker, not a blocking state callback. Either peer can
start first. The public source accepts the same exact-format/session properties. The lower-level
consumer API remains `fl_gst_session_open_local_audio`. No external network discovery or shared secret property.

Applications with a broker/pre-negotiated session install the owning
`framelink.session.v1` GstContext and authenticate the separate audio stream via
`fl_gst_session_authenticate_audio_stream` before starting the element. Coordinated
A/V requires `tracks=av` and a matching context; no dummy video track is created.
Use `fl_gst_session_get_config` for selected formats. Read-only `session-id`,
`generation`, `audio-track-id`, `video-track-id` expose negotiated identities.

The sink claims one authenticated stream per generation. Invalid/unaligned or
oversized PCM, invalid TIME/PTS/duration, incompatible caps and unmarked continuity
breaks produce errors rather than silence or hidden conversion. DISCONT begins a
new PCM continuity interval without zeroing transmitted PTS. Audio queue is a hard
budget, not a fill target; a slow peer blocks bounded transport and then errors.
There is one in-flight block in addition to queued data; upstream application
queues must also be bounded. Ordinary GIO socket idle timeouts round up to seconds;
startup has an exact whole-operation millisecond deadline. NULL/READY teardown
cancels pending setup/I/O; reuse requires a fresh session or generation.

GDP's outer serialized byte segment is adapted for non-seekable output; its
encapsulated original PCM segment and timestamps remain intact. The validation
fixture's byte-at-a-time GIO receiver is not a production reader. The public source uses
incremental bounded socket reads so packet tails never wait for a future PCM block.
Physical speaker alignment and independent clock drift correction are not implied.

### Audio consumer behavior

`framelinkaudiosrc session=/private/path format=S16LE rate=48000 channels=2`
exposes one PCM `src` pad for an independently chosen downstream playback or
processing path. It is live, non-seekable, does not provide a replacement clock,
and does not reset timestamps to the consumer pipeline origin. The latency query
reports live/zero mandatory buffering/unknown maximum: the queue budget is NOT a
promise of end-to-end playback latency. Common session-to-playback clock mapping
is a separate coordinated timing task, not implied by exact transport PTS.

The reader checks GDP declared payload bounds before accumulation and validates
received caps against the authenticated session. Missing-peer startup, transport
idle, peer closure/truncation and cancellation are bounded. A socket EOF is not
clean EOS. GDP does not serialize ordinary EOS; the public sink sends a private
`framelink-audio-eos` terminal event after its queue drains, including version,
generation, block and byte counts. The source accepts matching terminal accounting
and emits standard downstream EOS. GDP truncates custom event types to16 bits;
this private record is recognized by its exact wire type/name/schema and consumed
inside the source. This is not general custom-event preservation. Raw older GDP
senders without this terminal record do not qualify as clean public-source EOS.
No dummy PCM block or extra PCM buffering is introduced for termination.

### Explicit cached shared-CPU video

On Linux/Android, both A/V bins support `shared-cpu=true session=/private/path`
for current-user authenticated local setup, or `shared-cpu=true` with a
broker-provisioned context. Both endpoints must select this mode. Video is
required; `native-memory=true` is incompatible. This is **CPU copy-in/copy-out**,
not native GPU sharing or a zero-copy claim. The default native path is unchanged.
The local mode preserves common A/V clocks, readiness, EOS and cancellable
startup; it does not add intentional playback buffering. Android cross-UID
clients require an authorized broker rather than this current-user discovery.
See [validation and limits](../docs/m6/cpu-path/LOCAL_CPU_BINS.md).
