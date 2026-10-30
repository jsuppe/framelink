# M4 independent transport timing validation

## Scope and result (Linux, 2026-10-30)

Four native-hardware tests pass. `test/m4_transport_timing.cpp` creates independent
producer/consumer GStreamer pipelines carrying GPU-backed FrameLink BGRA video
and GDP/TCP PCM audio. This is the production plugin, not a fake frame ring.
Both pipelines live in **one process**, so receipt/send timestamps share the same
monotonic clock. This does not exercise M3 authentication/negotiation or the M4
sample application's playback scheduler. No speaker/scanout claim is made.

- 100 marked pairs with audio **37 ms later** than video: exact relative media
  offset preserved, 0 ns error.
- 100 pairs with audio **37 ms earlier**: exact relative offset preserved.
- 100 pairs with producer audio enqueue delays cycling 0–6 ms and source audio
  timestamp drift of 100 us/frame: exact producer offset (including intended
  drift) preserved. Transport must not silently erase source timing.
- Negative control disables video timestamp preservation at the receiving
  FrameLink source: its arrival retimestamping is explicitly detected. Test
  fails if the deliberate configuration change no longer changes timestamps;
  generic process failures are not accepted as successful negative controls.

Video frame identity is checked in pixel payload, audio identity in the complete
expected PCM sequence, and both media timestamps/running-time mappings checked.
Receipt times are captured by **separate sink-pad probes before sink scheduling**,
not after sequential pulls. Logs report each producer enqueue time and each
consumer receipt time plus exact producer/consumer relative media offsets.

Differential enqueue-to-receive delay is computed as
`(audio_receive - audio_enqueue) - (video_receive - video_enqueue)`.
P50/P99/min/max and endpoint change per media second are reported separately from
A/V media-offset preservation. Enqueue timing includes transport staging, CPU
conversion, and queueing; it is not an isolated kernel transit measurement.
Endpoint change over this short test is **not sustained clock drift estimation**.
No hard arrival-jitter budget is invented: measurements describe this run only.

This fixture verifies one pair at a time and is not throughput, buffer-overrun,
long-duration drift-recovery, or saturated-renderer qualification. CTest's
`m4_transport_*` tests require native FrameLink-capable hardware and are not
added to the hosted CPU-only whitelist. Raw evidence is in `transport-evidence/`.

## Remaining application-level gate

At playback checkpoint `cc72396`, `trackAudio` requires audio PTS derived from
sample index starting at zero, and the producer generates zero-origin tracks.
These tests therefore **do not prove** that the authenticated streaming apps
accept arbitrary upstream nonzero A/V offsets or GstSegment mappings. That
integration must be tested separately after the runtime supports it.

## Independent renderer ownership review (`4e5997d`)

Reviewed `wayland_compositor.cpp`, `win_service.cpp`, `scene_host.cpp`,
`video_pass.cpp`, and `av_input.h`:

- Pending queue holds at most six frames against an eight-image producer ring.
  Immediate bookkeeping release relies on deferred/GPU-sync consumed fences;
  it must not be transplanted into immediate-release mode.
- Generation changes clear pending frame snapshots before reuse.
- Linux old imported surfaces are removed only after `vkDeviceWaitIdle`;
  deferred consumed watermarks use alternating in-flight frame-fence retirement.
- Windows consumed timeline values are signaled on the graphics queue after
  prior sampling; ring teardown waits device idle before destroying imports.
- VideoPass checks missing slot/descriptor keys. No conclusive new GPU UAF or
  unsafe premature recycle was identified by this source review.

Source review is not runtime proof. Slow-GPU queue stress and generation changes
remain needed. Failed import/upload after a scheduled pop can retain ring
pressure until a subsequent successful adoption or teardown; this warrants
failure-path testing, not a claim that every error path is recovery-qualified.
