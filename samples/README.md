# FrameLink producer and consumer validation applications

Two configurable **command-line** samples, separate from the narrowly scoped
`fl_produce`/`fl_consume` regression fixtures:

- `framelink_consumer`: owns a channel, optionally verifies pixels, reports counters.
- `framelink_producer`: connects, writes identifiable frames and submits them.

Supported here: Windows D3D11 and Linux DMA-BUF, BGRA and NV12. Android sample
executables are not built. These are diagnostic tools, not GUI previews or
zero-overhead throughput benchmarks.

## Build

```sh
cmake -S . -B build -DFRAMELINK_BUILD_SAMPLES=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Linux executables are in `build/`; Windows executables and `framelink.dll` are in
`build/Release/`. `--help` prints options and exit codes. Samples do not require
GStreamer; set `FRAMELINK_BUILD_GST=OFF` if it is not wanted.

## Start in two terminals

Consumer:

```sh
framelink_consumer --channel demo --format nv12 --size 1920x1080 --pool 4 --frames 120
```

Producer:

```sh
framelink_producer --channel demo --format nv12 --fps 60 --frames 600
```

Use `.exe` or `./build/` as appropriate for your shell. Either process can be
started first, provided the other starts within the idle deadline. Producer
frames is a **maximum**; a consumer exiting after its requested capture is a
normal producer stop. Consumer frames is the required number of received frames.
Use a higher producer count because FrameLink can skip frames (latest wins).

## Options and modes

Both:

- `--channel NAME`: exact name, default `framelink.sample`.
- `--frames N`: producer send limit (default 600), consumer receive target (120).
- `--timeout-ms N`: timeout for each acquire call, default 100.
- `--deadline-ms N`: maximum idle/connect time without progress, default 10000;
  must be positive and at least timeout-ms. Not an overall run duration.
- `--format bgra|nv12`: consumer allocation format; producer requires a match
  only when explicitly given. No implicit color conversion.
- `--size WIDTHxHEIGHT`: consumer allocation (default 640x360); producer requests
  geometry at connection/reconnection only when explicitly specified. It queries
  the resulting geometry and fails if the request was declined. Width 8..8192,
  height 1..8192; NV12 requires even dimensions.

Producer:

- `--fps N`: CPU pacing target, default 60; 0 means unpaced. Not a guarantee of
  fresh delivery rate. Pacing does not build a catch-up burst after a stall.
- `--slots N`: use `flOpenProducerSlot` to select `NAME.0..NAME.(N-1)`.
  Each consumer owns one named channel; start more consumers for a slot set.
- `--reconnect-at N`: disconnect/reconnect once before zero-based frame N;
  pattern identity continues across that reconnect.
- `--submit auto|manual`: auto uses `flSubmit`. Windows manual demonstrates
  `flBeginSubmit`, importing/signaling the ready fence on the channel's D3D11
  context, and `flEndSubmit`. It does not pretend to exercise Vulkan/D3D12.
- `--corrupt-at N`: intentionally flip one payload byte of frame N. A verifying
  consumer should exit 1 with the failing frame/plane/pixel coordinate. The
  frame must actually reach the consumer; latest-wins may skip it under load.

Consumer:

- `--pool 1..8`: ring size, default 4.
- `--delay-ms N`: hold each acquired frame for N ms before release (0..60000).
  Exercises slow-consumer/backpressure behavior.
- `--verify 0|1`: default 1 verifies the sample's in-band identity and all active
  plane bytes. 0 observes metadata without reading pixels, suitable for a real
  renderer/other producer. No pixel correctness claim is made in this mode.
- `--map-cpu 0|1`: sets `FL_MAP_CPU`. Defaults to 1 on Linux, 0 on Windows.
  Linux verification and this sample's Linux writer require a linear mappable
  ring. `--map-cpu 0 --verify 0` is for an external GPU producer, not this CPU
  pattern writer. Windows uses upload/staging readback rather than flImageMap.
- `--gpu-sync 0|1` (Windows): create with `FL_GPU_SYNC`, import the ready and
  consumed fences, queue GPU wait/readback/signal, and call `flRelease` for
  bookkeeping. With verification on, staging Map still blocks for readback.
- `--adapter HEX` (Windows): 16 hex digits representing the eight LUID bytes in
  the exact order printed by the tools, passed to `flCreateChannelOnAdapter`.
  Do not confuse this with a GPU ordinal or numeric LUID display order.

Wrong-role options, unsupported Linux fence/LUID modes and malformed arguments
are rejected rather than silently ignored.

## Examples

Windows explicit fences (two terminals):

```sh
framelink_consumer --channel sync --format nv12 --gpu-sync 1 --frames 120
framelink_producer --channel sync --submit manual --fps 60 --frames 600
```

Slow-consumer test (gaps are expected, corrupted pixels are not):

```sh
framelink_consumer --channel slow --format nv12 --pool 2 --delay-ms 50 --frames 60
framelink_producer --channel slow --fps 120 --frames 2000
```

Startup geometry request and producer reconnect:

```sh
framelink_consumer --channel resize --format nv12 --size 640x360 --frames 120
framelink_producer --channel resize --size 1280x720 --reconnect-at 30 --frames 600
```

Slot selection:

```sh
framelink_consumer --channel slots.0 --frames 120
framelink_consumer --channel slots.1 --frames 120
framelink_producer --channel slots --slots 2 --frames 600
```

Observe vk renderer output, without expecting the test pattern:

```sh
framelink_consumer --channel vkrender.scene --format nv12 --size 1920x1080 --verify 0 --frames 300 --deadline-ms 30000
```

Start the renderer and an active input during that interval. Channel ownership
still belongs to this consumer; it cannot attach as a second consumer to a
channel another application already owns.

## Interpretation and limitations

Producer summaries are on stderr; consumer summaries are on stdout; errors
are on stderr. Exit 0 = requested capture completed / producer limit or peer
shutdown, 1 = API/idle-timeout/pixel failure, 2 = invalid or unsupported CLI.

- `verified`: frames whose active pixels passed byte-exact validation. Padding
  is excluded. The pattern stores a uint64 identity in the first eight bytes;
  the remainder varies by identity, plane, row and column. It is not a normal
  color-calibrated video image.
- `pattern_gaps`: identities missing **between received frames**. This is not a
  pixel error. Initial frames skipped before the first receive are not counted.
- `sequence_gaps`: gaps in FrameLink delivery sequence (reconnect may reset it).
- Backward/repeated pattern identities are verification failures. The built-in
  reconnect keeps its identity counter; restarting a brand-new producer resets
  it, so restart the verifier too or use `--verify 0` for that scenario.
- `timeouts` includes temporary disconnect/no-frame retries during reconnect.
- `fps` includes startup and verification/readback cost. `pts_age_*` is age at
  acquisition/verification completion relative to the producer's monotonic PTS,
  **not** physical presentation latency. Other producers must use the same clock
  domain for those age figures to be meaningful.
- Geometry is requested before production, not while a frame is held. This is
  not a runtime-resize-race stress test or an external-GPU import test.
- CPU-generated bytes and readback make these correctness tools, not a pure
  GPU transfer benchmark. Linux requires an accessible compatible DRM device.

## Automated coverage

CTest covers BGRA/NV12, pool sizes 1/2/8, slow consumers, metadata-only capture,
startup geometry negotiation, reconnect, slot selection, malformed/wrong-role
CLI, and deliberate pixel corruption. Windows additionally tests manual
submission + consumer-owned GPU fences, plus disconnection with a full ring.
Both child exit statuses are checked. The full-ring test protects a Windows
fix: acquire now detects a closed consumer pipe even when no further slot is
available to submit and discover the disconnect through a write.

Validated on 2026-10-29: 14/14 total CTest tests passed on Linux/Melchior and
18/18 on Windows/Caspar (RTX 5090, MSVC x64 Release). The existing NV12/GStreamer
regressions remain included in those totals. Caspar binaries are under
`C:\Users\jonsu\dev\framelink-nv12-20261029\build\Release`.

## Platform-parity additions

The samples also build on Android. Supported format options are `bgra`, `rgba`,
`nv12` and `rgb10a2`, subject to device allocation support. Android BGRA maps
native RGBA with swizzling; NV12 maps or normalizes native YUV420 planes.

`--gpu-sync 1` and `--submit manual` now exercise explicit synchronization on
all three platforms. On POSIX the CPU-verifying sample waits a borrowed ready
completion FD and reports already-completed CPU reads/writes with FD `-1`.
This is an API-contract test, not a GPU performance demonstration; the renderer's
`vulkan_sync_fd_contract` test separately validates real GPU completion fences.
Adapter LUID selection remains Windows-specific.

### GPU-rendered sources

`--gpu-render 1 --verify 0` requests a GPU-renderable ring for an external renderer.
It is separate from `--gpu-sync`, which controls consumer-side fence ownership.
On Android, unsupported native format/usage combinations are refused; see
[GPU_RENDER.md](../docs/GPU_RENDER.md). Use `--map-cpu 0` when pixels need not be
read by the CPU. The pattern verifier applies only to the sample producer.

### Producer disconnect during release

The consumer keeps its channel alive if `flRelease` returns `FL_DISCONNECTED`.
The frame has already been released locally; the producer-notification send may
race with peer shutdown. Retrying that release would be incorrect. Subsequent
acquires wait for a replacement producer as usual, subject to the idle deadline.
Other release errors remain fatal. This does not relax ring generation or GPU
completion ownership checks in the library.

Linux `sample_release_disconnected` deterministically injects this notification
result after a real successful release and asserts that injection occurred.
It tests sample recovery policy; `reconnect_held` separately tests real producer
departure, held-storage protection, and a new generation after release.

The same rule applies to POSIX `flSignalConsumedSyncFd`: a disconnected
notification after locally recording completion is tolerated, then the sample
still releases its held frame. `sample_consumed_disconnected` asserts both
consumed-completion and release notification injections in GPU-sync mode.

### External decoded A/V pipeline (M5 developer handoff)

`framelink_av_producer --input-pipeline FILE` accepts trusted GStreamer pipeline
text with `@VIDEO_SINK@` / `@AUDIO_SINK@` placeholders. It sends decoded video and
PCM to an independent streaming consumer while retaining their common timeline.
See [Windows pipeline guide](../docs/M5_WINDOWS_PIPELINE.md) for build pins,
reproducible MJPEG/PCM validation, and finite-CFR/live-stream limitations.
Optional `--trace-media` compares payload SHA256, running PTS and durations; it
maps/hashes frames and must not be used for performance qualification.
