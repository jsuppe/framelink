Signed Android native RGBA A/V integration — implementation checkpoint

Initial checkpoint before device unlock (superseded by qualification below). Native AHB allocation, source callback,
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

## Unlocked-device qualification

After unlocking, two example defects were fixed: acquisition requested2000ms
although the action limit is1000ms; and an async preroll sink could not reach
PAUSED before the first native allocation was requested. The native producer
now uses1000ms and async=false (sync/presentation policy is unchanged).
The initially generic timeout error is now an acquisition error; opt-in
GStreamer diagnostics include warning/error context.

The native suite passed18/18 cases on TCL8483A/API35/Mali-G52, separate signed
producer/consumer UIDs. First suite uses --native-rgba --frames300; historical
case name `bgra` selects native RGBA when this flag is present, NOT BGRA memory.
Initial, producer-restart, consumer-restart and foreground-reopen each passed
300/300 original frame identities/pixels/PTS and480000 PCM samples. Other checks
cover producer/consumer death, background, wrong key/signer. Second suite uses
--native-rgba --frames60: unsupported format, invalid bounds, revoke, replacement,
broker death/restart, audio focus/reopen and audible playback all passed.
Replacement uses180 frames; broker/focus reopen and audible60/60 with96000 PCM.
A separate30-frame diagnostic run passed with48000 PCM samples. No source/relay
payload mapping is inferred from sink presentation: the source intentionally
CPU-fills its original AHB; final GPU pixel verification intentionally reads back.

Each original AHB is sampled with EGL; GstMemory remains pinned until completion.
PCM and video retain their original media timing. Swap acceptance and OpenSLES
success do not prove physical scanout/acoustic alignment. This does not qualify
GPU decoder output, opaque YUV as NV12, hang recovery or sustained throughput.
Final arm64 APK build passed; x86_64 rebuild recorded separately. Existing Linux
and Windows compatibility results above apply to unchanged shared producer code.

Final x86_64 JNI rebuild passed; x86 runtime is not claimed.
