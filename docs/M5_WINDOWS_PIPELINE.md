# Windows external GStreamer A/V pipeline handoff

## Scope and tested pins

FrameLink implementation commit `0335805` (based on merged M4 `ed81cbb`),
Windows x64, MSVC Visual Studio 2022, GStreamer MSVC x86_64 **1.28.0** runtime
and development packages. Renderer M4 baseline is `4c2ff15`; optional renderer
commands below are integration instructions, not a claim that this test exercised
physical display/speaker output. Python 3 is required for the test harness.

This example uses a real MJPEG/PCM AVI demux/decode pipeline, not synthetic frame
submission inside the transport. It splits that pipeline into an independently
launched consumer. BGRA and NV12 pixels, PCM bytes, timestamps, and durations are
compared across the process boundary. It uses system-memory GStreamer buffers;
GPU allocation negotiation and decoder-to-renderer zero-copy belong to M5a.

## Build

Use a developer PowerShell with CMake and MSVC installed:

```powershell
$env:GSTREAMER_1_0_ROOT_MSVC_X86_64 = 'C:/Program Files/gstreamer/1.0/msvc_x86_64'
$env:PATH = "$env:GSTREAMER_1_0_ROOT_MSVC_X86_64/bin;$env:PATH"
git checkout 0335805
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
cmake --build build --config Release --parallel 8
python test/m5_external_pipeline.py --build build/Release --output evidence
```

Required plugins: `avimux`, `avidemux`, `jpegenc`, `jpegdec`, `videoconvert`,
`audioconvert`, `gdppay`, `gdpdepay`, `giostreamsink`, `appsrc`, `appsink`, test
sources, and FrameLink. Missing plugins must fail; there is no hidden fallback.

The test creates `evidence/decoded-source.avi`, pipeline files, both process logs,
and `summary.json`. Use forward slashes in GStreamer Windows filenames to avoid
GStreamer treating backslashes as escapes.

## Independent launch

Create a trusted UTF-8/plain-text pipeline file (ASCII paths tested), for example
`source.pipeline`, with each placeholder exactly once:

```text
filesrc location="C:/media/clip.avi" ! avidemux name=demux
 demux.video_0 ! queue ! jpegdec ! videoconvert ! @VIDEO_SINK@
 demux.audio_0 ! queue ! audioconvert ! @AUDIO_SINK@
```

This particular example expects 2 seconds, 100 video frames at 50 fps,
160x90, and 96000 interleaved stereo S16LE PCM samples at 48 kHz. The input must
match those settings or include explicit conversion/resampling in its pipeline.
The placeholders inject negotiated caps and transport endpoints; they are not
GStreamer factory names. No arbitrary pipeline string is received from the peer.

Start each command in a separate terminal. The session directory is protected by
the existing same-user Windows ACL/authentication mechanism; both peers must run
as the same user. Either startup order is supported by the existing rendezvous.

```powershell
$bin = 'C:/src/framelink/build/Release'
$plugin = "$bin/gstframelink.dll"
$session = 'C:/Users/jonsu/temp/fl-external-session'
& "$bin/framelink_av_producer.exe" $plugin --session $session --format BGRA --frames 100 --input-pipeline C:/media/source.pipeline --timeout-ms 15000
```

Receiver terminal, with the same variables:

```powershell
& "$bin/framelink_av_consumer.exe" $plugin --session $session --format BGRA --stream --audio-sink fakesink --timeout-ms 15000
```

The streaming sample consumer validates/schedules video but **does not open a
video display**. `fakesink` deliberately produces no sound. Use `autoaudiosink`
for the selected Windows audio device; actual speaker timing is not measured by
this test. To render independently, use the renderer instead of that consumer
(do not attach both to the same single-consumer session):

```powershell
vkrenderer.exe --serve external-av --av-session $session --av-plugin $plugin --av-format BGRA --av-exit-on-end
```

The renderer's negotiated defaults match this example. For other formats/rates,
set matching `--av-width`, `--av-height`, `--av-fps`, `--av-rate`, and
`--av-channels`. Its NV12 rendering currently includes a CPU conversion fallback.
The renderer's internal channel name is `external-av`; the session control
exchange supplies the dynamically created FrameLink video channel.

## Timing and ownership contract

* Producer video uses `framelinksink preserve-timestamps=true sync=true`.
* PCM travels through GDP over the authenticated session's GIO stream. It is
  paced on the **same producer pipeline clock** before GDP, not independently
  timestamped on arrival. Without this pacing, file decode can send all audio
  before clock-paced video starts and invalidate the startup latency budget.
* Existing TIME segment running-time semantics and relative audio offset are
  retained. No per-track first-buffer subtraction is performed.
* The consumer maps both tracks onto one playback clock and shared epoch. GPU
  fences remain memory-readiness signals, never presentation timestamps.
* FrameLink's sink owns its copied/imported output. This path neither requires
  callers to retain an appsink buffer manually nor promises a zero-copy import.

## Explicit restrictions (not a general live pipeline SDK yet)

The M4 control protocol declares finite `--frames` before starting. Therefore:

* Finite CFR clips only; video running-time starts at zero and ends on the
  declared frame grid. PCM sample count must equal `frames * rate / fps`.
* PCM must be contiguous, with valid PTS and sample-derived durations. Audio can
  retain a nonzero origin; codec padding/gaps must be trimmed upstream, not
  silently retimestamped. TIME segments must have rate/applied-rate 1.
* Seeking, trick modes, unbounded live streams, dynamic format changes, and
  decoder priming/padding negotiation are **not claimed** by this example.
* Synthetic `--fault`, `--pacing-ppm`, and `--audio-offset-ns` cannot be combined
  with external input. The caller's pipeline supplies real timestamps instead.
* An error or unsupported timeline fails the bounded session; it is not converted
  into successful playback. The process watchdog still bounds a stalled peer.

General live input requires a separately reviewed duration/EOS control extension,
not inventing an arbitrary huge frame count. This remains an integration gap.

## Evidence and interpretation

See `docs/m5/external-pipeline-windows` and `external-pipeline-linux`.
The Windows run decoded both BGRA and NV12 successfully, with all 100 PCM blocks
(96000 samples) byte-for-byte and PTS/duration preserved. Each run received 94 of
100 video frames at the consumer sink pad; every received pixel hash and PTS
matched its producer frame. The finite latest-frame consumer scheduled 30/33
frames respectively, with zero late scheduled frames. **This is not a lossless
presentation or throughput qualification.** Linux sink traces received all 100.

`--trace-media` is optional and expensive: it maps pixels and computes SHA256,
therefore must be disabled for performance measurements. Trace reports at sink
arrival are not scanout timestamps. The test checks full PCM, changing video,
final-frame delivery, and exact data/timing for every received frame; dropped
frames are reported, not concealed behind callback totals.
