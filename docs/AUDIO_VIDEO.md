# Audio alongside FrameLink: existing-transport first

FrameLink still transports images, not audio. This working example pairs a
FrameLink video channel with a **separate GStreamer GDP-over-loopback-TCP PCM
track**. No `flFrame` change, audio device driver, mixer, codec, or new IPC wire
protocol was introduced. GStreamer handles audio caps and synchronized sink
scheduling. The example is a session coordinator with two pipelines; it is not
a generic independently launched cross-process media-session manager.

## Run

Build with `FRAMELINK_BUILD_GST=ON` and `FRAMELINK_BUILD_SAMPLES=ON` (defaults):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build/framelink_av_session build/libgstframelink.so
build/framelink_av_session build/libgstframelink.so --faults
# Optional real speaker/window output (requires available device/display):
build/framelink_av_session build/libgstframelink.so --play
ctest --test-dir build -R gst_av --output-on-failure
```

On Windows use the executable/plugin DLL paths in the selected build directory
and put the GStreamer runtime and FrameLink DLL on `PATH`. Required GStreamer
plugins are app, coreelements, GDP and TCP; `--play` additionally needs
videoconvert, audioconvert, audioresample and platform audio/video sinks.
`--play` emits a modest 440-Hz tone for three seconds. Default tests produce no
sound/window. All audio/video buffers are also checked at appsinks.

## Session contract / negotiation

The coordinator creates a unique session/channel name and an ephemeral TCP
port bound only to 127.0.0.1. It chooses a **single GstSystemClock and identical
explicit pipeline base time** for sender and receiver. That pair defines the
session running-time epoch. Ports and channels are not authentication tokens:
this prototype assumes trusted local processes and makes no sandbox claim.

The concrete supported choice in this example is:

- Video: BGRA, 160x90, 50 fps, dedicated FrameLink channel.
- Audio: PCM S16LE, 48000 samples/second, stereo interleaved, FL/FR channel mask
  `0x3`, 960 samples per block (20 ms).
- Audio PTS identifies the **first sample**, duration is samples/rate.
- GDP transports caps, duration, PTS and discontinuity flags; receiver caps
  explicitly require the selected format. No silent transport conversion.
- `audioconvert`/`audioresample` occur only in the optional playback branch.

This is explicit fixed-capability selection, not a new dynamic offer/answer
protocol. A production coordinator should intersect advertised track caps,
assign session identity/clock/base/configuration generation, and recreate or
flush both tracks together on renegotiation. **Ring generation alone is not an
A/V session generation.** Arbitrary F32/rates/layouts and independent producer
negotiation have not been implemented in this sample. GStreamer already has
these audio formats/converters; there is no evidence yet requiring their
reimplementation inside FrameLink.

## Opt-in video timestamp preservation

Both elements now expose `preserve-timestamps=true`, configured before PLAYING:

- `framelinksink` converts buffer PTS through its TIME segment to running time,
  then submits that value. Missing/invalid/out-of-segment PTS is an error.
- `framelinksrc` emits that running-time PTS rather than replacing it with
  arrival time; sequence gaps, ring-generation changes and backward/repeated
  PTS mark DISCONT. Source start also marks DISCONT.
- Default `false` is unchanged: sink submits GLib monotonic time, source stamps
  arrival time. Existing renderer latency telemetry retains its semantics.

**Use a dedicated channel with both ends agreeing on this mode.** There is no
on-wire clock negotiation; do not connect this channel to an existing renderer
that interprets `ptsNs` as absolute monotonic capture/submit time. Do not enable
only one endpoint. Two independently started pipelines with different bases or
unrelated audio-device/network clocks require explicit clock/epoch mapping by
the application; this property does not infer it. Transport retains media time,
not submit latency. Custom/remote clock recovery remains GStreamer's/session
coordinator's responsibility. Neither GLib nor C++ monotonic clocks are assumed
to share epoch with Android BOOTTIME or arbitrary hardware audio clocks.

FrameLink has no wire field for an explicit same-timestamp producer discontinuity
flag. This mode detects generation/sequence/backward-time discontinuities; if
an application needs another reset it must coordinate track flushing/new
session. It does not pretend the video flag serializes arbitrary upstream flags.

## Buffering, synchronization and scope

The image ring remains bounded (four buffers); appsrc/appsink queues have two
buffer bounds, audio queue has four blocks / 80 ms, GDP server retention has an
eight-buffer bound. TCP kernel buffering is additional: these settings are **not
an end-to-end latency guarantee**. Slow-consumer/disconnect handling in this
example is a three-second receive deadline; no unbounded wait/recovery claim.
Never drop arbitrary TCP bytes to implement latest-wins audio. A production
interactive path needs measured latency policy and controlled discontinuities;
video and audio should not share a latest-frame-wins drop policy.

Both test sinks schedule against the shared clock. Optional hardware playback
uses that clock; this is not automatic audio-device-master drift compensation.
For long-running real playback, select the audio sink's clock (or an explicitly
disciplined clock), measure device drift, and let the pipeline resample/schedule.
A zero-buffer audio guarantee would be misleading.

The sample verifies transported pixels, every PCM sample, caps, exact PTS and
first-sample duration. Fault mode intentionally omits one audio block, injects
40 blocks of increasing audio timestamp drift (0.1 to 4 ms), then resets media
time. It checks missing-block detection, drift detection and discontinuity
preservation. It **detects**, not repairs, these faults. Reported pull skew and
scheduled lateness are application observations, not physical speaker-to-pixel
or scanout synchronization. Reset mode intentionally schedules past timestamps;
its latency figures are not a performance baseline.

## Platform status and decision

- Linux: automated real FrameLink + GDP/TCP PCM roundtrip and fault injection;
  evidence in [audio-session-20261029](audio-session-20261029/).
- Windows: portable GStreamer/C++ code and CMake wiring; runtime qualification
  must be recorded separately (never infer it from Linux).
- Android: this example is not built as a standalone executable/APK yet. GDP/TCP
  plugins must be packaged/registered and app sandbox/service ownership managed.
  Existing FrameLink Android Binder integration is not bypassed by loopback TCP.

**Decision:** retain the image-only core and implement A/V timing at the session
layer. Existing GStreamer audio transport is sufficient for this initial local
controlled example. It is not enough evidence to declare universal cross-app
Android or low-latency hardware-clock parity. Do not invent a companion shared
memory protocol until a measured requirement (latency, deployment, sandbox,
copy cost) cannot be met with the existing framework. If one is needed, separate
PCM tracks and negotiated clock/configuration generation remain the contract.
