# External decoded Windows A/V -> native renderer

Source: renderer merged `4c2ff15`, FrameLink external pipeline `0335805`.
Fresh isolated Windows Release renderer build, MSVC 2022, NVIDIA RTX 5090,
GStreamer 1.28.0. Build log and binary hashes retained. Shared dependency source
caches were reused; no existing live demo installation was rebuilt or stopped.

Reproduce after building renderer against this FrameLink source:

```powershell
$env:PATH = 'C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;' + $env:PATH
python test/m5_external_renderer.py --build build/Release --renderer C:/src/vkrender/build/Release/vkrenderer.exe --output renderer-evidence
```

The harness generates a 6-second MJPEG/PCM AVI, decodes it through the external
pipeline producer, and starts an independent renderer. Real `autoaudiosink`
selects WASAPI2 and its audio clock; quiet 440 Hz PCM is submitted. The renderer
uses an offscreen Vulkan surface and provides JPEG preview images. BGRA and NV12
previews were inspected: the source's moving white ball appears on both cube
faces. No physical-monitor scanout or audible lip-sync measurement is claimed.

Normal cases require successful exits, >20 video scheduling decisions, selected
audio master, increasing audio samples, two distinct JPEG previews, no invalid
PTS/import failures, and queue depth <= 6. Source-death and missing-file cases
require producer failure, bounded receiver exit and a closed control port.

The initial harness incorrectly allowed 23 seconds for all cases. Missing-input
recovery can consume 15 seconds per attempt for initial + 3 recovery attempts. The
failed run is retained in initial-harness-timeout.log; the final harness allows
70 seconds for failure cases. This was a harness budget correction, not a runtime
fix. Process force-kill is only emergency harness cleanup and is not counted as
successful bounded runtime exit.

Important interpretation:

* `due_decisions` measures scheduling, not displayed/optically verified frames.
* A 300-frame source produced substantially fewer renderer due-decisions; this is
  functional integration evidence, not lossless 50 fps throughput qualification.
* NV12 renderer conversion currently includes a CPU fallback; no end-to-end
  zero-copy claim.
* WASAPI2 logs include an initial AUDCLNT_E_NOT_INITIALIZED stop warning followed
  by successful client acquire, and padding informational messages. We retain
  these; audio-master selection does not prove absence of device underruns.
* The session finite-duration restrictions in M5_WINDOWS_PIPELINE.md still apply.

## Final results

* BGRA and NV12: producer/renderer exit 0, 67 scheduling decisions each, complete
  300 PCM blocks / 288000 samples, real audio master, two distinct previews.
* Producer death: both processes exit nonzero in 16.043 seconds; control closed.
* Missing source: both exit nonzero in 63.129 seconds after bounded recovery;
  control closed. Neither failure was counted as completed playback.

All four final cases passed their stated functional/cleanup criteria.
