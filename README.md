# framelink

Native shared GPU frames between processes. A native producer and consumer can
use the **same GPU allocation** without a payload copy. CPU maps, current
GStreamer system-memory elements and GDP audio can copy; see the capability
matrix below rather than assuming every pipeline is zero-copy.

**Windows** (shared NT-handle textures), **Linux** (dma-buf over a Unix socket),
and **Android** (AHardwareBuffer) share the core C API. Platform headers expose
interop handles; allocation/format/fence support still depends on the driver.

## Design and progress — November 3, 2026

**Shipped on `master`: M1–M5, M5a and M5b are complete within documented scope.**
M5b merged in [PR #8](https://github.com/jsuppe/framelink/pull/8) as `9f86c35`.
Final source `4fafe51` passed Linux **112/112**, Windows **110/110**, the M5b
validation matrix on Linux, Windows and Android, and hosted Linux/Windows CI. See
[M5b final evidence](docs/m5b/final-integration/README.md). M5a merged in
[PR #7](https://github.com/jsuppe/framelink/pull/7) as `3e1d51f`; see
[M5a final evidence and attribution limits](docs/m5a/final-integration-20261101/README.md).

- Native allocation negotiation, external pools, downstream reader completion,
  native GStreamer adapters and authenticated relay paths are implemented.
- Supported two-buffer one/two-hop 1080p60 cases deliver 3,720/3,720 frames;
  simultaneous PCM retains the producer media offset. See
  [performance scope, copies and exclusions](docs/m5a/multihop-performance-20261101/README.md).
- [Audio policy](docs/AUDIO_MEMORY_POLICY.md) retains serialized GDP/GIO PCM;
  native video does not imply whole-A/V zero-copy.
- **M5b (PR #8, merged):** public GStreamer audio elements (`framelinkaudiosink`,
  `framelinkaudiosrc`), coordinated A/V sessions for the video elements, and
  the `framelinkavsink`/`framelinkavsrc` bins, qualified on Linux, Windows and
  Android. Start with the [developer guide](samples/GST_AV_EXAMPLE.md), the
  [element reference](docs/GST_AV_REFERENCE.md) and the
  [migration guide](docs/GST_AV_MIGRATION.md).
- **M6 active; M7 pending:** low-latency reliability/performance qualification,
  then release. See the [current resolution plan](docs/m6/LOW_LATENCY_PLAN.md).
  Coordinated-session producers still copy into the video ring; their native
  producer integration remains M6.9, not an established whole-path zero-copy claim.
- **M6 timing migration:** `framelinkavsrc` defaults to `latency-ms=0` on this
  development branch. Set `latency-ms=150` explicitly to retain the former
  buffered playback setting; see the [migration guide](docs/GST_AV_MIGRATION.md).

M5a shares compatible allocations at process boundaries with explicit CPU/GPU
usage negotiation. Storage remains owned until downstream work completes.
Strict paths reject incompatibilities; permitted staging/conversion is explicit.
Physical scanout, acoustic alignment, arbitrary drivers/decoders and cold wake
are not established by software timing or GPU completion results.

**Pool capacity is not intentional queue latency.** A 12-buffer allocation must
not imply a 12-frame playback reservoir at every hop. Pass-through boundaries
preserve allocation identity and the common A/V media timeline; final outputs own
presentation scheduling. Zero-copy does not mean zero synchronization or elapsed
transfer time. Audio transport costs are assessed separately from video sharing.

Start here:
- [Current detailed task checklist](docs/ROADMAP_TASKS.md)
- [Pipeline-boundary design and acceptance criteria](docs/M5A_PIPELINE_BOUNDARIES.md)
- [Development memory contract](docs/MEMORY_CONTRACT.md)
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

Before committing evidence or scripts, run `python3 tools/setup_local_env.py` once
per clone so machine-local paths, users and hosts are committed as placeholders;
see [docs/LOCAL_ENV.md](docs/LOCAL_ENV.md).

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
Native memory negotiation, external pools, bounded reader leases and explicit
GStreamer native-memory source/sink adapters are implemented on the M5a branch.
See [native integration](docs/GST_NATIVE_MEMORY.md),
[memory contract](docs/MEMORY_CONTRACT.md), [reader completion](docs/READ_LEASES.md),
[Windows decoder example](samples/D3D_DECODER.md) and [relay](samples/NATIVE_RELAY.md).
Fan-out to multiple consumers of one channel is not implemented (use separate channels).

**M5b:** public audio elements, coordinated A/V sessions and convenience bins;
see the [A/V contract](docs/GSTREAMER_AV_CONTRACT.md) and
[platform qualification](docs/m5b/platform-qualification/README.md).
[Checkable roadmap](docs/ROADMAP_TASKS.md) distinguishes implemented, locally
qualified and merged work. M6 reliability qualification and M7 release follow.

[Performance evidence](docs/m5a/multihop-performance-20261101/README.md) records
supported one/two-boundary1080p60 paths with two original allocations: nominal
3720/3720 frames, exact A/V media offset and explicit GPU completion/copy accounting.
Warmed Android and same-host native fixtures are not cold-start, arbitrary decoder,
physical scanout or end-to-end zero-copy proof. Windows verification reads a private
copy; Linux/Android verification samples originals into private output. See evidence
for unsupported formats, fault drops and measurement limits.

Audio remains CPU PCM over GDP/GIO, separately described in the
[audio memory/copy policy](docs/AUDIO_MEMORY_POLICY.md). Native video does not make
an entire audio/video session zero-copy.
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
