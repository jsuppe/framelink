# Two-process A/V with the public GStreamer elements

`framelink_gst_av` splits one A/V pipeline across two processes using only the
public elements (`framelinksink`, `framelinksrc`, `framelinkaudiosink`,
`framelinkaudiosrc`) and the public session helpers in
`gst/framelink_session_transport.h`. No GDP, socket or authentication code is
written by the application.

## Run it

Use the same private `--session` directory for both processes. The producer
creates it with current-user-only permissions; either process may start first.

Windows (Developer PowerShell, GStreamer MSVC runtime on `PATH`):

```
$env:PATH = "<build>\Release;C:\Program Files\gstreamer\1.0\msvc_x86_64\bin;" + $env:PATH
$env:GST_PLUGIN_PATH = "<build>\Release"
framelink_gst_av consumer --session $env:TEMP\fl-demo --display
framelink_gst_av producer --session $env:TEMP\fl-demo --seconds 10
```

Linux:

```
export GST_PLUGIN_PATH=<build>
./framelink_gst_av consumer --session $XDG_RUNTIME_DIR/fl-demo --display
./framelink_gst_av producer --session $XDG_RUNTIME_DIR/fl-demo --seconds 10
```

Options shared by both roles: `--tracks av|audio|video`, `--format
RGBA|BGRA|NV12`, `--size WxH`, `--fps N`, `--seconds N`, `--timeout-ms N`.
Producer: `--video-source` and `--audio-source` replace the live test sources
with any GStreamer source fragment (for example a decoder chain). Consumer:
`--display` renders with `autovideosink` and plays through `autoaudiosink`;
`--verify` is headless and prints a JSON summary (frame and sample counts,
uniform-pixel check, contiguous audio timestamps, delivery lag).
`--latency-ms` (default 150) and `--pool` (default 8) are explained below.

On Windows, display mode needs an interactive desktop session. The plugin's
directory must be on `PATH` as well as `GST_PLUGIN_PATH`, because the
registry scanner loads it in a separate process. The Android command-line build
supports `--verify` only.

## Audio only with gst-launch

The audio elements carry their own standalone discovery, so no code is needed:

```
gst-launch-1.0 framelinkaudiosrc session=$XDG_RUNTIME_DIR/fl-audio ! queue ! audioconvert ! autoaudiosink
gst-launch-1.0 audiotestsrc is-live=true ! audio/x-raw,format=S16LE,layout=interleaved,rate=48000,channels=2 ! framelinkaudiosink session=$XDG_RUNTIME_DIR/fl-audio
```

Standalone audio uses each pipeline's own clock; there is no cross-process
clock mapping in this mode. Use the A/V helper below when timing matters.

## Integrating in an application

Producer:

```c
FlGstSession* s = fl_gst_session_new(3);           // 1 video | 2 audio
flChannel* lease = NULL;
fl_gst_session_open_local_av(s, TRUE, dir, &caps, 2, 0, 80, 15000, 15000, 0, &lease, NULL, &err);
GstContext* c = fl_gst_session_create_context(s);  gst_element_set_context(pipeline, c);
g_signal_emit_by_name(video_sink, "adopt-channel", lease, &adopted);
fl_gst_session_local_ready(s, TRUE, 15000, &err);   // consumer devices exist
fl_gst_session_apply_pipeline_clock(s, GST_PIPELINE(pipeline), TRUE, 0, &err);
gst_element_set_state(pipeline, GST_STATE_PLAYING);
/* ... EOS ... */
fl_gst_session_finish_local(s, TRUE, 15000, &err);  // consumer rendered the tail
gst_element_set_state(pipeline, GST_STATE_NULL);
```

Consumer: the same calls with `producer = FALSE`, a video pool size, a
pipeline latency in `apply_pipeline_clock`, `local_ready` after setting
PLAYING, and `finish_local` after EOS.

What the helper does: current-user rendezvous and a private loopback control
connection; authenticated negotiation of formats, track set and queue budgets;
an authenticated round-trip clock measurement (rejected above 50ms
uncertainty) installed as the consumer's mapping; authenticated audio and event
streams; and the video lease. The consumer creates the ring under a
secret-derived name and returns only after the producer confirms the attach.

## Timing, latency and pool size

The producer's pipeline runs on the monotonic system clock with base time at
the negotiated epoch. The consumer's base time is 0 and the mapping turns
producer running time into consumer system-clock time, so synchronized sinks
present at PTS plus the pipeline latency. Relative A/V timestamps are never
rewritten.

Put a `queue` after each FrameLink source. Without it, a synchronized sink
blocks the source's streaming thread for the whole latency and the source can
only ever see the newest frame. With the queue, frames held downstream still
occupy ring images, so size the pool to cover them:
`pool >= ceil(latency * fps) + 2`. The defaults (150ms, 30fps, pool 8) satisfy
this. A pool that is too small shows up as `framelink-source-sequence-gap`
messages, which the example prints.

## Evidence and limits

Two-process qualification: `test/m5b_gst_av_example.py` (registered as the
`m5b_gst_av_example` test) and [task7 evidence](../docs/m5b/examples/README.md).

The examples use CPU-mappable rings. Native GPU memory composes with the same
session through the native-memory elements described in
[GST_NATIVE_MEMORY.md](../docs/GST_NATIVE_MEMORY.md) and is qualified there,
not here. Verify mode checks presentation against the system clock; it is not
a measurement of physical display scanout or speaker output. Reconnecting after
a peer failure requires a new session, as with the other coordinated elements.
