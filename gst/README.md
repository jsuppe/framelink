# GStreamer elements

Two elements that connect framelink to the GStreamer ecosystem. They build
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
- Caps accept BGRA and RGBA; if the ring's byte order differs the element
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
