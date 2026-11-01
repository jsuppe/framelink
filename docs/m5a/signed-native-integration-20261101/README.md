Signed Android native RGBA A/V integration — implementation checkpoint

Source is not yet device-qualified. Native AHB allocation, source callback,
authenticated allocation binding and EGL sampling are connected. Input GstMemory
is retained until GPU completion; timeout quarantines allocation/context until
process restart. Pixel verification performs explicit final-consumer readback.
CPU writes generate the test source; no zero-copy decoder claim is made.

Build evidence: arm64 signed debug APK build and x86_64 JNI build passed.
Linux broker_video_factory passed five cases: exact RGBA frames/PCM and null,
exception, changed-PTS, changed-duration rejection (20.72 seconds).

Device attempts did not reach media: tablet keyguard remained showing and Viewer
was backgrounded/surface-destroyed. Unlock requested from owner. This is neither a
native playback pass nor a native-media failure. Native cross-UID qualification,
lifecycle cases and trace/copy accounting remain pending. M5a.12 stays open.

Additional Linux affected regressions passed 4/4: m3_av_apps,
broker_producer_stream, broker_memory_extension and broker_memory_binding
(57.68 seconds total). These are compatibility checks, not Android native GPU
playback evidence.
Windows producer/consumer builds and the existing ten-case m3_av_apps startup,
format, restart and audio-fault matrix passed (30.39 seconds). No new native
Android runtime evidence is implied by these Windows compatibility results.
